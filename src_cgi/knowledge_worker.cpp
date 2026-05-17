#include "util_cgi.h"
#include "dashscope_api.h"
#include "faiss_wrapper.h"
/**
 * @file knowledge_worker.cpp
 * @brief 知识层异步解析 worker（常驻后台进程，非 FastCGI）
 *
 * 职责：
 *   1. 轮询 ai_parse_task 表取 pending 任务
 *   2. 从 FastDFS 下载源文件
 *   3. 提取文本内容（txt/md/code 直接读，pdf 用 pdftotext）
 *   4. 生成摘要（截取前 300 字）、标签（[[link]] + 关键词）
 *   5. 调用 DashScope 生成 embedding 写入全局/用户缓存
 *   6. 构建 wiki_page、解析 [[显式双链]] → wiki_link
 *   7. 更新任务状态 success/failed
 *   8. 失败任务 retry_count < 3 时重置为 pending
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <curl/curl.h>
#include <time.h>
#include <ctype.h>

extern "C" {
#include "make_log.h"
#include "cfg.h"
#include "cJSON.h"
#include "deal_mysql.h"
}

// 前置声明
static char *escape_mysql_text(MYSQL *conn, const char *input);

#define KUTIL_LOG_PROC       "cgi"
#define KUTIL_LOG_PROC         "knowledge_worker"

/* 全局配置 */
static char mysql_user[128] = {0};
static char mysql_pwd[128] = {0};
static char mysql_db[128] = {0};
static char embedding_model[64] = {0};
static char vl_model[64] = {0};
static int  embedding_dimension = 1024;
static char web_server_ip[30] = {0};
static char web_server_port[10] = {0};
static char faiss_user_index_dir[512] = {0};
static char faiss_lock_dir[512] = "/tmp/faiss_locks";
/* DashScope API Key：cfg.json 优先（出厂可用） */
static char dashscope_api_key[256] = {0};
/* 自动双链阈值 */
static int   auto_link_topk = 5;
static float auto_link_threshold = 0.55f;
static int   auto_link_type_fallback = 1;
static int   auto_link_type_limit = 3;

static int g_running = 1;

static void handle_signal(int sig)
{
    if (sig == SIGTERM || sig == SIGINT) {
        LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
            "received signal %d, shutting down gracefully\n", sig);
        g_running = 0;
    }
}

static void read_cfg()
{
    get_cfg_value(CFG_PATH, "mysql", "user", mysql_user);
    get_cfg_value(CFG_PATH, "mysql", "password", mysql_pwd);
    get_cfg_value(CFG_PATH, "mysql", "database", mysql_db);
    get_cfg_value(CFG_PATH, "dashscope", "embedding_model", embedding_model);
    get_cfg_value(CFG_PATH, "dashscope", "vl_model", vl_model);
    if (strlen(vl_model) == 0) strncpy(vl_model, "qwen-vl-plus", sizeof(vl_model) - 1);
    char dim_str[16] = {0};
    get_cfg_value(CFG_PATH, "dashscope", "embedding_dimension", dim_str);
    if (strlen(dim_str) > 0) embedding_dimension = atoi(dim_str);
    get_cfg_value(CFG_PATH, "faiss", "user_index_dir", faiss_user_index_dir);
    get_cfg_value(CFG_PATH, "web_server", "ip", web_server_ip);
    get_cfg_value(CFG_PATH, "web_server", "port", web_server_port);
    get_cfg_value(CFG_PATH, "dashscope", "api_key", dashscope_api_key);

    /* 自动双链阈值（可选） */
    char tk[16] = {0}, th[16] = {0}, tf[16] = {0}, tl[16] = {0};
    get_cfg_value(CFG_PATH, "dashscope", "auto_link_topk", tk);
    get_cfg_value(CFG_PATH, "dashscope", "auto_link_threshold", th);
    get_cfg_value(CFG_PATH, "dashscope", "auto_link_type_fallback", tf);
    get_cfg_value(CFG_PATH, "dashscope", "auto_link_type_limit", tl);
    if (strlen(tk) > 0) auto_link_topk = atoi(tk);
    if (strlen(th) > 0) auto_link_threshold = (float)atof(th);
    if (strlen(tf) > 0) auto_link_type_fallback = atoi(tf);
    if (strlen(tl) > 0) auto_link_type_limit = atoi(tl);
    if (auto_link_type_limit < 0) auto_link_type_limit = 0;

    LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
        "config loaded: mysql=%s, dim=%d, web=%s:%s, vl=%s, key=%s, topk=%d, th=%.2f, type_fallback=%d/%d\n",
        mysql_db, embedding_dimension, web_server_ip, web_server_port,
        vl_model, strlen(dashscope_api_key) > 0 ? "configured" : "EMPTY",
        auto_link_topk, auto_link_threshold, auto_link_type_fallback, auto_link_type_limit);
}

/* ---- 工具函数 ---- */

static char *escape_mysql(MYSQL *conn, const char *s)
{
    if (!s) s = "";
    unsigned long len = (unsigned long)strlen(s);
    char *esc = (char *)malloc(len * 2 + 1);
    if (!esc) return NULL;
    mysql_real_escape_string(conn, esc, s, len);
    return esc;
}

static char *escape_blob(MYSQL *conn, const void *data, unsigned long len)
{
    char *esc = (char *)malloc(len * 2 + 1);
    if (!esc) return NULL;
    mysql_real_escape_string(conn, esc, (const char *)data, len);
    return esc;
}

/* 从 user_info 读取用户的 DashScope API Key */
static int load_user_api_key(MYSQL *conn, const char *user, char *key_buf, int buf_len)
{
    char *esc = escape_mysql(conn, user);
    if (!esc) return -1;
    char sql[512] = {0};
    snprintf(sql, sizeof(sql), "SELECT api_key FROM user_info WHERE user_name='%s'", esc);
    free(esc);

    if (mysql_query(conn, sql) != 0) return -1;
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) return -1;
    MYSQL_ROW row = mysql_fetch_row(res);
    int ret = -1;
    if (row && row[0] && strlen(row[0]) > 0) {
        strncpy(key_buf, row[0], buf_len - 1);
        key_buf[buf_len - 1] = '\0';
        ret = 0;
    }
    mysql_free_result(res);
    return ret;
}

