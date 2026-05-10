/**
 * @file ai_cgi.cpp
 * @brief  AI 智能检索 FastCGI 程序
 *         功能：describe（生成文件描述+向量）、search（语义搜索）、rebuild（重建索引）
 */

#include "fcgi_config.h"
#include "fcgi_stdio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>

extern "C" {
#include "make_log.h"
#include "util_cgi.h"
#include "deal_mysql.h"
#include "redis_keys.h"
#include "redis_op.h"
#include "cfg.h"
#include "cJSON.h"
#include "md5.h"
}

#include "dashscope_api.h"
#include "faiss_wrapper.h"

#define AI_LOG_MODULE "cgi"
#define AI_LOG_PROC   "ai"

// 全局配置
static char mysql_user[128] = {0};
static char mysql_pwd[128] = {0};
static char mysql_db[128] = {0};
static char redis_ip[30] = {0};
static char redis_port[10] = {0};
static char embedding_model[64] = {0};
static char vl_model[64] = {0};
static int  embedding_dimension = 1024;
static char faiss_index_path[512] = {0};
static char faiss_user_index_dir[512] = {0};
static char web_server_ip[30] = {0};
static char web_server_port[10] = {0};
static char public_server_ip[30] = {0};
static char public_server_port[10] = {0};
static char faiss_lock_dir[512] = "/tmp/faiss_locks";

static void read_cfg()
{
    get_cfg_value(CFG_PATH, "mysql", "user", mysql_user);
    get_cfg_value(CFG_PATH, "mysql", "password", mysql_pwd);
    get_cfg_value(CFG_PATH, "mysql", "database", mysql_db);

    get_cfg_value(CFG_PATH, "redis", "ip", redis_ip);
    get_cfg_value(CFG_PATH, "redis", "port", redis_port);

    get_cfg_value(CFG_PATH, "dashscope", "embedding_model", embedding_model);
    get_cfg_value(CFG_PATH, "dashscope", "vl_model", vl_model);

    char dim_str[16] = {0};
    get_cfg_value(CFG_PATH, "dashscope", "embedding_dimension", dim_str);
    if (strlen(dim_str) > 0) embedding_dimension = atoi(dim_str);

    get_cfg_value(CFG_PATH, "faiss", "index_path", faiss_index_path);
    get_cfg_value(CFG_PATH, "faiss", "user_index_dir", faiss_user_index_dir);

    if (strlen(faiss_user_index_dir) == 0 && strlen(faiss_index_path) > 0) {
        strncpy(faiss_user_index_dir, faiss_index_path, sizeof(faiss_user_index_dir) - 1);
        char *last_slash = strrchr(faiss_user_index_dir, '/');
        if (last_slash) {
            *last_slash = '\0';
            strncat(faiss_user_index_dir, "/users",
                    sizeof(faiss_user_index_dir) - strlen(faiss_user_index_dir) - 1);
        }
    }

    get_cfg_value(CFG_PATH, "web_server", "ip", web_server_ip);
    get_cfg_value(CFG_PATH, "web_server", "port", web_server_port);

    get_cfg_value(CFG_PATH, "public_server", "ip", public_server_ip);
    get_cfg_value(CFG_PATH, "public_server", "port", public_server_port);

    LOG(AI_LOG_MODULE, AI_LOG_PROC,
        "config loaded: mysql=[%s], redis=[%s:%s], dim=%d, global_index=%s, user_index_dir=%s, public=%s:%s\n",
        mysql_db, redis_ip, redis_port, embedding_dimension, faiss_index_path,
        faiss_user_index_dir, public_server_ip, public_server_port);
}

// 判断是否是图片类型
static int is_image_type(const char *type)
{
    if (!type) return 0;
    return (strcasecmp(type, "png") == 0 ||
            strcasecmp(type, "jpg") == 0 ||
            strcasecmp(type, "jpeg") == 0 ||
            strcasecmp(type, "gif") == 0 ||
            strcasecmp(type, "bmp") == 0 ||
            strcasecmp(type, "webp") == 0 ||
            strcasecmp(type, "svg") == 0);
}

// curl 写回调：用 open/write 绕过 fcgi_stdio 对 FILE 的重定义
static size_t download_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    int fd = *(int *)userdata;
    size_t total = size * nmemb;
    ssize_t written = write(fd, ptr, total);
    return (written > 0) ? (size_t)written : 0;
}

// 下载文件到本地临时路径
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

    if (res != CURLE_OK || http_code != 200) {
        remove(save_path);
        return -1;
    }
    return 0;
}

// 从 docx 文件提取纯文本（docx 是 ZIP，内含 word/document.xml）
// 用 pipe+fork+exec 代替 popen 以避免 fcgi_stdio 重定义
static int extract_docx_text(const char *docx_path, char *out_text, int max_len)
{
    char cmd[1024] = {0};
    snprintf(cmd, sizeof(cmd), "unzip -p '%s' word/document.xml 2>/dev/null", docx_path);

    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;

    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }

    if (pid == 0) {
        // 子进程
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(1);
    }

    // 父进程
    close(pipefd[1]);

    // 读取 XML 内容
    char *xml_buf = (char *)malloc(256 * 1024); // 256KB
    if (!xml_buf) { close(pipefd[0]); return -1; }

    int total = 0;
    int n;
    while ((n = read(pipefd[0], xml_buf + total, 256 * 1024 - total - 1)) > 0) {
        total += n;
        if (total >= 256 * 1024 - 1) break;
    }
    xml_buf[total] = '\0';
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    if (total == 0) { free(xml_buf); return -1; }

    // 从 XML 中提取 <w:t> 或 <w:t ...> 标签内的文本
    // 注意：必须精确匹配 <w:t> 或 <w:t + 空格，避免匹配 <w:tab>、<w:tabs> 等
    int out_pos = 0;
    char *p = xml_buf;
    while (*p && out_pos < max_len - 2) {
        char *tag_start = strstr(p, "<w:t");
        if (!tag_start) break;

        // 检查 <w:t 后面是 > 或空格（表示属性），排除 <w:tab 等
        char next_ch = tag_start[4]; // "<w:t" 之后的字符
        if (next_ch != '>' && next_ch != ' ') {
            p = tag_start + 4;
            continue;
        }

        // 找到 > 结束
        char *content_start = strchr(tag_start, '>');
        if (!content_start) break;
        content_start++;

        // 找到 </w:t>
        char *content_end = strstr(content_start, "</w:t>");
        if (!content_end) break;

        int len = content_end - content_start;
        if (len > 0 && out_pos + len < max_len - 1) {
            memcpy(out_text + out_pos, content_start, len);
            out_pos += len;
        }

        p = content_end + 6;
    }
    out_text[out_pos] = '\0';

    free(xml_buf);
    return (out_pos > 0) ? 0 : -1;
}

// 读取纯文本文件（用 open/read 绕过 fcgi_stdio）
static int read_text_file(const char *path, char *out_text, int max_len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    int n = read(fd, out_text, max_len - 1);
    close(fd);
    if (n <= 0) return -1;
    out_text[n] = '\0';
    return 0;
}

