#include "fcgi_config.h"
#include "fcgi_stdio.h"

#include "dashscope_api.h"
#include "faiss_snapshot.h"
#include "hash_util.h"
#include "knowledge_store.h"

extern "C" {
#include "cJSON.h"
#include "cfg.h"
#include "deal_mysql.h"
#include "make_log.h"
#include "util_cgi.h"
#include "knowledge_task.h"
}

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string Cfg(const char *section, const char *key, const char *fallback = "") {
    char value[1024] = {0};
    if (get_cfg_value(CFG_PATH, const_cast<char *>(section), const_cast<char *>(key), value) != 0) return fallback;
    return value;
}

std::string GlobalApiKey() {
    const char *environment = std::getenv("DASHSCOPE_API_KEY");
    if (environment && *environment) return environment;
    return Cfg("dashscope", "api_key");
}

std::string Field(cJSON *root, const char *name) {
    cJSON *item = cJSON_GetObjectItem(root, name);
    return item && item->type == cJSON_String && item->valuestring ? item->valuestring : std::string();
}

bool Required(cJSON *root, std::string *user, std::string *token) {
    if (!root || root->type != cJSON_Object || !user || !token) return false;
    *user = Field(root, "user"); *token = Field(root, "token");
    return !user->empty() && !token->empty() && verify_token(const_cast<char *>(user->c_str()), const_cast<char *>(token->c_str())) == 0;
}

void Print(cJSON *value) {
    char *text = cJSON_PrintUnformatted(value);
    if (text) { printf("%s\n", text); free(text); }
}

void Error(int code, const char *message) {
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "code", code);
    cJSON_AddStringToObject(response, "msg", message ? message : "error");
    Print(response); cJSON_Delete(response);
}

std::string UserHash(const std::string &user) {
    hydrastore::Sha256 sha; sha.Update(user.data(), user.size()); return sha.FinalHex();
}

struct CachedIndex {
    std::shared_ptr<hydrastore::FaissSnapshot> snapshot;
    std::uint64_t access = 0;
};

class ReadTransactionGuard {
public:
    explicit ReadTransactionGuard(hydrastore::KnowledgeStore *store) : store_(store) {}
    ~ReadTransactionGuard() { if (active_) store_->RollbackSourceRead(); }

    bool Acquire() {
        active_ = store_ && store_->BeginReadTransaction();
        return active_;
    }

    bool Commit() {
        if (!active_) return true;
        if (!store_->CommitSourceRead()) return false;
        active_ = false;
        return true;
    }

private:
    hydrastore::KnowledgeStore *store_ = nullptr;
    bool active_ = false;
};

std::mutex g_index_cache_mutex;
std::map<std::pair<std::string, std::string>, CachedIndex> g_index_cache;
std::uint64_t g_index_cache_access = 0;

std::size_t SearchCacheCapacity() {
    const long configured = std::strtol(Cfg("faiss", "search_cache_users", "8").c_str(), nullptr, 10);
    return static_cast<std::size_t>(std::max(1L, std::min(32L, configured)));
}

float SearchMinScore() {
    const float configured = static_cast<float>(std::strtod(Cfg("faiss", "search_min_score", "0.45").c_str(), nullptr));
    if (!std::isfinite(configured)) return 0.45f;
    return std::max(-1.0f, std::min(1.0f, configured));
}

bool LoadCachedIndex(const std::string &user, std::int64_t generation,
                     const std::string &path, int dimension,
                     std::size_t capacity,
                     std::shared_ptr<hydrastore::FaissSnapshot> *result) {
    if (!result || generation <= 0 || dimension <= 0) return false;
    const std::pair<std::string, std::string> key(user, path);
    std::lock_guard<std::mutex> lock(g_index_cache_mutex);
    const auto existing = g_index_cache.find(key);
    if (existing != g_index_cache.end()) {
        existing->second.access = ++g_index_cache_access;
        *result = existing->second.snapshot;
        return true;
    }
    std::shared_ptr<hydrastore::FaissSnapshot> snapshot(new hydrastore::FaissSnapshot());
    if (!snapshot->Load(path, dimension)) return false;
    for (auto it = g_index_cache.begin(); it != g_index_cache.end();) {
        if (it->first.first == user) it = g_index_cache.erase(it);
        else ++it;
    }
    while (g_index_cache.size() >= capacity) {
        auto oldest = g_index_cache.begin();
        for (auto it = g_index_cache.begin(); it != g_index_cache.end(); ++it) {
            if (it->second.access < oldest->second.access) oldest = it;
        }
        g_index_cache.erase(oldest);
    }
    g_index_cache.emplace(key, CachedIndex{snapshot, ++g_index_cache_access});
    *result = std::move(snapshot);
    return true;
}

