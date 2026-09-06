#include "knowledge_store.h"

#include "hash_util.h"

extern "C" {
#include "knowledge_task.h"
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <map>
#include <sstream>
#include <utility>

namespace hydrastore {
namespace {

std::int64_t ToInt(const std::string &value) {
    return value.empty() ? 0 : static_cast<std::int64_t>(std::strtoll(value.c_str(), nullptr, 10));
}

std::string Hex(const std::vector<float> &values) {
    static const char digits[] = "0123456789abcdef";
    const unsigned char *bytes = reinterpret_cast<const unsigned char *>(values.data());
    const std::size_t size = values.size() * sizeof(float);
    std::string result;
    result.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        result.push_back(digits[bytes[i] >> 4]);
        result.push_back(digits[bytes[i] & 0x0f]);
    }
    return result;
}

bool Failpoint(const char *name) {
    const char *value = std::getenv("HYDRA_DB_FAILPOINT");
    return value && name && std::string(value) == name;
}

bool NormalizeEmbedding(const std::vector<float> &input, int dimension,
                        std::vector<float> *output) {
    if (!output || dimension <= 0 || input.size() != static_cast<std::size_t>(dimension)) return false;
    double norm = 0.0;
    for (float value : input) {
        if (!std::isfinite(value)) return false;
        norm += static_cast<double>(value) * value;
    }
    norm = std::sqrt(norm);
    if (!std::isfinite(norm) || norm <= 0.0) return false;
    *output = input;
    for (float &value : *output) value = static_cast<float>(value / norm);
    return true;
}

}  // namespace

KnowledgeStore::KnowledgeStore(const std::string &host, unsigned int port,
                               const std::string &user, const std::string &password,
                               const std::string &database)
    : host_(host), port_(port), user_(user), password_(password), database_(database) {}

KnowledgeStore::~KnowledgeStore() { Close(); }

void KnowledgeStore::Close() {
    if (connection_) mysql_close(connection_);
    connection_ = nullptr;
}

bool KnowledgeStore::EnsureConnection() {
    if (connection_ && mysql_ping(connection_) == 0) return true;
    Close();
    connection_ = mysql_init(nullptr);
    if (!connection_) return false;
    if (!mysql_real_connect(connection_, host_.c_str(), user_.c_str(), password_.c_str(),
                            database_.c_str(), port_, nullptr, 0)) {
        Close(); return false;
    }
    mysql_set_character_set(connection_, "utf8mb4");
    return true;
}

bool KnowledgeStore::Connect() { return EnsureConnection(); }

bool KnowledgeStore::Exec(const std::string &sql) {
    if (!EnsureConnection() || mysql_query(connection_, sql.c_str()) != 0) return false;
    affected_rows_ = mysql_affected_rows(connection_);
    last_insert_id_ = static_cast<long long>(mysql_insert_id(connection_));
    return true;
}

bool KnowledgeStore::Query(const std::string &sql,
                           std::vector<std::vector<std::string>> *rows) {
    if (!rows || !EnsureConnection() || mysql_query(connection_, sql.c_str()) != 0) return false;
    rows->clear();
    MYSQL_RES *result = mysql_store_result(connection_);
    if (!result) return false;
    const unsigned int fields = mysql_num_fields(result);
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        unsigned long *lengths = mysql_fetch_lengths(result);
        std::vector<std::string> values;
        values.reserve(fields);
        for (unsigned int i = 0; i < fields; ++i) {
            values.emplace_back(row[i] ? row[i] : "", row[i] ? lengths[i] : 0);
        }
        rows->push_back(std::move(values));
    }
    mysql_free_result(result);
    return true;
}

bool KnowledgeStore::ReadSingle(const std::string &sql, std::vector<std::string> *row) {
    std::vector<std::vector<std::string>> rows;
    if (!Query(sql, &rows) || rows.empty()) return false;
    *row = std::move(rows.front());
    return true;
}

std::string KnowledgeStore::Escape(const std::string &value) const {
    if (!connection_) return {};
    std::string escaped(value.size() * 2 + 1, '\0');
    const unsigned long length = mysql_real_escape_string(
        const_cast<MYSQL *>(connection_), escaped.data(), value.data(), value.size());
    escaped.resize(length);
    return escaped;
}

std::string KnowledgeStore::InList(const std::vector<std::int64_t> &ids) const {
    if (ids.empty()) return "NULL";
    std::ostringstream out;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i != 0) out << ',';
        out << ids[i];
    }
    return out.str();
}

std::string KnowledgeStore::BuildValidWikiRevisionPredicate(
    const std::string &revision_alias, const std::string &page_alias) const {
    const std::string revision = revision_alias.empty() ? "r" : revision_alias;
    const std::string page = page_alias.empty() ? "p" : page_alias;
    return page + ".status='ACTIVE' AND " + revision + ".status='PUBLISHED' AND " +
           page + ".current_revision_id=" + revision + ".id AND EXISTS (" +
           "SELECT 1 FROM llm_wiki_citation valid_citation "
           "JOIN knowledge_chunk valid_chunk ON valid_chunk.user=valid_citation.user "
           "AND valid_chunk.id=valid_citation.chunk_id AND valid_chunk.state='PUBLISHED' "
           "WHERE valid_citation.user=" + revision + ".user AND valid_citation.revision_id=" +
           revision + ".id AND valid_citation.state='ACTIVE' AND EXISTS (" +
           "SELECT 1 FROM user_file_list valid_relation WHERE valid_relation.user=valid_chunk.user "
           "AND valid_relation.md5=valid_chunk.md5)) AND NOT EXISTS (" +
           "SELECT 1 FROM llm_wiki_citation invalid_citation "
           "JOIN knowledge_chunk invalid_chunk ON invalid_chunk.user=invalid_citation.user "
           "AND invalid_chunk.id=invalid_citation.chunk_id "
           "WHERE invalid_citation.user=" + revision + ".user AND invalid_citation.revision_id=" +
           revision + ".id AND invalid_citation.state='ACTIVE' AND NOT EXISTS (" +
           "SELECT 1 FROM user_file_list invalid_relation WHERE invalid_relation.user=invalid_chunk.user "
           "AND invalid_relation.md5=invalid_chunk.md5))";
}

bool KnowledgeStore::BeginTransaction() { return Exec("START TRANSACTION"); }
bool KnowledgeStore::Commit() { return Exec("COMMIT"); }
void KnowledgeStore::Rollback() { if (connection_) mysql_query(connection_, "ROLLBACK"); }
bool KnowledgeStore::AffectedOne() const { return affected_rows_ == 1; }

bool KnowledgeStore::VerifyTaskLeaseInTransaction(const KnowledgeTaskClaim &claim) {
    std::vector<std::string> row;
    return ReadSingle("SELECT 1 FROM ai_parse_task WHERE id=" + std::to_string(claim.id) +
                      " AND status='running' AND worker_id='" + Escape(claim.worker_id) +
                      "' AND lease_epoch=" + std::to_string(claim.lease_epoch) +
                      " AND lease_until IS NOT NULL AND lease_until>NOW() FOR UPDATE", &row);
}

bool KnowledgeStore::ClaimTask(const std::string &worker_id, KnowledgeTaskClaim *claim) {
    if (!claim || worker_id.empty() || !EnsureConnection() || !BeginTransaction()) return false;
    std::vector<std::vector<std::string>> rows;
    const bool selected = Query(
        "SELECT id,user,md5,task_type,retry_count,lease_epoch FROM ai_parse_task "
        "WHERE status='pending' AND (next_retry_at IS NULL OR next_retry_at<=NOW()) "
        "ORDER BY created_at,id LIMIT 1 FOR UPDATE SKIP LOCKED", &rows);
    if (!selected) { Rollback(); return false; }
    if (rows.empty()) { Commit(); return false; }
    const auto &row = rows.front();
    if (row.size() < 6) { Rollback(); return false; }
    const std::int64_t id = ToInt(row[0]);
    const std::int64_t old_epoch = ToInt(row[5]);
    const std::int64_t epoch = old_epoch + 1;
    const std::string worker = Escape(worker_id);
    const std::string sql =
        "UPDATE ai_parse_task SET status='running',worker_id='" + worker +
        "',lease_epoch=" + std::to_string(epoch) +
        ",lease_until=DATE_ADD(NOW(),INTERVAL 10 MINUTE),started_at=COALESCE(started_at,NOW()),error_msg=NULL "
        "WHERE id=" + std::to_string(id) + " AND status='pending'";
    if (!Exec(sql) || !AffectedOne() || !Commit()) { Rollback(); return false; }
    claim->id = id;
    claim->user = row[1];
    claim->md5 = row[2];
    claim->task_type = row[3] == "parse_file" ? "parse_source" : row[3];
    claim->retry_count = static_cast<int>(ToInt(row[4]));
    claim->worker_id = worker_id;
    claim->lease_epoch = epoch;
    return true;
}