/* 下载回调：绕过 fcgi_stdio */
static size_t download_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    int fd = *(int *)userdata;
    size_t total = size * nmemb;
    ssize_t written = write(fd, ptr, total);
    return (written > 0) ? (size_t)written : 0;
}

static int download_file(const char *url, const char *save_path)
{
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    int fd = open(save_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { curl_easy_cleanup(curl); return -1; }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, download_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fd);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    close(fd);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK || http_code != 200) { remove(save_path); return -1; }
    return 0;
}

/* 获取文件 FastDFS URL */
static int get_file_url(MYSQL *conn, const char *md5, char *url_buf, int buf_len)
{
    char *esc = escape_mysql(conn, md5);
    if (!esc) return -1;
    char sql[512] = {0};
    snprintf(sql, sizeof(sql), "SELECT url FROM file_info WHERE md5='%s'", esc);
    free(esc);
    if (mysql_query(conn, sql) != 0) return -1;
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) return -1;
    MYSQL_ROW row = mysql_fetch_row(res);
    int ret = -1;
    if (row && row[0]) {
        strncpy(url_buf, row[0], buf_len - 1);
        url_buf[buf_len - 1] = '\0';
        ret = 0;
    }
    mysql_free_result(res);
    return ret;
}

/* 判断文件类型 */
static int is_text_type(const char *type)
{
    if (!type) return 0;
    const char *text_types[] = {
        "txt","md","csv","json","xml","html","htm","log",
        "c","cpp","h","hpp","py","js","ts","jsx","tsx",
        "css","java","go","rs","rb","php","sh","bat","yaml","yml", NULL
    };
    for (int i = 0; text_types[i]; i++)
        if (strcasecmp(type, text_types[i]) == 0) return 1;
    return 0;
}

/* 判断是否为图片（走 Qwen-VL 多模态） */
static int is_image_type(const char *type)
{
    if (!type) return 0;
    const char *imgs[] = {"png","jpg","jpeg","gif","bmp","webp", NULL};
    for (int i = 0; imgs[i]; i++)
        if (strcasecmp(type, imgs[i]) == 0) return 1;
    return 0;
}

static const char *broad_file_category(const char *type)
{
    if (!type || strlen(type) == 0) return "other";
    if (is_image_type(type)) return "image";
    if (strcasecmp(type, "pdf") == 0) return "document";
    if (strcasecmp(type, "doc") == 0 || strcasecmp(type, "docx") == 0 ||
        strcasecmp(type, "ppt") == 0 || strcasecmp(type, "pptx") == 0 ||
        strcasecmp(type, "xls") == 0 || strcasecmp(type, "xlsx") == 0) {
        return "document";
    }
    if (strcasecmp(type, "c") == 0 || strcasecmp(type, "cpp") == 0 ||
        strcasecmp(type, "h") == 0 || strcasecmp(type, "hpp") == 0 ||
        strcasecmp(type, "py") == 0 || strcasecmp(type, "js") == 0 ||
        strcasecmp(type, "ts") == 0 || strcasecmp(type, "jsx") == 0 ||
        strcasecmp(type, "tsx") == 0 || strcasecmp(type, "java") == 0 ||
        strcasecmp(type, "go") == 0 || strcasecmp(type, "rs") == 0 ||
        strcasecmp(type, "rb") == 0 || strcasecmp(type, "php") == 0 ||
        strcasecmp(type, "sh") == 0 || strcasecmp(type, "bat") == 0) {
        return "code";
    }
    if (is_text_type(type)) return "text";
    if (strcasecmp(type, "zip") == 0 || strcasecmp(type, "rar") == 0 ||
        strcasecmp(type, "7z") == 0 || strcasecmp(type, "tar") == 0 ||
        strcasecmp(type, "gz") == 0) {
        return "archive";
    }
    if (strcasecmp(type, "mp3") == 0 || strcasecmp(type, "wav") == 0 ||
        strcasecmp(type, "flac") == 0 || strcasecmp(type, "aac") == 0) {
        return "audio";
    }
    if (strcasecmp(type, "mp4") == 0 || strcasecmp(type, "mov") == 0 ||
        strcasecmp(type, "avi") == 0 || strcasecmp(type, "mkv") == 0) {
        return "video";
    }
    return "other";
}

static int read_text_file(const char *path, char *out, int max_len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int n = read(fd, out, max_len - 1);
    close(fd);
    if (n <= 0) return -1;
    out[n] = '\0';
    return n;
}

static int extract_pdf_text(const char *pdf_path, char *out, int max_len)
{
    char cmd[1024] = {0};
    snprintf(cmd, sizeof(cmd), "pdftotext '%s' - 2>/dev/null", pdf_path);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    int total = 0;
    while (total < max_len - 1 && fgets(out + total, max_len - total, fp)) {
        total = strlen(out);
    }
    pclose(fp);
    return (total > 0) ? total : -1;
}

/* 提取 [[显式双链]] → JSON 标签数组，如 ["FastDFS","FAISS"] */
static void extract_tags_from_wikilinks(const char *text, char *tags_json, int tags_size)
{
    char buf[2048] = {0};
    int pos = 0;
    const char *p = text;

    while (*p && pos < (int)sizeof(buf) - 8) {
        const char *start = strstr(p, "[[");
        if (!start) break;
        const char *end = strstr(start + 2, "]]");
        if (!end) break;

        int len = (int)(end - start - 2);
        if (len > 0 && len < 64) {
            if (pos > 0) { buf[pos++] = ','; }
            buf[pos++] = '"';
            memcpy(buf + pos, start + 2, len);
            pos += len;
            buf[pos++] = '"';
            buf[pos] = '\0';
        }
        p = end + 2;
    }
    snprintf(tags_json, tags_size, "[%s]", buf);
}

typedef struct {
    char md5[260];
    char name[260];
    float score;
} AutoLinkCandidate;

static int candidate_list_contains(const AutoLinkCandidate *cands, int cnt, const char *md5)
{
    if (!cands || !md5) return 0;
    for (int i = 0; i < cnt; i++) {
        if (strcmp(cands[i].md5, md5) == 0) return 1;
    }
    return 0;
}

