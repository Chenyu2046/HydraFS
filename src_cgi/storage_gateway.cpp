#include "fcgi_stdio.h"
#include "cJSON.h"
extern "C" {
#include "cfg.h"
#include "util_cgi.h"
}
#include "fdfs_client.h"

#ifdef byte
#undef byte
#endif

#include "hash_util.h"
#include "metadata_store.h"
#include "storage_resilience.h"
#include "storage_query.h"
#include "storage_types.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <cstring>
#include <csignal>
#include <cmath>
#include <fcntl.h>
#include <limits>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

struct Config {
    std::string mysql_host = "127.0.0.1";
    unsigned int mysql_port = 3306;
    std::string mysql_user;
    std::string mysql_password;
    std::string mysql_database;
    std::string fdfs_client;
};

std::string g_failpoint;

Config ReadConfig() {
    Config config;
    char value[512] = {0};
    if (get_cfg_value(CFG_PATH, (char *)"mysql", (char *)"ip", value) == 0) config.mysql_host = value;
    memset(value, 0, sizeof(value));
    if (get_cfg_value(CFG_PATH, (char *)"mysql", (char *)"port", value) == 0) config.mysql_port = static_cast<unsigned int>(strtoul(value, nullptr, 10));
    memset(value, 0, sizeof(value));
    get_cfg_value(CFG_PATH, (char *)"mysql", (char *)"user", value); config.mysql_user = value;
    memset(value, 0, sizeof(value));
    get_cfg_value(CFG_PATH, (char *)"mysql", (char *)"password", value); config.mysql_password = value;
    memset(value, 0, sizeof(value));
    get_cfg_value(CFG_PATH, (char *)"mysql", (char *)"database", value); config.mysql_database = value;
    memset(value, 0, sizeof(value));
    get_cfg_value(CFG_PATH, (char *)"dfs_path", (char *)"client", value); config.fdfs_client = value;
    return config;
}

bool ParseContentLength(std::int64_t *length) {
    const char *raw = std::getenv("CONTENT_LENGTH");
    if (!raw || !*raw) return false;
    errno = 0;
    char *end = nullptr;
    const long long parsed = std::strtoll(raw, &end, 10);
    if (errno == ERANGE || end == raw || *end != '\0' || parsed < 0) return false;
    *length = static_cast<std::int64_t>(parsed);
    return true;
}

void DrainBody();

std::string Body() {
    std::int64_t size = 0;
    if (!ParseContentLength(&size)) return {};
    if (size > 2 * 1024 * 1024) { DrainBody(); return {}; }
    if (size == 0) return {};
    std::string body(static_cast<std::size_t>(size), '\0');
    const std::size_t got = FCGI_fread(body.data(), 1, body.size(), FCGI_stdin);
    if (got != body.size()) return {};
    return body;
}

std::string Query(const char *key) {
    const char *query = getenv("QUERY_STRING");
    std::string value;
    return hydrastore::ParseQueryValue(query, key, &value) ? value : std::string();
}

void DrainBody() {
    std::int64_t remaining = 0;
    if (!ParseContentLength(&remaining)) return;
    char buffer[64 * 1024];
    while (remaining > 0) {
        const std::size_t want = static_cast<std::size_t>(std::min<std::int64_t>(remaining, sizeof(buffer)));
        const std::size_t got = FCGI_fread(buffer, 1, want, FCGI_stdin);
        if (got == 0) break;
        remaining -= static_cast<long>(got);
    }
}

void Failpoint(const char *name) {
    const char *configured = g_failpoint.empty() ? std::getenv("HYDRA_FAILPOINT") : g_failpoint.c_str();
    if (configured && strcmp(configured, name) == 0) raise(SIGKILL);
}

bool Credentials(std::string *user, std::string *token) {
    const char *u = getenv("HTTP_X_UPLOAD_USER");
    const char *t = getenv("HTTP_X_UPLOAD_TOKEN");
    if (!u || !t || !*u || !*t) return false;
    *user = u; *token = t;
    if (user->size() > 256 || token->size() > 256) return false;
    return verify_token(const_cast<char *>(user->c_str()), const_cast<char *>(token->c_str())) == 0;
}

