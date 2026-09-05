#include "metadata_store.h"

#include <mysql/mysql.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>

namespace hydrastore {
namespace {

std::string NewId() {
    static std::mt19937_64 generator(std::random_device{}());
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (int i = 0; i < 2; ++i) {
        out << std::setw(16) << generator();
    }
    return out.str();
}

std::string ToString(std::int64_t value) {
    return std::to_string(value);
}

bool IsReady(const std::string &state) {
    return state == "READY";
}

std::string FileSuffix(const std::string &filename) {
    const std::size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= filename.size()) return {};
    std::string suffix = filename.substr(dot + 1);
    if (suffix.size() > 31) return {};
    for (char &ch : suffix) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return suffix;
}

bool IsParseableType(const std::string &type) {
    static const char *const types[] = {
        "txt", "md", "csv", "json", "xml", "html", "htm", "log",
        "c", "cpp", "h", "hpp", "py", "js", "ts", "jsx", "tsx",
        "css", "java", "go", "rs", "rb", "php", "sh", "bat", "yaml", "yml",
        "pdf", "png", "jpg", "jpeg", "gif", "bmp", "webp", nullptr
    };
    for (const char *candidate : types) {
        if (candidate && type == candidate) return true;
    }
    return false;
}

long long NowSeconds() {
    return static_cast<long long>(std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now()));
}

bool DbFailpoint(const char *name) {
    const char *configured = std::getenv("HYDRA_DB_FAILPOINT");
    return configured && std::strcmp(configured, name) == 0;
}

}  // namespace

MetadataStore::MetadataStore(const std::string &host, unsigned int port,
                             const std::string &user, const std::string &password,
                             const std::string &database)
    : host_(host), port_(port), user_(user), password_(password), database_(database),
      connection_(mysql_init(nullptr)) {}

MetadataStore::~MetadataStore() {
    Close();
}

void MetadataStore::Close() {
    if (connection_) {
        mysql_close(connection_);
        connection_ = nullptr;
    }
}

bool MetadataStore::Txn(const char *sql) {
    if (!connection_ && !(connection_ = mysql_init(nullptr))) {
        return false;
    }
    if (connection_->thread_id == 0 &&
        !mysql_real_connect(connection_, host_.c_str(), user_.c_str(), password_.c_str(),
                            database_.c_str(), port_, nullptr, 0)) {
        return false;
    }
    return mysql_query(connection_, sql) == 0;
}

bool MetadataStore::Exec(const std::string &sql, const std::vector<std::string> &params) {
    if (!connection_ && !(connection_ = mysql_init(nullptr))) {
        return false;
    }
    if (connection_->thread_id == 0 &&
        !mysql_real_connect(connection_, host_.c_str(), user_.c_str(), password_.c_str(),
                            database_.c_str(), port_, nullptr, 0)) {
        return false;
    }

    MYSQL_STMT *statement = mysql_stmt_init(connection_);
    if (!statement || mysql_stmt_prepare(statement, sql.c_str(), sql.size()) != 0) {
        if (statement) mysql_stmt_close(statement);
        return false;
    }

    std::vector<MYSQL_BIND> binds(params.size());
    std::vector<unsigned long> lengths(params.size());
    for (std::size_t i = 0; i < params.size(); ++i) {
        memset(&binds[i], 0, sizeof(MYSQL_BIND));
        lengths[i] = static_cast<unsigned long>(params[i].size());
        binds[i].buffer_type = MYSQL_TYPE_STRING;
        binds[i].buffer = const_cast<char *>(params[i].data());
        binds[i].buffer_length = lengths[i];
        binds[i].length = &lengths[i];
    }
    const bool bound = params.empty() || mysql_stmt_bind_param(statement, binds.data()) == 0;
    const bool ok = bound && mysql_stmt_execute(statement) == 0;
    if (ok) {
        last_affected_rows_ = mysql_stmt_affected_rows(statement);
        last_insert_id_ = static_cast<long long>(mysql_stmt_insert_id(statement));
    }
    mysql_stmt_close(statement);
    return ok;
}

bool MetadataStore::Query(const std::string &sql, const std::vector<std::string> &params,
                          std::vector<std::vector<std::string>> *rows) {
    if (!rows || !Exec("SET NAMES utf8mb4")) {
        return false;
    }
    MYSQL_STMT *statement = mysql_stmt_init(connection_);
    if (!statement || mysql_stmt_prepare(statement, sql.c_str(), sql.size()) != 0) {
        if (statement) mysql_stmt_close(statement);
        return false;
    }
    std::vector<MYSQL_BIND> binds(params.size());
    std::vector<unsigned long> lengths(params.size());
    for (std::size_t i = 0; i < params.size(); ++i) {
        memset(&binds[i], 0, sizeof(MYSQL_BIND));
        lengths[i] = static_cast<unsigned long>(params[i].size());
        binds[i].buffer_type = MYSQL_TYPE_STRING;
        binds[i].buffer = const_cast<char *>(params[i].data());
        binds[i].buffer_length = lengths[i];
        binds[i].length = &lengths[i];
    }
    if ((!params.empty() && mysql_stmt_bind_param(statement, binds.data()) != 0) ||
        mysql_stmt_execute(statement) != 0 || mysql_stmt_store_result(statement) != 0) {
        mysql_stmt_close(statement);
        return false;
    }
    MYSQL_RES *metadata = mysql_stmt_result_metadata(statement);
    if (!metadata) {
        mysql_stmt_close(statement);
        return false;
    }
    const unsigned int field_count = mysql_num_fields(metadata);
    std::vector<std::vector<char>> buffers(field_count, std::vector<char>(4096));
    std::vector<unsigned long> result_lengths(field_count);
    std::vector<MYSQL_BIND> result_binds(field_count);
    for (unsigned int i = 0; i < field_count; ++i) {
        memset(&result_binds[i], 0, sizeof(MYSQL_BIND));
        result_binds[i].buffer_type = MYSQL_TYPE_STRING;
        result_binds[i].buffer = buffers[i].data();
        result_binds[i].buffer_length = buffers[i].size();
        result_binds[i].length = &result_lengths[i];
    }
    if (mysql_stmt_bind_result(statement, result_binds.data()) != 0) {
        mysql_free_result(metadata);
        mysql_stmt_close(statement);
        return false;
    }
    while (true) {
        const int result = mysql_stmt_fetch(statement);
        if (result == MYSQL_NO_DATA) break;
        if (result == 1) {
            mysql_free_result(metadata);
            mysql_stmt_close(statement);
            return false;
        }
        std::vector<std::string> row;
        row.reserve(field_count);
        for (unsigned int i = 0; i < field_count; ++i) {
            row.emplace_back(buffers[i].data(), result_lengths[i]);
        }
        rows->push_back(std::move(row));
    }
    mysql_free_result(metadata);
    mysql_stmt_close(statement);
    return true;
}

