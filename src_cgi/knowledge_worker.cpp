#include "knowledge_chunker.h"
#include "knowledge_store.h"
#include "document_extractor.h"
#include "dashscope_api.h"
#include "hash_util.h"
#include "object_reader.h"
#include "storage_types.h"

extern "C" {
#include "cfg.h"
#include "cJSON.h"
#include "make_log.h"
#include "knowledge_task.h"
}

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <unistd.h>
#include <vector>
#include <signal.h>
#include <fcntl.h>

namespace {

volatile sig_atomic_t running = 1;

void Stop(int) { running = 0; }

class ScopedTempFile {
public:
    ScopedTempFile() = default;
    explicit ScopedTempFile(std::string path) : path_(std::move(path)) {}
    ~ScopedTempFile() { Reset({}); }

    ScopedTempFile(const ScopedTempFile &) = delete;
    ScopedTempFile &operator=(const ScopedTempFile &) = delete;

    void Reset(std::string path) {
        if (!path_.empty() && path_ != path) unlink(path_.c_str());
        path_ = std::move(path);
    }

    const std::string &Path() const { return path_; }

private:
    std::string path_;
};

std::string Config(const char *section, const char *key, const char *fallback = "") {
    char value[1024] = {0};
    if (get_cfg_value(CFG_PATH, const_cast<char *>(section), const_cast<char *>(key), value) != 0) return fallback;
    return value;
}

std::string UserHash(const std::string &user) {
    hydrastore::Sha256 sha;
    sha.Update(user.data(), user.size());
    return sha.FinalHex().substr(0, 16);
}

std::string BuildUrl(const std::string &value, const std::string &storage_ip,
                    const std::string &storage_port) {
    if (value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0) return value;
    if (value.empty()) return {};
    std::string path = value;
    const std::size_t group = value.find("/group");
    if (group != std::string::npos) path = value.substr(group);
    else if (value.rfind("group", 0) == 0) path = "/" + value;
    if (!storage_ip.empty() && !storage_port.empty() && path[0] == '/') {
        return "http://" + storage_ip + ":" + storage_port + path;
    }
    return value;
}

bool Download(const std::string &url, const std::string &path, std::string *error) {
    CURL *curl = curl_easy_init();
    if (!curl) { if(error)*error="curl init failed"; return false; }
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0) { curl_easy_cleanup(curl); if(error)*error="cannot create source file"; return false; }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, reinterpret_cast<void *>(static_cast<intptr_t>(fd)));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, [](void *data, size_t size, size_t count, void *context) -> size_t {
        const int output = static_cast<int>(reinterpret_cast<intptr_t>(context));
        const char *cursor = static_cast<const char *>(data);
        std::size_t remaining = size * count;
        while (remaining != 0) {
            const ssize_t written = write(output, cursor, remaining);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) return 0;
            cursor += written; remaining -= static_cast<std::size_t>(written);
        }
        return size * count;
    });
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    const CURLcode result = curl_easy_perform(curl);
    long status = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    close(fd); curl_easy_cleanup(curl);
    if (result != CURLE_OK || (status != 0 && status != 200)) {
        unlink(path.c_str());
        if (error) *error = "source download failed";
        return false;
    }
    return true;
}

bool WriteTempText(const std::string &text, std::string *path) {
    char name[] = "/tmp/hydra_ai_image_XXXXXX";
    const int fd = mkstemp(name);
    if (fd < 0) return false;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const ssize_t written = write(fd, text.data() + offset, text.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { close(fd); unlink(name); return false; }
        offset += static_cast<std::size_t>(written);
    }
    close(fd); *path = name; return true;
}

std::string ReadText(const std::string &path, std::size_t limit) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::string text; text.reserve(std::min<std::size_t>(limit, 1024 * 1024));
    char buffer[8192];
    while (input && text.size() < limit) {
        input.read(buffer, std::min<std::size_t>(sizeof(buffer), limit - text.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) text.append(buffer, static_cast<std::size_t>(count));
    }
    return text;
}