// 判断是否为文本类文件
static int is_text_type(const char *type)
{
    if (!type) return 0;
    return (strcasecmp(type, "txt") == 0 ||
            strcasecmp(type, "md") == 0 ||
            strcasecmp(type, "csv") == 0 ||
            strcasecmp(type, "json") == 0 ||
            strcasecmp(type, "xml") == 0 ||
            strcasecmp(type, "html") == 0 ||
            strcasecmp(type, "htm") == 0 ||
            strcasecmp(type, "log") == 0 ||
            strcasecmp(type, "c") == 0 ||
            strcasecmp(type, "cpp") == 0 ||
            strcasecmp(type, "h") == 0 ||
            strcasecmp(type, "py") == 0 ||
            strcasecmp(type, "js") == 0 ||
            strcasecmp(type, "css") == 0 ||
            strcasecmp(type, "java") == 0);
}

/**
 * 解析 API Key：仅接受请求体里的 api_key
 */
static const char *resolve_api_key(cJSON *apikey_item)
{
    if (apikey_item && apikey_item->valuestring && strlen(apikey_item->valuestring) > 0) {
        return apikey_item->valuestring;
    }
    return NULL;
}

typedef struct {
    long id;
    int status;
    char *description;
    char *model;
    unsigned char *embedding;
    unsigned long embedding_len;
} AiCacheData;

static void free_ai_cache_data(AiCacheData *data)
{
    if (!data) return;
    if (data->description) free(data->description);
    if (data->model) free(data->model);
    if (data->embedding) free(data->embedding);
    memset(data, 0, sizeof(AiCacheData));
}