long long MetadataStore::LastInsertId() const {
    return last_insert_id_;
}

unsigned long long MetadataStore::AffectedRows() const {
    return last_affected_rows_;
}

bool MetadataStore::ReadSession(const std::vector<std::string> &row,
                                UploadSession *session) const {
    if (!session || row.size() < 9) return false;
    session->id = row[0];
    session->user = row[1];
    session->filename = row[2];
    session->size = std::stoll(row[3]);
    session->content_digest = row[4];
    session->chunk_count = std::stoi(row[5]);
    session->state = row[6];
    session->object_id = row[7];
    session->manifest_id = row[8].empty() ? 0 : std::stoll(row[8]);
    return true;
}

bool MetadataStore::GetSession(const std::string &upload_id, const std::string &user,
                               UploadSession *session) {
    std::vector<std::vector<std::string>> rows;
    if (!Query("SELECT id,user,filename,size,content_digest,chunk_count,state,"
               "COALESCE(object_id,''),COALESCE(manifest_id,0) FROM upload_session "
               "WHERE id=? AND user=? LIMIT 1", {upload_id, user}, &rows) || rows.empty()) {
        return false;
    }
    return ReadSession(rows[0], session) && ReadObjectInfo(session);
}

bool MetadataStore::ReadStatuses(const std::string &upload_id,
                                 const std::string &current_upload_id,
                                 std::vector<PartStatus> *statuses) {
    std::vector<std::vector<std::string>> rows;
    if (!Query("SELECT p.part_index,p.size,p.sha256,COALESCE(c.state,'MISSING'),"
               "COALESCE(c.backend_file_id,''),COALESCE(c.id,0),"
               "COALESCE(c.owner_upload_id,''),COALESCE(UNIX_TIMESTAMP(c.lease_until),0),"
               "COALESCE(c.lease_epoch,0) FROM upload_part p LEFT JOIN chunk_blob c ON c.id=p.chunk_id "
               "WHERE p.upload_id=? ORDER BY p.part_index", {upload_id}, &rows)) {
        return false;
    }
    statuses->clear();
    const long long now = NowSeconds();
    for (const auto &row : rows) {
        if (row.size() < 9) return false;
        PartStatus status;
        status.spec.index = std::stoi(row[0]);
        status.spec.size = std::stoll(row[1]);
        status.spec.sha256 = row[2];
        status.state = row[3];
        status.backend_file_id = row[4];
        status.chunk_id = std::stoll(row[5]);
        status.owner_upload_id = row[6];
        status.lease_until = std::stoll(row[7]);
        status.lease_epoch = std::stoll(row[8]);
        if (status.state == "READY") {
            status.availability = PartAvailability::kReady;
        } else if (status.state == "UPLOADING" &&
                   status.owner_upload_id == current_upload_id) {
            status.availability = PartAvailability::kUploadable;
        } else if (status.state == "UPLOADING" &&
                   status.owner_upload_id != current_upload_id &&
                   status.lease_until > now) {
            status.availability = PartAvailability::kWaiting;
        } else {
            status.availability = PartAvailability::kMissing;
        }
        statuses->push_back(std::move(status));
    }
    return true;
}

bool MetadataStore::ReadObjectInfo(UploadSession *session) {
    if (!session) return false;
    std::vector<std::vector<std::string>> rows;
    if (!Query("SELECT COALESCE(storage_mode,'legacy'),COALESCE(object_id,''),"
               "COALESCE(manifest_id,0),COALESCE(url,'') FROM file_info "
               "WHERE md5=? LIMIT 1", {session->content_digest}, &rows)) {
        return false;
    }
    if (!rows.empty()) {
        session->storage_mode = rows[0][0];
        session->object_id = rows[0][1];
        session->manifest_id = rows[0][2].empty() ? 0 : std::stoll(rows[0][2]);
        session->legacy_url = rows[0][3];
        if (session->storage_mode == "legacy") session->object_id.clear();
    }
    return true;
}

bool MetadataStore::ManifestMatchesUpload(std::int64_t manifest_id,
                                          const std::string &upload_id) {
    std::vector<std::vector<std::string>> manifest_meta;
    std::vector<std::vector<std::string>> upload_meta;
    if (!Query("SELECT total_size,chunk_count FROM object_manifest WHERE id=?",
               {ToString(manifest_id)}, &manifest_meta) || manifest_meta.size() != 1 ||
        !Query("SELECT size,chunk_count FROM upload_session WHERE id=?",
               {upload_id}, &upload_meta) || upload_meta.size() != 1 ||
        manifest_meta[0][0] != upload_meta[0][0] || manifest_meta[0][1] != upload_meta[0][1]) {
        return false;
    }

    std::vector<std::vector<std::string>> manifest_parts;
    std::vector<std::vector<std::string>> upload_parts;
    if (!Query("SELECT mc.part_index,mc.size,c.sha256 FROM manifest_chunk mc "
               "JOIN chunk_blob c ON c.id=mc.chunk_id WHERE mc.manifest_id=? "
               "ORDER BY mc.part_index", {ToString(manifest_id)}, &manifest_parts) ||
        !Query("SELECT part_index,size,sha256 FROM upload_part WHERE upload_id=? "
               "ORDER BY part_index", {upload_id}, &upload_parts) ||
        manifest_parts.size() != upload_parts.size()) {
        return false;
    }
    for (std::size_t i = 0; i < manifest_parts.size(); ++i) {
        if (manifest_parts[i].size() < 3 || upload_parts[i].size() < 3 ||
            manifest_parts[i][0] != upload_parts[i][0] ||
            manifest_parts[i][1] != upload_parts[i][1] ||
            manifest_parts[i][2] != upload_parts[i][2]) {
            return false;
        }
    }
    return true;
}