void JsonResponse(cJSON *root, int status = 200, int retry_after = 0, const char *backpressure = nullptr) {
    char *out = cJSON_PrintUnformatted(root);
    if (status != 200) FCGI_fprintf(FCGI_stdout, "Status: %d\r\n", status);
    FCGI_fprintf(FCGI_stdout, "Content-Type: application/json\r\nCache-Control: no-store\r\n");
    if (retry_after > 0) FCGI_fprintf(FCGI_stdout, "Retry-After: %d\r\n", retry_after);
    if (backpressure) FCGI_fprintf(FCGI_stdout, "X-Hydra-Backpressure: %s\r\n", backpressure);
    FCGI_fprintf(FCGI_stdout, "\r\n%s", out ? out : "{\"code\":1}");
    if (out) free(out);
    cJSON_Delete(root);
}

class PutAdmission {
public:
    PutAdmission() {
        const char *value = std::getenv("HYDRA_GATEWAY_ADMISSION_LIMIT");
        limit_ = value && *value ? std::max(1, atoi(value)) : 1;
        const int previous = active_.fetch_add(1);
        acquired_ = previous < limit_;
        if (!acquired_) active_.fetch_sub(1);
    }
    ~PutAdmission() { if (acquired_) active_.fetch_sub(1); }
    bool acquired() const { return acquired_; }

private:
    static std::atomic<int> active_;
    int limit_ = 1;
    bool acquired_ = false;
};

std::atomic<int> PutAdmission::active_{0};

cJSON *Error(int code, const char *message) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "code", code);
    if (message) cJSON_AddStringToObject(root, "msg", message);
    return root;
}

bool Hex64(const std::string &value) {
    if (value.size() != 64) return false;
    return std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}

void AddStatuses(cJSON *root, const std::vector<hydrastore::PartStatus> &statuses) {
    cJSON *missing = cJSON_CreateArray();
    cJSON *reused = cJSON_CreateArray();
    cJSON *uploadable = cJSON_CreateArray();
    cJSON *waiting = cJSON_CreateArray();
    for (const auto &part : statuses) {
        switch (part.availability) {
        case hydrastore::PartAvailability::kReady:
            cJSON_AddItemToArray(reused, cJSON_CreateNumber(part.spec.index));
            break;
        case hydrastore::PartAvailability::kUploadable:
            cJSON_AddItemToArray(uploadable, cJSON_CreateNumber(part.spec.index));
            cJSON_AddItemToArray(missing, cJSON_CreateNumber(part.spec.index));
            break;
        case hydrastore::PartAvailability::kWaiting:
            cJSON_AddItemToArray(waiting, cJSON_CreateNumber(part.spec.index));
            break;
        case hydrastore::PartAvailability::kMissing:
            cJSON_AddItemToArray(missing, cJSON_CreateNumber(part.spec.index));
            cJSON_AddItemToArray(uploadable, cJSON_CreateNumber(part.spec.index));
            break;
        }
    }
    cJSON_AddItemToObject(root, "missingParts", missing);
    cJSON_AddItemToObject(root, "reusedParts", reused);
    cJSON_AddItemToObject(root, "uploadableParts", uploadable);
    cJSON_AddItemToObject(root, "waitingParts", waiting);
}