static int ensure_dir_recursive(const char *path)
{
    char tmp[512] = {0};
    int len = 0;

    if (!path || strlen(path) == 0) return -1;

    strncpy(tmp, path, sizeof(tmp) - 1);
    len = strlen(tmp);
    if (len == 0) return -1;

    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

static void md5_hex_string(const char *input, char out[33])
{
    MD5_CTX ctx;
    unsigned char digest[16] = {0};

    memset(out, 0, 33);
    MD5Init(&ctx);
    MD5Update(&ctx, (unsigned char *)input, (unsigned int)strlen(input));
    MD5Final(&ctx, digest);

    for (int i = 0; i < 16; i++) {
        sprintf(out + i * 2, "%02x", digest[i]);
    }
}

static void get_user_index_path(const char *user, char *out_path, int out_len)
{
    char hash[33] = {0};
    md5_hex_string(user, hash);
    snprintf(out_path, out_len, "%s/%s.index.bin", faiss_user_index_dir, hash);
}

static void get_user_lock_path(const char *user, char *out_path, int out_len)
{
    char hash[33] = {0};
    md5_hex_string(user, hash);
    snprintf(out_path, out_len, "%s/%s.lock", faiss_lock_dir, hash);
}

static void get_user_dirty_path(const char *user, char *out_path, int out_len)
{
    char hash[33] = {0};
    md5_hex_string(user, hash);
    snprintf(out_path, out_len, "%s/%s.dirty", faiss_lock_dir, hash);
}

static int is_user_index_dirty(const char *user)
{
    char dirty_path[512] = {0};

    get_user_dirty_path(user, dirty_path, sizeof(dirty_path));
    return access(dirty_path, F_OK) == 0 ? 1 : 0;
}

static void clear_user_index_dirty(const char *user)
{
    char dirty_path[512] = {0};

    get_user_dirty_path(user, dirty_path, sizeof(dirty_path));
    remove(dirty_path);
}

static int acquire_user_lock(const char *user, int lock_op)
{
    char lock_path[512] = {0};
    int fd = -1;

    if (ensure_dir_recursive(faiss_lock_dir) != 0) {
        return -1;
    }

    get_user_lock_path(user, lock_path, sizeof(lock_path));
    fd = open(lock_path, O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        return -1;
    }

    if (flock(fd, lock_op) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void release_user_lock(int fd)
{
    if (fd >= 0) {
        flock(fd, LOCK_UN);
        close(fd);
    }
}

static char *escape_mysql_text(MYSQL *conn, const char *input);

static int user_owns_file(MYSQL *conn, const char *user, const char *md5, char *file_name, int file_name_len)
{
    char sql[1024] = {0};
    char *escaped_user = NULL;
    char *escaped_md5 = NULL;
    int ret = -1;

    if (file_name && file_name_len > 0) {
        file_name[0] = '\0';
    }

    escaped_user = escape_mysql_text(conn, user);
    escaped_md5 = escape_mysql_text(conn, md5);
    if (!escaped_user || !escaped_md5) {
        goto END;
    }

    snprintf(sql, sizeof(sql),
             "SELECT file_name FROM user_file_list WHERE user='%s' AND md5='%s' LIMIT 1",
             escaped_user, escaped_md5);

    if (file_name && file_name_len > 0) {
        ret = process_result_one(conn, sql, file_name);
    } else {
        ret = process_result_one(conn, sql, NULL);
    }

END:
    if (escaped_user) free(escaped_user);
    if (escaped_md5) free(escaped_md5);
    return ret;
}

static int get_user_ai_status(MYSQL *conn, const char *user, const char *md5, int *status_out)
{
    char sql[1024] = {0};
    char tmp[64] = {0};
    char *escaped_user = NULL;
    char *escaped_md5 = NULL;
    int ret = 0;

    escaped_user = escape_mysql_text(conn, user);
    escaped_md5 = escape_mysql_text(conn, md5);
    if (!escaped_user || !escaped_md5) {
        ret = -1;
        goto END;
    }

    snprintf(sql, sizeof(sql),
             "SELECT status FROM user_file_ai_desc WHERE user='%s' AND md5='%s' LIMIT 1",
             escaped_user, escaped_md5);
    ret = process_result_one(conn, sql, tmp);
    if (ret == 0 && status_out) {
        *status_out = atoi(tmp);
    }

END:
    if (escaped_user) free(escaped_user);
    if (escaped_md5) free(escaped_md5);
    return ret;
}

static int load_global_ai_cache(MYSQL *conn, const char *md5, AiCacheData *data)
{
    char sql[1024] = {0};
    char *escaped_md5 = NULL;
    MYSQL_RES *res = NULL;
    MYSQL_ROW row = NULL;
    unsigned long *lengths = NULL;
    int ret = -1;

    memset(data, 0, sizeof(AiCacheData));
    data->id = -1;

    escaped_md5 = escape_mysql_text(conn, md5);
    if (!escaped_md5) {
        goto END;
    }

    snprintf(sql, sizeof(sql),
             "SELECT id, description, embedding, model, status FROM file_ai_desc WHERE md5='%s' LIMIT 1",
             escaped_md5);

    if (mysql_query(conn, sql) != 0) {
        goto END;
    }

    res = mysql_store_result(conn);
    if (!res) goto END;

    row = mysql_fetch_row(res);
    if (!row) {
        ret = 1;
        goto END;
    }

    lengths = mysql_fetch_lengths(res);
    data->id = row[0] ? atol(row[0]) : -1;
    data->status = row[4] ? atoi(row[4]) : 0;

    if (row[1]) {
        data->description = (char *)malloc(lengths[1] + 1);
        if (!data->description) {
            mysql_free_result(res);
            free_ai_cache_data(data);
            return -1;
        }
        memcpy(data->description, row[1], lengths[1]);
        data->description[lengths[1]] = '\0';
    }

    if (row[2] && lengths[2] > 0) {
        data->embedding = (unsigned char *)malloc(lengths[2]);
        if (!data->embedding) {
            mysql_free_result(res);
            free_ai_cache_data(data);
            return -1;
        }
        memcpy(data->embedding, row[2], lengths[2]);
        data->embedding_len = lengths[2];
    }

    if (row[3]) {
        data->model = (char *)malloc(lengths[3] + 1);
        if (!data->model) {
            mysql_free_result(res);
            free_ai_cache_data(data);
            return -1;
        }
        memcpy(data->model, row[3], lengths[3]);
        data->model[lengths[3]] = '\0';
    }

    ret = 0;

END:
    if (escaped_md5) free(escaped_md5);
    if (res) mysql_free_result(res);
    if (ret != 0 && ret != 1) {
        free_ai_cache_data(data);
    }
    return ret;
}

static char *escape_mysql_bytes(MYSQL *conn, const unsigned char *data, unsigned long len)
{
    char *escaped = (char *)malloc(len * 2 + 1);
    if (!escaped) return NULL;
    mysql_real_escape_string(conn, escaped, (const char *)data, len);
    return escaped;
}

static char *escape_mysql_text(MYSQL *conn, const char *input)
{
    if (!input) {
        input = "";
    }
    return escape_mysql_bytes(conn, (const unsigned char *)input, strlen(input));
}

static int replace_global_ai_cache(MYSQL *conn, const char *md5, const char *description,
                                   const float *vector, const char *model, int status)
{
    char *escaped_md5 = NULL;
    char *escaped_desc = NULL;
    char *escaped_blob = NULL;
    char *escaped_model = NULL;
    char *sql = NULL;
    int ret = -1;
    int blob_len = 0;
    int sql_len = 0;

    escaped_md5 = escape_mysql_text(conn, md5);
    escaped_desc = escape_mysql_bytes(conn, (const unsigned char *)description, strlen(description));
    escaped_model = escape_mysql_text(conn, model ? model : "");
    if (!escaped_md5 || !escaped_desc || !escaped_model) goto END;

    if (vector && status == 1) {
        blob_len = embedding_dimension * (int)sizeof(float);
        escaped_blob = escape_mysql_bytes(conn, (const unsigned char *)vector, blob_len);
        if (!escaped_blob) goto END;
    }

    sql_len = (escaped_desc ? (int)strlen(escaped_desc) : 0)
            + (escaped_blob ? (int)strlen(escaped_blob) : 0)
            + 4096;
    sql = (char *)malloc(sql_len);
    if (!sql) goto END;

    if (escaped_blob) {
        snprintf(sql, sql_len,
                 "REPLACE INTO file_ai_desc (md5, description, embedding, faiss_id, model, status) "
                 "VALUES ('%s', '%s', '%s', -1, '%s', %d)",
                 escaped_md5, escaped_desc, escaped_blob, escaped_model, status);
    } else {
        snprintf(sql, sql_len,
                 "REPLACE INTO file_ai_desc (md5, description, faiss_id, model, status) "
                 "VALUES ('%s', '%s', -1, '%s', %d)",
                 escaped_md5, escaped_desc, escaped_model, status);
    }

    ret = (mysql_query(conn, sql) == 0) ? 0 : -1;

END:
    if (escaped_md5) free(escaped_md5);
    if (escaped_desc) free(escaped_desc);
    if (escaped_blob) free(escaped_blob);
    if (escaped_model) free(escaped_model);
    if (sql) free(sql);
    return ret;
}

static int replace_user_ai_record(MYSQL *conn, const char *user, const char *md5, long cache_id,
                                  const char *description, const float *vector,
                                  const char *model, int status)
{
    char *escaped_user = NULL;
    char *escaped_md5 = NULL;
    char *escaped_desc = NULL;
    char *escaped_blob = NULL;
    char *escaped_model = NULL;
    char *sql = NULL;
    int ret = -1;
    int blob_len = 0;
    int sql_len = 0;
    char cache_id_sql[64] = {0};

    if (cache_id >= 0) {
        snprintf(cache_id_sql, sizeof(cache_id_sql), "%ld", cache_id);
    } else {
        strncpy(cache_id_sql, "NULL", sizeof(cache_id_sql) - 1);
    }

    escaped_user = escape_mysql_text(conn, user);
    escaped_md5 = escape_mysql_text(conn, md5);
    escaped_desc = escape_mysql_bytes(conn, (const unsigned char *)description, strlen(description));
    escaped_model = escape_mysql_text(conn, model ? model : "");
    if (!escaped_user || !escaped_md5 || !escaped_desc || !escaped_model) goto END;

    if (vector && status == 1) {
        blob_len = embedding_dimension * (int)sizeof(float);
        escaped_blob = escape_mysql_bytes(conn, (const unsigned char *)vector, blob_len);
        if (!escaped_blob) goto END;
    }

    sql_len = (escaped_desc ? (int)strlen(escaped_desc) : 0)
            + (escaped_blob ? (int)strlen(escaped_blob) : 0)
            + 4096;
    sql = (char *)malloc(sql_len);
    if (!sql) goto END;

    if (escaped_blob) {
        snprintf(sql, sql_len,
                 "REPLACE INTO user_file_ai_desc "
                 "(user, md5, cache_id, description, embedding, faiss_id, model, status) "
                 "VALUES ('%s', '%s', %s, '%s', '%s', -1, '%s', %d)",
                 escaped_user, escaped_md5, cache_id_sql, escaped_desc, escaped_blob, escaped_model, status);
    } else {
        snprintf(sql, sql_len,
                 "REPLACE INTO user_file_ai_desc "
                 "(user, md5, cache_id, description, faiss_id, model, status) "
                 "VALUES ('%s', '%s', %s, '%s', -1, '%s', %d)",
                 escaped_user, escaped_md5, cache_id_sql, escaped_desc, escaped_model, status);
    }

    ret = (mysql_query(conn, sql) == 0) ? 0 : -1;

END:
    if (escaped_user) free(escaped_user);
    if (escaped_md5) free(escaped_md5);
    if (escaped_desc) free(escaped_desc);
    if (escaped_blob) free(escaped_blob);
    if (escaped_model) free(escaped_model);
    if (sql) free(sql);
    return ret;
}

static int copy_cache_to_user_ai(MYSQL *conn, const char *user, const char *md5, const AiCacheData *cache)
{
    float *vec = NULL;
    int ret = -1;

    if (!cache || !cache->description || cache->status != 1 ||
        !cache->embedding || cache->embedding_len != (unsigned long)(embedding_dimension * sizeof(float))) {
        return -1;
    }

    vec = (float *)malloc(cache->embedding_len);
    if (!vec) return -1;
    memcpy(vec, cache->embedding, cache->embedding_len);

    ret = replace_user_ai_record(conn, user, md5, cache->id, cache->description,
                                 vec, cache->model ? cache->model : embedding_model, 1);
    free(vec);
    return ret;
}

static int sync_missing_user_ai_from_cache(MYSQL *conn, const char *user)
{
    char sql[2048] = {0};
    char *escaped_user = NULL;
    int ret = -1;

    escaped_user = escape_mysql_text(conn, user);
    if (!escaped_user) {
        return -1;
    }

    snprintf(sql, sizeof(sql),
             "INSERT INTO user_file_ai_desc "
             "(user, md5, cache_id, description, embedding, faiss_id, model, status) "
             "SELECT ufl.user, fad.md5, fad.id, fad.description, fad.embedding, -1, fad.model, fad.status "
             "FROM user_file_list ufl "
             "JOIN file_ai_desc fad ON fad.md5 = ufl.md5 AND fad.status = 1 "
             "LEFT JOIN user_file_ai_desc uad ON uad.user = ufl.user AND uad.md5 = ufl.md5 "
             "WHERE ufl.user = '%s' AND uad.id IS NULL",
             escaped_user);

    if (mysql_query(conn, sql) != 0) {
        LOG(AI_LOG_MODULE, AI_LOG_PROC,
            "sync_missing_user_ai_from_cache failed for user=%s, err=%s\n",
            user, mysql_error(conn));
        goto END;
    }

    ret = (int)mysql_affected_rows(conn);

END:
    if (escaped_user) free(escaped_user);
    return ret;
}

static int append_user_faiss_entry(MYSQL *conn, const char *user, const char *md5)
{
    char index_path[512] = {0};
    char sql[1024] = {0};
    char update_sql[512] = {0};
    char *escaped_user = NULL;
    char *escaped_md5 = NULL;
    MYSQL_RES *res = NULL;
    MYSQL_ROW row = NULL;
    unsigned long *lengths = NULL;
    float *vec = NULL;
    int faiss_id = -1;
    int lock_fd = -1;
    int ret = -1;

    if (ensure_dir_recursive(faiss_user_index_dir) != 0) {
        return -1;
    }

    escaped_user = escape_mysql_text(conn, user);
    escaped_md5 = escape_mysql_text(conn, md5);
    if (!escaped_user || !escaped_md5) {
        goto END;
    }

    snprintf(sql, sizeof(sql),
             "SELECT id, embedding, faiss_id FROM user_file_ai_desc "
             "WHERE user='%s' AND md5='%s' AND status=1 LIMIT 1",
             escaped_user, escaped_md5);
    if (mysql_query(conn, sql) != 0) {
        goto END;
    }

    res = mysql_store_result(conn);
    if (!res) goto END;

    row = mysql_fetch_row(res);
    if (!row) {
        ret = -1;
        goto END;
    }

    lengths = mysql_fetch_lengths(res);
    if (row[2] && atol(row[2]) >= 0) {
        ret = (int)atol(row[2]);
        goto END;
    }
    if (!row[1] || lengths[1] != (unsigned long)(embedding_dimension * sizeof(float))) {
        goto END;
    }

    vec = (float *)malloc(lengths[1]);
    if (!vec) goto END;
    memcpy(vec, row[1], lengths[1]);

    get_user_index_path(user, index_path, sizeof(index_path));
    lock_fd = acquire_user_lock(user, LOCK_EX);
    if (lock_fd < 0) {
        goto END;
    }

    if (faiss_init(index_path, embedding_dimension) != 0) {
        goto END;
    }

    faiss_id = faiss_add(vec, embedding_dimension);
    if (faiss_id < 0) {
        goto END;
    }

    snprintf(update_sql, sizeof(update_sql),
             "UPDATE user_file_ai_desc SET faiss_id=%d WHERE id=%s",
             faiss_id, row[0]);
    if (mysql_query(conn, update_sql) != 0) {
        goto END;
    }

    ret = faiss_id;

END:
    if (res) mysql_free_result(res);
    if (vec) free(vec);
    if (escaped_user) free(escaped_user);
    if (escaped_md5) free(escaped_md5);
    release_user_lock(lock_fd);
    return ret;
}

static int append_pending_user_faiss_entries(MYSQL *conn, const char *user)
{
    char index_path[512] = {0};
    char sql[1024] = {0};
    char *escaped_user = NULL;
    MYSQL_RES *res = NULL;
    MYSQL_ROW row = NULL;
    unsigned long *lengths = NULL;
    int lock_fd = -1;
    int count = 0;
    int ret = -1;

    if (ensure_dir_recursive(faiss_user_index_dir) != 0) {
        return -1;
    }

    escaped_user = escape_mysql_text(conn, user);
    if (!escaped_user) {
        goto END;
    }

    get_user_index_path(user, index_path, sizeof(index_path));
    lock_fd = acquire_user_lock(user, LOCK_EX);
    if (lock_fd < 0) {
        goto END;
    }

    if (faiss_init(index_path, embedding_dimension) != 0) {
        goto END;
    }

    snprintf(sql, sizeof(sql),
             "SELECT id, embedding FROM user_file_ai_desc "
             "WHERE user='%s' AND status=1 AND embedding IS NOT NULL AND faiss_id < 0 ORDER BY id",
             escaped_user);
    if (mysql_query(conn, sql) != 0) {
        goto END;
    }

    res = mysql_store_result(conn);
    if (!res) goto END;

    while ((row = mysql_fetch_row(res)) != NULL) {
        int faiss_id = -1;
        float *vec = NULL;

        lengths = mysql_fetch_lengths(res);
        if (!row[1] || lengths[1] != (unsigned long)(embedding_dimension * sizeof(float))) {
            continue;
        }

        vec = (float *)malloc(lengths[1]);
        if (!vec) {
            continue;
        }
        memcpy(vec, row[1], lengths[1]);

        faiss_id = faiss_add(vec, embedding_dimension);
        free(vec);

        if (faiss_id >= 0) {
            char update_sql[512] = {0};
            snprintf(update_sql, sizeof(update_sql),
                     "UPDATE user_file_ai_desc SET faiss_id=%d WHERE id=%s",
                     faiss_id, row[0]);
            if (mysql_query(conn, update_sql) == 0) {
                count++;
            }
        }
    }

    ret = count;

END:
    if (res) mysql_free_result(res);
    if (escaped_user) free(escaped_user);
    release_user_lock(lock_fd);
    return ret;
}

static int rebuild_user_faiss_index(MYSQL *conn, const char *user)
{
    char index_path[512] = {0};
    char sql[1024] = {0};
    char *escaped_user = NULL;
    MYSQL_RES *res = NULL;
    MYSQL_ROW row = NULL;
    unsigned long *lengths = NULL;
    int count = 0;
    int lock_fd = -1;
    int ret = -1;

    if (ensure_dir_recursive(faiss_user_index_dir) != 0) {
        return -1;
    }

    lock_fd = acquire_user_lock(user, LOCK_EX);
    if (lock_fd < 0) {
        LOG(AI_LOG_MODULE, AI_LOG_PROC, "acquire user lock failed for %s\n", user);
        return -1;
    }

    get_user_index_path(user, index_path, sizeof(index_path));
    escaped_user = escape_mysql_text(conn, user);
    if (!escaped_user) {
        goto END;
    }

    faiss_reset();
    remove(index_path);
    if (faiss_init(index_path, embedding_dimension) != 0) {
        goto END;
    }
    faiss_set_auto_save(0);

    snprintf(sql, sizeof(sql), "UPDATE user_file_ai_desc SET faiss_id=-1 WHERE user='%s'", escaped_user);
    if (mysql_query(conn, sql) != 0) {
        goto END;
    }

    snprintf(sql, sizeof(sql),
             "SELECT id, embedding FROM user_file_ai_desc "
             "WHERE user='%s' AND status=1 AND embedding IS NOT NULL ORDER BY id",
             escaped_user);
    if (mysql_query(conn, sql) != 0) {
        goto END;
    }

    res = mysql_store_result(conn);
    if (!res) goto END;

    while ((row = mysql_fetch_row(res)) != NULL) {
        int faiss_id = -1;
        float *vec = NULL;

        lengths = mysql_fetch_lengths(res);
        if (!row[1] || lengths[1] != (unsigned long)(embedding_dimension * sizeof(float))) {
            continue;
        }

        vec = (float *)malloc(lengths[1]);
        if (!vec) continue;
        memcpy(vec, row[1], lengths[1]);

        faiss_id = faiss_add(vec, embedding_dimension);
        free(vec);

        if (faiss_id >= 0) {
            char update_sql[512] = {0};
            snprintf(update_sql, sizeof(update_sql),
                     "UPDATE user_file_ai_desc SET faiss_id=%d WHERE id=%s",
                     faiss_id, row[0]);
            mysql_query(conn, update_sql);
            count++;
        }
    }

    faiss_set_auto_save(1);
    if (faiss_save(index_path) != 0) {
        goto END;
    }

    LOG(AI_LOG_MODULE, AI_LOG_PROC, "rebuilt user index for %s, count=%d\n", user, count);
    ret = count;

END:
    faiss_set_auto_save(1);
    if (res) mysql_free_result(res);
    if (escaped_user) free(escaped_user);
    release_user_lock(lock_fd);
    return ret;
}

static int generate_description_for_file(MYSQL *conn, const char *md5, const char *filename,
                                         const char *type, const char *api_key,
                                         char *description, int description_len)
{
    char sql_cmd[1024] = {0};
    char *escaped_md5 = NULL;

    memset(description, 0, description_len);
    escaped_md5 = escape_mysql_text(conn, md5);
    if (!escaped_md5) {
        return -1;
    }

    if (is_image_type(type)) {
        char file_url[1024] = {0};
        char db_url[512] = {0};

        snprintf(sql_cmd, sizeof(sql_cmd), "select url from file_info where md5='%s'", escaped_md5);
        if (process_result_one(conn, sql_cmd, db_url) == 0 && strlen(db_url) > 0) {
            char *path_part = strstr(db_url, "/group");
            if (!path_part) path_part = strstr(db_url, "group");

            if (strlen(public_server_ip) > 0 && path_part) {
                snprintf(file_url, sizeof(file_url), "http://%s:%s%s%s",
                         public_server_ip, public_server_port,
                         path_part[0] == '/' ? "" : "/", path_part);
            } else if (path_part) {
                snprintf(file_url, sizeof(file_url), "http://%s:%s%s%s",
                         web_server_ip, web_server_port,
                         path_part[0] == '/' ? "" : "/", path_part);
            } else {
                strncpy(file_url, db_url, sizeof(file_url) - 1);
            }
        }

        if (strlen(file_url) > 0) {
            if (dashscope_describe_image(api_key, file_url, description, description_len) != 0) {
                snprintf(description, description_len, "图片文件：%s", filename);
            }
        } else {
            snprintf(description, description_len, "图片文件：%s", filename);
        }

        free(escaped_md5);
        return 0;
    }

    {
        int content_extracted = 0;
        char file_url[1024] = {0};
        char db_url[512] = {0};

        snprintf(sql_cmd, sizeof(sql_cmd), "select url from file_info where md5='%s'", escaped_md5);
        if (process_result_one(conn, sql_cmd, db_url) == 0 && strlen(db_url) > 0) {
            char *path_part = strstr(db_url, "/group");
            if (!path_part) path_part = strstr(db_url, "group");
            if (path_part) {
                snprintf(file_url, sizeof(file_url), "http://%s:%s%s%s",
                         web_server_ip, web_server_port,
                         path_part[0] == '/' ? "" : "/", path_part);
            } else {
                strncpy(file_url, db_url, sizeof(file_url) - 1);
            }
        }

        if (strlen(file_url) > 0 && (strcasecmp(type, "docx") == 0 || is_text_type(type))) {
            char tmp_path[512] = {0};
            snprintf(tmp_path, sizeof(tmp_path), "/tmp/ai_desc_%s.%s", md5, type);

            if (download_file(file_url, tmp_path) == 0) {
                char *content_buf = (char *)malloc(8192);
                if (content_buf) {
                    memset(content_buf, 0, 8192);
                    if (strcasecmp(type, "docx") == 0) {
                        if (extract_docx_text(tmp_path, content_buf, 8000) == 0 && strlen(content_buf) > 0) {
                            content_extracted = 1;
                        }
                    } else if (is_text_type(type)) {
                        if (read_text_file(tmp_path, content_buf, 8000) == 0 && strlen(content_buf) > 0) {
                            content_extracted = 1;
                        }
                    }

                    if (content_extracted) {
                        int desc_len = strlen(content_buf);
                        if (desc_len > 3000) desc_len = 3000;
                        snprintf(description, description_len, "文件名：%s\n文件内容：%.*s",
                                 filename, desc_len, content_buf);
                    }
                    free(content_buf);
                }
                remove(tmp_path);
            }
        }

        if (!content_extracted) {
            snprintf(description, description_len, "%s类型的文件：%s",
                     (type && strlen(type) > 0) ? type : "未知", filename);
        }
    }

    free(escaped_md5);
    return 0;
}

/**
 * 处理 describe 请求：为文件生成 AI 描述 + embedding，存入 MySQL 和 FAISS
 * POST body: { "user": "xxx", "token": "xxx", "md5": "xxx", "filename": "xxx", "type": "png", "api_key": "sk-xxx" }
 */
static int handle_describe(char *post_data)
{
    int ret = -1;
    MYSQL *conn = NULL;
    cJSON *root = NULL;
    float *vector = NULL;
    AiCacheData cache_data;
    int cache_loaded = 0;
    char owner_file_name[256] = {0};
    char description[4096] = {0};

    root = cJSON_Parse(post_data);
    if (!root) {
        LOG(AI_LOG_MODULE, AI_LOG_PROC, "parse describe request failed\n");
        printf("{\"code\":1,\"msg\":\"invalid json\"}\n");
        return -1;
    }

    memset(&cache_data, 0, sizeof(cache_data));
    cache_data.id = -1;

    // 提取参数
    cJSON *user_item = cJSON_GetObjectItem(root, "user");
    cJSON *token_item = cJSON_GetObjectItem(root, "token");
    cJSON *md5_item = cJSON_GetObjectItem(root, "md5");
    cJSON *filename_item = cJSON_GetObjectItem(root, "filename");
    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    cJSON *apikey_item = cJSON_GetObjectItem(root, "api_key");
    cJSON *force_item = cJSON_GetObjectItem(root, "force");
    cJSON *skip_rebuild_item = cJSON_GetObjectItem(root, "skip_rebuild");
    int force_update = (force_item && force_item->type == cJSON_True) ? 1 : 0;
    int skip_rebuild = (skip_rebuild_item && skip_rebuild_item->type == cJSON_True) ? 1 : 0;

    if (!user_item || !token_item || !md5_item || !filename_item) {
        LOG(AI_LOG_MODULE, AI_LOG_PROC, "describe missing required fields\n");
        printf("{\"code\":1,\"msg\":\"missing fields\"}\n");
        cJSON_Delete(root);
        return -1;
    }

    char *user = user_item->valuestring;
    char *token = token_item->valuestring;

    // 仅接受请求体中的 api_key
    const char *api_key = resolve_api_key(apikey_item);
    if (!api_key || strlen(api_key) == 0) {
        printf("{\"code\":1,\"msg\":\"missing api_key\"}\n");
        cJSON_Delete(root);
        return -1;
    }
    char *md5 = md5_item->valuestring;
    char *filename = filename_item->valuestring;
    char *type = type_item ? type_item->valuestring : (char *)"";

    // 验证 token
    ret = verify_token(user, token);
    if (ret != 0) {
        printf("{\"code\":4,\"msg\":\"token error\"}\n");
        cJSON_Delete(root);
        return -1;
    }

    // 连接数据库
    conn = msql_conn(mysql_user, mysql_pwd, mysql_db);
    if (!conn) {
        LOG(AI_LOG_MODULE, AI_LOG_PROC, "mysql connect failed\n");
        printf("{\"code\":1,\"msg\":\"db error\"}\n");
        cJSON_Delete(root);
        return -1;
    }
    mysql_query(conn, "set names utf8mb4");

    // 校验当前用户是否真正拥有该文件
    if (user_owns_file(conn, user, md5, owner_file_name, sizeof(owner_file_name)) != 0) {
        printf("{\"code\":1,\"msg\":\"file not found or no permission\"}\n");
        goto END;
    }

    // 如果当前用户已有完成记录，直接返回
    {
        int user_ai_status = 0;
        int user_ai_ret = get_user_ai_status(conn, user, md5, &user_ai_status);
        if (user_ai_ret == 0 && user_ai_status == 1 && !force_update) {
            printf("{\"code\":0,\"msg\":\"already exists\"}\n");
            ret = 0;
            goto END;
        }
    }

    // 如果全局缓存已存在，直接复制到当前用户的 AI 记录
    if (!force_update) {
        int cache_ret = load_global_ai_cache(conn, md5, &cache_data);
        if (cache_ret == 0) {
            cache_loaded = 1;
            if (cache_data.status == 1) {
                if (copy_cache_to_user_ai(conn, user, md5, &cache_data) == 0 &&
                    (append_user_faiss_entry(conn, user, md5) >= 0 ||
                     rebuild_user_faiss_index(conn, user) >= 0)) {
                    printf("{\"code\":0,\"msg\":\"ok\"}\n");
                    ret = 0;
                    goto END;
                }
            }
        }
    }

    // 生成新的描述与向量
    if (generate_description_for_file(conn, md5,
                                      strlen(filename) > 0 ? filename : owner_file_name,
                                      type, api_key,
                                      description, sizeof(description)) != 0) {
        printf("{\"code\":1,\"msg\":\"describe failed\"}\n");
        goto END;
    }

    LOG(AI_LOG_MODULE, AI_LOG_PROC, "description for %s: %.200s\n", md5, description);

    vector = (float *)malloc(sizeof(float) * embedding_dimension);
    if (!vector) {
        LOG(AI_LOG_MODULE, AI_LOG_PROC, "malloc vector failed\n");
        printf("{\"code\":1,\"msg\":\"memory error\"}\n");
        goto END;
    }
    memset(vector, 0, sizeof(float) * embedding_dimension);

    ret = dashscope_get_embedding(api_key, embedding_model,
                                  description, vector, embedding_dimension);
    if (ret != 0) {
        LOG(AI_LOG_MODULE, AI_LOG_PROC, "get_embedding failed for %s\n", md5);
        replace_global_ai_cache(conn, md5, description, NULL, embedding_model, 2);
        replace_user_ai_record(conn, user, md5, -1, description, NULL, embedding_model, 2);
        printf("{\"code\":1,\"msg\":\"embedding failed\"}\n");
        goto END;
    }

    vector_l2_normalize(vector, embedding_dimension);

    if (replace_global_ai_cache(conn, md5, description, vector, embedding_model, 1) != 0) {
        printf("{\"code\":1,\"msg\":\"cache update failed\"}\n");
        goto END;
    }

    if (cache_loaded) {
        free_ai_cache_data(&cache_data);
        cache_loaded = 0;
    }

    if (load_global_ai_cache(conn, md5, &cache_data) == 0) {
        cache_loaded = 1;
    }

    if (replace_user_ai_record(conn, user, md5,
                               cache_loaded ? cache_data.id : -1,
                               description, vector, embedding_model, 1) != 0) {
        printf("{\"code\":1,\"msg\":\"user ai update failed\"}\n");
        goto END;
    }

    if (force_update) {
        if (!skip_rebuild && rebuild_user_faiss_index(conn, user) < 0) {
            printf("{\"code\":1,\"msg\":\"rebuild failed\"}\n");
            goto END;
        }
    } else if (append_user_faiss_entry(conn, user, md5) < 0 &&
               rebuild_user_faiss_index(conn, user) < 0) {
        printf("{\"code\":1,\"msg\":\"rebuild failed\"}\n");
        goto END;
    }

    printf("{\"code\":0,\"msg\":\"ok\"}\n");
    ret = 0;
END:
    if (cache_loaded) {
        free_ai_cache_data(&cache_data);
    }
    if (vector) free(vector);
    if (conn) mysql_close(conn);
    if (root) cJSON_Delete(root);
    return ret;
}

/**
 * 处理 search 请求：语义搜索文件
 * POST body: { "user": "xxx", "token": "xxx", "query": "红色沙发上的猫", "api_key": "sk-xxx" }
 */
static int handle_search(char *post_data)
{
    int ret = -1;
    MYSQL *conn = NULL;
    cJSON *root = NULL;
    float *query_vec = NULL;
    cJSON *resp = NULL;
    int lock_fd = -1;
    char index_path[512] = {0};
    char *escaped_user = NULL;

    root = cJSON_Parse(post_data);
    if (!root) {
        printf("{\"code\":1,\"msg\":\"invalid json\"}\n");
        return -1;
    }

    cJSON *user_item = cJSON_GetObjectItem(root, "user");
    cJSON *token_item = cJSON_GetObjectItem(root, "token");
    cJSON *query_item = cJSON_GetObjectItem(root, "query");
    cJSON *apikey_item = cJSON_GetObjectItem(root, "api_key");

    if (!user_item || !token_item || !query_item) {
        printf("{\"code\":1,\"msg\":\"missing fields\"}\n");
        cJSON_Delete(root);
        return -1;
    }

    char *user = user_item->valuestring;
    char *token = token_item->valuestring;
    char *query = query_item->valuestring;

    // 仅接受请求体中的 api_key
    const char *api_key = resolve_api_key(apikey_item);
    if (!api_key || strlen(api_key) == 0) {
        printf("{\"code\":1,\"msg\":\"missing api_key\"}\n");
        cJSON_Delete(root);
        return -1;
    }

    // 验证 token
    ret = verify_token(user, token);
    if (ret != 0) {
        printf("{\"code\":4,\"msg\":\"token error\"}\n");
        cJSON_Delete(root);
        return -1;
    }

    if (strlen(query) == 0) {
        printf("{\"code\":1,\"msg\":\"empty query\"}\n");
        cJSON_Delete(root);
        return -1;
    }

    conn = msql_conn(mysql_user, mysql_pwd, mysql_db);
    if (!conn) {
        printf("{\"code\":1,\"msg\":\"db error\"}\n");
        cJSON_Delete(root);
        return -1;
    }
    mysql_query(conn, "set names utf8mb4");
    escaped_user = escape_mysql_text(conn, user);
    if (!escaped_user) {
        printf("{\"code\":1,\"msg\":\"db error\"}\n");
        goto END;
    }

    if (is_user_index_dirty(user)) {
        if (rebuild_user_faiss_index(conn, user) < 0) {
            printf("{\"code\":1,\"msg\":\"rebuild failed\"}\n");
            goto END;
        }
        clear_user_index_dirty(user);
    }

    {
        int sync_ret = sync_missing_user_ai_from_cache(conn, user);
        if (sync_ret < 0) {
            LOG(AI_LOG_MODULE, AI_LOG_PROC, "sync user ai cache failed for %s\n", user);
            printf("{\"code\":1,\"msg\":\"sync failed\"}\n");
            goto END;
        }
        if (sync_ret > 0) {
            int append_ret = append_pending_user_faiss_entries(conn, user);
            if (append_ret < 0 && rebuild_user_faiss_index(conn, user) < 0) {
                printf("{\"code\":1,\"msg\":\"rebuild failed\"}\n");
                goto END;
            }
        }
    }

    // 获取查询文本的 embedding
    query_vec = (float *)malloc(sizeof(float) * embedding_dimension);
    if (!query_vec) {
        printf("{\"code\":1,\"msg\":\"memory error\"}\n");
        goto END;
    }
    memset(query_vec, 0, sizeof(float) * embedding_dimension);

    ret = dashscope_get_embedding(api_key, embedding_model,
                                  query, query_vec, embedding_dimension);
    if (ret != 0) {
        LOG(AI_LOG_MODULE, AI_LOG_PROC, "search embedding failed\n");
        printf("{\"code\":1,\"msg\":\"embedding failed\"}\n");
        goto END;
    }

    // L2 归一化查询向量（使内积 = 余弦相似度）
    vector_l2_normalize(query_vec, embedding_dimension);

    get_user_index_path(user, index_path, sizeof(index_path));
    lock_fd = acquire_user_lock(user, LOCK_SH);
    if (lock_fd < 0) {
        printf("{\"code\":1,\"msg\":\"lock error\"}\n");
        goto END;
    }

    if (faiss_init(index_path, embedding_dimension) != 0) {
        printf("{\"code\":1,\"msg\":\"index init failed\"}\n");
        goto END;
    }

    if (faiss_get_ntotal() == 0) {
        char ready_sql[512] = {0};
        char ready_count[64] = {0};

        snprintf(ready_sql, sizeof(ready_sql),
                 "SELECT COUNT(*) FROM user_file_ai_desc "
                 "WHERE user='%s' AND status=1 AND embedding IS NOT NULL",
                 escaped_user);
        if (process_result_one(conn, ready_sql, ready_count) == 0 && atoi(ready_count) > 0) {
            release_user_lock(lock_fd);
            lock_fd = -1;

            if (rebuild_user_faiss_index(conn, user) < 0) {
                printf("{\"code\":1,\"msg\":\"rebuild failed\"}\n");
                goto END;
            }

            lock_fd = acquire_user_lock(user, LOCK_SH);
            if (lock_fd < 0) {
                printf("{\"code\":1,\"msg\":\"lock error\"}\n");
                goto END;
            }

            if (faiss_init(index_path, embedding_dimension) != 0) {
                printf("{\"code\":1,\"msg\":\"index init failed\"}\n");
                goto END;
            }
        }
    }

    if (faiss_get_ntotal() == 0) {
        printf("{\"code\":0,\"count\":0,\"files\":[]}\n");
        ret = 0;
        goto END;
    }

    // FAISS 搜索
    {
        int topk = 10;
        long ids[10] = {0};
        float scores[10] = {0};
        // 余弦相似度阈值：低于此值的结果认为不相关
        float score_threshold = 0.45f;
        int result_count = 0;

        int found = faiss_search(query_vec, embedding_dimension, topk, ids, scores);
        if (found <= 0) {
            printf("{\"code\":0,\"count\":0,\"files\":[]}\n");
            ret = 0;
            goto END;
        }

        // 构造返回 JSON
        resp = cJSON_CreateObject();
        cJSON_AddNumberToObject(resp, "code", 0);
        cJSON *files_arr = cJSON_CreateArray();

        for (int i = 0; i < found; i++) {
            // 过滤掉低相似度结果
            if (scores[i] < score_threshold) continue;

            char sql_cmd[2048] = {0};
            MYSQL_RES *res = NULL;
            MYSQL_ROW row = NULL;
            snprintf(sql_cmd, sizeof(sql_cmd),
                     "SELECT uad.md5, ufl.file_name, uad.description, fi.url, fi.size, fi.type "
                     "FROM user_file_ai_desc uad "
                     "JOIN user_file_list ufl ON ufl.user = uad.user AND ufl.md5 = uad.md5 "
                     "JOIN file_info fi ON fi.md5 = uad.md5 "
                     "WHERE uad.user = '%s' AND uad.faiss_id = %ld AND uad.status = 1 "
                     "LIMIT 1",
                     escaped_user, ids[i]);

            if (mysql_query(conn, sql_cmd) != 0) {
                LOG(AI_LOG_MODULE, AI_LOG_PROC, "search query failed: %s\n", mysql_error(conn));
                continue;
            }
            res = mysql_store_result(conn);
            if (!res) continue;

            row = mysql_fetch_row(res);
            if (row) {
                cJSON *file_obj = cJSON_CreateObject();
                cJSON_AddStringToObject(file_obj, "md5", row[0] ? row[0] : "");
                cJSON_AddStringToObject(file_obj, "filename", row[1] ? row[1] : "");
                cJSON_AddStringToObject(file_obj, "description", row[2] ? row[2] : "");

                // 文件 URL（数据库已存储完整 URL）
                if (row[3] && strlen(row[3]) > 0) {
                    cJSON_AddStringToObject(file_obj, "url", row[3]);
                } else {
                    cJSON_AddStringToObject(file_obj, "url", "");
                }

                cJSON_AddStringToObject(file_obj, "size", row[4] ? row[4] : "0");
                cJSON_AddStringToObject(file_obj, "type", row[5] ? row[5] : "");
                cJSON_AddNumberToObject(file_obj, "score", scores[i]);
                cJSON_AddItemToArray(files_arr, file_obj);
                result_count++;
            }
            mysql_free_result(res);
        }

        cJSON_AddNumberToObject(resp, "count", result_count);
        cJSON_AddItemToObject(resp, "files", files_arr);

        char *resp_str = cJSON_PrintUnformatted(resp);
        if (resp_str) {
            printf("%s\n", resp_str);
            free(resp_str);
        }
        cJSON_Delete(resp);
        resp = NULL;
        ret = 0;
    }

END:
    release_user_lock(lock_fd);
    if (escaped_user) free(escaped_user);
    if (resp) cJSON_Delete(resp);
    if (query_vec) free(query_vec);
    if (conn) mysql_close(conn);
    if (root) cJSON_Delete(root);
    return ret;
}

/**
 * 处理 rebuild 请求：从 MySQL 重建 FAISS 索引
 * POST body: { "user": "xxx", "token": "xxx" }
 */
static int handle_rebuild(char *post_data)
{
    int ret = -1;
    MYSQL *conn = NULL;
    cJSON *root = NULL;
    char *user = NULL;

    root = cJSON_Parse(post_data);
    if (!root) {
        printf("{\"code\":1,\"msg\":\"invalid json\"}\n");
        return -1;
    }

    cJSON *user_item = cJSON_GetObjectItem(root, "user");
    cJSON *token_item = cJSON_GetObjectItem(root, "token");

    if (!user_item || !token_item) {
        printf("{\"code\":1,\"msg\":\"missing fields\"}\n");
        cJSON_Delete(root);
        return -1;
    }

    ret = verify_token(user_item->valuestring, token_item->valuestring);
    if (ret != 0) {
        printf("{\"code\":4,\"msg\":\"token error\"}\n");
        cJSON_Delete(root);
        return -1;
    }

    user = user_item->valuestring;

    conn = msql_conn(mysql_user, mysql_pwd, mysql_db);
    if (!conn) {
        printf("{\"code\":1,\"msg\":\"db error\"}\n");
        cJSON_Delete(root);
        return -1;
    }
    mysql_query(conn, "set names utf8mb4");

    {
        int sync_ret = sync_missing_user_ai_from_cache(conn, user);
        int rebuild_count = 0;

        if (sync_ret < 0) {
            printf("{\"code\":1,\"msg\":\"sync failed\"}\n");
            goto END;
        }

        rebuild_count = rebuild_user_faiss_index(conn, user);
        if (rebuild_count < 0) {
            printf("{\"code\":1,\"msg\":\"rebuild failed\"}\n");
            goto END;
        }
        clear_user_index_dirty(user);

        printf("{\"code\":0,\"msg\":\"rebuilt\",\"count\":%d}\n", rebuild_count);
        ret = 0;
    }

END:
    if (conn) mysql_close(conn);
    if (root) cJSON_Delete(root);
    return ret;
}

int main()
{
    // 读取配置
    read_cfg();

    // 初始化 libcurl 全局
    curl_global_init(CURL_GLOBAL_ALL);

    if (ensure_dir_recursive(faiss_user_index_dir) != 0) {
        LOG(AI_LOG_MODULE, AI_LOG_PROC, "ensure user index dir failed: %s\n", faiss_user_index_dir);
    }
    if (ensure_dir_recursive(faiss_lock_dir) != 0) {
        LOG(AI_LOG_MODULE, AI_LOG_PROC, "ensure faiss lock dir failed: %s\n", faiss_lock_dir);
    }

    LOG(AI_LOG_MODULE, AI_LOG_PROC, "ai_cgi started, user_index_dir=%s, lock_dir=%s\n",
        faiss_user_index_dir, faiss_lock_dir);

    // FastCGI 主循环
    while (FCGI_Accept() >= 0) {
        char *query_string = getenv("QUERY_STRING");
        char *content_length_str = getenv("CONTENT_LENGTH");

        // 输出 HTTP 头
        printf("Content-Type: application/json\r\n\r\n");

        // 获取 cmd 参数
        char cmd[64] = {0};
        if (query_string) {
            int cmd_len = 0;
            query_parse_key_value(query_string, "cmd", cmd, &cmd_len);
        }

        if (strlen(cmd) == 0) {
            printf("{\"code\":1,\"msg\":\"missing cmd\"}\n");
            continue;
        }

        // 读取 POST body
        char *post_data = NULL;
        int content_length = 0;

        if (content_length_str) {
            content_length = atoi(content_length_str);
        }

        if (content_length > 0 && content_length < 1024 * 1024) {
            post_data = (char *)malloc(content_length + 1);
            if (post_data) {
                int bytes_read = fread(post_data, 1, content_length, stdin);
                post_data[bytes_read] = '\0';
            }
        }

        if (!post_data || strlen(post_data) == 0) {
            printf("{\"code\":1,\"msg\":\"no post data\"}\n");
            if (post_data) free(post_data);
            continue;
        }

        LOG(AI_LOG_MODULE, AI_LOG_PROC, "cmd=%s, data=%.200s\n", cmd, post_data);

        // 分发处理
        if (strcmp(cmd, "describe") == 0) {
            handle_describe(post_data);
        } else if (strcmp(cmd, "search") == 0) {
            handle_search(post_data);
        } else if (strcmp(cmd, "rebuild") == 0) {
            handle_rebuild(post_data);
        } else {
            printf("{\"code\":1,\"msg\":\"unknown cmd: %s\"}\n", cmd);
        }

        if (post_data) free(post_data);
    }

    curl_global_cleanup();
    return 0;
}