hydrastore::KnowledgeStore MakeStore() {
    const unsigned int port = static_cast<unsigned int>(std::strtoul(Cfg("mysql", "port", "3306").c_str(), nullptr, 10));
    return hydrastore::KnowledgeStore(Cfg("mysql", "ip", "db"), port, Cfg("mysql", "user"), Cfg("mysql", "password"), Cfg("mysql", "database", "yuncuchu"));
}

std::string ApiKey(hydrastore::KnowledgeStore *store, cJSON *root, const std::string &user) {
    const std::string configured = GlobalApiKey();
    if (!configured.empty()) return configured;
    std::string persisted;
    store->LoadApiKey(user, &persisted);
    if (!persisted.empty()) return persisted;
    return Field(root, "api_key");
}

void AddCard(cJSON *data, const hydrastore::FileKnowledgeCard &card) {
    cJSON_AddStringToObject(data, "md5", card.md5.c_str());
    cJSON_AddStringToObject(data, "filename", card.filename.c_str());
    cJSON_AddStringToObject(data, "type", card.type.c_str());
    cJSON_AddNumberToObject(data, "size", static_cast<double>(card.size));
    cJSON_AddStringToObject(data, "url", card.url.c_str());
    cJSON_AddStringToObject(data, "parse_status", card.parse_status.c_str());
    cJSON_AddStringToObject(data, "task_status", card.task_status.c_str());
    cJSON_AddNumberToObject(data, "task_id", static_cast<double>(card.task_id));
    cJSON_AddStringToObject(data, "summary", card.summary.c_str());
    cJSON_AddStringToObject(data, "description", card.description.c_str());
    cJSON_AddStringToObject(data, "error_msg", card.error.c_str());
    cJSON_AddNumberToObject(data, "wiki_ready", card.wiki_ready ? 1 : 0);
    cJSON_AddNumberToObject(data, "ai_evidence_ready", card.evidence_ready ? 1 : 0);
    cJSON_AddNumberToObject(data, "ai_partial_source", card.partial_source ? 1 : 0);
    cJSON_AddStringToObject(data, "ai_error", card.error.c_str());
}

cJSON *PageJson(const hydrastore::WikiPageView &page) {
    cJSON *value = cJSON_CreateObject();
    cJSON_AddStringToObject(value, "page_key", page.page_key.c_str());
    cJSON_AddStringToObject(value, "title", page.title.c_str());
    cJSON_AddNumberToObject(value, "revision_id", static_cast<double>(page.revision_id));
    cJSON_AddStringToObject(value, "summary", page.summary.c_str());
    cJSON_AddStringToObject(value, "body_markdown", page.body_markdown.c_str());
    cJSON *claims = cJSON_CreateArray();
    for (const auto &claim : page.claims) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", static_cast<double>(claim.id));
        cJSON_AddStringToObject(item, "text", claim.text.c_str());
        cJSON_AddNumberToObject(item, "confidence", claim.confidence);
        cJSON *citations = cJSON_CreateArray();
        cJSON *sources = cJSON_CreateArray();
        for (const auto &citation : claim.citations) {
            cJSON *ref = cJSON_CreateObject();
            cJSON_AddStringToObject(ref, "md5", citation.first.c_str());
            cJSON_AddNumberToObject(ref, "chunk_id", static_cast<double>(citation.second));
            cJSON_AddItemToArray(citations, ref);
            cJSON *source = cJSON_CreateObject();
            cJSON_AddStringToObject(source, "md5", citation.first.c_str());
            cJSON_AddNumberToObject(source, "chunkId", static_cast<double>(citation.second));
            cJSON_AddItemToArray(sources, source);
        }
        cJSON_AddItemToObject(item, "citations", citations);
        cJSON_AddItemToObject(item, "sources", sources);
        cJSON_AddItemToArray(claims, item);
    }
    cJSON_AddItemToObject(value, "claims", claims);
    return value;
}

