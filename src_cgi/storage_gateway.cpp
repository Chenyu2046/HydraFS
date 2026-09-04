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
#include "storage_types.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <string>
#include <thread>
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

std::string Body() {
    const char *length = getenv("CONTENT_LENGTH");
    const long size = length ? strtol(length, nullptr, 10) : 0;
    if (size <= 0 || size > 2 * 1024 * 1024) return {};
    std::string body(static_cast<std::size_t>(size), '\0');
    const std::size_t got = FCGI_fread(body.data(), 1, body.size(), FCGI_stdin);
    if (got != body.size()) return {};
    return body;
}

std::string Query(const char *key) {
    char value[1024] = {0};
    int length = 0;
    const char *query = getenv("QUERY_STRING");
    return query && query_parse_key_value(query, key, value, &length) == 0
        ? std::string(value, static_cast<std::size_t>(length)) : std::string();
}

void DrainBody() {
    const char *length = getenv("CONTENT_LENGTH");
    long remaining = length ? strtol(length, nullptr, 10) : 0;
    char buffer[64 * 1024];
    while (remaining > 0) {
        const std::size_t want = static_cast<std::size_t>(std::min<long>(remaining, sizeof(buffer)));
        const std::size_t got = FCGI_fread(buffer, 1, want, FCGI_stdin);
        if (got == 0) break;
        remaining -= static_cast<long>(got);
    }
}

void Failpoint(const char *name) {
    const char *configured = getenv("HYDRA_FAILPOINT");
    if (configured && strcmp(configured, name) == 0) raise(SIGKILL);
}

bool Credentials(std::string *user, std::string *token) {
    const char *u = getenv("HTTP_X_UPLOAD_USER");
    const char *t = getenv("HTTP_X_UPLOAD_TOKEN");
    if (!u || !t || !*u || !*t) return false;
    *user = u; *token = t;
    return verify_token(const_cast<char *>(user->c_str()), const_cast<char *>(token->c_str())) == 0;
}

void JsonResponse(cJSON *root) {
    char *out = cJSON_PrintUnformatted(root);
    FCGI_fprintf(FCGI_stdout, "Content-Type: application/json\r\nCache-Control: no-store\r\n\r\n%s",
                 out ? out : "{\"code\":1}");
    if (out) free(out);
    cJSON_Delete(root);
}

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
    bool ok = !user->empty() && !token->empty() && !filename->empty() && !digest->empty() &&
              is_type(size_item, cJSON_Number) && size_item->valuedouble >= 0 &&
              is_type(array, cJSON_Array);
    if (ok) {
        *size = static_cast<std::int64_t>(size_item->valuedouble);
        const int count = cJSON_GetArraySize(array);
        std::int64_t declared_size = 0;
        for (int i = 0; i < count; ++i) {
            cJSON *item = cJSON_GetArrayItem(array, i);
            cJSON *index = cJSON_GetObjectItem(item, "index");
            cJSON *part_size = cJSON_GetObjectItem(item, "size");
            cJSON *sha = cJSON_GetObjectItem(item, "sha256");
            if (!item || !is_type(index, cJSON_Number) || !is_type(part_size, cJSON_Number) ||
                !is_type(sha, cJSON_String) ||
                !Hex64(sha->valuestring) || index->valueint != i || part_size->valuedouble < 0) {
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
    return !user->empty() && !token->empty() && !upload_id->empty();
}

}  // namespace

int main() {
    const Config config = ReadConfig();
    ignore_signal_pipe();
    hydrastore::MetadataStore metadata(config.mysql_host, config.mysql_port, config.mysql_user,
                                       config.mysql_password, config.mysql_database);
    const std::unique_ptr<hydrastore::BlobStore> blobs = hydrastore::MakeFastDFSBlobStore(config.fdfs_client.c_str());

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
            std::string user, token;
            if (!Credentials(&user, &token)) { DrainBody(); JsonResponse(Error(4, "token验证失败")); continue; }
            const std::string upload_id = Query("uploadId");
            const std::string index_value = Query("index");
            const std::string claimed_hash = Query("sha256");
            if (upload_id.empty() || index_value.empty() || !Hex64(claimed_hash)) {
                DrainBody(); JsonResponse(Error(1, "invalid part")); continue;
            }
            const int index = atoi(index_value.c_str());
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
            if (expected.state == "READY") { DrainBody(); JsonResponse(Error(0, nullptr)); continue; }
            const char *length = getenv("CONTENT_LENGTH");
            const long content_length = length ? strtol(length, nullptr, 10) : -1;
            if (content_length != expected.spec.size) {
                DrainBody(); JsonResponse(Error(1, "part size mismatch")); continue;
            }
            hydrastore::PartClaim claim;
            bool claimed = false;
            for (int attempt = 0; attempt < 3 && !claimed; ++attempt) {
                claimed = metadata.ClaimPartUpload(upload_id, index, &claim);
                if (!claimed && attempt < 2) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20 * (attempt + 1)));
                }
            }
            if (!claimed) {
                DrainBody();
                JsonResponse(Error(2, "part is being uploaded by another session"));
                continue;
            }
            InputContext input;
            hydrastore::BlobSource source{&input, ReadAndHash};
            std::string backend_id;
            const bool stored = blobs->Put(source, expected.spec.size, "", &backend_id);
            const bool hash_ok = stored && input.bytes == expected.spec.size &&
                                 input.hash.FinalHex() == expected.spec.sha256;
            if (!hash_ok) {
                if (stored) blobs->Delete(backend_id);
                metadata.MarkPartFailed(upload_id, index, upload_id, claim.lease_epoch);
                JsonResponse(Error(1, "sha256 or blob upload failed")); continue;
            }
            Failpoint("after_blob_put");
            if (!metadata.MarkPartReady(upload_id, index, upload_id, claim.lease_epoch, backend_id)) {
                blobs->Delete(backend_id);
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
