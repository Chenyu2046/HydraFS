#include "knowledge_store.h"

#include <cassert>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {
std::string Env(const char *name, const char *fallback) {
    const char *value = std::getenv(name);
    return value && *value ? value : fallback;
}

bool Raw(MYSQL *db, const std::string &sql) {
    return db && mysql_query(db, sql.c_str()) == 0;
}

std::string Scalar(MYSQL *db, const std::string &sql) {
    if (!Raw(db, sql)) return {};
    MYSQL_RES *result = mysql_store_result(db);
    if (!result) return {};
    MYSQL_ROW row = mysql_fetch_row(result);
    const std::string value = row && row[0] ? row[0] : std::string();
    mysql_free_result(result);
    return value;
}
}

int main() {
    const std::string host = Env("HYDRA_TEST_DB_HOST", "127.0.0.1");
    const unsigned int port = static_cast<unsigned int>(std::strtoul(Env("HYDRA_TEST_DB_PORT", "3306").c_str(), nullptr, 10));
    const std::string user = Env("HYDRA_TEST_DB_USER", "root");
    const std::string password = Env("HYDRA_TEST_DB_PASSWORD", "123456");
    const std::string database = Env("HYDRA_TEST_DB_NAME", "yuncuchu");
    hydrastore::KnowledgeStore unavailable(host, port, user, password, database);
    std::int64_t published = 7;
    std::int64_t dirty = 8;
    assert(!unavailable.LoadIndexState("__hydra_unavailable_state", &published, &dirty));
    hydrastore::KnowledgeStore seed(host, port, user, password, database);
    if (!seed.Connect()) return 77;
    const std::string test_user = "__hydra_claim_test";
    const std::string test_md5 = "__hydra_claim_test_md5";
    assert(seed.EnqueueTask(test_user, test_md5, "parse_source", "test", true));
    std::vector<hydrastore::KnowledgeTaskClaim> claims(2);
    std::vector<bool> claimed(2, false);
    std::thread first([&] { hydrastore::KnowledgeStore store(host, port, user, password, database); claimed[0] = store.Connect() && store.ClaimTask("claim-test-a", &claims[0]); });
    std::thread second([&] { hydrastore::KnowledgeStore store(host, port, user, password, database); claimed[1] = store.Connect() && store.ClaimTask("claim-test-b", &claims[1]); });
    first.join(); second.join();
    assert(static_cast<int>(claimed[0]) + static_cast<int>(claimed[1]) == 1);
    hydrastore::KnowledgeTaskClaim winner = claimed[0] ? claims[0] : claims[1];
    hydrastore::KnowledgeStore cleanup(host, port, user, password, database);
    assert(cleanup.Connect());
    assert(cleanup.FinishTask(winner));
    assert(mysql_query(cleanup.Connection(), ("UPDATE ai_parse_task SET status='pending',lease_until=NULL WHERE user='" + test_user + "' AND md5='" + test_md5 + "'").c_str()) == 0);
    hydrastore::KnowledgeTaskClaim stale;
    assert(cleanup.ClaimTask("claim-test-stale", &stale));
    assert(mysql_query(cleanup.Connection(), ("UPDATE ai_parse_task SET lease_until=DATE_SUB(NOW(),INTERVAL 1 MINUTE) WHERE id=" + std::to_string(stale.id)).c_str()) == 0);
    assert(!cleanup.FinishTask(stale));
    assert(!cleanup.ContinueTask(stale));
    assert(cleanup.RecoverExpiredTasks());
    assert(mysql_query(cleanup.Connection(), ("UPDATE ai_parse_task SET next_retry_at=NOW() WHERE id=" + std::to_string(stale.id)).c_str()) == 0);
    hydrastore::KnowledgeTaskClaim recovered;
    assert(cleanup.ClaimTask("claim-test-recovered", &recovered));
    assert(!cleanup.FinishTask(stale));
    assert(cleanup.ContinueTask(recovered));
    hydrastore::KnowledgeTaskClaim continued;
    assert(cleanup.ClaimTask("claim-test-continued", &continued));
    assert(continued.retry_count == recovered.retry_count);
    assert(cleanup.FinishTask(continued));
    assert(mysql_query(cleanup.Connection(), ("DELETE FROM ai_parse_task WHERE user='" + test_user + "' AND md5='" + test_md5 + "'").c_str()) == 0);

    const std::string enqueue_user = "__hydra_enqueue_race_test";
    const std::string enqueue_md5 = "__hydra_enqueue_race_md5";
    assert(Raw(cleanup.Connection(), "DELETE FROM ai_parse_task WHERE user='" + enqueue_user + "' AND md5='" + enqueue_md5 + "'"));
    std::vector<std::thread> enqueuers;
    std::vector<int> enqueued(32, 0);
    for (int i = 0; i < 32; ++i) {
        enqueuers.emplace_back([&, i] {
            hydrastore::KnowledgeStore store(host, port, user, password, database);
            enqueued[i] = store.Connect() && store.EnqueueTask(enqueue_user, enqueue_md5, "parse_file", "race", true) ? 1 : 0;
        });
    }
    for (auto &enqueuer : enqueuers) enqueuer.join();
    for (const int ok : enqueued) assert(ok != 0);
    assert(Scalar(cleanup.Connection(), "SELECT COUNT(*) FROM ai_parse_task WHERE user='" + enqueue_user + "' AND md5='" + enqueue_md5 + "' AND task_type='parse_source' AND status IN ('pending','running')") == "1");
    assert(Raw(cleanup.Connection(), "DELETE FROM ai_parse_task WHERE user='" + enqueue_user + "' AND md5='" + enqueue_md5 + "'"));

    const std::string retry_user = "__hydra_retry_budget_test";
    const std::string retry_md5 = "__hydra_retry_budget_md5";
    assert(Raw(cleanup.Connection(), "DELETE FROM ai_parse_task WHERE user='" + retry_user + "' AND md5='" + retry_md5 + "'"));
    assert(cleanup.EnqueueTask(retry_user, retry_md5, "parse_source", "retry", true));
    for (int attempt = 0; attempt < 4; ++attempt) {
        hydrastore::KnowledgeTaskClaim retry_claim;
        assert(cleanup.ClaimTask("retry-worker-" + std::to_string(attempt), &retry_claim));
        assert(mysql_query(cleanup.Connection(), ("UPDATE ai_parse_task SET lease_until=DATE_SUB(NOW(),INTERVAL 1 MINUTE),next_retry_at=NOW() WHERE id=" + std::to_string(retry_claim.id)).c_str()) == 0);
        assert(cleanup.RecoverExpiredTasks());
        assert(mysql_query(cleanup.Connection(), ("UPDATE ai_parse_task SET next_retry_at=NOW() WHERE user='" + retry_user + "' AND md5='" + retry_md5 + "' AND status='pending'").c_str()) == 0);
    }
    assert(Scalar(cleanup.Connection(), "SELECT status FROM ai_parse_task WHERE user='" + retry_user + "' AND md5='" + retry_md5 + "'") == "failed");
    hydrastore::KnowledgeTaskClaim no_retry_claim;
    assert(!cleanup.ClaimTask("retry-worker-after-budget", &no_retry_claim));
    assert(Raw(cleanup.Connection(), "DELETE FROM ai_parse_task WHERE user='" + retry_user + "' AND md5='" + retry_md5 + "'"));

    const std::string delete_user = "__hydra_delete_test";
    const std::string deleted_md5 = "__hydra_delete_source_a";
    const std::string shared_md5 = "__hydra_delete_source_b";
    assert(Raw(cleanup.Connection(), "DELETE FROM llm_wiki_citation WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM llm_wiki_claim WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM llm_wiki_revision WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM llm_wiki_page WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM knowledge_vector WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM knowledge_chunk WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM knowledge_document WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM knowledge_index_state WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM ai_parse_task WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM user_file_list WHERE user='" + delete_user + "'"));
    std::int64_t no_index_published = 7;
    std::int64_t no_index_dirty = 8;
    assert(cleanup.LoadIndexState(delete_user, &no_index_published, &no_index_dirty));
    assert(no_index_published == 0 && no_index_dirty == 0);
    assert(Raw(cleanup.Connection(), "DELETE FROM file_info WHERE md5 IN ('" + deleted_md5 + "','" + shared_md5 + "')"));
    assert(Raw(cleanup.Connection(), "INSERT INTO file_info(md5,file_id,url,size,type,count) VALUES('" + deleted_md5 + "','/group1/delete-a','http://storage/delete-a',10,'txt',1),('" + shared_md5 + "','/group1/delete-b','http://storage/delete-b',10,'txt',1)"));
    assert(Raw(cleanup.Connection(), "INSERT INTO user_file_list(user,md5,file_name,shared_status,pv) VALUES('" + delete_user + "','" + deleted_md5 + "','delete-a.txt',0,0),('" + delete_user + "','" + shared_md5 + "','delete-b.txt',0,0)"));
    assert(Raw(cleanup.Connection(), "INSERT INTO knowledge_document(user,md5,current_generation,published_generation,evidence_state,wiki_state) VALUES('" + delete_user + "','" + deleted_md5 + "',1,1,'READY','READY')"));
    assert(Raw(cleanup.Connection(), "INSERT INTO knowledge_chunk(user,md5,generation,chunk_no,heading,content,content_sha256,start_offset,end_offset,state) VALUES('" + delete_user + "','" + deleted_md5 + "',1,0,'A','deleted evidence','aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',0,10,'PUBLISHED')"));
    const std::string deleted_chunk = Scalar(cleanup.Connection(), "SELECT LAST_INSERT_ID()");
    assert(!deleted_chunk.empty());
    assert(Raw(cleanup.Connection(), "INSERT INTO knowledge_chunk(user,md5,generation,chunk_no,heading,content,content_sha256,start_offset,end_offset,state) VALUES('" + delete_user + "','" + shared_md5 + "',1,0,'B','shared evidence','bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb',0,10,'PUBLISHED')"));
    const std::string shared_chunk = Scalar(cleanup.Connection(), "SELECT LAST_INSERT_ID()");
    assert(!shared_chunk.empty());
    assert(Raw(cleanup.Connection(), "INSERT INTO llm_wiki_page(user,page_key,title,status) VALUES('" + delete_user + "','1111111111111111111111111111111111111111','Shared page','ACTIVE'),('" + delete_user + "','2222222222222222222222222222222222222222','Deleted page','ACTIVE')"));
    const std::string shared_page = Scalar(cleanup.Connection(), "SELECT id FROM llm_wiki_page WHERE user='" + delete_user + "' AND page_key='1111111111111111111111111111111111111111'");
    const std::string deleted_page = Scalar(cleanup.Connection(), "SELECT id FROM llm_wiki_page WHERE user='" + delete_user + "' AND page_key='2222222222222222222222222222222222222222'");
    assert(!shared_page.empty() && !deleted_page.empty());
    assert(Raw(cleanup.Connection(), "INSERT INTO llm_wiki_revision(user,page_id,base_revision_id,summary,body_markdown,compiler_version,model,status) VALUES('" + delete_user + "'," + shared_page + ",0,'shared','shared','test','test','PUBLISHED'),('" + delete_user + "'," + deleted_page + ",0,'deleted','deleted','test','test','PUBLISHED')"));
    const std::string shared_revision = Scalar(cleanup.Connection(), "SELECT id FROM llm_wiki_revision WHERE user='" + delete_user + "' AND page_id=" + shared_page);
    const std::string deleted_revision = Scalar(cleanup.Connection(), "SELECT id FROM llm_wiki_revision WHERE user='" + delete_user + "' AND page_id=" + deleted_page);
    assert(!shared_revision.empty() && !deleted_revision.empty());
    assert(Raw(cleanup.Connection(), "UPDATE llm_wiki_page SET current_revision_id=" + shared_revision + " WHERE id=" + shared_page));
    assert(Raw(cleanup.Connection(), "UPDATE llm_wiki_page SET current_revision_id=" + deleted_revision + " WHERE id=" + deleted_page));
    assert(Raw(cleanup.Connection(), "INSERT INTO llm_wiki_claim(user,page_id,revision_id,ordinal,text,confidence,status) VALUES('" + delete_user + "'," + shared_page + "," + shared_revision + ",0,'shared claim',1,'ACTIVE'),('" + delete_user + "'," + deleted_page + "," + deleted_revision + ",0,'deleted claim',1,'ACTIVE')"));
    const std::string shared_claim = Scalar(cleanup.Connection(), "SELECT id FROM llm_wiki_claim WHERE user='" + delete_user + "' AND revision_id=" + shared_revision);
    const std::string deleted_claim = Scalar(cleanup.Connection(), "SELECT id FROM llm_wiki_claim WHERE user='" + delete_user + "' AND revision_id=" + deleted_revision);
    assert(!shared_claim.empty() && !deleted_claim.empty());
    assert(Raw(cleanup.Connection(), "INSERT INTO llm_wiki_citation(user,claim_id,revision_id,chunk_id,state) VALUES('" + delete_user + "'," + shared_claim + "," + shared_revision + "," + deleted_chunk + ",'ACTIVE'),('" + delete_user + "'," + shared_claim + "," + shared_revision + "," + shared_chunk + ",'ACTIVE'),('" + delete_user + "'," + deleted_claim + "," + deleted_revision + "," + deleted_chunk + ",'ACTIVE')"));
    assert(Raw(cleanup.Connection(), "INSERT INTO knowledge_vector(user,source_type,source_id,model,dimension,embedding,status) VALUES('" + delete_user + "','wiki_revision'," + shared_revision + ",'test',2,UNHEX('0000000000000000'),'ACTIVE'),('" + delete_user + "','wiki_revision'," + deleted_revision + ",'test',2,UNHEX('0000000000000000'),'ACTIVE')"));
    const std::string shared_vector = Scalar(cleanup.Connection(), "SELECT id FROM knowledge_vector WHERE user='" + delete_user + "' AND source_type='wiki_revision' AND source_id=" + shared_revision);
    const std::string deleted_vector = Scalar(cleanup.Connection(), "SELECT id FROM knowledge_vector WHERE user='" + delete_user + "' AND source_type='wiki_revision' AND source_id=" + deleted_revision);
    assert(!shared_vector.empty() && !deleted_vector.empty());
    assert(Raw(cleanup.Connection(), "DELETE FROM user_file_list WHERE user='" + delete_user + "' AND md5='" + deleted_md5 + "'"));
    std::vector<hydrastore::KnowledgeVectorRecord> immediate_vectors;
    assert(cleanup.LoadActiveVectors(delete_user, &immediate_vectors));
    for (const auto &vector : immediate_vectors) assert(vector.source_type != "wiki_revision");
    std::vector<hydrastore::SearchHydration> immediate_hydration;
    assert(cleanup.LoadSearchHydration(delete_user, {std::stoll(shared_vector), std::stoll(deleted_vector)}, &immediate_hydration));
    for (const auto &row : immediate_hydration) assert(row.source_type != "wiki_revision");
    std::vector<hydrastore::WikiCandidate> immediate_candidates;
    assert(cleanup.LoadWikiCandidates(delete_user, 5, &immediate_candidates));
    assert(immediate_candidates.empty());
    std::vector<hydrastore::WikiPageView> immediate_pages;
    assert(cleanup.LoadWikiForSource(delete_user, shared_md5, &immediate_pages));
    assert(immediate_pages.empty());
    assert(cleanup.DeleteSourceKnowledge(delete_user, deleted_md5, nullptr));
    assert(Scalar(cleanup.Connection(), "SELECT state FROM knowledge_chunk WHERE user='" + delete_user + "' AND md5='" + deleted_md5 + "'") == "DELETED");
    assert(Scalar(cleanup.Connection(), "SELECT state FROM llm_wiki_citation WHERE user='" + delete_user + "' AND claim_id=" + shared_claim + " AND chunk_id=" + deleted_chunk) == "STALE");
    assert(Scalar(cleanup.Connection(), "SELECT status FROM llm_wiki_claim WHERE id=" + shared_claim) == "ACTIVE");
    assert(Scalar(cleanup.Connection(), "SELECT status FROM llm_wiki_claim WHERE id=" + deleted_claim) == "STALE");
    assert(Scalar(cleanup.Connection(), "SELECT status FROM llm_wiki_page WHERE id=" + shared_page) == "STALE");
    assert(Scalar(cleanup.Connection(), "SELECT status FROM llm_wiki_page WHERE id=" + deleted_page) == "RETIRED");
    assert(Scalar(cleanup.Connection(), "SELECT status FROM knowledge_vector WHERE source_id=" + shared_revision + " AND source_type='wiki_revision'") == "INACTIVE");
    assert(Scalar(cleanup.Connection(), "SELECT status FROM knowledge_vector WHERE source_id=" + deleted_revision + " AND source_type='wiki_revision'") == "INACTIVE");
    std::vector<hydrastore::WikiPageView> stale_pages;
    assert(cleanup.LoadStaleWikiForSource(delete_user, deleted_md5, &stale_pages));
    assert(stale_pages.size() == 1 && stale_pages[0].title == "Shared page");
    assert(Scalar(cleanup.Connection(), "SELECT COUNT(*) FROM ai_parse_task WHERE user='" + delete_user + "' AND md5='" + deleted_md5 + "' AND task_type='repair_wiki' AND status IN ('pending','running')") == "1");
    std::vector<hydrastore::WikiPageView> shared_pages;
    assert(cleanup.LoadWikiForSource(delete_user, shared_md5, &shared_pages));
    assert(shared_pages.empty());
    assert(std::strtoll(Scalar(cleanup.Connection(), "SELECT dirty_generation FROM knowledge_index_state WHERE user='" + delete_user + "'").c_str(), nullptr, 10) > 0);
    assert(Raw(cleanup.Connection(), "DELETE FROM llm_wiki_citation WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM llm_wiki_claim WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM llm_wiki_revision WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM llm_wiki_page WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM knowledge_vector WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM knowledge_chunk WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM knowledge_document WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM knowledge_index_state WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM ai_parse_task WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM user_file_list WHERE user='" + delete_user + "'"));
    assert(Raw(cleanup.Connection(), "DELETE FROM file_info WHERE md5 IN ('" + deleted_md5 + "','" + shared_md5 + "')"));
    return 0;
}
