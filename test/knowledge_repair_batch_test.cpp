#include "knowledge_store.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
std::string Env(const char *name, const char *fallback) {
    const char *value = std::getenv(name);
    return value && *value ? value : fallback;
}

bool Raw(MYSQL *db, const std::string &sql) {
    return db && mysql_query(db, sql.c_str()) == 0;
}

std::int64_t InsertId(MYSQL *db) {
    return db ? static_cast<std::int64_t>(mysql_insert_id(db)) : 0;
}

std::string PageKey(int index) {
    return std::string(39, '0') + static_cast<char>('1' + index);
}
}

int main() {
    const std::string host = Env("HYDRA_TEST_DB_HOST", "127.0.0.1");
    const unsigned int port = static_cast<unsigned int>(std::strtoul(Env("HYDRA_TEST_DB_PORT", "3306").c_str(), nullptr, 10));
    const std::string user = Env("HYDRA_TEST_DB_USER", "root");
    const std::string password = Env("HYDRA_TEST_DB_PASSWORD", "123456");
    const std::string database = Env("HYDRA_TEST_DB_NAME", "yuncuchu");
    hydrastore::KnowledgeStore store(host, port, user, password, database);
    if (!store.Connect()) return 77;

    const std::string test_user = "__hydra_repair_batch_test";
    const std::string deleted_md5 = "__hydra_repair_batch_deleted";
    const std::string survivor_md5 = "__hydra_repair_batch_survivor";
    assert(Raw(store.Connection(), "DELETE FROM llm_wiki_citation WHERE user='" + test_user + "'"));
    assert(Raw(store.Connection(), "DELETE FROM llm_wiki_claim WHERE user='" + test_user + "'"));
    assert(Raw(store.Connection(), "DELETE FROM llm_wiki_revision WHERE user='" + test_user + "'"));
    assert(Raw(store.Connection(), "DELETE FROM llm_wiki_page WHERE user='" + test_user + "'"));
    assert(Raw(store.Connection(), "DELETE FROM knowledge_chunk WHERE user='" + test_user + "'"));
    assert(Raw(store.Connection(), "DELETE FROM knowledge_document WHERE user='" + test_user + "'"));
    assert(Raw(store.Connection(), "DELETE FROM knowledge_index_state WHERE user='" + test_user + "'"));
    assert(Raw(store.Connection(), "DELETE FROM ai_parse_task WHERE user='" + test_user + "'"));
    assert(Raw(store.Connection(), "DELETE FROM user_file_list WHERE user='" + test_user + "'"));
    assert(Raw(store.Connection(), "INSERT INTO user_file_list(user,md5,file_name,shared_status,pv) VALUES('" + test_user + "','" + deleted_md5 + "','deleted.txt',0,0),('" + test_user + "','" + survivor_md5 + "','survivor.txt',0,0)"));
    assert(Raw(store.Connection(), "INSERT INTO knowledge_chunk(user,md5,generation,chunk_no,heading,content,content_sha256,start_offset,end_offset,state) VALUES('" + test_user + "','" + deleted_md5 + "',1,0,'deleted','deleted','aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',0,7,'PUBLISHED')"));
    const std::int64_t deleted_chunk = InsertId(store.Connection());
    assert(Raw(store.Connection(), "INSERT INTO knowledge_chunk(user,md5,generation,chunk_no,heading,content,content_sha256,start_offset,end_offset,state) VALUES('" + test_user + "','" + survivor_md5 + "',1,0,'survivor','survivor','bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb',0,8,'PUBLISHED')"));
    const std::int64_t survivor_chunk = InsertId(store.Connection());
    assert(deleted_chunk > 0 && survivor_chunk > 0);

    std::vector<std::int64_t> revisions;
    for (int index = 0; index < 7; ++index) {
        assert(Raw(store.Connection(), "INSERT INTO llm_wiki_page(user,page_key,title,status) VALUES('" + test_user + "','" + PageKey(index) + "','Batch page " + std::to_string(index) + "','ACTIVE')"));
        const std::int64_t page_id = InsertId(store.Connection());
        assert(page_id > 0);
        assert(Raw(store.Connection(), "INSERT INTO llm_wiki_revision(user,page_id,base_revision_id,summary,body_markdown,compiler_version,model,status) VALUES('" + test_user + "'," + std::to_string(page_id) + ",0,'summary','body','test','test','PUBLISHED')"));
        const std::int64_t revision_id = InsertId(store.Connection());
        assert(revision_id > 0);
        revisions.push_back(revision_id);
        assert(Raw(store.Connection(), "UPDATE llm_wiki_page SET current_revision_id=" + std::to_string(revision_id) + " WHERE id=" + std::to_string(page_id)));
        assert(Raw(store.Connection(), "INSERT INTO llm_wiki_claim(user,page_id,revision_id,ordinal,text,confidence,status) VALUES('" + test_user + "'," + std::to_string(page_id) + "," + std::to_string(revision_id) + ",0,'mixed source claim',1,'ACTIVE')"));
        const std::int64_t claim_id = InsertId(store.Connection());
        assert(claim_id > 0);
        assert(Raw(store.Connection(), "INSERT INTO llm_wiki_citation(user,claim_id,revision_id,chunk_id,state) VALUES('" + test_user + "'," + std::to_string(claim_id) + "," + std::to_string(revision_id) + "," + std::to_string(deleted_chunk) + ",'ACTIVE'),('" + test_user + "'," + std::to_string(claim_id) + "," + std::to_string(revision_id) + "," + std::to_string(survivor_chunk) + ",'ACTIVE')"));
    }

    assert(Raw(store.Connection(), "DELETE FROM user_file_list WHERE user='" + test_user + "' AND md5='" + deleted_md5 + "'"));
    assert(store.DeleteSourceKnowledge(test_user, deleted_md5, nullptr));
    bool has_more = false;
    for (const std::size_t expected : {std::size_t(3), std::size_t(3), std::size_t(1)}) {
        std::vector<hydrastore::WikiPageView> pages;
        assert(store.LoadStaleWikiForSource(test_user, deleted_md5, &pages, 3));
        assert(pages.size() == expected);
        for (const auto &page : pages) {
            assert(Raw(store.Connection(), "UPDATE llm_wiki_citation x JOIN knowledge_chunk k ON k.user=x.user AND k.id=x.chunk_id SET x.state='STALE' WHERE x.user='" + test_user + "' AND x.revision_id=" + std::to_string(page.revision_id) + " AND k.md5='" + deleted_md5 + "'"));
            assert(Raw(store.Connection(), "UPDATE llm_wiki_page SET status='ACTIVE' WHERE user='" + test_user + "' AND id=" + std::to_string(page.page_id)));
        }
        assert(store.HasStaleWikiForSource(test_user, deleted_md5, &has_more));
        if (expected != 1) assert(has_more);
    }
    assert(!has_more);

    assert(Raw(store.Connection(), "DELETE FROM llm_wiki_citation WHERE user='" + test_user + "'"));
    assert(Raw(store.Connection(), "DELETE FROM llm_wiki_claim WHERE user='" + test_user + "'"));
    assert(Raw(store.Connection(), "DELETE FROM llm_wiki_revision WHERE user='" + test_user + "'"));
    assert(Raw(store.Connection(), "DELETE FROM llm_wiki_page WHERE user='" + test_user + "'"));
    assert(Raw(store.Connection(), "DELETE FROM knowledge_chunk WHERE user='" + test_user + "'"));
    assert(Raw(store.Connection(), "DELETE FROM knowledge_document WHERE user='" + test_user + "'"));
    assert(Raw(store.Connection(), "DELETE FROM knowledge_index_state WHERE user='" + test_user + "'"));
    assert(Raw(store.Connection(), "DELETE FROM ai_parse_task WHERE user='" + test_user + "'"));
    assert(Raw(store.Connection(), "DELETE FROM user_file_list WHERE user='" + test_user + "'"));
    return 0;
}