void Normalize(std::vector<float> *vector) {
    double norm = 0.0;
    for (float value : *vector) norm += static_cast<double>(value) * value;
    norm = std::sqrt(norm);
    if (norm == 0.0) return;
    for (float &value : *vector) value = static_cast<float>(value / norm);
}

bool IsImage(const std::string &type) {
    return type == "png" || type == "jpg" || type == "jpeg" || type == "gif" || type == "bmp" || type == "webp";
}

bool ProcessSource(hydrastore::KnowledgeStore *store, const hydrastore::KnowledgeTaskClaim &task,
                   const std::string &embedding_model, const std::string &vl_model,
                   int dimension, const std::string &api_key,
                   const std::string &storage_ip, const std::string &storage_port,
                   const std::string &storage_client, bool *retryable, bool *skipped) {
    if (retryable) *retryable = true;
    if (skipped) *skipped = false;
    hydrastore::SourceObject source;
    if (!store->LoadSourceObject(task.user, task.md5, &source)) {
        bool exists = true;
        if (store->SourceRelationExists(task.user, task.md5, &exists) && !exists && retryable) {
            *retryable = false;
        }
        return false;
    }
    std::transform(source.type.begin(), source.type.end(), source.type.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(source.storage_mode.begin(), source.storage_mode.end(), source.storage_mode.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!is_parseable_type(source.type.c_str())) {
        if (skipped) *skipped = true;
        return false;
    }
    const std::string local_path = "/tmp/hydra_ai_source_" + std::to_string(task.id) +
                                   "_" + std::to_string(static_cast<long long>(getpid()));
    ScopedTempFile downloaded_source(local_path);
    source.local_path = downloaded_source.Path();
    std::string error;
    ScopedTempFile extracted_text;
    std::unique_ptr<hydrastore::BlobStore> blobs = hydrastore::MakeFastDFSBlobStore(storage_client.c_str());
    bool downloaded = false;
    if (!store->RenewTask(task)) return false;
    if (source.storage_mode == "manifest" && source.manifest_id > 0 && blobs) {
        hydrastore::ObjectReader reader(store->Connection(), blobs.get());
        downloaded = reader.DownloadToFile(source.md5, source.user, local_path);
        if (!downloaded) error = "manifest source read failed";
    } else {
        downloaded = Download(BuildUrl(source.legacy_url, storage_ip, storage_port), local_path, &error);
    }
    if (!downloaded) return false;
    if (!store->RenewTask(task)) {
        return false;
    }

    hydrastore::ExtractedDocument document;
    if (IsImage(source.type)) {
        if (api_key.empty()) {
            if (retryable) *retryable = false;
            return false;
        }
        if (!store->RenewTask(task)) {
            return false;
        }
        char description[32768] = {0};
        const bool described = dashscope_describe_image_file_model(api_key.c_str(), vl_model.c_str(), local_path.c_str(), source.type.c_str(), description, sizeof(description)) == 0;
        std::string extracted_path;
        if (!described || !store->RenewTask(task) || !WriteTempText(description, &extracted_path)) {
            return false;
        }
        extracted_text.Reset(extracted_path);
        document.text_path = extracted_text.Path();
        document.extracted_bytes = static_cast<std::int64_t>(std::strlen(description));
    } else {
        if (!store->RenewTask(task)) {
            return false;
        }
        hydrastore::ExtractorOptions extractor_options;
        extractor_options.timeout_ms = std::max(1, std::atoi(Config("knowledge", "extract_timeout_ms", "30000").c_str()));
        const long long configured_bytes = std::strtoll(Config("knowledge", "max_extracted_bytes", "8388608").c_str(), nullptr, 10);
        if (configured_bytes > 0) extractor_options.max_output_bytes = static_cast<std::size_t>(configured_bytes);
        if (!hydrastore::ExtractDocument(source, &document, &error, extractor_options)) {
            if (error == "unsupported document type" && skipped) *skipped = true;
            return false;
        }
        extracted_text.Reset(document.text_path);
        if (!store->RenewTask(task)) {
            return false;
        }
    }
    if (api_key.empty()) {
        if (retryable) *retryable = false;
        return false;
    }
    std::vector<hydrastore::EvidenceChunk> chunks;
    bool truncated = document.truncated;
    if (!hydrastore::ChunkDocumentFile(document.text_path, hydrastore::ChunkOptions(), &chunks, &truncated, &error) || chunks.empty()) {
        if (chunks.empty() && skipped) *skipped = true;
        return false;
    }
    std::int64_t generation = 0;
    if (!store->BeginEvidenceGeneration(source, &generation, &task)) {
        return false;
    }
    const char *fail_at = std::getenv("HYDRA_EMBED_FAIL_AT");
    const int fail_index = fail_at ? std::atoi(fail_at) : -1;
    int index = 0;
    for (auto &chunk : chunks) {
        if (!store->RenewTask(task)) {
            store->AbortEvidenceGeneration(source, generation, "task lease lost", &task);
            return false;
        }
        if (index == fail_index || api_key.empty()) {
            if (retryable && api_key.empty()) *retryable = false;
            store->AbortEvidenceGeneration(source, generation, api_key.empty() ? "DashScope key is not configured" : "embedding failpoint", &task);
            return false;
        }
        std::vector<float> embedding(static_cast<std::size_t>(dimension), 0.0f);
        if (dashscope_get_embedding(api_key.c_str(), embedding_model.c_str(), chunk.content.c_str(), embedding.data(), dimension) != 0 ||
            !store->RenewTask(task)) {
            store->AbortEvidenceGeneration(source, generation, "embedding request failed", &task);
            return false;
        }
        Normalize(&embedding);
        if (!store->PutStagingChunk(source, generation, chunk, &chunk.id) || !store->PutVector(source.user, "chunk", chunk.id, embedding_model, dimension, embedding, nullptr)) {
            store->AbortEvidenceGeneration(source, generation, "evidence write failed", &task);
            return false;
        }
        ++index;
    }
    if (!store->PublishEvidenceGeneration(source, generation, static_cast<int>(chunks.size()), truncated, document.extracted_bytes, &task)) {
        store->AbortEvidenceGeneration(source, generation, "evidence publish failed", &task);
        return false;
    }
    const std::string source_text = ReadText(document.text_path, 4096);
    std::string summary = source_text.substr(0, std::min<std::size_t>(source_text.size(), 2048));
    store->UpdateLegacyAiRecord(source, source_text, summary, embedding_model);
    if (!store->EnqueueTask(source.user, source.md5, "compile_wiki", "evidence_ready", false)) {
        return false;
    }
    return true;
}

bool ProcessTask(hydrastore::KnowledgeStore *store, const hydrastore::KnowledgeTaskClaim &task,
                 const std::string &embedding_model, const std::string &vl_model, int dimension,
                 const std::string &configured_key, const std::string &storage_ip,
                 const std::string &storage_port, const std::string &storage_client,
                 bool *retryable, bool *skipped) {
    if (retryable) *retryable = true;
    if (skipped) *skipped = false;
    std::string api_key = configured_key;
    if (api_key.empty()) store->LoadApiKey(task.user, &api_key);
    if (task.task_type == "delete_source") {
        std::string error; return store->DeleteSourceKnowledge(task.user, task.md5, &error);
    }
    if (task.task_type == "compile_wiki" || task.task_type == "repair_wiki") {
        if (retryable && api_key.empty()) *retryable = false;
        if (task.task_type == "compile_wiki") {
            bool source_exists = true;
            if (store->SourceRelationExists(task.user, task.md5, &source_exists) && !source_exists && retryable) {
                *retryable = false;
            }
        }
        hydrastore::WikiCompiler compiler(Config("dashscope", "wiki_model", "qwen-plus"), "wiki-compiler-v2", dimension,
                                          embedding_model, Config("faiss", "user_index_dir", "/data/faiss/users"));
        std::string error;
        return compiler.Compile(store, task, api_key, &error);
    }
    return ProcessSource(store, task, embedding_model, vl_model, dimension, api_key, storage_ip, storage_port, storage_client, retryable, skipped);
}

}  // namespace