void AddClaim(cJSON *array, const hydrastore::WikiClaimView &claim) {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "id", static_cast<double>(claim.id));
    cJSON_AddStringToObject(item, "text", claim.text.c_str());
    cJSON_AddNumberToObject(item, "confidence", claim.confidence);
    cJSON *citations = cJSON_CreateArray();
    cJSON *sources = cJSON_CreateArray();
    for (const auto &citation : claim.citations) {
        cJSON *ref = cJSON_CreateObject();
        cJSON_AddStringToObject(ref, "md5", citation.first.c_str());
        cJSON_AddNumberToObject(ref, "chunk_id", static_cast<double>(citation.second));
        cJSON_AddItemToArray(citations, ref);
        cJSON *source = cJSON_CreateObject();
        cJSON_AddStringToObject(source, "md5", citation.first.c_str());
        cJSON_AddNumberToObject(source, "chunkId", static_cast<double>(citation.second));
        cJSON_AddItemToArray(sources, source);
    }
    cJSON_AddItemToObject(item, "citations", citations);
    cJSON_AddItemToObject(item, "sources", sources);
    cJSON_AddItemToArray(array, item);
}

int HandleDescribe(cJSON *root) {
    std::string user, token;
    if (!Required(root, &user, &token)) { Error(4, "token error"); return -1; }
    const std::string md5 = Field(root, "md5");
    if (md5.empty()) { Error(1, "missing md5"); return -1; }
    hydrastore::KnowledgeStore store = MakeStore();
    if (!store.Connect()) { Error(1, "db error"); return -1; }
    const bool force = cJSON_GetObjectItem(root, "force") && cJSON_GetObjectItem(root, "force")->valueint != 0;
    hydrastore::FileKnowledgeCard card;
    cJSON *response = cJSON_CreateObject(); cJSON_AddNumberToObject(response, "code", 0);
    const bool has_card = store.LoadFileCard(user, md5, &card);
    const bool queued = force || !has_card || !card.evidence_ready;
    if (queued && !store.EnqueueTask(user, md5, "parse_source", "describe", force)) { cJSON_Delete(response); Error(1, "task enqueue failed"); return -1; }
    cJSON_AddNumberToObject(response, "queued", queued ? 1 : 0);
    if (has_card) { cJSON *data=cJSON_CreateObject(); AddCard(data,card); cJSON_AddItemToObject(response,"data",data); }
    Print(response); cJSON_Delete(response); return 0;
}

int HandleRebuild(cJSON *root) {
    std::string user, token; if (!Required(root,&user,&token)) { Error(4,"token error"); return -1; }
    hydrastore::KnowledgeStore store=MakeStore();
    if (!store.Connect() || !store.MarkIndexDirty(user)) { Error(1,"index rebuild unavailable"); return -1; }
    cJSON *response=cJSON_CreateObject(); cJSON_AddNumberToObject(response,"code",0); cJSON_AddNumberToObject(response,"accepted",1); Print(response); cJSON_Delete(response); return 0;
}