bool ParseInit(const std::string &body, std::string *upload_id, std::string *user,
               std::string *token, std::string *filename, std::string *digest,
               std::int64_t *size, std::vector<hydrastore::PartSpec> *parts) {
    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) return false;
    auto is_type = [](cJSON *item, int type) {
        return item && (item->type & 0xFF) == type;
    };
    auto text = [root](const char *key) -> std::string {
        cJSON *item = cJSON_GetObjectItem(root, key);
        return item && (item->type & 0xFF) == cJSON_String && item->valuestring
            ? item->valuestring : "";
    };
    *upload_id = text("uploadId"); *user = text("user"); *token = text("token");
    *filename = text("filename"); *digest = text("contentDigest");
    if (digest->empty()) *digest = text("md5");
    cJSON *size_item = cJSON_GetObjectItem(root, "size");
    cJSON *array = cJSON_GetObjectItem(root, "parts");
    bool ok = upload_id->size() <= 256 &&
              !user->empty() && user->size() <= 256 && token->size() <= 256 &&
              filename->size() <= 1024 && digest->size() <= 128 &&
              is_type(size_item, cJSON_Number) && std::isfinite(size_item->valuedouble) &&
              std::floor(size_item->valuedouble) == size_item->valuedouble && size_item->valuedouble >= 0 &&
              size_item->valuedouble <= 9007199254740991.0 &&
              is_type(array, cJSON_Array) && cJSON_GetArraySize(array) <= 100000;
    if (ok) {
        *size = static_cast<std::int64_t>(size_item->valuedouble);
        const int count = cJSON_GetArraySize(array);
        std::int64_t declared_size = 0;
        for (int i = 0; i < count; ++i) {
            cJSON *item = cJSON_GetArrayItem(array, i);
            cJSON *index = item ? cJSON_GetObjectItem(item, "index") : nullptr;
            cJSON *part_size = item ? cJSON_GetObjectItem(item, "size") : nullptr;
            cJSON *sha = item ? cJSON_GetObjectItem(item, "sha256") : nullptr;
            if (!item || !is_type(index, cJSON_Number) || !std::isfinite(index->valuedouble) ||
                std::floor(index->valuedouble) != index->valuedouble || index->valuedouble != i ||
                !is_type(part_size, cJSON_Number) || !std::isfinite(part_size->valuedouble) ||
                std::floor(part_size->valuedouble) != part_size->valuedouble ||
                part_size->valuedouble > 9007199254740991.0 ||
                !is_type(sha, cJSON_String) ||
                !Hex64(sha->valuestring) || part_size->valuedouble < 0) {
                ok = false; break;
            }
            const std::int64_t declared_part_size = static_cast<std::int64_t>(part_size->valuedouble);
            if (declared_part_size > *size - declared_size) { ok = false; break; }
            declared_size += declared_part_size;
            parts->push_back({index->valueint, declared_part_size, sha->valuestring});
        }
        ok = ok && declared_size == *size;
    }
    cJSON_Delete(root);
    return ok && !parts->empty();
}

struct InputContext {
    hydrastore::Sha256 hash;
    std::int64_t bytes = 0;
};

std::size_t ReadAndHash(void *context, void *buffer, std::size_t capacity) {
    auto *input = static_cast<InputContext *>(context);
    const std::size_t got = FCGI_fread(buffer, 1, capacity, FCGI_stdin);
    if (got) {
        input->hash.Update(buffer, got);
        input->bytes += static_cast<std::int64_t>(got);
    }
    return got;
}

bool ParseCredentialsBody(const std::string &body, std::string *user,
                          std::string *token, std::string *upload_id) {
    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) return false;
    auto text = [root](const char *key) -> std::string {
        cJSON *item = cJSON_GetObjectItem(root, key);
        return item && (item->type & 0xFF) == cJSON_String && item->valuestring
            ? item->valuestring : "";
    };
    *user = text("user"); *token = text("token"); *upload_id = text("uploadId");
    if (upload_id->empty()) *upload_id = text("objectId");
    cJSON_Delete(root);
    return !user->empty() && user->size() <= 256 && !token->empty() && token->size() <= 256 &&
           !upload_id->empty() && upload_id->size() <= 256;
}

bool ParseIndex(const std::string &value, int *index) {
    if (value.empty()) return false;
    errno = 0;
    char *end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (errno == ERANGE || end == value.c_str() || *end != '\0' || parsed < 0 || parsed > INT_MAX) return false;
    *index = static_cast<int>(parsed);
    return true;
}

std::string SafeHeaderValue(const char *name, const char *fallback = "-") {
    const char *raw = std::getenv(name);
    if (!raw || !*raw) return fallback;
    std::string value(raw);
    if (value.size() > 128) return fallback;
    for (const char c : value) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == ':')) {
            return fallback;
        }
    }
    return value;
}

long long UnixMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

long long ElapsedMillis(const std::chrono::steady_clock::time_point &started) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
}

std::string g_gateway_instance = "-";
std::string g_trace_path = "/app/logs/storage_gateway.trace.log";