bool KnowledgeStore::RenewTask(const KnowledgeTaskClaim &claim) {
    const std::string sql =
        "UPDATE ai_parse_task SET lease_until=DATE_ADD(NOW(),INTERVAL 10 MINUTE),updated_at=NOW() "
        "WHERE id=" + std::to_string(claim.id) + " AND status='running' AND worker_id='" +
        Escape(claim.worker_id) + "' AND lease_epoch=" + std::to_string(claim.lease_epoch) +
        " AND lease_until IS NOT NULL AND lease_until>NOW()";
    return Exec(sql) && AffectedOne();
}

bool KnowledgeStore::FinishTask(const KnowledgeTaskClaim &claim) {
    const std::string sql =
        "UPDATE ai_parse_task SET status='success',lease_until=NULL,finished_at=NOW(),updated_at=NOW() "
        "WHERE id=" + std::to_string(claim.id) + " AND status='running' AND worker_id='" +
        Escape(claim.worker_id) + "' AND lease_epoch=" + std::to_string(claim.lease_epoch) +
        " AND lease_until IS NOT NULL AND lease_until>NOW()";
    return Exec(sql) && AffectedOne();
}

bool KnowledgeStore::ContinueTask(const KnowledgeTaskClaim &claim) {
    const std::string sql =
        "UPDATE ai_parse_task SET status='pending',worker_id=NULL,lease_until=NULL,"
        "next_retry_at=NOW(),finished_at=NULL,error_msg=NULL,updated_at=NOW() "
        "WHERE id=" + std::to_string(claim.id) + " AND status='running' AND worker_id='" +
        Escape(claim.worker_id) + "' AND lease_epoch=" + std::to_string(claim.lease_epoch) +
        " AND lease_until IS NOT NULL AND lease_until>NOW()";
    return Exec(sql) && AffectedOne();
}

bool KnowledgeStore::SkipTask(const KnowledgeTaskClaim &claim, const std::string &reason) {
    const std::string sql =
        "UPDATE ai_parse_task SET status='skipped',error_msg='" + Escape(reason.substr(0, 8192)) +
        "',lease_until=NULL,finished_at=NOW(),updated_at=NOW() WHERE id=" +
        std::to_string(claim.id) + " AND status='running' AND worker_id='" +
        Escape(claim.worker_id) + "' AND lease_epoch=" + std::to_string(claim.lease_epoch) +
        " AND lease_until IS NOT NULL AND lease_until>NOW()";
    return Exec(sql) && AffectedOne();
}

bool KnowledgeStore::FailTask(const KnowledgeTaskClaim &claim, const std::string &error,
                              bool retryable) {
    const int next_retry = claim.retry_count + 1;
    const int delay = next_retry <= 3 ? (5 << (next_retry - 1)) : 0;
    std::string status = retryable && next_retry <= 3 ? "pending" : "failed";
    const std::string safe_error = Escape(error.substr(0, 8192));
    const std::string retry_at = status == "pending"
        ? ",next_retry_at=DATE_ADD(NOW(),INTERVAL (" + std::to_string(delay) + "+FLOOR(RAND()*" + std::to_string(delay / 5 + 1) + ")) SECOND)" : ",next_retry_at=NULL";
    const std::string sql =
        "UPDATE ai_parse_task SET status='" + status + "',retry_count=" + std::to_string(next_retry) +
        ",error_msg='" + safe_error + "',lease_until=NULL,finished_at=" +
        (status == "failed" ? "NOW()" : "NULL") + retry_at + ",updated_at=NOW() "
        "WHERE id=" + std::to_string(claim.id) + " AND status='running' AND worker_id='" +
        Escape(claim.worker_id) + "' AND lease_epoch=" + std::to_string(claim.lease_epoch) +
        " AND lease_until IS NOT NULL AND lease_until>NOW()";
    return Exec(sql) && AffectedOne();
}

bool KnowledgeStore::MarkWikiFailed(const KnowledgeTaskClaim &claim,
                                    const std::string &error) {
    const std::string sql =
        "UPDATE knowledge_document d JOIN ai_parse_task t ON t.user=d.user AND t.md5=d.md5 "
        "SET d.wiki_state='FAILED',d.last_error='" + Escape(error.substr(0, 8192)) +
        "',d.updated_at=NOW() WHERE t.id=" + std::to_string(claim.id) +
        " AND t.task_type IN ('compile_wiki','repair_wiki') AND t.status='failed' AND "
        "t.worker_id='" + Escape(claim.worker_id) + "' AND t.lease_epoch=" +
        std::to_string(claim.lease_epoch);
    return Exec(sql) && AffectedOne();
}

bool KnowledgeStore::RecoverExpiredTasks() {
    const bool retried = Exec(
        "UPDATE ai_parse_task SET status='pending',worker_id=NULL,lease_until=NULL,"
        "next_retry_at=DATE_ADD(NOW(),INTERVAL (CASE retry_count WHEN 0 THEN 5 WHEN 1 THEN 10 ELSE 20 END+"
        "FLOOR(RAND()*(CASE retry_count WHEN 0 THEN 2 WHEN 1 THEN 3 ELSE 5 END))) SECOND),"
        "retry_count=retry_count+1,finished_at=NULL,error_msg=CONCAT(COALESCE(error_msg,''),' lease expired'),"
        "updated_at=NOW() WHERE status='running' AND lease_until IS NOT NULL AND lease_until<NOW()"
        " AND retry_count<3");
    if (!retried) return false;
    return Exec(
        "UPDATE ai_parse_task SET status='failed',worker_id=NULL,lease_until=NULL,next_retry_at=NULL,"
        "retry_count=retry_count+1,finished_at=NOW(),error_msg=CONCAT(COALESCE(error_msg,''),"
        "' lease expired retry budget exhausted'),updated_at=NOW() "
        "WHERE status='running' AND lease_until IS NOT NULL AND lease_until<NOW() AND retry_count>=3");
}

bool KnowledgeStore::EnqueueTask(const std::string &user, const std::string &md5,
                                 const std::string &task_type, const std::string &source,
                                 bool force) {
    if (!EnsureConnection() || user.empty() || md5.empty() || task_type.empty()) return false;
    return enqueue_knowledge_task(connection_, user.c_str(), md5.c_str(), task_type.c_str(),
                                  source.c_str(), force ? 1 : 0) == 0;
}

bool KnowledgeStore::LoadSourceObject(const std::string &user, const std::string &md5,
                                      SourceObject *source) {
    if (!source) return false;
    std::vector<std::string> row;
    const std::string sql =
        "SELECT u.user,u.md5,u.file_name,COALESCE(f.type,''),COALESCE(f.storage_mode,'legacy'),"
        "COALESCE(f.object_id,''),COALESCE(f.manifest_id,0),COALESCE(f.size,0),COALESCE(f.url,''),"
        "COALESCE(f.content_digest,f.md5),COALESCE(d.truncated,0) FROM user_file_list u JOIN file_info f ON f.md5=u.md5 "
        "LEFT JOIN knowledge_document d ON d.user=u.user AND d.md5=u.md5 "
        "WHERE u.user='" + Escape(user) + "' AND u.md5='" + Escape(md5) + "' LIMIT 1";
    if (!ReadSingle(sql, &row) || row.size() < 11) return false;
    source->user = row[0]; source->md5 = row[1]; source->filename = row[2]; source->type = row[3];
    source->storage_mode = row[4]; source->object_id = row[5]; source->manifest_id = ToInt(row[6]);
    source->size = ToInt(row[7]); source->legacy_url = row[8]; source->truncated = ToInt(row[10]) != 0;
    return true;
}

bool KnowledgeStore::SourceRelationExists(const std::string &user, const std::string &md5,
                                          bool *exists) {
    if (!exists) return false;
    std::vector<std::vector<std::string>> rows;
    if (!Query("SELECT 1 FROM user_file_list WHERE user='" + Escape(user) +
               "' AND md5='" + Escape(md5) + "' LIMIT 1", &rows)) return false;
    *exists = !rows.empty();
    return true;
}

bool KnowledgeStore::LoadApiKey(const std::string &user, std::string *api_key) {
    if (!api_key) return false;
    std::vector<std::string> row;
    if (!ReadSingle("SELECT COALESCE(api_key,'') FROM user_info WHERE user_name='" + Escape(user) + "' LIMIT 1", &row)) {
        api_key->clear(); return true;
    }
    *api_key = row.empty() ? std::string() : row[0];
    return true;
}