int HandleSearch(cJSON *root) {
    std::string user, token;
    if (!Required(root, &user, &token)) { Error(4, "token error"); return -1; }
    const std::string query = Field(root, "query");
    if (query.empty()) { Error(1, "empty query"); return -1; }
    if (query.size() > 4096) { Error(1, "query too long"); return -1; }
    hydrastore::KnowledgeStore store = MakeStore();
    if (!store.Connect()) { Error(1, "db error"); return -1; }
    const std::string key = ApiKey(&store, root, user);
    if (key.empty()) { Error(1, "missing api_key"); return -1; }
    const std::string model = Cfg("dashscope", "embedding_model", "text-embedding-v3");
    const int dimension = std::max(1, std::atoi(Cfg("dashscope", "embedding_dimension", "1024").c_str()));
    std::vector<float> embedding(static_cast<std::size_t>(dimension), 0.0f);
    if (dashscope_get_embedding(key.c_str(), model.c_str(), query.c_str(), embedding.data(), dimension) != 0) {
        Error(1, "embedding failed"); return -1;
    }
    double norm = 0.0;
    for (float value : embedding) {
        if (!std::isfinite(value)) { Error(1, "embedding failed"); return -1; }
        norm += static_cast<double>(value) * value;
    }
    norm = std::sqrt(norm);
    if (norm == 0.0) { Error(1, "embedding failed"); return -1; }
    for (float &value : embedding) value = static_cast<float>(value / norm);
    std::int64_t published = 0, dirty = 0, published_lease_epoch = 0;
    if (!store.LoadIndexState(user, &published, &dirty, &published_lease_epoch)) { Error(1, "index state unavailable"); return -1; }
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "code", 0);
    cJSON_AddNumberToObject(response, "index_generation", static_cast<double>(published));
    cJSON *files = cJSON_CreateArray(), *wiki = cJSON_CreateArray();
    if (published == 0) {
        cJSON_AddNumberToObject(response, "count", 0);
        cJSON_AddItemToObject(response, "files", files); cJSON_AddItemToObject(response, "wiki", wiki);
        Print(response); cJSON_Delete(response); return 0;
    }
    const std::string path = Cfg("faiss", "user_index_dir", "/data/faiss/users") + "/" +
        UserHash(user) + "/vectors." + std::to_string(published) +
        (published_lease_epoch > 0 ? "." + std::to_string(published_lease_epoch) : std::string()) + ".faiss";
    std::shared_ptr<hydrastore::FaissSnapshot> snapshot;
    if (!LoadCachedIndex(user, published, path, dimension, SearchCacheCapacity(), &snapshot)) {
        cJSON_Delete(response); Error(2, "index unavailable"); return -1;
    }
    std::vector<std::int64_t> ids;
    std::vector<float> scores;
    if (!snapshot->Search(embedding, 30, &ids, &scores)) {
        cJSON_Delete(response); Error(2, "index unavailable"); return -1;
    }
    const float min_score = SearchMinScore();
    std::vector<std::int64_t> filtered_ids;
    std::map<std::int64_t, float> score_by_id;
    for (std::size_t i = 0; i < ids.size() && i < scores.size(); ++i) {
        if (ids[i] > 0 && std::isfinite(scores[i]) && scores[i] >= min_score) {
            filtered_ids.push_back(ids[i]); score_by_id[ids[i]] = scores[i];
        }
    }
    ReadTransactionGuard search_read(&store);
    if (!search_read.Acquire()) {
        cJSON_Delete(response); Error(1, "search read unavailable"); return -1;
    }
    std::vector<hydrastore::SearchHydration> hydrated;
    if (!filtered_ids.empty() && !store.LoadSearchHydration(user, filtered_ids, &hydrated)) {
        cJSON_Delete(response); Error(1, "search hydration failed"); return -1;
    }
    std::vector<std::int64_t> revisions;
    for (const auto &row : hydrated) if (row.source_type == "wiki_revision" &&
        std::find(revisions.begin(), revisions.end(), row.revision_id) == revisions.end()) revisions.push_back(row.revision_id);
    std::vector<hydrastore::WikiClaimView> claims;
    if (!revisions.empty() && !store.LoadWikiClaims(user, revisions, &claims)) {
        cJSON_Delete(response); Error(1, "claim hydration failed"); return -1;
    }
    for (const auto &row : hydrated) {
        const auto score_it = score_by_id.find(row.vector_id);
        if (score_it == score_by_id.end()) continue;
        const float score = score_it->second;
        if (row.source_type == "chunk") {
            cJSON *file = nullptr;
            for (int i = 0; i < cJSON_GetArraySize(files); ++i) {
                cJSON *item = cJSON_GetArrayItem(files, i);
                cJSON *md5 = cJSON_GetObjectItem(item, "md5");
                if (md5 && md5->valuestring && std::string(md5->valuestring) == row.md5) { file = item; break; }
            }
            if (!file) {
                file = cJSON_CreateObject();
                cJSON_AddStringToObject(file, "md5", row.md5.c_str());
                cJSON_AddStringToObject(file, "filename", row.filename.c_str());
                cJSON_AddStringToObject(file, "type", row.type.c_str());
                cJSON_AddNumberToObject(file, "size", static_cast<double>(row.size));
                cJSON_AddStringToObject(file, "url", row.url.c_str());
                cJSON_AddNumberToObject(file, "score", score);
                cJSON_AddNumberToObject(file, "wiki_ready", 0);
                cJSON_AddItemToObject(file, "matches", cJSON_CreateArray());
                cJSON_AddItemToObject(file, "snippets", cJSON_CreateArray());
                cJSON_AddItemToArray(files, file);
            } else {
                cJSON *file_score = cJSON_GetObjectItem(file, "score");
                if (file_score && score > static_cast<float>(file_score->valuedouble)) file_score->valuedouble = score;
            }
            cJSON *matches = cJSON_GetObjectItem(file, "matches");
            if (matches && cJSON_GetArraySize(matches) < 2) {
                cJSON *match = cJSON_CreateObject();
                cJSON_AddNumberToObject(match, "chunkId", static_cast<double>(row.source_id));
                cJSON_AddNumberToObject(match, "score", score);
                cJSON_AddStringToObject(match, "snippet", row.snippet.c_str());
                cJSON_AddItemToArray(matches, match);
            }
            cJSON *snippets = cJSON_GetObjectItem(file, "snippets");
            if (snippets && cJSON_GetArraySize(snippets) < 2) cJSON_AddItemToArray(snippets, cJSON_CreateString(row.snippet.c_str()));
        } else if (row.source_type == "wiki_revision") {
            hydrastore::WikiPageView view;
            view.page_key = row.page_key; view.title = row.title; view.revision_id = row.revision_id;
            view.summary = row.summary; view.body_markdown = row.body_markdown;
            cJSON *page = PageJson(view);
            cJSON_AddNumberToObject(page, "score", score);
            cJSON *claim_array = cJSON_CreateArray();
            for (const auto &claim : claims) if (claim.id > 0 && claim.revision_id == row.revision_id) AddClaim(claim_array, claim);
            cJSON_ReplaceItemInObject(page, "claims", claim_array);
            cJSON_AddItemToArray(wiki, page);
        }
    }
    cJSON_AddNumberToObject(response,"count",cJSON_GetArraySize(files));cJSON_AddItemToObject(response,"files",files);cJSON_AddItemToObject(response,"wiki",wiki);Print(response);const bool committed=search_read.Commit();cJSON_Delete(response);return committed?0:-1;
}