static int is_space_char(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static void trim_line(char *line)
{
    if (!line) return;
    char *start = line;
    while (*start && is_space_char(*start)) start++;
    if (start != line) memmove(line, start, strlen(start) + 1);

    int len = (int)strlen(line);
    while (len > 0 && is_space_char(line[len - 1])) {
        line[len - 1] = '\0';
        len--;
    }
}

static void build_outline_json(const char *text, char *outline_json, int outline_size)
{
    if (!outline_json || outline_size <= 0) return;
    snprintf(outline_json, outline_size, "[]");
    if (!text || strlen(text) == 0) return;

    char *copy = strdup(text);
    if (!copy) return;

    cJSON *outline = cJSON_CreateArray();
    int heading_count = 0;
    int fallback_count = 0;
    char *saveptr = NULL;
    char *line = strtok_r(copy, "\n", &saveptr);

    while (line) {
        trim_line(line);
        if (strlen(line) == 0) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        char candidate[128] = {0};
        if (line[0] == '#') {
            const char *title = line;
            while (*title == '#') title++;
            while (*title && is_space_char(*title)) title++;
            if (*title) {
                snprintf(candidate, sizeof(candidate), "%.*s", 100, title);
                cJSON_AddItemToArray(outline, cJSON_CreateString(candidate));
                heading_count++;
            }
        } else if (heading_count == 0 && fallback_count < 3) {
            snprintf(candidate, sizeof(candidate), "%.*s", 100, line);
            cJSON_AddItemToArray(outline, cJSON_CreateString(candidate));
            fallback_count++;
        }

        if (heading_count >= 6 || (heading_count == 0 && fallback_count >= 3)) break;
        line = strtok_r(NULL, "\n", &saveptr);
    }

    char *json_str = cJSON_PrintUnformatted(outline);
    if (json_str) {
        strncpy(outline_json, json_str, outline_size - 1);
        outline_json[outline_size - 1] = '\0';
        free(json_str);
    }

    cJSON_Delete(outline);
    free(copy);
}

/* 生成摘要：取文本前 300 字符（UTF-8 安全截断） */
static void make_summary(const char *text, char *summary, int max_len)
{
    const char *src = text;
    char *dst = summary;
    int char_count = 0;
    int byte_pos = 0;
    int total_bytes = (int)strlen(text);

    while (byte_pos < total_bytes && char_count < 300 && (dst - summary) < max_len - 4) {
        unsigned char c = (unsigned char)text[byte_pos];
        int char_bytes = 1;
        if (c >= 0xFC)      char_bytes = 6;
        else if (c >= 0xF8) char_bytes = 5;
        else if (c >= 0xF0) char_bytes = 4;
        else if (c >= 0xE0) char_bytes = 3;
        else if (c >= 0xC0) char_bytes = 2;

        /* 确保不截断多字节字符 */
        if (byte_pos + char_bytes > total_bytes) break;
        if ((dst - summary) + char_bytes >= max_len - 1) break;

        memcpy(dst, text + byte_pos, char_bytes);
        dst += char_bytes;
        byte_pos += char_bytes;
        char_count++;
    }
    *dst = '\0';
}

/* 写全局缓存 file_ai_desc */
static int write_global_cache(MYSQL *conn, const char *md5, const char *desc, 
                              const float *vec, const char *summary, 
                              const char *tags, const char *model) 
{
    char sql[4096] = {0};
    int ret = 0;
    
    char *esc_md5 = escape_mysql_text(conn, md5);
    char *esc_desc = escape_mysql_text(conn, desc);
    char *esc_model = escape_mysql_text(conn, model);
    char *esc_summary = escape_mysql_text(conn, summary);
    char *esc_tags = escape_mysql_text(conn, tags);
    char *esc_blob = NULL;
    if (!esc_md5 || !esc_desc || !esc_model || !esc_summary || !esc_tags) goto fail;

    if (vec) {
        int blob_len = embedding_dimension * (int)sizeof(float);
        esc_blob = escape_blob(conn, vec, blob_len);
        if (!esc_blob) goto fail;
    }

    snprintf(sql, sizeof(sql), 
             "insert into global_ai_cache (md5, description, embedding, summary, outline_json) "
             "values ('%s', '%s', '%s', '%s', '%s') "
             "on duplicate key update description=values(description), "
             "embedding=values(embedding), summary=values(summary), outline_json=values(outline_json)",
             md5, esc_desc, esc_blob, esc_summary, esc_tags);
             
    ret = mysql_query(conn, sql) == 0 ? 0 : -1;

fail:
    if (esc_md5) free(esc_md5);
    if (esc_desc) free(esc_desc);
    if (esc_model) free(esc_model);
    if (esc_summary) free(esc_summary);
    if (esc_tags) free(esc_tags);
    if (esc_blob) free(esc_blob);
    return ret;
}

/* 写用户私有记录 user_file_ai_desc + 更新 parse_status */
static int write_user_ai_record(MYSQL *conn, const char *user, const char *md5, 
                                const char *desc, const float *vec, 
                                const char *summary, const char *tags, const char *status, const char *model) 
{
    char sql[4096] = {0};
    int ret = 0;
    
    char *esc_user = escape_mysql_text(conn, user);
    char *esc_md5 = escape_mysql_text(conn, md5);
    char *esc_desc = escape_mysql_text(conn, desc);
    char *esc_model = escape_mysql_text(conn, model);
    char *esc_summary = escape_mysql_text(conn, summary);
    char *esc_tags = escape_mysql_text(conn, tags);
    char *esc_status = escape_mysql(conn, status);
    char *esc_blob = NULL;
    if (!esc_user || !esc_md5 || !esc_desc || !esc_model ||
        !esc_summary || !esc_tags || !esc_status) goto fail;

    if (vec) {
        int blob_len = embedding_dimension * (int)sizeof(float);
        esc_blob = escape_blob(conn, vec, blob_len);
        if (!esc_blob) goto fail;
    }

    snprintf(sql, sizeof(sql),
             "insert into user_file_ai_desc (user, md5, description, embedding, summary, tags, parse_status) "
             "values ('%s', '%s', '%s', '%s', '%s', '%s', '%s') "
             "on duplicate key update description=values(description), "
             "embedding=values(embedding), summary=values(summary), tags=values(tags), parse_status=values(parse_status)",
             esc_user, esc_md5, esc_desc, esc_blob, esc_summary, esc_tags, esc_status);
             
    ret = mysql_query(conn, sql) == 0 ? 0 : -1;

fail:
    if (esc_user) free(esc_user);
    if (esc_md5) free(esc_md5);
    if (esc_desc) free(esc_desc);
    if (esc_model) free(esc_model);
    if (esc_summary) free(esc_summary);
    if (esc_tags) free(esc_tags);
    if (esc_status) free(esc_status);
    if (esc_blob) free(esc_blob);
    return ret;
}

/* 写 wiki_page */
static int write_wiki_page(MYSQL *conn, const char *user, const char *md5, 
                           const char *title, const char *summary,
                           const char *tags, const char *content_json) 
{
    char sql[4096] = {0};
    int ret = 0;

    char *esc_user = escape_mysql_text(conn, user);
    char *esc_md5 = escape_mysql_text(conn, md5);
    char *esc_title = escape_mysql_text(conn, title);
    char *esc_summary = escape_mysql_text(conn, summary);
    char *esc_tags = escape_mysql_text(conn, tags);
    char *esc_content = escape_mysql_text(conn, content_json);
    if (!esc_user || !esc_md5 || !esc_title) goto fail;
    
    snprintf(sql, sizeof(sql),
             "insert into wiki_page (user, md5, title, content_json, summary, status) "
             "values ('%s', '%s', '%s', '%s', '%s', 'active') "
             "on duplicate key update title=values(title), content_json=values(content_json), summary=values(summary), status='active'",
             esc_user, esc_md5, esc_title, esc_content, esc_summary);
             
    ret = mysql_query(conn, sql) == 0 ? 0 : -1;

fail:
    if (esc_user) free(esc_user);
    if (esc_md5) free(esc_md5);
    if (esc_title) free(esc_title);
    if (esc_summary) free(esc_summary);
    if (esc_tags) free(esc_tags);
    if (esc_content) free(esc_content);
    return ret;
}

/* 写 wiki_link（按概念去重替换） */
static int write_wiki_links(MYSQL *conn, const char *user, const char *md5,
                            const char *tags_json)
{
    if (!tags_json || strlen(tags_json) <= 2) return 0;
    /* tags_json 格式: ["ConceptA", "ConceptB"] */
    const char *p = tags_json + 1; /* skip '[' */
    int ok = 0, fail = 0;
    while (*p && *p != ']') {
        if (*p == ',' || *p == ' ') { p++; continue; }
        if (*p != '"') break;
        p++;
        const char *end = strchr(p, '"');
        if (!end) break;
        int len = (int)(end - p);
        if (len <= 0 || len >= 255) { p = end + 1; continue; }
        char name[256] = {0};
        memcpy(name, p, len);
        name[len] = '\0';

        char *esc_user = escape_mysql(conn, user);
        char *esc_md5 = escape_mysql(conn, md5);
        char *esc_name = escape_mysql(conn, name);
        if (esc_user && esc_md5 && esc_name) {
            char sql[1024] = {0};
            snprintf(sql, sizeof(sql),
                     "REPLACE INTO wiki_link (user, src_md5, dst_name, link_type) "
                     "VALUES ('%s','%s','%s','explicit')",
                     esc_user, esc_md5, esc_name);
            if (mysql_query(conn, sql) == 0) ok++; else fail++;
        }
        if (esc_user) free(esc_user);
        if (esc_md5) free(esc_md5);
        if (esc_name) free(esc_name);
        p = end + 1;
    }
    return (fail > 0 && ok == 0) ? -1 : 0;
}

/* 更新任务状态 */
static int update_task_status(MYSQL *conn, long task_id, const char *status,
                              const char *error_msg)
{
    char sql[1024] = {0};
    if (error_msg && strlen(error_msg) > 0) {
        char *esc = escape_mysql(conn, error_msg);
        if (esc) {
            snprintf(sql, sizeof(sql),
                     "UPDATE ai_parse_task SET status='%s', error_msg='%s' WHERE id=%ld",
                     status, esc, task_id);
            free(esc);
        }
    } else {
        snprintf(sql, sizeof(sql),
                 "UPDATE ai_parse_task SET status='%s', error_msg=NULL WHERE id=%ld",
                 status, task_id);
    }
    return mysql_query(conn, sql) == 0 ? 0 : -1;
}

static int update_user_parse_state(MYSQL *conn, const char *user, const char *md5,
                                   const char *parse_status, const char *error_msg)
{
    char *esc_user = escape_mysql(conn, user);
    char *esc_md5 = escape_mysql(conn, md5);
    char *esc_status = escape_mysql(conn, parse_status);
    char *esc_error = error_msg ? escape_mysql(conn, error_msg) : NULL;
    if (!esc_user || !esc_md5 || !esc_status) {
        if (esc_user) free(esc_user);
        if (esc_md5) free(esc_md5);
        if (esc_status) free(esc_status);
        if (esc_error) free(esc_error);
        return -1;
    }

    char sql[2048] = {0};
    if (esc_error) {
        snprintf(sql, sizeof(sql),
                 "UPDATE user_file_ai_desc SET parse_status='%s', status=%d, error_msg='%s' "
                 "WHERE user='%s' AND md5='%s'",
                 esc_status,
                 strcmp(parse_status, "success") == 0 ? 1 : 2,
                 esc_error,
                 esc_user, esc_md5);
    } else {
        snprintf(sql, sizeof(sql),
                 "UPDATE user_file_ai_desc SET parse_status='%s', status=%d, error_msg=NULL "
                 "WHERE user='%s' AND md5='%s'",
                 esc_status,
                 strcmp(parse_status, "success") == 0 ? 1 : 2,
                 esc_user, esc_md5);
    }

    int ret = mysql_query(conn, sql) == 0 ? 0 : -1;
    free(esc_user);
    free(esc_md5);
    free(esc_status);
    if (esc_error) free(esc_error);
    return ret;
}

/* === 自动隐式双链：基于向量余弦相似度，连接同用户其他文件 ===
 *
 * 流程：
 *   1. 查询同 user 下 parse_status='success' 且 md5 != self 的所有 (md5, file_name, embedding)
 *   2. 已 L2 normalize → 余弦相似度 = 点积
 *   3. 排序取 top-K，过滤 score >= threshold
 *   4. INSERT/UPSERT wiki_link：双向写两条，link_type='implicit'
 *
 * 这是 Obsidian 风格"上传自动连接"的真双链核心。
 */
static int auto_build_implicit_links(MYSQL *conn, const char *user,
                                     const char *self_md5, const char *self_type,
                                     const float *self_vec)
{
    if (!conn || !user || !self_md5 || !self_vec) return -1;
    if (auto_link_topk <= 0) return 0;

    char *eu0 = escape_mysql(conn, user);
    char *es0 = escape_mysql(conn, self_md5);
    if (!eu0 || !es0) { if (eu0) free(eu0); if (es0) free(es0); return -1; }

    char sql[2048] = {0};
    snprintf(sql, sizeof(sql),
             "SELECT u.md5, u.embedding, COALESCE(l.file_name, u.md5) "
             "FROM user_file_ai_desc u "
             "LEFT JOIN user_file_list l ON l.user=u.user AND l.md5=u.md5 "
             "WHERE u.user='%s' AND u.parse_status='success' AND u.md5 <> '%s' "
             "AND u.embedding IS NOT NULL",
             eu0, es0);
    free(eu0); free(es0);

    if (mysql_query(conn, sql) != 0) {
        LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
            "auto_link query failed: %s\n", mysql_error(conn));
        return -1;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) return -1;

    int cap = 64, cnt = 0;
    AutoLinkCandidate *cands = (AutoLinkCandidate *)malloc(sizeof(AutoLinkCandidate) * cap);
    if (!cands) { mysql_free_result(res); return -1; }

    int blob_len = embedding_dimension * (int)sizeof(float);
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        unsigned long *lens = mysql_fetch_lengths(res);
        if (!row[0] || !row[1] || (int)lens[1] != blob_len) continue;
        const float *vec = (const float *)row[1];

        float dot = 0.0f;
        for (int i = 0; i < embedding_dimension; i++) dot += self_vec[i] * vec[i];
        if (dot < auto_link_threshold) continue;

        if (cnt >= cap) {
            cap *= 2;
            AutoLinkCandidate *p = (AutoLinkCandidate *)realloc(cands, sizeof(AutoLinkCandidate) * cap);
            if (!p) break;
            cands = p;
        }
        strncpy(cands[cnt].md5, row[0], sizeof(cands[cnt].md5) - 1);
        cands[cnt].md5[sizeof(cands[cnt].md5) - 1] = '\0';
        cands[cnt].name[0] = '\0';
        if (row[2]) {
            strncpy(cands[cnt].name, row[2], sizeof(cands[cnt].name) - 1);
            cands[cnt].name[sizeof(cands[cnt].name) - 1] = '\0';
        }
        cands[cnt].score = dot;
        cnt++;
    }
    mysql_free_result(res);

    if (cnt == 0) {
        LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
            "auto_link[%.32s]: no similar files (th=%.2f)\n",
            self_md5, auto_link_threshold);
    }

    /* 选 top-K（选择排序，K<=10，n 数百级别足够） */
    int k = cnt < auto_link_topk ? cnt : auto_link_topk;
    for (int i = 0; i < k; i++) {
        int max_idx = i;
        for (int j = i + 1; j < cnt; j++)
            if (cands[j].score > cands[max_idx].score) max_idx = j;
        if (max_idx != i) {
            AutoLinkCandidate tmp = cands[i]; cands[i] = cands[max_idx]; cands[max_idx] = tmp;
        }
    }

    /* 清掉本文件作为 src 的旧 implicit 边 */
    {
        char *eu = escape_mysql(conn, user);
        char *es = escape_mysql(conn, self_md5);
        if (eu && es) {
            char delsql[512];
            snprintf(delsql, sizeof(delsql),
                "DELETE FROM wiki_link WHERE user='%s' AND src_md5='%s' AND link_type='implicit'",
                eu, es);
            mysql_query(conn, delsql);
        }
        if (eu) free(eu);
        if (es) free(es);
    }

    int written = 0;
    for (int i = 0; i < k; i++) {
        char *eu = escape_mysql(conn, user);
        char *es = escape_mysql(conn, self_md5);
        char *ed = escape_mysql(conn, cands[i].md5);
        char *en = escape_mysql(conn, cands[i].name[0] ? cands[i].name : cands[i].md5);
        if (!eu || !es || !ed || !en) {
            if (eu) free(eu); if (es) free(es);
            if (ed) free(ed); if (en) free(en);
            continue;
        }

        char ins[1024];
        /* A→B */
        snprintf(ins, sizeof(ins),
            "INSERT INTO wiki_link (user, src_md5, dst_md5, dst_name, link_type, score) "
            "VALUES ('%s','%s','%s','%s','implicit',%.4f) "
            "ON DUPLICATE KEY UPDATE score=VALUES(score), dst_name=VALUES(dst_name)",
            eu, es, ed, en, cands[i].score);
        if (mysql_query(conn, ins) == 0) written++;

        /* B→A 反向 */
        snprintf(ins, sizeof(ins),
            "INSERT INTO wiki_link (user, src_md5, dst_md5, dst_name, link_type, score) "
            "VALUES ('%s','%s','%s','%s','implicit',%.4f) "
            "ON DUPLICATE KEY UPDATE score=VALUES(score)",
            eu, ed, es, es, cands[i].score);
        mysql_query(conn, ins);

        LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
            "auto_link[%.32s] -> %.32s (%.3f)\n",
            self_md5, cands[i].md5, cands[i].score);

        free(eu); free(es); free(ed); free(en);
    }

    if (auto_link_type_fallback && written < auto_link_topk && auto_link_type_limit > 0) {
        const char *self_category = broad_file_category(self_type);
        int fallback_limit = auto_link_topk - written;
        if (fallback_limit > auto_link_type_limit) fallback_limit = auto_link_type_limit;

        char *eu = escape_mysql(conn, user);
        char *es = escape_mysql(conn, self_md5);
        if (eu && es && strcmp(self_category, "other") != 0) {
            char fsql[2048] = {0};
            snprintf(fsql, sizeof(fsql),
                     "SELECT uad.md5, COALESCE(ufl.file_name, uad.md5), COALESCE(fi.type, '') "
                     "FROM user_file_ai_desc uad "
                     "JOIN user_file_list ufl ON ufl.user=uad.user AND ufl.md5=uad.md5 "
                     "LEFT JOIN file_info fi ON fi.md5=uad.md5 "
                     "WHERE uad.user='%s' AND uad.md5 <> '%s' "
                     "AND uad.status=1 AND uad.parse_status='success' "
                     "ORDER BY uad.updated_at DESC LIMIT 50",
                     eu, es);

            if (mysql_query(conn, fsql) == 0) {
                MYSQL_RES *fres = mysql_store_result(conn);
                if (fres) {
                    MYSQL_ROW row;
                    while (written < auto_link_topk && fallback_limit > 0 &&
                           (row = mysql_fetch_row(fres)) != NULL) {
                        if (!row[0] || strcmp(broad_file_category(row[2]), self_category) != 0) {
                            continue;
                        }
                        if (candidate_list_contains(cands, k, row[0])) {
                            continue;
                        }

                        char *ed = escape_mysql(conn, row[0]);
                        char *en = escape_mysql(conn, row[1] ? row[1] : row[0]);
                        if (!ed || !en) {
                            if (ed) free(ed);
                            if (en) free(en);
                            continue;
                        }

                        char ins[1024];
                        snprintf(ins, sizeof(ins),
                            "INSERT INTO wiki_link (user, src_md5, dst_md5, dst_name, link_type, score) "
                            "VALUES ('%s','%s','%s','%s','implicit',0.0000) "
                            "ON DUPLICATE KEY UPDATE score=VALUES(score), dst_name=VALUES(dst_name)",
                            eu, es, ed, en);
                        if (mysql_query(conn, ins) == 0) {
                            written++;
                            fallback_limit--;
                            LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
                                "auto_link[%.32s] -> %.32s by category=%s\n",
                                self_md5, row[0], self_category);
                        }

                        snprintf(ins, sizeof(ins),
                            "INSERT INTO wiki_link (user, src_md5, dst_md5, dst_name, link_type, score) "
                            "VALUES ('%s','%s','%s','%s','implicit',0.0000) "
                            "ON DUPLICATE KEY UPDATE score=VALUES(score), dst_name=VALUES(dst_name)",
                            eu, ed, es, es);
                        mysql_query(conn, ins);

                        free(ed);
                        free(en);
                    }
                    mysql_free_result(fres);
                }
            } else {
                LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
                    "auto_link type fallback query failed: %s\n", mysql_error(conn));
            }
        }
        if (eu) free(eu);
        if (es) free(es);
    }

    free(cands);
    LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
        "auto_link[%.32s]: wrote %d implicit edges (top%d, th=%.2f)\n",
        self_md5, written, k, auto_link_threshold);
    return written;
}