bool MetadataStore::InitOrResume(const std::string &upload_id, const std::string &user,
                                 const std::string &filename, std::int64_t object_size,
                                 const std::string &content_digest,
                                 const std::vector<PartSpec> &parts,
                                 UploadSession *session,
                                 std::vector<PartStatus> *statuses) {
    if (parts.empty()) {
        return !upload_id.empty() && GetSession(upload_id, user, session) &&
               ReadStatuses(upload_id, upload_id, statuses);
    }
    if (!Txn("START TRANSACTION")) return false;
    auto rollback = [this]() { Txn("ROLLBACK"); };
    std::string id = upload_id.empty() ? NewId() : upload_id;
    std::vector<std::vector<std::string>> existing;
    if (!Query("SELECT id,user,filename,size,content_digest,chunk_count,state,"
               "COALESCE(object_id,''),COALESCE(manifest_id,0) FROM upload_session "
               "WHERE id=? AND user=? FOR UPDATE", {id, user}, &existing)) {
        rollback(); return false;
    }
    if (existing.empty()) {
        if (upload_id.empty()) {
            std::vector<std::vector<std::string>> file;
            // A digest identifies bytes; it is not proof that this user owns them.
            // Restrict instant-hit to an existing private relation for this user.
            if (!Query("SELECT f.storage_mode,COALESCE(f.object_id,''),COALESCE(f.manifest_id,0) "
                       "FROM file_info f JOIN user_file_list u ON u.md5=f.md5 "
                       "WHERE f.md5=? AND u.user=? FOR UPDATE", {content_digest, user}, &file)) {
                rollback(); return false;
            }
            if (!file.empty()) {
                if (!Exec("INSERT INTO upload_session(id,user,filename,size,content_digest,"
                          "chunk_count,state,object_id,manifest_id) VALUES(?,?,?,?,?,?,'COMMITTED',?,?)",
                          {id, user, filename, ToString(object_size), content_digest,
                           ToString(static_cast<std::int64_t>(parts.size())), file[0][1], file[0][2]})) {
                    rollback(); return false;
                }
                if (!Exec("INSERT IGNORE INTO user_file_list(user,md5,file_name,shared_status,pv) "
                          "VALUES(?,?,?,0,0)", {user, content_digest, filename})) {
                    rollback(); return false;
                }
                if (AffectedRows() == 1 &&
                    (!Exec("INSERT INTO user_file_count(user,count) VALUES(?,1) "
                           "ON DUPLICATE KEY UPDATE count=count+1", {user}) ||
                     !Exec("UPDATE file_info SET count=count+1 WHERE md5=?", {content_digest}))) {
                    rollback(); return false;
                }
                if (!Txn("COMMIT") || !GetSession(id, user, session)) {
                    rollback(); return false;
                }
                statuses->clear();
                return true;
            }
        }
        if (!Exec("INSERT INTO upload_session(id,user,filename,size,content_digest,chunk_count,state) "
                  "VALUES(?,?,?,?,?,?,'UPLOADING')",
                  {id, user, filename, ToString(object_size), content_digest,
                   ToString(static_cast<std::int64_t>(parts.size()))})) {
            rollback(); return false;
        }
    } else if (!ReadSession(existing[0], session) || session->content_digest != content_digest ||
               session->size != object_size || session->chunk_count != static_cast<int>(parts.size())) {
        rollback(); return false;
    }
    if (!existing.empty() && session->state != "INIT" && session->state != "UPLOADING" &&
        session->state != "COMMITTING" && session->state != "COMMITTED") {
        rollback(); return false;
    }
    if (!existing.empty() && session->state == "COMMITTED") {
        if (!Txn("COMMIT") || !GetSession(id, user, session) ||
            !ReadStatuses(id, id, statuses)) {
            rollback(); return false;
        }
        return true;
    }

    for (const PartSpec &part : parts) {
        if (part.index < 0 || part.size < 0 || part.sha256.size() != 64) {
            rollback(); return false;
        }
        if (!Exec("INSERT IGNORE INTO upload_part(upload_id,part_index,size,sha256,state) "
                  "VALUES(?,?,?,?, 'MISSING')",
                  {id, ToString(part.index), ToString(part.size), part.sha256})) {
            rollback(); return false;
        }
        if (AffectedRows() == 0) {
            std::vector<std::vector<std::string>> declared;
            if (!Query("SELECT size,sha256 FROM upload_part WHERE upload_id=? AND part_index=? FOR UPDATE",
                       {id, ToString(part.index)}, &declared) || declared.size() != 1 ||
                declared[0][0] != ToString(part.size) || declared[0][1] != part.sha256) {
                rollback(); return false;
            }
        }
        std::vector<std::vector<std::string>> chunk;
        if (!Query("SELECT id,state,COALESCE(owner_upload_id,''),"
                   "COALESCE(UNIX_TIMESTAMP(lease_until),0),COALESCE(lease_epoch,0) FROM chunk_blob "
                   "WHERE sha256=? AND size=? FOR UPDATE",
                   {part.sha256, ToString(part.size)}, &chunk)) {
            rollback(); return false;
        }
        std::string chunk_id;
        std::string state;
        if (chunk.empty()) {
            if (!Exec("INSERT IGNORE INTO chunk_blob(sha256,size,state,owner_upload_id,lease_until) "
                      "VALUES(?,?, 'UPLOADING',?, DATE_ADD(NOW(), INTERVAL 15 MINUTE))",
                      {part.sha256, ToString(part.size), id})) {
                rollback(); return false;
            }
            if (AffectedRows() == 1 && LastInsertId() != 0) {
                chunk_id = ToString(LastInsertId());
                state = "UPLOADING";
            } else {
                if (!Query("SELECT id,state,COALESCE(owner_upload_id,''),"
                           "COALESCE(UNIX_TIMESTAMP(lease_until),0),COALESCE(lease_epoch,0) FROM chunk_blob "
                           "WHERE sha256=? AND size=? FOR UPDATE",
                           {part.sha256, ToString(part.size)}, &chunk) || chunk.empty()) {
                    rollback(); return false;
                }
                chunk_id = chunk[0][0];
                state = chunk[0][1];
            }
        } else {
            chunk_id = chunk[0][0];
            state = chunk[0][1];
            const long long lease = std::stoll(chunk[0][3]);
            const long long now = NowSeconds();
            if (state == "UPLOADING" && chunk[0][2] == id) {
                if (!Exec("UPDATE chunk_blob SET owner_upload_id=?,lease_until="
                          "DATE_ADD(NOW(), INTERVAL 15 MINUTE),state='UPLOADING' WHERE id=?",
                          {id, chunk_id})) {
                    rollback(); return false;
                }
            } else if (state == "UPLOADING" && lease < now) {
                if (!Exec("UPDATE chunk_blob SET owner_upload_id=?,lease_until="
                          "DATE_ADD(NOW(), INTERVAL 15 MINUTE),lease_epoch=lease_epoch+1,"
                          "state='UPLOADING' WHERE id=? AND state='UPLOADING' AND "
                          "(lease_until IS NULL OR lease_until<NOW())",
                          {id, chunk_id}) || AffectedRows() != 1) {
                    rollback(); return false;
                }
            } else if (state == "GC_PENDING" || state == "FAILED") {
                if (!Exec("UPDATE chunk_blob SET owner_upload_id=?,lease_until="
                          "DATE_ADD(NOW(), INTERVAL 15 MINUTE),lease_epoch=lease_epoch+1,"
                          "state='UPLOADING' WHERE id=? AND ref_count=0 AND state IN ('GC_PENDING','FAILED')",
                          {id, chunk_id}) || AffectedRows() != 1) {
                    rollback(); return false;
                }
                state = "UPLOADING";
            } else if (state == "DELETING") {
                // GC owns this row until FinishGc. Never bind an active upload to
                // a blob that is already being removed from the backend.
                rollback();
                return false;
            }
        }
        if (!Exec("UPDATE upload_part SET size=?,sha256=?,chunk_id=?,state=? "
                  "WHERE upload_id=? AND part_index=?",
                  {ToString(part.size), part.sha256, chunk_id,
                   IsReady(state) ? "READY" : state, id, ToString(part.index)})) {
            rollback(); return false;
        }
    }
    if (!Txn("COMMIT") || !GetSession(id, user, session) || !ReadStatuses(id, id, statuses)) {
        rollback(); return false;
    }
    return true;
}