int HandleFileCard(cJSON *root) {
    std::string user,token;if(!Required(root,&user,&token)){Error(4,"token error");return -1;} const std::string md5=Field(root,"md5");
    hydrastore::KnowledgeStore store=MakeStore();hydrastore::FileKnowledgeCard card;if(!store.Connect()||!store.LoadFileCard(user,md5,&card)){Error(1,"file not found");return -1;}
    cJSON *response=cJSON_CreateObject();cJSON_AddNumberToObject(response,"code",0);cJSON *data=cJSON_CreateObject();AddCard(data,card);cJSON_AddItemToObject(response,"data",data);Print(response);cJSON_Delete(response);return 0;
}

int HandleWiki(cJSON *root) {
    std::string user,token;if(!Required(root,&user,&token)){Error(4,"token error");return -1;}
    const std::string md5=Field(root,"md5");hydrastore::KnowledgeStore store=MakeStore();
    std::vector<hydrastore::WikiPageView> pages;
    if(!store.Connect()||!store.BeginSourceRead(user,md5)||!store.LoadWikiForSource(user,md5,&pages)){
        store.RollbackSourceRead();Error(1,"wiki unavailable");return -1;
    }
    cJSON *response=cJSON_CreateObject();cJSON_AddNumberToObject(response,"code",0);cJSON *array=cJSON_CreateArray();for(const auto &page:pages)cJSON_AddItemToArray(array,PageJson(page));cJSON_AddItemToObject(response,"pages",array);
    hydrastore::FileKnowledgeCard card;
    if (store.LoadFileCard(user, md5, &card)) {
        cJSON *source=cJSON_CreateObject();cJSON_AddStringToObject(source,"md5",card.md5.c_str());cJSON_AddStringToObject(source,"filename",card.filename.c_str());cJSON_AddStringToObject(source,"type",card.type.c_str());cJSON_AddNumberToObject(source,"size",static_cast<double>(card.size));cJSON_AddStringToObject(source,"url",card.url.c_str());cJSON_AddNumberToObject(source,"evidence_ready",card.evidence_ready?1:0);cJSON_AddNumberToObject(source,"partial_source",card.partial_source?1:0);cJSON_AddItemToObject(response,"source",source);
    }
    if(!pages.empty())cJSON_AddItemToObject(response,"data",PageJson(pages.front()));else cJSON_AddNullToObject(response,"data");Print(response);const bool committed=store.CommitSourceRead();cJSON_Delete(response);return committed?0:-1;
}