bool KnowledgeStore::BeginEvidenceGeneration(const SourceObject &source,
                                             std::int64_t *generation,
                                             const KnowledgeTaskClaim *claim) {
    if (!generation || !BeginTransaction()) return false;
    if (claim && !VerifyTaskLeaseInTransaction(*claim)) {
        Rollback();
        return false;
    }
    const std::string user = Escape(source.user), md5 = Escape(source.md5);
    std::vector<std::string> relation;
    if (!ReadSingle("SELECT 1 FROM user_file_list u JOIN file_info f ON f.md5=u.md5 WHERE u.user='" +
                    user + "' AND u.md5='" + md5 + "' FOR UPDATE", &relation)) {
        Rollback();
        return false;
    }
    const std::string insert =
        "INSERT INTO knowledge_document(user,md5,object_id,manifest_id,current_generation,evidence_state,wiki_state,parser_version,chunker_version,source_bytes) "
        "VALUES('" + user + "','" + md5 + "','" + Escape(source.object_id) + "'," +
        (source.manifest_id ? std::to_string(source.manifest_id) : "NULL") + ",0,'PENDING','PENDING','v2','v2'," +
        std::to_string(source.size) + ") ON DUPLICATE KEY UPDATE object_id=VALUES(object_id),manifest_id=VALUES(manifest_id)";
    if (!Exec(insert)) { Rollback(); return false; }
    std::vector<std::string> row;
    if (!ReadSingle("SELECT current_generation FROM knowledge_document WHERE user='" + user + "' AND md5='" + md5 + "' FOR UPDATE", &row)) {
        Rollback(); return false;
    }
    const std::int64_t next = (row.empty() ? 0 : ToInt(row[0])) + 1;
    if (!Exec("UPDATE knowledge_document SET current_generation=" + std::to_string(next) +
              ",evidence_state='EXTRACTING',last_error=NULL,updated_at=NOW() WHERE user='" + user + "' AND md5='" + md5 + "'")) {
        Rollback(); return false;
    }
    if (!Commit()) { Rollback(); return false; }
    *generation = next;
    return true;
}

bool KnowledgeStore::PutStagingChunk(const SourceObject &source, std::int64_t generation,
                                     const EvidenceChunk &chunk, std::int64_t *chunk_id) {
    if (!chunk_id || generation <= 0) return false;
    const std::string sql =
        "INSERT INTO knowledge_chunk(user,md5,generation,chunk_no,heading,content,content_sha256,start_offset,end_offset,state) VALUES('" +
        Escape(source.user) + "','" + Escape(source.md5) + "'," + std::to_string(generation) + "," +
        std::to_string(chunk.chunk_no) + ", '" + Escape(chunk.heading) + "','" + Escape(chunk.content) + "','" +
        Escape(chunk.content_sha256) + "'," + std::to_string(chunk.start_offset) + "," + std::to_string(chunk.end_offset) +
        ",'STAGING') ON DUPLICATE KEY UPDATE id=LAST_INSERT_ID(id),heading=VALUES(heading),content=VALUES(content),content_sha256=VALUES(content_sha256),start_offset=VALUES(start_offset),end_offset=VALUES(end_offset),state='STAGING'";
    if (!Exec(sql)) return false;
    *chunk_id = last_insert_id_;
    return *chunk_id > 0;
}

bool KnowledgeStore::PutVector(const std::string &user, const std::string &source_type,
                               std::int64_t source_id, const std::string &model, int dimension,
                               const std::vector<float> &embedding, std::int64_t *vector_id) {
    std::vector<float> normalized;
    if (!NormalizeEmbedding(embedding, dimension, &normalized)) return false;
    const std::string sql =
        "INSERT INTO knowledge_vector(user,source_type,source_id,model,dimension,embedding,status) VALUES('" +
        Escape(user) + "','" + Escape(source_type) + "'," + std::to_string(source_id) + ",'" + Escape(model) + "'," +
        std::to_string(dimension) + ",UNHEX('" + Hex(normalized) + "'),'ACTIVE') ON DUPLICATE KEY UPDATE id=LAST_INSERT_ID(id),model=VALUES(model),dimension=VALUES(dimension),embedding=VALUES(embedding),status='ACTIVE'";
    if (!Exec(sql)) return false;
    if (vector_id) *vector_id = last_insert_id_;
    return last_insert_id_ > 0;
}

bool KnowledgeStore::PublishEvidenceGeneration(const SourceObject &source, std::int64_t generation,
                                               int chunk_count, bool truncated,
                                               std::int64_t source_bytes,
                                               const KnowledgeTaskClaim *claim) {
    if (generation <= 0 || chunk_count <= 0 || !BeginTransaction()) return false;
    const std::string user = Escape(source.user), md5 = Escape(source.md5);
    std::vector<std::string> row;
    if (claim && !VerifyTaskLeaseInTransaction(*claim)) {
        Rollback();
        return false;
    }
    std::vector<std::string> relation;
    if (!ReadSingle("SELECT 1 FROM user_file_list WHERE user='" + user +
                    "' AND md5='" + md5 + "' FOR UPDATE", &relation)) {
        Rollback();
        return false;
    }
    if (!ReadSingle("SELECT current_generation FROM knowledge_document WHERE user='" + user +
                    "' AND md5='" + md5 + "' FOR UPDATE", &row) || row.empty() ||
        ToInt(row[0]) != generation) {
        Rollback();
        return false;
    }
    const std::string verify =
        "SELECT COUNT(*),SUM((SELECT COUNT(*) FROM knowledge_vector v WHERE v.user=c.user AND v.source_type='chunk' AND v.source_id=c.id AND v.status='ACTIVE')) FROM knowledge_chunk c WHERE c.user='" + user + "' AND c.md5='" + md5 + "' AND c.generation=" + std::to_string(generation) + " AND c.state='STAGING'";
    if (!ReadSingle(verify, &row) || row.size() < 2 || ToInt(row[0]) != chunk_count || ToInt(row[1]) != chunk_count) {
        Rollback(); return false;
    }
    if (Failpoint("evidence_before_publish")) { Rollback(); return false; }
    const std::string old_vectors = "UPDATE knowledge_vector v JOIN knowledge_chunk c ON c.user=v.user AND c.id=v.source_id AND v.source_type='chunk' SET v.status='INACTIVE' WHERE c.user='" + user + "' AND c.md5='" + md5 + "' AND c.state='PUBLISHED'";
    if (!Exec(old_vectors) || !Exec("UPDATE knowledge_chunk SET state='SUPERSEDED' WHERE user='" + user + "' AND md5='" + md5 + "' AND state='PUBLISHED'") ||
        !Exec("UPDATE knowledge_chunk SET state='PUBLISHED' WHERE user='" + user + "' AND md5='" + md5 + "' AND generation=" + std::to_string(generation) + " AND state='STAGING'") ||
        !Exec("UPDATE knowledge_document SET published_generation=" + std::to_string(generation) + ",evidence_state='READY',truncated=" + std::to_string(truncated ? 1 : 0) + ",source_bytes=" + std::to_string(source_bytes) + ",last_error=NULL,updated_at=NOW() WHERE user='" + user + "' AND md5='" + md5 + "'") ||
        !MarkIndexDirty(source.user) || !Commit()) {
        Rollback(); return false;
    }
    return true;
}

bool KnowledgeStore::AbortEvidenceGeneration(const SourceObject &source, std::int64_t generation,
                                             const std::string &error,
                                             const KnowledgeTaskClaim *claim) {
    if (!BeginTransaction()) return false;
    if (claim && !VerifyTaskLeaseInTransaction(*claim)) {
        const std::string user = Escape(source.user), md5 = Escape(source.md5);
        const bool cleaned = Exec("UPDATE knowledge_vector v JOIN knowledge_chunk c ON c.user=v.user AND c.id=v.source_id AND v.source_type='chunk' SET v.status='INACTIVE' WHERE c.user='" + user + "' AND c.md5='" + md5 + "' AND c.generation=" + std::to_string(generation)) &&
            Exec("UPDATE knowledge_chunk SET state='DELETED' WHERE user='" + user + "' AND md5='" + md5 + "' AND generation=" + std::to_string(generation) + " AND state='STAGING'") && Commit();
        if (!cleaned) Rollback();
        return false;
    }
    const std::string user = Escape(source.user), md5 = Escape(source.md5);
    std::vector<std::vector<std::string>> relations;
    if (!Query("SELECT 1 FROM user_file_list WHERE user='" + user + "' AND md5='" + md5 + "' LIMIT 1 FOR UPDATE", &relations)) {
        Rollback();
        return false;
    }
    const std::string document_update = relations.empty()
        ? "UPDATE knowledge_document SET evidence_state='DELETED',wiki_state='STALE',last_error=NULL,updated_at=NOW() WHERE user='" + user + "' AND md5='" + md5 + "'"
        : "UPDATE knowledge_document SET evidence_state='FAILED',last_error='" + Escape(error.substr(0, 8192)) + "',updated_at=NOW() WHERE user='" + user + "' AND md5='" + md5 + "'";
    const bool ok = Exec("UPDATE knowledge_vector v JOIN knowledge_chunk c ON c.user=v.user AND c.id=v.source_id AND v.source_type='chunk' SET v.status='INACTIVE' WHERE c.user='" + user + "' AND c.md5='" + md5 + "' AND c.generation=" + std::to_string(generation)) &&
        Exec("UPDATE knowledge_chunk SET state='DELETED' WHERE user='" + user + "' AND md5='" + md5 + "' AND generation=" + std::to_string(generation) + " AND state='STAGING'") &&
        Exec(document_update) && Commit();
    if (!ok) Rollback();
    return ok;
}