bool MetadataStore::MarkPartReady(const std::string &upload_id, int part_index,
                                  const std::string &owner_upload_id,
                                  std::int64_t lease_epoch,
                                  const std::string &backend_file_id) {
    if (!Txn("START TRANSACTION")) return false;
    std::vector<std::vector<std::string>> chunk;
    bool ok = Query("SELECT chunk_id FROM upload_part WHERE upload_id=? AND part_index=? FOR UPDATE",
                    {upload_id, ToString(part_index)}, &chunk) && chunk.size() == 1 &&
              Exec("UPDATE chunk_blob SET state='READY',backend_file_id=?,owner_upload_id=NULL,"
                   "lease_until=NULL WHERE id=? AND state='UPLOADING' AND owner_upload_id=? "
                   "AND lease_epoch=?", {backend_file_id, chunk[0][0], owner_upload_id,
                                          ToString(lease_epoch)}) && AffectedRows() == 1 &&
              Exec("UPDATE upload_part SET state='READY' WHERE chunk_id=?", {chunk[0][0]}) &&
              Txn("COMMIT");
    if (!ok) Txn("ROLLBACK");
    return ok;
}

bool MetadataStore::RecordPartBackend(const std::string &upload_id, int part_index,
                                      const std::string &owner_upload_id,
                                      std::int64_t lease_epoch,
                                      const std::string &backend_file_id) {
    if (backend_file_id.empty() || !Txn("START TRANSACTION")) return false;
    std::vector<std::vector<std::string>> chunk;
    bool ok = Query("SELECT chunk_id FROM upload_part WHERE upload_id=? AND part_index=? FOR UPDATE",
                    {upload_id, ToString(part_index)}, &chunk) && chunk.size() == 1 &&
              Exec("UPDATE chunk_blob SET backend_file_id=? WHERE id=? AND state='UPLOADING' "
                   "AND owner_upload_id=? AND lease_epoch=? AND ref_count=0",
                   {backend_file_id, chunk[0][0], owner_upload_id, ToString(lease_epoch)}) &&
              AffectedRows() == 1 && Txn("COMMIT");
    if (!ok) Txn("ROLLBACK");
    return ok;
}