struct PartTrace {
    PartTrace()
        : trace_id(SafeHeaderValue("HTTP_X_HYDRA_TRACE_ID")),
          gateway_instance(g_gateway_instance),
          retry_reason(SafeHeaderValue("HTTP_X_HYDRA_RETRY_REASON")),
          received_unix_ms(UnixMillis()), started(std::chrono::steady_clock::now()) {
        const std::string attempt_value = SafeHeaderValue("HTTP_X_HYDRA_ATTEMPT", "0");
        char *end = nullptr;
        const long parsed = std::strtol(attempt_value.c_str(), &end, 10);
        attempt = end != attempt_value.c_str() && *end == '\0' && parsed >= 0 && parsed <= INT_MAX
            ? static_cast<int>(parsed) : 0;
    }

    ~PartTrace() {
        const int worker_pid = static_cast<int>(getpid());
        const long long gateway_total_ms = ElapsedMillis(started);
        const char *format =
            "HYDRA_TRACE trace_id=%s gateway_instance=%s worker_pid=%d "
            "received_unix_ms=%lld part_index=%d client_attempt=%d "
            "claim_part_ms=%lld blob_put_ms=%lld metadata_ready_ms=%lld "
            "gateway_total_ms=%lld breaker_state=%s storage_success=%d hash_ok=%d "
            "retry_reason=%s\n";
        const int fd = open(g_trace_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            dprintf(fd, format, trace_id.c_str(), gateway_instance.c_str(), worker_pid,
                    received_unix_ms, part_index, attempt, claim_part_ms, blob_put_ms,
                    metadata_ready_ms, gateway_total_ms, breaker_state.c_str(),
                    storage_success ? 1 : 0, hash_ok ? 1 : 0, retry_reason.c_str());
            close(fd);
        }
    }

    std::string trace_id;
    std::string gateway_instance;
    std::string retry_reason;
    long long received_unix_ms = 0;
    std::chrono::steady_clock::time_point started;
    int part_index = -1;
    int attempt = 0;
    long long claim_part_ms = -1;
    long long blob_put_ms = -1;
    long long metadata_ready_ms = -1;
    std::string breaker_state = "not_checked";
    bool storage_success = false;
    bool hash_ok = false;
};

}  // namespace