bool KnowledgeStore::LoadPublishedEvidence(const SourceObject &source, std::int64_t generation,
                                            std::vector<EvidenceChunk> *chunks) {
    if (!chunks) return false;
    if (generation <= 0) {
        std::vector<std::string> row;
        if (!ReadSingle("SELECT published_generation FROM knowledge_document WHERE user='" + Escape(source.user) + "' AND md5='" + Escape(source.md5) + "'", &row)) return false;
        generation = row.empty() ? 0 : ToInt(row[0]);
    }
    std::vector<std::vector<std::string>> rows;
    if (!Query("SELECT id,chunk_no,COALESCE(heading,''),content,content_sha256,start_offset,end_offset FROM knowledge_chunk WHERE user='" + Escape(source.user) + "' AND md5='" + Escape(source.md5) + "' AND generation=" + std::to_string(generation) + " AND state='PUBLISHED' ORDER BY chunk_no", &rows)) return false;
    chunks->clear();
    for (const auto &row : rows) if (row.size() >= 7) {
        EvidenceChunk chunk; chunk.id=ToInt(row[0]); chunk.source_md5=source.md5; chunk.chunk_no=static_cast<int>(ToInt(row[1])); chunk.heading=row[2]; chunk.content=row[3]; chunk.content_sha256=row[4]; chunk.start_offset=ToInt(row[5]); chunk.end_offset=ToInt(row[6]); chunks->push_back(std::move(chunk));
    }
    return true;
}

bool KnowledgeStore::UpdateLegacyAiRecord(const SourceObject &source, const std::string &description,
                                          const std::string &summary, const std::string &model) {
    const std::string sql =
        "INSERT INTO user_file_ai_desc(user,md5,description,model,status,summary,parse_status,error_msg) VALUES('" +
        Escape(source.user) + "','" + Escape(source.md5) + "','" + Escape(description) + "','" + Escape(model) + "',1,'" +
        Escape(summary) + "','success',NULL) ON DUPLICATE KEY UPDATE description=VALUES(description),model=VALUES(model),status=1,summary=VALUES(summary),parse_status='success',error_msg=NULL";
    return Exec(sql);
}

bool KnowledgeStore::MarkIndexDirty(const std::string &user) {
    return Exec("INSERT INTO knowledge_index_state(user,dirty_generation,state,updated_at) VALUES('" + Escape(user) + "',1,'DIRTY',NOW()) ON DUPLICATE KEY UPDATE dirty_generation=dirty_generation+1,state='DIRTY',updated_at=NOW()");
}

bool KnowledgeStore::LoadIndexState(const std::string &user, std::int64_t *published,
                                    std::int64_t *dirty) {
    if (!published || !dirty) return false;
    *published = 0;
    *dirty = 0;
    std::vector<std::vector<std::string>> rows;
    if (!Query("SELECT published_generation,dirty_generation FROM knowledge_index_state WHERE user='" +
               Escape(user) + "'", &rows)) return false;
    if (rows.empty()) return true;
    if (rows.size() != 1 || rows.front().size() < 2) return false;
    *published = ToInt(rows.front()[0]);
    *dirty = ToInt(rows.front()[1]);
    return true;
}

bool KnowledgeStore::ClaimDirtyIndex(const std::string &worker_id, std::string *user,
                                     std::int64_t *generation) {
    if (!user || !generation || !BeginTransaction()) return false;
    std::vector<std::vector<std::string>> rows;
    if (!Query("SELECT user,dirty_generation FROM knowledge_index_state WHERE dirty_generation>published_generation AND (lease_until IS NULL OR lease_until<NOW()) ORDER BY updated_at,user LIMIT 1 FOR UPDATE SKIP LOCKED", &rows)) { Rollback(); return false; }
    if (rows.empty()) { Commit(); return false; }
    const std::string selected_user = rows[0][0]; const std::int64_t selected_generation = ToInt(rows[0][1]);
    if (!Exec("UPDATE knowledge_index_state SET worker_id='" + Escape(worker_id) + "',lease_until=DATE_ADD(NOW(),INTERVAL 10 MINUTE),state='BUILDING' WHERE user='" + Escape(selected_user) + "' AND dirty_generation=" + std::to_string(selected_generation) + " AND dirty_generation>published_generation") || !AffectedOne() || !Commit()) { Rollback(); return false; }
    *user = selected_user; *generation = selected_generation; return true;
}

bool KnowledgeStore::PublishIndexGeneration(const std::string &user, const std::string &worker_id,
                                            std::int64_t generation) {
    return Exec("UPDATE knowledge_index_state SET published_generation=" + std::to_string(generation) + ",lease_until=NULL,worker_id=NULL,last_error=NULL,state=IF(dirty_generation>" + std::to_string(generation) + ",'DIRTY','READY'),updated_at=NOW() WHERE user='" + Escape(user) + "' AND worker_id='" + Escape(worker_id) + "' AND dirty_generation>=" + std::to_string(generation)) && AffectedOne();
}

bool KnowledgeStore::FailIndexGeneration(const std::string &user,
                                         const std::string &worker_id,
                                         std::int64_t generation,
                                         const std::string &error) {
    return Exec("UPDATE knowledge_index_state SET lease_until=NULL,worker_id=NULL,state='DIRTY',last_error='" +
                Escape(error.substr(0, 8192)) + "',updated_at=NOW() WHERE user='" + Escape(user) +
                "' AND worker_id='" + Escape(worker_id) + "' AND dirty_generation>=" +
                std::to_string(generation)) && AffectedOne();
}

bool KnowledgeStore::LoadActiveVectors(const std::string &user,
                                       std::vector<KnowledgeVectorRecord> *vectors) {
    if (!vectors) return false;
    std::vector<std::vector<std::string>> rows;
    const std::string wiki_validity = BuildValidWikiRevisionPredicate("r", "p");
    if (!Query("SELECT id,source_type,source_id,model,dimension,embedding FROM knowledge_vector v WHERE v.user='" +
               Escape(user) + "' AND v.status='ACTIVE' AND ((v.source_type='chunk' AND EXISTS (SELECT 1 FROM knowledge_chunk c "
               "JOIN user_file_list u ON u.user=c.user AND u.md5=c.md5 WHERE c.user=v.user AND c.id=v.source_id "
               "AND c.state='PUBLISHED')) OR (v.source_type='wiki_revision' AND EXISTS (SELECT 1 FROM llm_wiki_revision r "
               "JOIN llm_wiki_page p ON p.user=r.user AND p.id=r.page_id WHERE r.user=v.user AND r.id=v.source_id AND " +
               wiki_validity + "))) ORDER BY id", &rows)) return false;
    vectors->clear();
    for (const auto &row : rows) if (row.size() >= 6) {
        KnowledgeVectorRecord value; value.id=ToInt(row[0]); value.source_type=row[1]; value.source_id=ToInt(row[2]); value.model=row[3]; value.dimension=static_cast<int>(ToInt(row[4]));
        value.embedding_bytes = row[5].size();
        if (row[5].size() % sizeof(float) == 0) {
            value.embedding.resize(row[5].size() / sizeof(float));
            if (!row[5].empty()) std::memcpy(value.embedding.data(), row[5].data(), row[5].size());
        }
        vectors->push_back(std::move(value));
    }
    return true;
}

bool KnowledgeStore::LoadVectorsForSources(
    const std::string &user, const std::string &source_type,
    const std::vector<std::int64_t> &source_ids,
    std::vector<KnowledgeVectorRecord> *vectors) {
    if (!vectors || (source_type != "chunk" && source_type != "wiki_revision")) return false;
    vectors->clear();
    if (source_ids.empty()) return true;
    const std::string published_source = source_type == "chunk"
        ? "EXISTS (SELECT 1 FROM knowledge_chunk c JOIN user_file_list u ON u.user=c.user AND u.md5=c.md5 WHERE c.user=v.user AND c.id=v.source_id AND c.state='PUBLISHED')"
        : "EXISTS (SELECT 1 FROM llm_wiki_revision r JOIN llm_wiki_page p ON p.user=r.user AND p.id=r.page_id WHERE r.user=v.user AND r.id=v.source_id AND " + BuildValidWikiRevisionPredicate("r", "p") + ")";
    const std::string sql =
        "SELECT v.id,v.source_type,v.source_id,v.model,v.dimension,v.embedding FROM knowledge_vector v WHERE v.user='" +
        Escape(user) + "' AND v.source_type='" + source_type + "' AND v.source_id IN (" + InList(source_ids) +
        ") AND v.status='ACTIVE' AND " + published_source + " ORDER BY v.source_id";
    std::vector<std::vector<std::string>> rows;
    if (!Query(sql, &rows)) return false;
    for (const auto &row : rows) if (row.size() >= 6) {
        KnowledgeVectorRecord value;
        value.id = ToInt(row[0]); value.source_type = row[1]; value.source_id = ToInt(row[2]);
        value.model = row[3]; value.dimension = static_cast<int>(ToInt(row[4]));
        value.embedding_bytes = row[5].size();
        if (row[5].size() % sizeof(float) == 0) {
            value.embedding.resize(row[5].size() / sizeof(float));
            if (!row[5].empty()) std::memcpy(value.embedding.data(), row[5].data(), row[5].size());
        }
        vectors->push_back(std::move(value));
    }
    return true;
}