static void cleanup_wiki_artifacts(MYSQL *conn, const char *user, const char *md5)
{
    char *esc_user = escape_mysql(conn, user);
    char *esc_md5 = escape_mysql(conn, md5);
    if (!esc_user || !esc_md5) {
        if (esc_user) free(esc_user);
        if (esc_md5) free(esc_md5);
        return;
    }

    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "DELETE FROM wiki_link WHERE user='%s' AND src_md5='%s'",
             esc_user, esc_md5);
    mysql_query(conn, sql);

    snprintf(sql, sizeof(sql),
             "DELETE FROM wiki_page WHERE user='%s' AND md5='%s'",
             esc_user, esc_md5);
    mysql_query(conn, sql);

    free(esc_user);
    free(esc_md5);
}

/* 处理失败重试 */
static int handle_task_failure(MYSQL *conn, long task_id, int retry_count,
                               const char *error_msg)
{
    const char *msg = error_msg ? error_msg : "unknown";
    char *esc_msg = (char *)malloc(strlen(msg) * 2 + 1);
    if (!esc_msg) return -1;
    mysql_real_escape_string(conn, esc_msg, msg, (unsigned long)strlen(msg));

    if (retry_count < 3) {
        char sql[1024] = {0};
        snprintf(sql, sizeof(sql),
                 "UPDATE ai_parse_task SET status='pending', retry_count=%d, error_msg='%s' WHERE id=%ld",
                 retry_count + 1, esc_msg, task_id);
        free(esc_msg);
        return mysql_query(conn, sql) == 0 ? 0 : -1;
    }
    free(esc_msg);
    return update_task_status(conn, task_id, "failed", error_msg);
}