bool MetadataStore::ClaimPartUpload(const std::string &upload_id, int part_index,
                                    PartClaim *claim) {
    if (!claim) return false;
    *claim = PartClaim();
    if (!Txn("START TRANSACTION")) return false;
    std::vector<std::vector<std::string>> rows;
    if (!Query("SELECT c.id,c.state,COALESCE(c.backend_file_id,''),"
               "COALESCE(c.owner_upload_id,''),COALESCE(UNIX_TIMESTAMP(c.lease_until),0),"
               "COALESCE(c.lease_epoch,0),c.ref_count FROM upload_part p "
               "JOIN chunk_blob c ON c.id=p.chunk_id WHERE p.upload_id=? AND p.part_index=? FOR UPDATE",
               {upload_id, ToString(part_index)}, &rows) || rows.size() != 1) {
        Txn("ROLLBACK");
        return false;
    }
    const std::string &chunk_id = rows[0][0];
    if ((rows[0][6] != "0" && rows[0][6] != "") || rows[0][1] == "READY") {
        Txn("ROLLBACK");
        return false;
    }
    const long long lease = std::stoll(rows[0][4]);
    const long long now = NowSeconds();
    // A second request in the same session must not start another physical
    // PUT while the first owner has not recorded a recoverable backend ID.
    if (rows[0][1] == "UPLOADING" && rows[0][3] == upload_id &&
        rows[0][2].empty() && lease >= now && rows[0][5] != "0") {
        Txn("ROLLBACK");
        return false;
    }
    if (!(rows[0][1] == "UPLOADING" && rows[0][3] == upload_id && !rows[0][2].empty())) {
        if (!Exec("UPDATE chunk_blob SET state='UPLOADING',owner_upload_id=?,"
                  "lease_until=DATE_ADD(NOW(),INTERVAL 15 MINUTE),lease_epoch=lease_epoch+1,"
                  "gc_after=NULL WHERE id=? AND ref_count=0 AND ("
                  "state IN ('FAILED','GC_PENDING') OR "
                  "(state='UPLOADING' AND (owner_upload_id=? OR lease_until IS NULL OR lease_until<NOW())))",
                  {upload_id, chunk_id, upload_id}) || AffectedRows() != 1) {
            Txn("ROLLBACK");
            return false;
        }
    }
    std::vector<std::vector<std::string>> claimed;
    if (!Query("SELECT id,state,lease_epoch,COALESCE(backend_file_id,'') FROM chunk_blob WHERE id=?", {chunk_id}, &claimed) ||
        claimed.size() != 1 || !Txn("COMMIT")) {
        Txn("ROLLBACK");
        return false;
    }
    claim->granted = true;
    claim->chunk_id = std::stoll(claimed[0][0]);
    claim->state = claimed[0][1];
    claim->lease_epoch = std::stoll(claimed[0][2]);
    claim->backend_file_id = claimed[0][3];
    return true;
}

bool MetadataStore::MarkPartFailed(const std::string &upload_id, int part_index,
                                   const std::string &owner_upload_id,
                                   std::int64_t lease_epoch) {
    if (!Txn("START TRANSACTION")) return false;
    std::vector<std::vector<std::string>> chunk;
    bool ok = Query("SELECT chunk_id FROM upload_part WHERE upload_id=? AND part_index=? FOR UPDATE",
                    {upload_id, ToString(part_index)}, &chunk) && chunk.size() == 1 &&
              Exec("UPDATE chunk_blob SET state='FAILED',backend_file_id=NULL,owner_upload_id=NULL,"
                   "lease_until=NULL,retry_count=retry_count+1 WHERE id=? AND owner_upload_id=? "
                   "AND lease_epoch=?",
                   {chunk[0][0], owner_upload_id, ToString(lease_epoch)}) && AffectedRows() == 1 &&
              Exec("UPDATE upload_part SET state='MISSING' WHERE upload_id=? AND part_index=?",
                   {upload_id, ToString(part_index)}) && Txn("COMMIT");
    if (!ok) Txn("ROLLBACK");
    return ok;
}