bool KnowledgeStore::LoadSearchHydration(const std::string &user,
                                         const std::vector<std::int64_t> &vector_ids,
                                         std::vector<SearchHydration> *rows) {
    if (!rows) return false;
    std::vector<std::vector<std::string>> result;
    const std::string wiki_validity = BuildValidWikiRevisionPredicate("r", "p");
    const std::string sql =
        "SELECT v.id,v.source_type,v.source_id,COALESCE(c.md5,''),COALESCE(uf.file_name,''),"
        "COALESCE(f.type,''),COALESCE(f.size,0),COALESCE(f.url,''),COALESCE(c.chunk_no,0),"
        "COALESCE(c.content,''),COALESCE(p.page_key,''),COALESCE(p.title,''),COALESCE(r.id,0),"
        "COALESCE(r.summary,''),COALESCE(r.body_markdown,'') FROM knowledge_vector v "
        "LEFT JOIN knowledge_chunk c ON c.id=v.source_id AND v.source_type='chunk' "
        "AND c.user=v.user AND c.state='PUBLISHED' "
        "LEFT JOIN user_file_list uf ON uf.user=c.user AND uf.md5=c.md5 "
        "LEFT JOIN file_info f ON f.md5=c.md5 "
        "LEFT JOIN llm_wiki_revision r ON r.id=v.source_id AND v.source_type='wiki_revision' "
        "AND r.user=v.user LEFT JOIN llm_wiki_page p ON p.user=r.user AND p.id=r.page_id "
        "WHERE v.user='" + Escape(user) + "' AND v.id IN (" + InList(vector_ids) +
        ") AND v.status='ACTIVE' AND ((v.source_type='chunk' AND c.id IS NOT NULL AND "
        "EXISTS (SELECT 1 FROM user_file_list u WHERE u.user=c.user AND u.md5=c.md5)) OR "
        "(v.source_type='wiki_revision' AND r.id IS NOT NULL AND p.id IS NOT NULL AND " +
        wiki_validity + "))";
    if (!Query(sql, &result)) return false;
    rows->clear();
    for (const auto &row : result) if (row.size() >= 15) {
        SearchHydration value; value.vector_id=ToInt(row[0]); value.source_type=row[1]; value.source_id=ToInt(row[2]); value.md5=row[3]; value.filename=row[4]; value.type=row[5]; value.size=ToInt(row[6]); value.url=row[7]; value.chunk_no=static_cast<int>(ToInt(row[8])); value.snippet=row[9]; value.page_key=row[10]; value.title=row[11]; value.revision_id=ToInt(row[12]); value.summary=row[13]; value.body_markdown=row[14]; rows->push_back(std::move(value));
    }
    return true;
}

bool KnowledgeStore::LoadWikiClaims(const std::string &user,
                                    const std::vector<std::int64_t> &revision_ids,
                                    std::vector<WikiClaimView> *claims) {
    return LoadWikiClaimsInternal(user, revision_ids, claims, true);
}

bool KnowledgeStore::LoadWikiClaimsInternal(
    const std::string &user, const std::vector<std::int64_t> &revision_ids,
    std::vector<WikiClaimView> *claims, bool require_valid_revision) {
    if (!claims) return false;
    std::vector<std::vector<std::string>> rows;
    const std::string wiki_validity = require_valid_revision
        ? " AND " + BuildValidWikiRevisionPredicate("r", "p") : std::string();
    const std::string sql =
        "SELECT c.id,c.revision_id,c.text,c.confidence,k.md5,k.id FROM llm_wiki_claim c "
        "JOIN llm_wiki_revision r ON r.user=c.user AND r.id=c.revision_id "
        "JOIN llm_wiki_page p ON p.user=r.user AND p.id=r.page_id "
        "JOIN llm_wiki_citation x ON x.claim_id=c.id AND x.user=c.user "
        "AND x.revision_id=c.revision_id AND x.state='ACTIVE' "
        "JOIN knowledge_chunk k ON k.id=x.chunk_id AND k.user=x.user AND k.state='PUBLISHED' "
        "WHERE c.user='" + Escape(user) + "' AND c.revision_id IN (" + InList(revision_ids) +
        ") AND c.status='ACTIVE' AND EXISTS (SELECT 1 FROM user_file_list u WHERE u.user=k.user "
        "AND u.md5=k.md5)" + wiki_validity +
        " ORDER BY c.revision_id,c.ordinal,c.id";
    if (!Query(sql, &rows)) return false;
    claims->clear(); std::map<std::int64_t, std::size_t> positions;
    for (const auto &row : rows) if (row.size() >= 6) {
        const std::int64_t id = ToInt(row[0]);
        auto it = positions.find(id);
        if (it == positions.end()) { positions[id]=claims->size(); claims->push_back({id,ToInt(row[1]),row[2],static_cast<float>(std::atof(row[3].c_str())),{}}); it=positions.find(id); }
        if (!row[4].empty() && ToInt(row[5]) > 0) claims->at(it->second).citations.emplace_back(row[4], ToInt(row[5]));
    }
    return true;
}

bool KnowledgeStore::LoadFileCard(const std::string &user, const std::string &md5,
                                  FileKnowledgeCard *card) {
    if (!card) return false;
    std::vector<std::string> row;
    const std::string sql = "SELECT u.md5,u.file_name,COALESCE(f.type,''),COALESCE(f.size,0),COALESCE(f.url,''),COALESCE(d.evidence_state,''),COALESCE(d.wiki_state,''),COALESCE(t.status,''),COALESCE(t.id,0),COALESCE(a.summary,''),COALESCE(a.description,''),COALESCE(d.truncated,0),COALESCE(d.last_error,'') FROM user_file_list u JOIN file_info f ON f.md5=u.md5 LEFT JOIN knowledge_document d ON d.user=u.user AND d.md5=u.md5 LEFT JOIN user_file_ai_desc a ON a.user=u.user AND a.md5=u.md5 LEFT JOIN (SELECT x.* FROM ai_parse_task x JOIN (SELECT user,md5,MAX(id) id FROM ai_parse_task GROUP BY user,md5) latest ON latest.id=x.id) t ON t.user=u.user AND t.md5=u.md5 WHERE u.user='" + Escape(user) + "' AND u.md5='" + Escape(md5) + "' LIMIT 1";
    if (!ReadSingle(sql, &row) || row.size() < 13) return false;
    card->md5=row[0]; card->filename=row[1]; card->type=row[2]; card->size=ToInt(row[3]); card->url=row[4]; card->parse_status=row[5]; card->task_status=row[7]; card->task_id=ToInt(row[8]); card->summary=row[9]; card->description=row[10]; card->error=row[12]; card->evidence_ready=row[5]=="READY"; card->wiki_ready=row[6]=="READY"; card->partial_source=ToInt(row[11]) != 0 || row[5]=="PARTIAL"; return true;
}

bool KnowledgeStore::LoadWikiForSource(const std::string &user, const std::string &md5,
                                       std::vector<WikiPageView> *pages) {
    if (!pages) return false;
    std::vector<std::vector<std::string>> rows;
    const std::string wiki_validity = BuildValidWikiRevisionPredicate("r", "p");
    const std::string sql =
        "SELECT p.id,p.page_key,p.title,r.id,r.summary,r.body_markdown FROM llm_wiki_page p "
        "JOIN llm_wiki_revision r ON r.user=p.user AND r.id=p.current_revision_id "
        "WHERE p.user='" + Escape(user) + "' AND " + wiki_validity +
        " AND EXISTS (SELECT 1 FROM llm_wiki_citation x JOIN knowledge_chunk k ON k.id=x.chunk_id "
        "AND k.state='PUBLISHED' WHERE x.user=p.user AND x.revision_id=r.id AND x.state='ACTIVE' "
        "AND k.user=p.user AND k.md5='" + Escape(md5) + "' AND EXISTS (SELECT 1 FROM user_file_list u "
        "WHERE u.user=k.user AND u.md5=k.md5)) ORDER BY p.title";
    if (!Query(sql, &rows)) return false;
    pages->clear(); std::vector<std::int64_t> revisions;
    for (const auto &row : rows) if (row.size() >= 6) { WikiPageView page; page.page_id=ToInt(row[0]); page.page_key=row[1]; page.title=row[2]; page.revision_id=ToInt(row[3]); page.summary=row[4]; page.body_markdown=row[5]; pages->push_back(std::move(page)); revisions.push_back(ToInt(row[3])); }
    std::vector<WikiClaimView> claims; if (!revisions.empty() && !LoadWikiClaims(user, revisions, &claims)) return false;
    for (auto &page : *pages) {
        for (const auto &claim : claims) if (claim.revision_id == page.revision_id) page.claims.push_back(claim);
    }
    return true;
}