int main() {
    // Capture container identity before FastCGI replaces the process environment with request variables.
    g_gateway_instance = SafeHeaderValue("HYDRA_GATEWAY_INSTANCE", SafeHeaderValue("HOSTNAME").c_str());
    const char *trace_path = std::getenv("HYDRA_TRACE_LOG");
    if (trace_path && *trace_path) g_trace_path = trace_path;
    const char *failpoint = std::getenv("HYDRA_FAILPOINT");
    if (failpoint && *failpoint) g_failpoint = failpoint;
    const Config config = ReadConfig();
    ignore_signal_pipe();
    hydrastore::MetadataStore metadata(config.mysql_host, config.mysql_port, config.mysql_user,
                                       config.mysql_password, config.mysql_database);
    std::unique_ptr<hydrastore::BlobStore> blobs = hydrastore::MakeFaultInjectingBlobStore(
        hydrastore::MakeFastDFSBlobStore(config.fdfs_client.c_str()));
    hydrastore::BlobStoreCircuitBreaker breaker;

    while (true) {
        if (FCGI_Accept() < 0) break;
        const std::string uri = getenv("REQUEST_URI") ? getenv("REQUEST_URI") : "";
        const std::string path = uri.substr(0, uri.find('?'));
        const std::string method = getenv("REQUEST_METHOD") ? getenv("REQUEST_METHOD") : "POST";
        const std::string action = path.substr(path.find("/api/object/") == std::string::npos ? path.size() : 12);
        if (action == "init") {
            const std::string body = Body();
            std::string upload_id, user, token, filename, digest;
            std::int64_t size = 0;
            std::vector<hydrastore::PartSpec> specs;
            if (!ParseInit(body, &upload_id, &user, &token, &filename, &digest, &size, &specs) ||
                verify_token(const_cast<char *>(user.c_str()), const_cast<char *>(token.c_str())) != 0) {
                JsonResponse(Error(4, "token验证失败")); continue;
            }
            hydrastore::UploadSession session;
            std::vector<hydrastore::PartStatus> statuses;
            bool initialized = false;
            for (int attempt = 0; attempt < 3 && !initialized; ++attempt) {
                initialized = metadata.InitOrResume(upload_id, user, filename, size, digest, specs,
                                                    &session, &statuses);
                if (!initialized && attempt < 2) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20 * (attempt + 1)));
                }
            }
            if (!initialized) {
                JsonResponse(Error(1, "object init failed")); continue;
            }
            cJSON *response = Error(0, nullptr);
            cJSON_AddStringToObject(response, "uploadId", session.id.c_str());
            cJSON_AddNumberToObject(response, "chunkCount", session.chunk_count);
            cJSON_AddBoolToObject(response, "instant", session.state == "COMMITTED");
            if (!session.object_id.empty()) cJSON_AddStringToObject(response, "objectId", session.object_id.c_str());
            if (!session.storage_mode.empty()) cJSON_AddStringToObject(response, "storageMode", session.storage_mode.c_str());
            if (!session.legacy_url.empty()) cJSON_AddStringToObject(response, "url", session.legacy_url.c_str());
            AddStatuses(response, statuses);
            JsonResponse(response);
        } else if (action == "part") {
            PartTrace trace;
            std::string user, token;
            if (!Credentials(&user, &token)) { DrainBody(); JsonResponse(Error(4, "token验证失败")); continue; }
            const std::string upload_id = Query("uploadId");
            const std::string index_value = Query("index");
            const std::string claimed_hash = Query("sha256");
            if (upload_id.empty() || index_value.empty() || !Hex64(claimed_hash)) {
                DrainBody(); JsonResponse(Error(1, "invalid part")); continue;
            }
            int index = -1;
            if (!ParseIndex(index_value, &index)) {
                DrainBody(); JsonResponse(Error(1, "invalid part index")); continue;
            }
            trace.part_index = index;
            hydrastore::UploadSession session;
            std::vector<hydrastore::PartStatus> statuses;
            if (!metadata.GetSession(upload_id, user, &session) || !metadata.InitOrResume(
                    upload_id, user, session.filename, session.size, session.content_digest,
                    {}, &session, &statuses)) {
                DrainBody(); JsonResponse(Error(1, "unknown upload")); continue;
            }
            hydrastore::PartStatus expected;
            bool found = false;
            for (const auto &part : statuses) if (part.spec.index == index) { expected = part; found = true; break; }
            if (!found || expected.spec.sha256 != claimed_hash) {
                DrainBody(); JsonResponse(Error(1, "part metadata mismatch")); continue;
            }
            std::int64_t content_length = 0;
            if (!ParseContentLength(&content_length) || content_length != expected.spec.size) {
                DrainBody(); JsonResponse(Error(1, "part size mismatch")); continue;
            }
            if (expected.state == "READY") {
                // A duplicate request must still prove that its body matches
                // the declared part.  Returning success before reading the
                // body would accept a same-size, wrong-payload retry.
                InputContext input;
                char buffer[64 * 1024];
                while (input.bytes < expected.spec.size) {
                    const std::size_t want = static_cast<std::size_t>(std::min<std::int64_t>(
                        expected.spec.size - input.bytes, sizeof(buffer)));
                    if (ReadAndHash(&input, buffer, want) == 0) break;
                }
                trace.hash_ok = input.bytes == expected.spec.size &&
                                input.hash.FinalHex() == expected.spec.sha256;
                if (!trace.hash_ok) {
                    JsonResponse(Error(1, "sha256 or part payload mismatch"));
                } else {
                    JsonResponse(Error(0, nullptr));
                }
                continue;
            }
            PutAdmission admission;
            if (!admission.acquired()) {
                DrainBody();
                JsonResponse(Error(1, "gateway overloaded"), 429, 1, "gateway-overload");
                continue;
            }
            const hydrastore::BreakerDecision decision = breaker.Allow();
            trace.breaker_state = decision.allowed ? "allow" : "open";
            if (!decision.allowed) {
                DrainBody();
                JsonResponse(Error(1, "blobstore circuit open"), 503, 2, decision.reason);
                continue;
            }
            hydrastore::PartClaim claim;
            bool claimed = false;
            const auto claim_started = std::chrono::steady_clock::now();
            for (int attempt = 0; attempt < 3 && !claimed; ++attempt) {
                claimed = metadata.ClaimPartUpload(upload_id, index, &claim);
                if (!claimed && attempt < 2) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20 * (attempt + 1)));
                }
            }
            trace.claim_part_ms = ElapsedMillis(claim_started);
            if (!claimed) {
                DrainBody();
                JsonResponse(Error(2, "part is being uploaded by another session"));
                continue;
            }
            if (!claim.backend_file_id.empty()) {
                // A worker may have died after the physical PUT was durable.
                // Revalidate the retry body and finalize the recorded blob
                // instead of creating a second physical object.
                InputContext input;
                char buffer[64 * 1024];
                while (input.bytes < expected.spec.size) {
                    const std::size_t want = static_cast<std::size_t>(std::min<std::int64_t>(
                        expected.spec.size - input.bytes, sizeof(buffer)));
                    if (ReadAndHash(&input, buffer, want) == 0) break;
                }
                trace.hash_ok = input.bytes == expected.spec.size &&
                                input.hash.FinalHex() == expected.spec.sha256;
                if (!trace.hash_ok) {
                    JsonResponse(Error(1, "sha256 or part payload mismatch"));
                    continue;
                }
                trace.storage_success = true;
                trace.metadata_ready_ms = 0;
                if (!metadata.MarkPartReady(upload_id, index, upload_id,
                                            claim.lease_epoch, claim.backend_file_id)) {
                    JsonResponse(Error(1, "part lease lost"));
                    continue;
                }
                JsonResponse(Error(0, nullptr));
                continue;
            }
            InputContext input;
            hydrastore::BlobSource source{&input, ReadAndHash};
            std::string backend_id;
            const auto blob_started = std::chrono::steady_clock::now();
            const bool stored = blobs->Put(source, expected.spec.size, "", &backend_id);
            const auto blob_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - blob_started).count();
            const char *timeout_config = std::getenv("HYDRA_BREAKER_TIMEOUT_MS");
            const long timeout_ms = timeout_config && *timeout_config
                ? std::max(1L, strtol(timeout_config, nullptr, 10)) : 2000L;
            breaker.Record(stored, blob_elapsed >= timeout_ms, blob_elapsed, decision.probe_generation);
            trace.blob_put_ms = blob_elapsed;
            trace.storage_success = stored;
            const bool hash_ok = stored && input.bytes == expected.spec.size &&
                                 input.hash.FinalHex() == expected.spec.sha256;
            trace.hash_ok = hash_ok;
            if (!stored) {
                metadata.MarkPartFailed(upload_id, index, upload_id, claim.lease_epoch);
                JsonResponse(Error(1, "blob upload failed"), 503, 2, "blobstore-failure");
                continue;
            }
            if (!hash_ok) {
                if (stored) blobs->Delete(backend_id);
                metadata.MarkPartFailed(upload_id, index, upload_id, claim.lease_epoch);
                JsonResponse(Error(1, "sha256 or blob upload failed")); continue;
            }
            if (!metadata.RecordPartBackend(upload_id, index, upload_id,
                                            claim.lease_epoch, backend_id)) {
                blobs->Delete(backend_id);
                metadata.MarkPartFailed(upload_id, index, upload_id, claim.lease_epoch);
                JsonResponse(Error(1, "part backend metadata failed")); continue;
            }
            Failpoint("after_blob_put");
            const auto metadata_started = std::chrono::steady_clock::now();
            const bool metadata_ready = metadata.MarkPartReady(
                upload_id, index, upload_id, claim.lease_epoch, backend_id);
            trace.metadata_ready_ms = ElapsedMillis(metadata_started);
            if (!metadata_ready) {
                // Delete only if this owner still fenced the row and the
                // failure transition cleared its backend ID.  A stale owner
                // must not delete a blob already adopted by a new owner.
                if (metadata.MarkPartFailed(upload_id, index, upload_id, claim.lease_epoch)) {
                    blobs->Delete(backend_id);
                }
                JsonResponse(Error(1, "part lease lost")); continue;
            }
            JsonResponse(Error(0, nullptr));
        } else if (action == "status") {
            const std::string body = Body();
            std::string user, token, upload_id;
            if (!ParseCredentialsBody(body, &user, &token, &upload_id) ||
                verify_token(const_cast<char *>(user.c_str()), const_cast<char *>(token.c_str())) != 0) {
                JsonResponse(Error(4, "token验证失败")); continue;
            }
            hydrastore::UploadSession session;
            std::vector<hydrastore::PartStatus> statuses;
            if (!metadata.GetSession(upload_id, user, &session) || !metadata.InitOrResume(
                    upload_id, user, session.filename, session.size, session.content_digest,
                    {}, &session, &statuses)) { JsonResponse(Error(1, "unknown upload")); continue; }
            cJSON *response = Error(0, nullptr); AddStatuses(response, statuses); JsonResponse(response);
        } else if (action == "commit") {
            const std::string body = Body();
            std::string user, token, upload_id;
            if (!ParseCredentialsBody(body, &user, &token, &upload_id) ||
                verify_token(const_cast<char *>(user.c_str()), const_cast<char *>(token.c_str())) != 0) {
                JsonResponse(Error(4, "token验证失败")); continue;
            }
            hydrastore::UploadSession session;
            Failpoint("before_commit");
            if (!metadata.Commit(upload_id, user, &session)) { JsonResponse(Error(1, "commit failed or parts missing")); continue; }
            Failpoint("after_db_commit_before_response");
            cJSON *response = Error(0, nullptr);
            cJSON_AddStringToObject(response, "uploadId", upload_id.c_str());
            cJSON_AddStringToObject(response, "objectId", session.object_id.c_str());
            cJSON_AddNumberToObject(response, "manifestId", session.manifest_id);
            cJSON_AddStringToObject(response, "storageMode", session.storage_mode.empty() ? "manifest" : session.storage_mode.c_str());
            if (!session.legacy_url.empty()) cJSON_AddStringToObject(response, "url", session.legacy_url.c_str());
            JsonResponse(response);
        } else if (action == "abort") {
            const std::string body = Body();
            std::string user, token, upload_id;
            if (!ParseCredentialsBody(body, &user, &token, &upload_id) ||
                verify_token(const_cast<char *>(user.c_str()), const_cast<char *>(token.c_str())) != 0 ||
                !metadata.Abort(upload_id, user)) { JsonResponse(Error(1, "abort failed")); continue; }
            JsonResponse(Error(0, nullptr));
        } else if (action == "delete") {
            const std::string body = Body();
            std::string user, token, upload_id;
            if (!ParseCredentialsBody(body, &user, &token, &upload_id) ||
                verify_token(const_cast<char *>(user.c_str()), const_cast<char *>(token.c_str())) != 0 ||
                !metadata.DeleteObjectForUser(upload_id, user)) { JsonResponse(Error(1, "delete failed")); continue; }
            JsonResponse(Error(0, nullptr));
        } else if ((action == "download" || action == "share-download") && method == "GET") {
            std::string user, token;
            hydrastore::UploadSession session;
            std::vector<hydrastore::PartStatus> parts;
            if (action == "share-download") {
                if (!metadata.GetSharedManifest(Query("shareToken"), &session, &parts)) {
                    JsonResponse(Error(1, "share object not found")); continue;
                }
            } else {
                if (!Credentials(&user, &token) ||
                    !metadata.GetManifest(Query("objectId"), user, &session, &parts)) {
                    JsonResponse(Error(4, "private object requires valid credentials")); continue;
                }
            }
            const std::string header =
                "Content-Type: application/octet-stream\r\nContent-Length: " +
                std::to_string(session.size) +
                "\r\nContent-Disposition: attachment; filename=\"object\"\r\n\r\n";
            FCGI_fwrite(const_cast<char *>(header.data()), 1, header.size(), FCGI_stdout);
            hydrastore::BlobSink sink;
            sink.context = nullptr;
            sink.write = [](void *, const void *data, std::size_t size) {
                return FCGI_fwrite(const_cast<void *>(data), 1, size, FCGI_stdout) == size;
            };
            bool ok = true;
            for (const auto &part : parts) {
                std::int64_t ignored_size = 0;
                if (!blobs->Get(part.backend_file_id, sink, 0, 0, &ignored_size)) {
                    ok = false;
                    break;
                }
            }
            if (!ok) FCGI_fprintf(FCGI_stderr, "HydraStore download failed object=%s\n", session.object_id.c_str());
        } else {
            DrainBody(); JsonResponse(Error(1, "unknown object route"));
        }
    }
    return 0;
}