bool MetadataStore::Commit(const std::string &upload_id, const std::string &user,
                           UploadSession *session) {
    if (!Txn("START TRANSACTION")) return false;
    auto rollback = [this]() { Txn("ROLLBACK"); };
    std::vector<std::vector<std::string>> rows;
    if (!Query("SELECT id,user,filename,size,content_digest,chunk_count,state,"
               "COALESCE(object_id,''),COALESCE(manifest_id,0) FROM upload_session "
               "WHERE id=? AND user=? FOR UPDATE", {upload_id, user}, &rows) || rows.empty() ||
        !ReadSession(rows[0], session)) {
        rollback(); return false;
    }
    if (session->state == "COMMITTED") return Txn("COMMIT");
    if (session->state != "UPLOADING" && session->state != "COMMITTING") {
        rollback(); return false;
    }
    std::vector<std::vector<std::string>> incomplete;
    if (!Query("SELECT part_index FROM upload_part WHERE upload_id=? AND state<>'READY' "
               "FOR UPDATE", {upload_id}, &incomplete) || !incomplete.empty()) {
        rollback(); return false;
    }
    if (!Exec("UPDATE upload_session SET state='COMMITTING' WHERE id=?", {upload_id})) {
        rollback(); return false;
    }

    std::vector<std::vector<std::string>> file;
    if (!Query("SELECT id,storage_mode,COALESCE(manifest_id,0),COALESCE(object_id,''),"
               "COALESCE(file_id,''),COALESCE(url,''),count FROM file_info WHERE md5=? FOR UPDATE",
               {session->content_digest}, &file)) {
        rollback(); return false;
    }
    std::string object_id;
    std::string manifest_id;
    if (file.empty()) {
        object_id = NewId();
        if (!Exec("INSERT INTO object_manifest(object_id,total_size,content_digest,chunk_count,state) "
                  "VALUES(?,?,?,?,'COMMITTED')",
                  {object_id, ToString(session->size), session->content_digest,
                   ToString(session->chunk_count)})) {
            rollback(); return false;
        }
        if (DbFailpoint("after_manifest_insert")) { rollback(); return false; }
        manifest_id = ToString(LastInsertId());
        std::vector<std::vector<std::string>> parts;
        if (!Query("SELECT part_index,chunk_id,size FROM upload_part WHERE upload_id=? "
                   "ORDER BY part_index", {upload_id}, &parts)) {
            rollback(); return false;
        }
        for (const auto &part : parts) {
            if (!Exec("INSERT INTO manifest_chunk(manifest_id,part_index,chunk_id,size) "
                      "VALUES(?,?,?,?)", {manifest_id, part[0], part[1], part[2]})) {
                rollback(); return false;
            }
            if (!Exec("UPDATE chunk_blob SET ref_count=ref_count+1,gc_after=NULL "
                      "WHERE id=? AND state='READY'", {part[1]}) || AffectedRows() != 1) {
                rollback(); return false;
            }
            if (DbFailpoint("after_ref_increment")) { rollback(); return false; }
        }
        if (!Exec("INSERT INTO file_info(md5,file_id,url,size,type,count,storage_mode,manifest_id,"
                  "object_id,content_digest) VALUES(?,?,?,?,?,1,'manifest',?,?,?)",
                  {session->content_digest, "", "/api/object/download?objectId=" + object_id,
                   ToString(session->size), FileSuffix(session->filename), manifest_id,
                   object_id, session->content_digest})) {
            rollback(); return false;
        }
    } else {
        object_id = file[0][3];
        manifest_id = file[0][2];
        session->storage_mode = file[0][1];
        session->legacy_url = file[0][5];
        if (session->storage_mode == "legacy") object_id.clear();
        if (session->storage_mode == "manifest") {
            if (manifest_id.empty() || !ManifestMatchesUpload(std::stoll(manifest_id), upload_id)) {
                rollback(); return false;
            }
        } else {
            std::vector<std::vector<std::string>> owned;
            if (!Query("SELECT 1 FROM user_file_list WHERE user=? AND md5=? LIMIT 1 FOR UPDATE",
                       {user, session->content_digest}, &owned) || owned.empty()) {
                rollback(); return false;
            }
        }
        if (!Exec("UPDATE chunk_blob c JOIN upload_part p ON p.chunk_id=c.id SET "
                  "c.state='GC_PENDING',c.gc_after=DATE_ADD(NOW(),INTERVAL 1 HOUR),"
                  "c.owner_upload_id=NULL,c.lease_until=NULL WHERE p.upload_id=? "
                  "AND c.state='READY' AND c.ref_count=0 AND NOT EXISTS ("
                  "SELECT 1 FROM upload_part p2 JOIN upload_session s2 ON s2.id=p2.upload_id "
                  "WHERE p2.chunk_id=c.id AND p2.upload_id<>? AND "
                  "s2.state IN ('INIT','UPLOADING','COMMITTING'))", {upload_id, upload_id})) {
            rollback(); return false;
        }
    }
    if (!Exec("INSERT IGNORE INTO user_file_list(user,md5,file_name,shared_status,pv) "
              "VALUES(?,?,?,0,0)", {user, session->content_digest, session->filename})) {
        rollback(); return false;
    }
    if (DbFailpoint("after_file_relation")) { rollback(); return false; }
    const bool new_relation = AffectedRows() == 1;
    if (new_relation && !Exec("INSERT INTO user_file_count(user,count) VALUES(?,1) "
                              "ON DUPLICATE KEY UPDATE count=count+1", {user})) {
        rollback(); return false;
    }
    if (!file.empty() && new_relation &&
        !Exec("UPDATE file_info SET count=count+1 WHERE id=?", {file[0][0]})) {
        rollback(); return false;
    }
    if (!EnqueueParseTask(user, session->content_digest, FileSuffix(session->filename),
                          "hydrastore_v2")) {
        rollback(); return false;
    }
    if (!Exec("UPDATE upload_session SET state='COMMITTED',object_id=?,manifest_id=? WHERE id=?",
              {object_id, manifest_id, upload_id}) || !Txn("COMMIT")) {
        rollback(); return false;
    }
    session->state = "COMMITTED";
    session->object_id = object_id;
    if (session->storage_mode.empty()) session->storage_mode = "manifest";
    session->manifest_id = manifest_id.empty() ? 0 : std::stoll(manifest_id);
    return true;
}

bool MetadataStore::Abort(const std::string &upload_id, const std::string &user) {
    if (!Txn("START TRANSACTION")) return false;
    bool ok = Exec("UPDATE upload_session SET state='ABORTED' WHERE id=? AND user=? "
                   "AND state NOT IN ('COMMITTED','ABORTED')", {upload_id, user}) &&
              Exec("UPDATE chunk_blob c JOIN upload_part p ON p.chunk_id=c.id SET "
                   "c.state='GC_PENDING',c.gc_after=DATE_ADD(NOW(),INTERVAL 1 HOUR),"
                   "c.owner_upload_id=NULL,c.lease_until=NULL WHERE p.upload_id=? "
                   "AND c.ref_count=0 AND ((c.state IN ('UPLOADING','FAILED') AND "
                   "c.owner_upload_id=?) OR (c.state='READY' AND NOT EXISTS ("
                   "SELECT 1 FROM upload_part p2 JOIN upload_session s2 ON s2.id=p2.upload_id "
                   "WHERE p2.chunk_id=c.id AND p2.upload_id<>? AND "
                   "s2.state IN ('INIT','UPLOADING','COMMITTING'))))",
                   {upload_id, upload_id, upload_id}) &&
              Txn("COMMIT");
    if (!ok) Txn("ROLLBACK");
    return ok;
}