bool KnowledgeStore::LoadStaleWikiForSource(const std::string &user, const std::string &md5,
                                            std::vector<WikiPageView> *pages, int limit) {
    if (!pages) return false;
    std::vector<std::vector<std::string>> rows;
    const int bounded_limit = std::max(1, std::min(limit, 3));
    const std::string sql =
        "SELECT p.id,p.page_key,p.title,r.id,r.summary,r.body_markdown FROM llm_wiki_page p "
        "JOIN llm_wiki_revision r ON r.user=p.user AND r.id=p.current_revision_id AND r.status='PUBLISHED' "
        "WHERE p.user='" + Escape(user) + "' AND p.status='STALE' AND EXISTS (SELECT 1 FROM llm_wiki_citation x "
        "JOIN knowledge_chunk k ON k.id=x.chunk_id WHERE x.user=p.user AND x.revision_id=r.id AND "
        "k.user=p.user AND k.md5='" + Escape(md5) + "') AND EXISTS (SELECT 1 FROM llm_wiki_citation surviving "
        "JOIN knowledge_chunk k2 ON k2.id=surviving.chunk_id AND k2.user=surviving.user WHERE surviving.user=r.user "
        "AND surviving.revision_id=r.id AND surviving.state='ACTIVE' AND k2.state='PUBLISHED' AND EXISTS "
        "(SELECT 1 FROM user_file_list u2 WHERE u2.user=k2.user AND u2.md5=k2.md5)) ORDER BY p.title LIMIT " +
        std::to_string(bounded_limit);
    if (!Query(sql, &rows)) return false;
    pages->clear();
    std::vector<WikiCandidate> candidates;
    for (const auto &row : rows) if (row.size() >= 6) {
        WikiPageView page; page.page_id=ToInt(row[0]); page.page_key=row[1]; page.title=row[2]; page.revision_id=ToInt(row[3]); page.summary=row[4]; page.body_markdown=row[5]; pages->push_back(std::move(page));
        candidates.push_back({ToInt(row[0]), row[1], row[2], ToInt(row[3]), row[4], row[5]});
    }
    if (!LoadWikiCandidateEvidence(user, &candidates, false)) return false;
    for (auto &page : *pages) {
        for (const auto &candidate : candidates) if (candidate.revision_id == page.revision_id) {
            page.claims = candidate.claims;
            page.evidence = candidate.evidence;
            for (const auto &evidence : candidate.evidence) page.source_md5s.push_back(evidence.source_md5);
        }
    }
    return true;
}

bool KnowledgeStore::HasStaleWikiForSource(const std::string &user, const std::string &md5,
                                           bool *has_more) {
    if (!has_more) return false;
    *has_more = false;
    std::vector<std::vector<std::string>> rows;
    const std::string sql =
        "SELECT 1 FROM llm_wiki_page p JOIN llm_wiki_revision r ON r.user=p.user "
        "AND r.id=p.current_revision_id AND r.status='PUBLISHED' WHERE p.user='" + Escape(user) +
        "' AND p.status='STALE' AND EXISTS (SELECT 1 FROM llm_wiki_citation x JOIN knowledge_chunk k "
        "ON k.id=x.chunk_id WHERE x.user=p.user AND x.revision_id=r.id AND k.user=p.user AND k.md5='" +
        Escape(md5) + "') AND EXISTS (SELECT 1 FROM llm_wiki_citation surviving JOIN knowledge_chunk k2 "
        "ON k2.id=surviving.chunk_id AND k2.user=surviving.user WHERE surviving.user=r.user "
        "AND surviving.revision_id=r.id AND surviving.state='ACTIVE' AND k2.state='PUBLISHED' AND EXISTS "
        "(SELECT 1 FROM user_file_list u2 WHERE u2.user=k2.user AND u2.md5=k2.md5)) LIMIT 1";
    if (!Query(sql, &rows)) return false;
    *has_more = !rows.empty();
    return true;
}

bool KnowledgeStore::LoadBacklinks(const std::string &user, const std::string &md5,
                                   std::vector<BacklinkView> *links) {
    if (!links) return false;
    std::vector<std::vector<std::string>> rows;
    const std::string outer_validity = BuildValidWikiRevisionPredicate("p_revision", "p");
    const std::string nested_validity = BuildValidWikiRevisionPredicate("r2", "p2");
    const std::string sql =
        "SELECT DISTINCT COALESCE(l.src_md5,''),p.page_key,p.title,p.id FROM llm_wiki_link l "
        "JOIN llm_wiki_page p ON p.user=l.user AND p.page_key=l.src_page_key "
        "JOIN llm_wiki_revision p_revision ON p_revision.user=p.user AND p_revision.id=p.current_revision_id "
        "WHERE l.user='" + Escape(user) + "' AND l.status='ACTIVE' AND " + outer_validity +
        " AND EXISTS (SELECT 1 FROM user_file_list source_relation WHERE source_relation.user=l.user "
        "AND source_relation.md5='" + Escape(md5) + "') AND (l.dst_md5='" + Escape(md5) +
        "' OR l.dst_page_key IN (SELECT p2.page_key FROM llm_wiki_page p2 "
        "JOIN llm_wiki_revision r2 ON r2.user=p2.user AND r2.id=p2.current_revision_id "
        "JOIN llm_wiki_citation c2 ON c2.revision_id=r2.id AND c2.user=p2.user AND c2.state='ACTIVE' "
        "JOIN knowledge_chunk k2 ON k2.id=c2.chunk_id AND k2.user=c2.user AND k2.state='PUBLISHED' "
        "WHERE p2.user='" + Escape(user) + "' AND " + nested_validity + " AND k2.md5='" + Escape(md5) +
        "' AND EXISTS (SELECT 1 FROM user_file_list target_relation WHERE target_relation.user=k2.user "
        "AND target_relation.md5=k2.md5))) ORDER BY p.title";
    if (!Query(sql, &rows)) return false;
    links->clear(); for (const auto &row : rows) if (row.size() >= 4) links->push_back({row[0],row[2],row[1],row[2],ToInt(row[3])}); return true;
}

bool KnowledgeStore::LoadRelated(const std::string &user, const std::string &md5,
                                 std::vector<BacklinkView> *links) {
    if (!links) return false;
    std::vector<std::vector<std::string>> rows;
    const std::string outer_validity = BuildValidWikiRevisionPredicate("p_revision", "p");
    const std::string nested_validity = BuildValidWikiRevisionPredicate("r2", "p2");
    const std::string sql =
        "SELECT DISTINCT COALESCE(l.dst_md5,''),p.page_key,p.title,p.id FROM llm_wiki_link l "
        "JOIN llm_wiki_page p ON p.user=l.user AND p.page_key=l.dst_page_key "
        "JOIN llm_wiki_revision p_revision ON p_revision.user=p.user AND p_revision.id=p.current_revision_id "
        "WHERE l.user='" + Escape(user) + "' AND l.status='ACTIVE' AND " + outer_validity +
        " AND EXISTS (SELECT 1 FROM user_file_list source_relation WHERE source_relation.user=l.user "
        "AND source_relation.md5='" + Escape(md5) + "') AND (l.src_md5='" + Escape(md5) +
        "' OR l.src_page_key IN (SELECT p2.page_key FROM llm_wiki_page p2 "
        "JOIN llm_wiki_revision r2 ON r2.user=p2.user AND r2.id=p2.current_revision_id "
        "JOIN llm_wiki_citation c2 ON c2.revision_id=r2.id AND c2.user=p2.user AND c2.state='ACTIVE' "
        "JOIN knowledge_chunk k2 ON k2.id=c2.chunk_id AND k2.user=c2.user AND k2.state='PUBLISHED' "
        "WHERE p2.user='" + Escape(user) + "' AND " + nested_validity + " AND k2.md5='" + Escape(md5) +
        "' AND EXISTS (SELECT 1 FROM user_file_list target_relation WHERE target_relation.user=k2.user "
        "AND target_relation.md5=k2.md5))) ORDER BY p.title LIMIT 30";
    if (!Query(sql, &rows)) return false;
    links->clear(); for (const auto &row : rows) if (row.size() >= 4) links->push_back({row[0],row[2],row[1],row[2],ToInt(row[3])}); return true;
}

bool KnowledgeStore::LoadWikiCandidates(const std::string &user, int limit,
                                        std::vector<WikiCandidate> *candidates) {
    if (!candidates) return false;
    std::vector<std::vector<std::string>> rows;
    const std::string wiki_validity = BuildValidWikiRevisionPredicate("r", "p");
    const std::string sql =
        "SELECT p.id,p.page_key,p.title,r.id,r.summary,r.body_markdown FROM llm_wiki_page p "
        "JOIN llm_wiki_revision r ON r.user=p.user AND r.id=p.current_revision_id WHERE p.user='" +
        Escape(user) + "' AND " + wiki_validity + " ORDER BY p.updated_at DESC LIMIT " +
        std::to_string(std::max(1,std::min(limit,20)));
    if (!Query(sql, &rows)) return false;
    candidates->clear(); for (const auto &row : rows) if (row.size() >= 6) candidates->push_back({ToInt(row[0]),row[1],row[2],ToInt(row[3]),row[4],row[5]});
    return LoadWikiCandidateEvidence(user, candidates);
}