/* ====== 主处理流程 ====== */

static int process_one_task(MYSQL *conn, long task_id, const char *user,
                            const char *md5, const char *task_type,
                            int retry_count)
{
    char db_url[512] = {0};
    char file_path[256] = {0};
    char text_content[65536] = {0};
    char description[4096] = {0};
    char summary[512] = {0};
    char tags_json[2048] = {0};
    char outline_json[2048] = {0};
    float *vec = NULL;
    int text_len = 0;
    char api_key[256] = {0};

    LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
        "processing task %ld: user=%s, md5=%.32s\n", task_id, user, md5);

    /* 1. 获取文件信息 */
    if (get_file_url(conn, md5, db_url, sizeof(db_url)) != 0) {
        LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC, "get_file_url failed for md5=%.32s\n", md5);
        handle_task_failure(conn, task_id, retry_count, "get file URL failed");
        return -1;
    }

    /* 构造文件下载 URL */
    char download_url[1024] = {0};
    char *path_part = strstr(db_url, "/group");
    if (!path_part) path_part = strstr(db_url, "group");
    if (path_part) {
        snprintf(download_url, sizeof(download_url), "http://%s:%s%s%s",
                 web_server_ip, web_server_port,
                 path_part[0] == '/' ? "" : "/", path_part);
    } else {
        strncpy(download_url, db_url, sizeof(download_url) - 1);
    }

    /* 获取文件类型 */
    char type[32] = {0};
    {
        char *esc_md5 = escape_mysql(conn, md5);
        if (esc_md5) {
            char sql[512] = {0};
            snprintf(sql, sizeof(sql), "SELECT type FROM file_info WHERE md5='%s'", esc_md5);
            free(esc_md5);
            if (mysql_query(conn, sql) == 0) {
                MYSQL_RES *res = mysql_store_result(conn);
                if (res) {
                    MYSQL_ROW row = mysql_fetch_row(res);
                    if (row && row[0]) strncpy(type, row[0], sizeof(type) - 1);
                    mysql_free_result(res);
                }
            }
        }
    }

    /* 2. 下载文件（图片不下载，直接用 URL 走 VL） */
    int is_image = is_image_type(type);
    if (!is_image) {
        snprintf(file_path, sizeof(file_path), "/tmp/knowledge_%ld_%s", task_id, md5);
        if (download_file(download_url, file_path) != 0) {
            LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC, "download failed: %s\n", download_url);
            handle_task_failure(conn, task_id, retry_count, "download failed");
            return -1;
        }
    }

    /* 3. 获取/生成文本描述 */
    char api_key_early[256] = {0};
    if (strlen(dashscope_api_key) > 0) {
        strncpy(api_key_early, dashscope_api_key, sizeof(api_key_early) - 1);
    } else {
        /* 兼容旧数据：从 user_info 取 */
        load_user_api_key(conn, user, api_key_early, sizeof(api_key_early));
    }

    if (is_image) {
        /* 调用 Qwen-VL 多模态 */
        char vl_desc[2048] = {0};
        if (strlen(api_key_early) == 0) {
            LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC, "no api key for VL, task %ld\n", task_id);
            handle_task_failure(conn, task_id, retry_count, "no api key for VL");
            return -1;
        }
        if (dashscope_describe_image(api_key_early, download_url,
                                     vl_desc, sizeof(vl_desc)) != 0) {
            LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC, "VL describe failed: %s\n", download_url);
            handle_task_failure(conn, task_id, retry_count, "VL describe failed");
            return -1;
        }
        snprintf(text_content, sizeof(text_content), "%s", vl_desc);
        text_len = (int)strlen(text_content);
        LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
            "image VL desc[%ld]: %.120s\n", task_id, vl_desc);
    } else if (strcasecmp(type, "pdf") == 0) {
        text_len = extract_pdf_text(file_path, text_content, sizeof(text_content) - 1);
    } else if (is_text_type(type)) {
        text_len = read_text_file(file_path, text_content, sizeof(text_content) - 1);
    }

    if (text_len <= 0) {
        LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC, "text extraction failed for type=%s\n", type);
        if (!is_image) remove(file_path);
        /* 非致命：仍尝试用文件名作为描述 */
        snprintf(text_content, sizeof(text_content), "%s file", type);
        text_len = strlen(text_content);
    }
    if (!is_image) remove(file_path);

    /* 4. 生成描述 */
    {
        char filename[256] = {0};
        char *esc_md5 = escape_mysql(conn, md5);
        if (esc_md5) {
            char sql[512] = {0};
            snprintf(sql, sizeof(sql),
                     "SELECT ufl.file_name FROM user_file_list ufl WHERE ufl.user='%s' AND ufl.md5='%s' LIMIT 1",
                     user, esc_md5);
            free(esc_md5);
            /* 简化：直接用 md5 作为文件名片段 */
        }
        /* 截取文本前 3000 字符作为描述（与 ai_cgi 一致） */
        int desc_len = text_len > 3000 ? 3000 : text_len;
        snprintf(description, sizeof(description), "文件名：%s\n文件内容：%.*s",
                 md5, desc_len, text_content);
    }

    /* 5. 生成摘要 */
    make_summary(text_content, summary, sizeof(summary));

    /* 提取 [[links]] → tags */
    extract_tags_from_wikilinks(text_content, tags_json, sizeof(tags_json));
    build_outline_json(text_content, outline_json, sizeof(outline_json));

    LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
        "summary=%.100s, tags=%s, outline=%s\n", summary, tags_json, outline_json);

    /* 7. 获取 API Key（cfg 优先，已在第 3 步加载） */
    strncpy(api_key, api_key_early, sizeof(api_key) - 1);

    /* 8. 生成 embedding */
    const char *embedding_error = NULL;
    if (strlen(api_key) > 0) {
        vec = (float *)malloc(sizeof(float) * embedding_dimension);
        if (vec) {
            memset(vec, 0, sizeof(float) * embedding_dimension);
            if (dashscope_get_embedding(api_key, embedding_model,
                                        description, vec, embedding_dimension) != 0) {
                LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
                    "embedding failed for task %ld\n", task_id);
                free(vec);
                vec = NULL;
                embedding_error = "embedding API call failed";
            } else {
                vector_l2_normalize(vec, embedding_dimension);
            }
        }
    } else {
        embedding_error = "no API key configured";
        LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
            "no API key for task %ld, embedding skipped\n", task_id);
    }

    /* 根据 embedding 结果决定最终状态 */
    const char *final_parse_status = (vec != NULL) ? "success" : "failed";
    const char *final_error_msg = embedding_error;

    /* 9. 写入各表 */
    if (write_global_cache(conn, md5, description, vec, summary, tags_json,
                           embedding_model) != 0) {
        LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC, "write_global_cache failed\n");
        if (vec) free(vec);
        handle_task_failure(conn, task_id, retry_count, "write global cache failed");
        return -1;
    }

    if (write_user_ai_record(conn, user, md5, description, vec, summary, tags_json,
                             embedding_model, final_parse_status) != 0) {
        LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC, "write_user_ai_record failed\n");
        if (vec) free(vec);
        handle_task_failure(conn, task_id, retry_count, "write user record failed");
        return -1;
    }

    if (vec == NULL) {
        cleanup_wiki_artifacts(conn, user, md5);
        update_task_status(conn, task_id, "failed", final_error_msg ? final_error_msg : "embedding failed");
        LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
            "task %ld failed before wiki build: %s\n", task_id,
            final_error_msg ? final_error_msg : "embedding failed");
        return -1;
    }

    /* Wiki 标题：使用文件名 */
    char wiki_title[256] = {0};
    {
        char *esc_md5 = escape_mysql(conn, md5);
        char *esc_user = escape_mysql(conn, user);
        if (esc_md5 && esc_user) {
            char sql[512] = {0};
            snprintf(sql, sizeof(sql),
                     "SELECT file_name FROM user_file_list WHERE user='%s' AND md5='%s' LIMIT 1",
                     esc_user, esc_md5);
            if (mysql_query(conn, sql) == 0) {
                MYSQL_RES *res = mysql_store_result(conn);
                if (res) {
                    MYSQL_ROW row = mysql_fetch_row(res);
                    if (row && row[0]) strncpy(wiki_title, row[0], sizeof(wiki_title) - 1);
                    mysql_free_result(res);
                }
            }
        }
        if (esc_md5) free(esc_md5);
        if (esc_user) free(esc_user);
    }
    if (strlen(wiki_title) == 0) {
        snprintf(wiki_title, sizeof(wiki_title), "%.32s", md5);
    }

    if (write_wiki_page(conn, user, md5, wiki_title, summary, tags_json, outline_json) != 0) {
        final_parse_status = "failed";
        final_error_msg = "write wiki page failed";
        LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC, "write_wiki_page failed\n");
    }

    /* 10. 写 wiki_link（显式 [[link]]） */
    if (strcmp(final_parse_status, "success") == 0 &&
        write_wiki_links(conn, user, md5, tags_json) != 0) {
        final_parse_status = "failed";
        final_error_msg = "write wiki links failed";
        LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC, "write_wiki_links failed\n");
    }

    /* 10.5 自动隐式双链：上传即建立 Obsidian 风格的反向连接 */
    if (strcmp(final_parse_status, "success") == 0 && vec != NULL) {
        int n = auto_build_implicit_links(conn, user, md5, type, vec);
        if (n < 0) {
            LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
                "auto_build_implicit_links returned error, ignore\n");
        }
    }

    if (vec) {
        free(vec);
        vec = NULL;
    }

    if (strcmp(final_parse_status, "success") != 0) {
        update_user_parse_state(conn, user, md5, "failed", final_error_msg);
        cleanup_wiki_artifacts(conn, user, md5);
        update_task_status(conn, task_id, "failed", final_error_msg);
        LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
            "task %ld marked failed after knowledge write: %s\n", task_id,
            final_error_msg ? final_error_msg : "unknown");
        return -1;
    }

    /* 11. 标记成功 */
    update_user_parse_state(conn, user, md5, "success", NULL);
    update_task_status(conn, task_id, "success", NULL);
    LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
        "task %ld completed successfully\n", task_id);
    return 0;
}