bool MetadataStore::DeleteObjectForUser(const std::string &object_id, const std::string &user) {
    if (!Txn("START TRANSACTION")) return false;
    auto rollback = [this]() { Txn("ROLLBACK"); };
    std::vector<std::vector<std::string>> rows;
    if (!Query("SELECT f.id,f.md5,COALESCE(f.manifest_id,0),f.count,ufl.file_name "
               "FROM file_info f JOIN user_file_list ufl ON ufl.md5=f.md5 "
               "WHERE f.object_id=? AND ufl.user=? FOR UPDATE", {object_id, user}, &rows) || rows.empty()) {
        rollback(); return false;
    }
    const std::string file_id = rows[0][0];
    const std::string digest = rows[0][1];
    const std::string manifest_id = rows[0][2];
    const int references = std::stoi(rows[0][3]);
    if (!Exec("DELETE FROM user_file_list WHERE user=? AND md5=? AND file_name=?",
              {user, digest, rows[0][4]})) {
        rollback(); return false;
    }
    if (!Exec("DELETE FROM share_file_list WHERE user=? AND md5=? AND file_name=?",
              {user, digest, rows[0][4]})) {
        rollback(); return false;
    }
    const bool had_share = AffectedRows() > 0;
    if (had_share && !Exec("UPDATE user_file_count SET count=GREATEST(count-1,0) WHERE user=?",
                           {"FILE_PUBLIC_COUNT"})) {
        rollback(); return false;
    }
    std::vector<std::vector<std::string>> remaining;
    if (!Query("SELECT 1 FROM user_file_list WHERE user=? AND md5=? AND file_name=? LIMIT 1",
               {user, digest, rows[0][4]}, &remaining) || !remaining.empty() ||
        !Exec("UPDATE user_file_count SET count=GREATEST(count-1,0) WHERE user=?", {user})) {
        rollback(); return false;
    }
    if (references > 1) {
        if (!Exec("UPDATE file_info SET count=count-1 WHERE id=?", {file_id}) || !Txn("COMMIT")) {
            rollback(); return false;
        }
        return true;
    }
    std::vector<std::vector<std::string>> chunks;
    if (!Query("SELECT chunk_id FROM manifest_chunk WHERE manifest_id=? FOR UPDATE", {manifest_id}, &chunks)) {
        rollback(); return false;
    }
    for (const auto &chunk : chunks) {
        if (!Exec("UPDATE chunk_blob SET state=IF(ref_count<=1,'GC_PENDING',state),"
                  "gc_after=IF(ref_count<=1,DATE_ADD(NOW(),INTERVAL 1 HOUR),gc_after),"
                  "ref_count=GREATEST(ref_count-1,0) "
                  "WHERE id=?", {chunk[0]})) { rollback(); return false; }
    }
    if (!Exec("DELETE FROM manifest_chunk WHERE manifest_id=?", {manifest_id}) ||
        !Exec("DELETE FROM object_manifest WHERE id=?", {manifest_id}) ||
        !Exec("DELETE FROM file_info WHERE id=?", {file_id}) || !Txn("COMMIT")) {
        rollback(); return false;
    }
    return true;
}

bool MetadataStore::ReconcileExpiredUploads() {
    if (!Txn("START TRANSACTION")) return false;
    if (!Exec("UPDATE upload_session SET state='EXPIRED' WHERE state IN ('INIT','UPLOADING') "
              "AND expires_at<NOW()")) {
        Txn("ROLLBACK"); return false;
    }
    if (!Exec("UPDATE chunk_blob c JOIN upload_part p ON p.chunk_id=c.id "
              "JOIN upload_session s ON s.id=p.upload_id SET c.state='GC_PENDING',"
              "c.gc_after=DATE_ADD(NOW(),INTERVAL 1 HOUR),c.owner_upload_id=NULL,c.lease_until=NULL "
              "WHERE c.ref_count=0 AND ((c.state IN ('UPLOADING','FAILED') AND "
              "c.owner_upload_id=s.id) OR (c.state='READY' AND NOT EXISTS ("
              "SELECT 1 FROM upload_part p2 JOIN upload_session s2 ON s2.id=p2.upload_id "
              "WHERE p2.chunk_id=c.id AND p2.upload_id<>s.id AND "
              "s2.state IN ('INIT','UPLOADING','COMMITTING'))) ) "
              "AND s.state IN ('ABORTED','EXPIRED')")) {
        Txn("ROLLBACK"); return false;
    }
    if (!Txn("COMMIT")) { Txn("ROLLBACK"); return false; }
    return true;
}

bool MetadataStore::ClaimGc(std::vector<GcCandidate> *candidates, int limit) {
    if (!candidates || limit <= 0 || !Txn("START TRANSACTION")) return false;
    if (!Exec("UPDATE chunk_blob SET state='GC_PENDING' WHERE state='DELETING' "
              "AND updated_at<DATE_SUB(NOW(),INTERVAL 15 MINUTE)")) {
        Txn("ROLLBACK"); return false;
    }
    std::vector<std::vector<std::string>> rows;
    const bool selected = Query(
        "SELECT c.id,COALESCE(c.backend_file_id,'') FROM chunk_blob c "
        "WHERE c.ref_count=0 AND c.state='GC_PENDING' AND c.gc_after<NOW() "
        "AND (c.next_retry_at IS NULL OR c.next_retry_at<=NOW()) "
        "AND NOT EXISTS (SELECT 1 FROM upload_part p JOIN upload_session s ON s.id=p.upload_id "
        "WHERE p.chunk_id=c.id AND s.state IN ('INIT','UPLOADING','COMMITTING')) "
        "ORDER BY c.id LIMIT ? FOR UPDATE", {ToString(limit)}, &rows);
    if (!selected) { Txn("ROLLBACK"); return false; }
    candidates->clear();
    for (const auto &row : rows) {
        if (row.size() < 2 || !Exec("UPDATE chunk_blob SET state='DELETING' WHERE id=? "
                                    "AND ref_count=0 AND state='GC_PENDING'", {row[0]})) {
            Txn("ROLLBACK"); return false;
        }
        candidates->push_back({std::stoll(row[0]), row[1]});
    }
    if (!Txn("COMMIT")) { Txn("ROLLBACK"); return false; }
    return true;
}