bool KnowledgeStore::LoadWikiCandidatesByRevisionIds(
    const std::string &user, const std::vector<std::int64_t> &revision_ids,
    std::vector<WikiCandidate> *candidates) {
    if (!candidates) return false;
    candidates->clear();
    if (revision_ids.empty()) return true;
    std::vector<std::vector<std::string>> rows;
    const std::string wiki_validity = BuildValidWikiRevisionPredicate("r", "p");
    const std::string sql =
        "SELECT p.id,p.page_key,p.title,r.id,r.summary,r.body_markdown "
        "FROM llm_wiki_page p JOIN llm_wiki_revision r ON r.id=p.current_revision_id "
        "AND r.user=p.user WHERE p.user='" + Escape(user) + "' AND r.user='" + Escape(user) +
        "' AND r.id IN (" + InList(revision_ids) + ") AND " + wiki_validity + " ORDER BY p.title";
    if (!Query(sql, &rows)) return false;
    for (const auto &row : rows) if (row.size() >= 6) {
        candidates->push_back({ToInt(row[0]), row[1], row[2], ToInt(row[3]), row[4], row[5]});
    }
    return LoadWikiCandidateEvidence(user, candidates);
}

bool KnowledgeStore::LoadWikiCandidateEvidence(
    const std::string &user, std::vector<WikiCandidate> *candidates,
    bool require_valid_revision) {
    if (!candidates) return false;
    std::vector<std::int64_t> revisions;
    for (const auto &candidate : *candidates) revisions.push_back(candidate.revision_id);
    std::vector<WikiClaimView> claims;
    if (!revisions.empty() &&
        (!require_valid_revision
             ? !LoadWikiClaimsInternal(user, revisions, &claims, false)
             : !LoadWikiClaims(user, revisions, &claims))) return false;
    std::vector<std::int64_t> chunk_ids;
    for (const auto &claim : claims) {
        for (const auto &citation : claim.citations) {
            if (std::find(chunk_ids.begin(), chunk_ids.end(), citation.second) == chunk_ids.end()) {
                chunk_ids.push_back(citation.second);
            }
        }
    }
    std::vector<std::vector<std::string>> rows;
    if (!chunk_ids.empty() && !Query(
        "SELECT k.id,k.md5,k.chunk_no,COALESCE(k.heading,''),k.content,k.content_sha256,k.start_offset,k.end_offset "
        "FROM knowledge_chunk k WHERE k.user='" + Escape(user) + "' AND k.id IN (" + InList(chunk_ids) +
        ") AND k.state='PUBLISHED' AND EXISTS (SELECT 1 FROM user_file_list u WHERE u.user=k.user AND u.md5=k.md5)",
        &rows)) return false;
    std::map<std::int64_t, EvidenceChunk> evidence_by_id;
    for (const auto &row : rows) if (row.size() >= 8) {
        EvidenceChunk evidence;
        evidence.id = ToInt(row[0]); evidence.source_md5 = row[1]; evidence.chunk_no = static_cast<int>(ToInt(row[2]));
        evidence.heading = row[3]; evidence.content = row[4]; evidence.content_sha256 = row[5];
        evidence.start_offset = ToInt(row[6]); evidence.end_offset = ToInt(row[7]);
        evidence_by_id[evidence.id] = std::move(evidence);
    }
    for (auto &candidate : *candidates) {
        candidate.claims.clear(); candidate.active_claims.clear(); candidate.evidence.clear();
        for (const auto &claim : claims) if (claim.revision_id == candidate.revision_id && !claim.citations.empty()) {
            candidate.claims.push_back(claim);
            candidate.active_claims.push_back(claim.text);
            for (const auto &citation : claim.citations) {
                const auto evidence = evidence_by_id.find(citation.second);
                if (evidence != evidence_by_id.end() && std::find_if(candidate.evidence.begin(), candidate.evidence.end(),
                    [&](const EvidenceChunk &item) { return item.id == evidence->first; }) == candidate.evidence.end()) {
                    candidate.evidence.push_back(evidence->second);
                }
            }
        }
    }
    return true;
}

bool KnowledgeStore::PublishWikiPatch(
    const WikiPublishContext &context, const WikiPatch &patch,
    const std::vector<WikiPageEmbedding> &embeddings,
    const std::string &model, const std::string &compiler_version,
    int embedding_dimension, std::string *error, bool *continued) {
    if (continued) *continued = false;
    if (context.user.empty() || (!context.repair_mode && context.trigger_md5.empty()) ||
        !BeginTransaction()) {
        if (error) *error = "cannot begin wiki transaction";
        return false;
    }
    auto fail = [this, error](const char *message) {
        Rollback();
        if (error) *error = message;
        return false;
    };
    if (context.task.id > 0 && !VerifyTaskLeaseInTransaction(context.task)) {
        return fail("task lease lost");
    }
    const std::string eu = Escape(context.user);
    const std::string em = Escape(context.trigger_md5);
    if (!context.repair_mode) {
        std::vector<std::string> relation;
        if (!ReadSingle("SELECT 1 FROM user_file_list WHERE user='" + eu +
                        "' AND md5='" + em + "' FOR UPDATE", &relation)) {
            return fail("source relation is unavailable");
        }
    }
    if (patch.pages.empty()) {
        if (context.repair_mode) return fail("repair patch has no page");
        if (!context.repair_mode &&
            !Exec("UPDATE knowledge_document SET wiki_state='READY',last_error=NULL,updated_at=NOW() WHERE user='" +
                  eu + "' AND md5='" + em + "' AND evidence_state<>'DELETED'")) {
            return fail("wiki state update failed");
        }
        for (const WikiLinkPatch &link : patch.links) {
            if (!Exec("INSERT INTO llm_wiki_link(user,src_page_key,dst_page_key,src_md5,relation,status) VALUES('" + eu + "','" +
                      Escape(link.src_page_key) + "','" + Escape(link.dst_page_key) + "','" + em + "','" +
                      Escape(link.relation) + "','ACTIVE') ON DUPLICATE KEY UPDATE relation=VALUES(relation),src_md5=VALUES(src_md5),status='ACTIVE'")) {
                return fail("wiki link insert failed");
            }
        }
        if (!MarkIndexDirty(context.user)) return fail("index dirty update failed");
        if (!Commit()) return fail("wiki transaction commit failed");
        return true;
    }
    std::map<std::string, std::vector<float>> normalized_embeddings;
    for (const auto &embedding : embeddings) {
        std::vector<float> normalized;
        if (!NormalizeEmbedding(embedding.values, embedding_dimension, &normalized) ||
            !normalized_embeddings.emplace(embedding.page_key, std::move(normalized)).second) {
            return fail("wiki page embedding is invalid");
        }
    }
    for (const WikiPagePatch &page : patch.pages) {
        const auto embedding = normalized_embeddings.find(page.page_key);
        if (embedding == normalized_embeddings.end()) return fail("wiki page embedding is missing");
        if (!Exec("INSERT INTO llm_wiki_page(user,page_key,title,status) VALUES('" + eu + "','" +
                  Escape(page.page_key) + "','" + Escape(page.title) + "','ACTIVE') ON DUPLICATE KEY UPDATE title=VALUES(title),status='ACTIVE'")) {
            return fail("page upsert failed");
        }
        std::vector<std::string> row;
        if (!ReadSingle("SELECT id,COALESCE(current_revision_id,0) FROM llm_wiki_page WHERE user='" + eu +
                        "' AND page_key='" + Escape(page.page_key) + "' FOR UPDATE", &row) || row.size() < 2) {
            return fail("page lock failed");
        }
        const std::int64_t page_id = ToInt(row[0]);
        const std::int64_t current = ToInt(row[1]);
        if ((!page.has_base_revision && current != 0) ||
            (page.has_base_revision && page.base_revision_id != current)) {
            return fail("wiki revision conflict");
        }
        if (!Exec("INSERT INTO llm_wiki_revision(user,page_id,base_revision_id,summary,body_markdown,compiler_version,model,status) VALUES('" +
                  eu + "'," + std::to_string(page_id) + "," + std::to_string(current) + ",'" +
                  Escape(page.summary) + "','" + Escape(page.body_markdown) + "','" + Escape(compiler_version) +
                  "','" + Escape(model) + "','STAGING')")) {
            return fail("revision insert failed");
        }
        const std::int64_t revision = last_insert_id_;
        int ordinal = 0;
        for (const WikiClaimPatch &wiki_claim : page.claims) {
            if (!Exec("INSERT INTO llm_wiki_claim(user,page_id,revision_id,ordinal,text,confidence,status) VALUES('" + eu + "'," +
                      std::to_string(page_id) + "," + std::to_string(revision) + "," + std::to_string(ordinal++) +
                      ",'" + Escape(wiki_claim.text) + "'," + std::to_string(wiki_claim.confidence) + ",'ACTIVE')")) {
                return fail("claim insert failed");
            }
            const std::int64_t claim_id = last_insert_id_;
            for (const std::int64_t chunk_id : wiki_claim.citations) {
                if (std::find(context.allowed_chunk_ids.begin(), context.allowed_chunk_ids.end(), chunk_id) ==
                    context.allowed_chunk_ids.end()) return fail("citation is not an allowed evidence chunk");
                std::vector<std::string> chunk;
                if (!ReadSingle("SELECT 1 FROM knowledge_chunk k WHERE k.id=" + std::to_string(chunk_id) +
                                " AND k.user='" + eu + "' AND k.state='PUBLISHED' AND EXISTS (SELECT 1 FROM user_file_list u WHERE u.user=k.user AND u.md5=k.md5)", &chunk)) {
                    return fail("citation is not an active source chunk");
                }
                if (!Exec("INSERT INTO llm_wiki_citation(user,claim_id,revision_id,chunk_id,state) VALUES('" + eu + "'," +
                          std::to_string(claim_id) + "," + std::to_string(revision) + "," + std::to_string(chunk_id) + ",'ACTIVE')")) {
                    return fail("citation insert failed");
                }
            }
        }
        if (Failpoint("wiki_after_claim")) return fail("wiki failpoint");
        if (!Exec("UPDATE knowledge_vector v JOIN llm_wiki_revision old_r ON old_r.id=v.source_id AND v.source_type='wiki_revision' SET v.status='INACTIVE' WHERE v.user='" + eu + "' AND old_r.page_id=" + std::to_string(page_id) + " AND old_r.status='PUBLISHED'") ||
            !Exec("UPDATE llm_wiki_revision SET status='SUPERSEDED' WHERE page_id=" + std::to_string(page_id) + " AND status='PUBLISHED'") ||
            !Exec("UPDATE llm_wiki_revision SET status='PUBLISHED',published_at=NOW() WHERE id=" + std::to_string(revision) + " AND status='STAGING'") ||
            !Exec("UPDATE llm_wiki_page SET current_revision_id=" + std::to_string(revision) + ",updated_at=NOW(),status='ACTIVE' WHERE id=" + std::to_string(page_id) + " AND user='" + eu + "'")) {
            return fail("wiki publish failed");
        }
        if (Failpoint("wiki_before_publish")) return fail("wiki failpoint");
        if (!PutVector(context.user, "wiki_revision", revision, model, embedding_dimension,
                       embedding->second, nullptr)) return fail("wiki vector insert failed");
    }
    for (const WikiLinkPatch &link : patch.links) {
        if (!Exec("INSERT INTO llm_wiki_link(user,src_page_key,dst_page_key,src_md5,relation,status) VALUES('" + eu + "','" +
                  Escape(link.src_page_key) + "','" + Escape(link.dst_page_key) + "','" +
                  (context.repair_mode ? std::string() : em) + "','" + Escape(link.relation) +
                  "','ACTIVE') ON DUPLICATE KEY UPDATE relation=VALUES(relation),src_md5=VALUES(src_md5),status='ACTIVE'")) {
            return fail("wiki link insert failed");
        }
    }
    if (!context.repair_mode &&
        (!Exec("UPDATE knowledge_document SET wiki_state='READY',last_error=NULL,updated_at=NOW() WHERE user='" + eu +
               "' AND md5='" + em + "' AND evidence_state<>'DELETED'"))) {
        return fail("wiki state update failed");
    }
    if (context.repair_mode && context.task.id > 0) {
        bool has_more = false;
        if (!HasStaleWikiForSource(context.user, context.task.md5, &has_more)) {
            return fail("repair continuation lookup failed");
        }
        if (has_more) {
            if (!ContinueTask(context.task)) return fail("repair continuation lease lost");
            if (continued) *continued = true;
        }
    }
    if (!MarkIndexDirty(context.user) || !Commit()) return fail("wiki transaction commit failed");
    return true;
}