/* ====== 主循环 ====== */

static int fetch_pending_task(MYSQL *conn, long *task_id, char *user,
                              char *md5, char *task_type, int *retry_count)
{
    const char *sql =
        "SELECT id, user, md5, task_type, retry_count FROM ai_parse_task "
        "WHERE status='pending' ORDER BY created_at ASC LIMIT 1";

    if (mysql_query(conn, sql) != 0) return -1;
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) return -1;
    MYSQL_ROW row = mysql_fetch_row(res);
    int ret = -1;
    if (row) {
        *task_id = row[0] ? atol(row[0]) : -1;
        strncpy(user, row[1] ? row[1] : "", 31);
        strncpy(md5, row[2] ? row[2] : "", 255);
        strncpy(task_type, row[3] ? row[3] : "parse_file", 31);
        *retry_count = row[4] ? atoi(row[4]) : 0;
        ret = 0;
    }
    mysql_free_result(res);
    return ret;
}

static int mark_task_running(MYSQL *conn, long task_id)
{
    char sql[512] = {0};
    snprintf(sql, sizeof(sql),
             "UPDATE ai_parse_task SET status='running' WHERE id=%ld AND status='pending'",
             task_id);
    return mysql_query(conn, sql) == 0 ? 0 : -1;
}

int main()
{
    MYSQL *conn = NULL;

    read_cfg();
    curl_global_init(CURL_GLOBAL_ALL);

    /* 连接 MySQL，重试最多 10 次 */
    for (int retry = 0; retry < 10; retry++) {
        conn = msql_conn(mysql_user, mysql_pwd, mysql_db);
        if (conn) break;
        LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
            "mysql connect failed (attempt %d/10), retrying...\n", retry + 1);
        sleep(3);
    }
    if (!conn) {
        LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC, "mysql connect failed after 10 retries, exiting\n");
        return 1;
    }
    mysql_query(conn, "set names utf8mb4");

    /* 注册信号处理 */
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    /* 崩溃恢复：重置残留的 running 任务 */
    mysql_query(conn,
        "UPDATE ai_parse_task SET status='pending', error_msg='worker restart recovery' "
        "WHERE status='running'");
    LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
        "crash recovery: reset %lld running tasks to pending\n",
        (long long)mysql_affected_rows(conn));

    LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC, "worker started\n");

    while (g_running) {
        long task_id = -1;
        char user[32] = {0};
        char md5[256] = {0};
        char task_type[32] = {0};
        int retry_count = 0;

        /* 取一个 pending 任务 */
        if (fetch_pending_task(conn, &task_id, user, md5, task_type, &retry_count) != 0 || task_id < 0) {
            sleep(5);
            /* 检查 MySQL 连接是否存活 */
            if (mysql_ping(conn) != 0) {
                mysql_close(conn);
                conn = msql_conn(mysql_user, mysql_pwd, mysql_db);
                if (conn) mysql_query(conn, "set names utf8mb4");
            }
            continue;
        }

        /* 标记为 running（乐观锁：status='pending' 时才更新） */
        if (mark_task_running(conn, task_id) != 0) {
            sleep(1);
            continue;
        }

        /* 处理任务 */
        if (process_one_task(conn, task_id, user, md5, task_type, retry_count) != 0) {
            LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC,
                "task %ld failed\n", task_id);
        }

        sleep(1);
    }

    /* 优雅退出：如果当前正在处理任务，等待完成 */
    LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC, "worker stopping\n");
    mysql_close(conn);
    curl_global_cleanup();
    LOG(UTIL_LOG_MODULE, UTIL_LOG_PROC, "worker stopped\n");
    return 0;
}

static char *escape_mysql_text(MYSQL *conn, const char *input)
{
    if (!input) return NULL;
    size_t len = strlen(input);
    char *escaped = (char *)malloc(len * 2 + 1);
    if (escaped) {
        mysql_real_escape_string(conn, escaped, input, len);
    }
    return escaped;
}

static int get_user_ai_config(MYSQL *conn, const char *user, char *api_key, char *model, size_t key_len, size_t model_len) {
    char sql[512] = {0};
    snprintf(sql, sizeof(sql), "SELECT api_key, embedding_model FROM user_info WHERE user_name='%s'", user);
    if (mysql_query(conn, sql) != 0) return -1;
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) return -1;
    MYSQL_ROW row = mysql_fetch_row(res);
    int ret = -1;
    if (row) {
        if (row[0] && strlen(row[0]) > 0) {
            strncpy(api_key, row[0], key_len - 1);
            api_key[key_len - 1] = '\0';
        }
        if (row[1] && strlen(row[1]) > 0) {
            strncpy(model, row[1], model_len - 1);
            model[model_len - 1] = '\0';
        }
        ret = 0;
    }
    mysql_free_result(res);
    return ret;
}