bool MetadataStore::FinishGc(const GcCandidate &candidate, bool deleted,
                             const std::string &error) {
    if (!Txn("START TRANSACTION")) return false;
    bool ok = false;
    if (deleted) {
        ok = Exec("DELETE FROM upload_part WHERE chunk_id=? AND upload_id IN ("
                  "SELECT id FROM upload_session WHERE state IN ('ABORTED','EXPIRED','COMMITTED'))",
                  {ToString(candidate.id)}) &&
             Exec("DELETE FROM chunk_blob WHERE id=? AND state='DELETING' AND ref_count=0",
                  {ToString(candidate.id)});
    } else {
        ok = Exec("UPDATE chunk_blob SET state='GC_PENDING',retry_count=retry_count+1,"
                  "next_retry_at=DATE_ADD(NOW(),INTERVAL LEAST(86400,POW(2,retry_count+1)) SECOND),"
                  "last_error=? WHERE id=? AND state='DELETING'", {error, ToString(candidate.id)});
    }
    if (ok && Txn("COMMIT")) return true;
    Txn("ROLLBACK");
    if (Txn("START TRANSACTION") &&
        Exec("UPDATE chunk_blob SET state='GC_PENDING',last_error=? WHERE id=? AND state='DELETING'",
             {error.empty() ? "GC database cleanup failed" : error, ToString(candidate.id)}) &&
        Txn("COMMIT")) {
        return false;
    }
    Txn("ROLLBACK");
    return false;
}

bool MetadataStore::GetManifest(const std::string &object_id, const std::string &user,
                                UploadSession *session,
                                std::vector<PartStatus> *parts) {
    if (user.empty()) return false;
    std::vector<std::vector<std::string>> rows;
    const bool visible = Query("SELECT m.object_id,?,ufl.file_name,m.total_size,m.content_digest,m.chunk_count,"
                               "'COMMITTED',m.object_id,m.id FROM object_manifest m JOIN file_info f "
                               "ON f.manifest_id=m.id JOIN user_file_list ufl ON ufl.md5=f.md5 "
                               "WHERE m.object_id=? AND ufl.user=? LIMIT 1",
                               {user, object_id, user}, &rows);
    if (!visible ||
        rows.empty() || !ReadSession(rows[0], session)) return false;
    std::vector<std::vector<std::string>> chunks;
    if (!Query("SELECT m.part_index,m.size,c.sha256,c.state,COALESCE(c.backend_file_id,''),c.id FROM manifest_chunk m "
               "JOIN chunk_blob c ON c.id=m.chunk_id WHERE m.manifest_id=? ORDER BY m.part_index",
               {ToString(session->manifest_id)}, &chunks)) return false;
    parts->clear();
    for (const auto &row : chunks) {
        PartStatus part;
        part.spec.index = std::stoi(row[0]);
        part.spec.size = std::stoll(row[1]);
        part.spec.sha256 = row[2];
        part.state = row[3];
        part.backend_file_id = row[4];
        part.chunk_id = std::stoll(row[5]);
        part.availability = PartAvailability::kReady;
        parts->push_back(std::move(part));
    }
    return true;
}

bool MetadataStore::GetManifestByDigest(const std::string &content_digest,
                                        const std::string &user,
                                        UploadSession *session,
                                        std::vector<PartStatus> *parts) {
    std::vector<std::vector<std::string>> rows;
    if (!Query("SELECT f.object_id FROM file_info f JOIN user_file_list u "
               "ON u.md5=f.md5 WHERE f.md5=? AND u.user=? AND "
               "f.storage_mode='manifest' LIMIT 1", {content_digest, user}, &rows) ||
        rows.size() != 1) {
        return false;
    }
    return GetManifest(rows[0][0], user, session, parts);
}

bool MetadataStore::EnqueueParseTask(const std::string &user,
                                     const std::string &content_digest,
                                     const std::string &type,
                                     const std::string &source) {
    const std::string status = IsParseableType(type) ? "pending" : "skipped";
    return Exec("INSERT INTO ai_parse_task(user,md5,task_type,source,status) "
                "SELECT ?,?,'parse_file',?,? FROM DUAL WHERE NOT EXISTS ("
                "SELECT 1 FROM ai_parse_task WHERE user=? AND md5=? "
                "AND status IN ('pending','running'))",
                {user, content_digest, source, status, user, content_digest});
}

bool MetadataStore::GetSharedManifest(const std::string &share_token,
                                      UploadSession *session,
                                      std::vector<PartStatus> *parts) {
    if (share_token.empty()) return false;
    std::vector<std::vector<std::string>> rows;
    if (!Query("SELECT m.object_id,'','shared',m.total_size,m.content_digest,m.chunk_count,"
               "'COMMITTED',m.object_id,m.id FROM share_file_list s JOIN file_info f "
               "ON f.md5=s.md5 AND f.storage_mode='manifest' JOIN object_manifest m "
               "ON m.id=f.manifest_id WHERE s.share_token=? LIMIT 1", {share_token}, &rows) ||
        rows.empty() || !ReadSession(rows[0], session)) return false;
    std::vector<std::vector<std::string>> chunks;
    if (!Query("SELECT m.part_index,m.size,c.sha256,c.state,COALESCE(c.backend_file_id,''),c.id "
               "FROM manifest_chunk m JOIN chunk_blob c ON c.id=m.chunk_id "
               "WHERE m.manifest_id=? ORDER BY m.part_index", {ToString(session->manifest_id)}, &chunks)) {
        return false;
    }
    parts->clear();
    for (const auto &row : chunks) {
        if (row.size() < 6) return false;
        PartStatus part;
        part.spec.index = std::stoi(row[0]);
        part.spec.size = std::stoll(row[1]);
        part.spec.sha256 = row[2];
        part.state = row[3];
        part.backend_file_id = row[4];
        part.chunk_id = std::stoll(row[5]);
        part.availability = PartAvailability::kReady;
        parts->push_back(std::move(part));
    }
    return true;
}

}  // namespace hydrastore