int HandleLinks(cJSON *root,bool backlinks) {
    std::string user,token;if(!Required(root,&user,&token)){Error(4,"token error");return -1;}
    const std::string md5=Field(root,"md5");hydrastore::KnowledgeStore store=MakeStore();std::vector<hydrastore::BacklinkView> links;
    if(!store.Connect()||!store.BeginSourceRead(user,md5)||(backlinks?!store.LoadBacklinks(user,md5,&links):!store.LoadRelated(user,md5,&links))){
        store.RollbackSourceRead();Error(1,"links unavailable");return -1;
    }
    cJSON *response=cJSON_CreateObject();cJSON_AddNumberToObject(response,"code",0);cJSON *array=cJSON_CreateArray();for(const auto &link:links){cJSON *item=cJSON_CreateObject();cJSON_AddStringToObject(item,"md5",link.md5.c_str());cJSON_AddStringToObject(item,"page_key",link.page_key.c_str());cJSON_AddStringToObject(item,"title",link.title.c_str());cJSON_AddStringToObject(item,"concept",link.concept.c_str());cJSON_AddNumberToObject(item,"page_id",static_cast<double>(link.page_id));cJSON_AddItemToArray(array,item);}cJSON_AddItemToObject(response,"links",array);Print(response);const bool committed=store.CommitSourceRead();cJSON_Delete(response);return committed?0:-1;
}

int HandleGetApiKey(cJSON *root) {
    std::string user,token;if(!Required(root,&user,&token)){Error(4,"token error");return -1;}hydrastore::KnowledgeStore store=MakeStore();std::string key;if(!store.Connect()||!store.LoadApiKey(user,&key)){Error(1,"db error");return -1;}const bool configured=!GlobalApiKey().empty()||!key.empty();cJSON *response=cJSON_CreateObject();cJSON_AddNumberToObject(response,"code",0);cJSON *data=cJSON_CreateObject();cJSON_AddNumberToObject(data,"configured",configured?1:0);cJSON_AddItemToObject(response,"data",data);Print(response);cJSON_Delete(response);return 0;
}

int HandleSetApiKey(cJSON *root) {
    std::string user,token;if(!Required(root,&user,&token)){Error(4,"token error");return -1;}const std::string key=Field(root,"api_key");if(key.size()>256){Error(1,"api_key too long");return -1;}MYSQL *conn=msql_conn(const_cast<char *>(Cfg("mysql","user").c_str()),const_cast<char *>(Cfg("mysql","password").c_str()),const_cast<char *>(Cfg("mysql","database","yuncuchu").c_str()));if(!conn){Error(1,"db error");return -1;}std::string escaped(key.size()*2+1,'\0');const unsigned long n=mysql_real_escape_string(conn,escaped.data(),key.data(),key.size());escaped.resize(n);std::string eu(user.size()*2+1,'\0');const unsigned long un=mysql_real_escape_string(conn,eu.data(),user.data(),user.size());eu.resize(un);const std::string sql="UPDATE user_info SET api_key='"+escaped+"' WHERE user_name='"+eu+"'";const bool ok=mysql_query(conn,sql.c_str())==0;mysql_close(conn);if(!ok){Error(1,"db error");return -1;}cJSON *response=cJSON_CreateObject();cJSON_AddNumberToObject(response,"code",0);Print(response);cJSON_Delete(response);return 0;
}

}  // namespace

int main() {
    while (FCGI_Accept() >= 0) {
        char *length_text = getenv("CONTENT_LENGTH");
        const long length = length_text ? std::strtol(length_text, nullptr, 10) : 0;
        if (length <= 0 || length > 2 * 1024 * 1024) { Error(1,"invalid request"); continue; }
        std::string body(static_cast<std::size_t>(length), '\0');
        if (fread(body.data(), 1, body.size(), stdin) != body.size()) { Error(1,"invalid request"); continue; }
        cJSON *root = cJSON_Parse(body.c_str()); if(!root){Error(1,"invalid json");continue;}
        std::string command=Field(root,"cmd").empty()?Field(root,"command"):Field(root,"cmd");
        if (command.empty()) {
            const char *query = getenv("QUERY_STRING");
            if (query) {
                const char *start = strstr(query, "cmd=");
                if (start) { start += 4; const char *end = strchr(start, '&'); command.assign(start, end ? end - start : strlen(start)); }
            }
        }
        printf("Content-Type: application/json\r\n\r\n");
        if(command=="describe")HandleDescribe(root);else if(command=="search")HandleSearch(root);else if(command=="rebuild")HandleRebuild(root);else if(command=="file_card")HandleFileCard(root);else if(command=="wiki")HandleWiki(root);else if(command=="backlinks")HandleLinks(root,true);else if(command=="related")HandleLinks(root,false);else if(command=="get_apikey")HandleGetApiKey(root);else if(command=="set_apikey")HandleSetApiKey(root);else Error(1,"unknown command");
        cJSON_Delete(root);
    }
    return 0;
}