int main() {
    signal(SIGTERM, Stop); signal(SIGINT, Stop);
    const std::string mysql_host = Config("mysql", "ip", "db");
    const unsigned int mysql_port = static_cast<unsigned int>(std::strtoul(Config("mysql", "port", "3306").c_str(), nullptr, 10));
    const std::string mysql_user = Config("mysql", "user");
    const std::string mysql_password = Config("mysql", "password");
    const std::string mysql_database = Config("mysql", "database", "yuncuchu");
    const std::string embedding_model = Config("dashscope", "embedding_model", "text-embedding-v3");
    const std::string vl_model = Config("dashscope", "vl_model", "qwen-vl-plus");
    const std::string configured_key = Config("dashscope", "api_key");
    const int dimension = std::max(1, std::atoi(Config("dashscope", "embedding_dimension", "1024").c_str()));
    const std::string storage_ip = Config("storage_web_server", "ip");
    const std::string storage_port = Config("storage_web_server", "port");
    const std::string storage_client = Config("dfs_path", "client", "/etc/fdfs/client.conf");
    const std::string host = Config("knowledge", "worker_id", "");
    const std::string worker_id = host.empty() ? ("knowledge:" + std::to_string(static_cast<long long>(getpid()))) : host + ":" + std::to_string(static_cast<long long>(getpid()));
    hydrastore::KnowledgeStore store(mysql_host, mysql_port, mysql_user, mysql_password, mysql_database);
    if (!store.Connect()) return 1;
    store.RecoverExpiredTasks();
    auto next_recovery = std::chrono::steady_clock::now();
    while (running) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_recovery) {
            store.RecoverExpiredTasks();
            next_recovery = now + std::chrono::seconds(2);
        }
        hydrastore::KnowledgeTaskClaim task;
        if (!store.ClaimTask(worker_id, &task)) { sleep(2); continue; }
        LOG("cgi", "knowledge_worker", "task=%lld type=%s worker=%s epoch=%lld user_hash=%s md5=%s retry=%d stage=start\n",
            static_cast<long long>(task.id), task.task_type.c_str(), worker_id.c_str(),
            static_cast<long long>(task.lease_epoch), UserHash(task.user).c_str(), task.md5.c_str(), task.retry_count);
        const auto started = std::chrono::steady_clock::now();
        bool retryable = true;
        bool skipped = false;
        const bool ok = ProcessTask(&store, task, embedding_model, vl_model, dimension, configured_key, storage_ip, storage_port, storage_client, &retryable, &skipped);
        if (ok) store.FinishTask(task);
        else if (skipped) store.SkipTask(task, "unsupported or empty source");
        else {
            const bool failed = store.FailTask(task, "knowledge task failed", retryable);
            if (failed && (task.task_type == "compile_wiki" || task.task_type == "repair_wiki")) {
                store.MarkWikiFailed(task, "knowledge task failed");
            }
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        LOG("cgi", "knowledge_worker", "task=%lld type=%s worker=%s epoch=%lld stage=%s\n",
            static_cast<long long>(task.id), task.task_type.c_str(), worker_id.c_str(),
            static_cast<long long>(task.lease_epoch), ok ? "finish" : "fail");
        LOG("cgi", "knowledge_worker", "task=%lld type=%s worker=%s epoch=%lld retry=%d elapsed_ms=%lld stage=done\n",
            static_cast<long long>(task.id), task.task_type.c_str(), worker_id.c_str(),
            static_cast<long long>(task.lease_epoch), task.retry_count,
            static_cast<long long>(elapsed));
    }
    return 0;
}