bool KnowledgeStore::DeleteSourceKnowledge(const std::string &user, const std::string &md5,
                                           std::string *error) {
    if (!BeginTransaction()) { if (error) *error = "cannot begin delete transaction"; return false; }
    const std::string eu = Escape(user), em = Escape(md5);
    std::vector<std::vector<std::string>> remaining_relations;
    if (!Query("SELECT 1 FROM user_file_list WHERE user='" + eu + "' AND md5='" + em +
               "' LIMIT 1 FOR UPDATE", &remaining_relations)) {
        Rollback(); if (error) *error = "source relation lookup failed"; return false;
    }
    if (!remaining_relations.empty()) {
        if (!Commit()) { Rollback(); if (error) *error = "source relation still exists"; return false; }
        return true;
    }
    const bool ok =
        Exec("UPDATE knowledge_document SET evidence_state='DELETED',wiki_state='STALE',last_error=NULL WHERE user='" + eu + "' AND md5='" + em + "'") &&
        Exec("UPDATE llm_wiki_citation SET state='STALE' WHERE user='" + eu + "' AND chunk_id IN (SELECT id FROM knowledge_chunk WHERE user='" + eu + "' AND md5='" + em + "')") &&
        Exec("UPDATE knowledge_chunk SET state='DELETED' WHERE user='" + eu + "' AND md5='" + em + "'") &&
        Exec("UPDATE knowledge_vector v JOIN knowledge_chunk c ON c.user=v.user AND c.id=v.source_id AND v.source_type='chunk' SET v.status='INACTIVE' WHERE c.user='" + eu + "' AND c.md5='" + em + "'") &&
        Exec("UPDATE llm_wiki_claim c JOIN llm_wiki_citation affected ON affected.user=c.user AND affected.claim_id=c.id JOIN knowledge_chunk source_chunk ON source_chunk.user=affected.user AND source_chunk.id=affected.chunk_id AND source_chunk.md5='" + em + "' SET c.status=IF(EXISTS (SELECT 1 FROM llm_wiki_citation x JOIN knowledge_chunk k ON k.user=x.user AND k.id=x.chunk_id AND k.state='PUBLISHED' WHERE x.user=c.user AND x.claim_id=c.id AND x.state='ACTIVE' AND EXISTS (SELECT 1 FROM user_file_list u WHERE u.user=k.user AND u.md5=k.md5)),'ACTIVE','STALE') WHERE c.user='" + eu + "'") &&
        Exec("UPDATE knowledge_vector v JOIN llm_wiki_revision r ON r.user=v.user AND r.id=v.source_id AND v.source_type='wiki_revision' JOIN llm_wiki_citation affected ON affected.user=r.user AND affected.revision_id=r.id JOIN knowledge_chunk source_chunk ON source_chunk.user=affected.user AND source_chunk.id=affected.chunk_id AND source_chunk.md5='" + em + "' SET v.status='INACTIVE' WHERE v.user='" + eu + "'") &&
        Exec("UPDATE llm_wiki_link SET status='STALE' WHERE user='" + eu + "' AND (src_md5='" + em + "' OR dst_md5='" + em + "')") &&
        Exec("UPDATE llm_wiki_page p JOIN llm_wiki_revision r ON r.id=p.current_revision_id JOIN llm_wiki_citation affected ON affected.user=r.user AND affected.revision_id=r.id JOIN knowledge_chunk source_chunk ON source_chunk.user=affected.user AND source_chunk.id=affected.chunk_id AND source_chunk.md5='" + em + "' SET p.status=IF(EXISTS (SELECT 1 FROM llm_wiki_citation x JOIN knowledge_chunk k ON k.user=x.user AND k.id=x.chunk_id AND k.state='PUBLISHED' WHERE x.user=r.user AND x.revision_id=r.id AND x.state='ACTIVE' AND EXISTS (SELECT 1 FROM user_file_list u WHERE u.user=k.user AND u.md5=k.md5)),'STALE','RETIRED') WHERE p.user='" + eu + "'");
    if (!ok) { Rollback(); if (error) *error = "source knowledge delete failed"; return false; }
    std::vector<std::vector<std::string>> stale_pages;
    if (!Query("SELECT 1 FROM llm_wiki_page p JOIN llm_wiki_revision r ON r.id=p.current_revision_id JOIN llm_wiki_citation affected ON affected.user=r.user AND affected.revision_id=r.id JOIN knowledge_chunk source_chunk ON source_chunk.user=affected.user AND source_chunk.id=affected.chunk_id AND source_chunk.md5='" + em + "' WHERE p.user='" + eu + "' AND p.status='STALE' LIMIT 1", &stale_pages)) {
        Rollback(); if (error) *error = "repair lookup failed"; return false;
    }
    if ((!stale_pages.empty() && enqueue_knowledge_task(connection_, user.c_str(), md5.c_str(),
                                                         "repair_wiki", "source_deleted", 0) != 0) ||
        !MarkIndexDirty(user) || !Commit()) {
        Rollback(); if (error) *error = "source knowledge delete failed"; return false;
    }
    return true;
}

}  // namespace hydrastore
