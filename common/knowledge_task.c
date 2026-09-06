#include "knowledge_task.h"

#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *PARSEABLE_TYPES[] = {
    "txt", "md", "csv", "json", "xml", "html", "htm", "log",
    "c", "cpp", "h", "hpp", "py", "js", "ts", "jsx", "tsx",
    "css", "java", "go", "rs", "rust", "rb", "php", "sh", "bat", "yaml", "yml",
    "pdf", "docx", "png", "jpg", "jpeg", "gif", "bmp", "webp", NULL
};

int is_parseable_type(const char *type) {
    if (!type || !*type) return 0;
    for (int i = 0; PARSEABLE_TYPES[i]; ++i) if (strcasecmp(type, PARSEABLE_TYPES[i]) == 0) return 1;
    return 0;
}

int enqueue_knowledge_task(MYSQL *conn, const char *user, const char *md5,
                           const char *task_type, const char *source, int force) {
    /* Terminal rows are deliberately reusable; the named lock still prevents force retries from duplicating active work. */
    (void)force;
    if (!conn || !user || !md5 || !task_type || !*task_type) return -1;
    const char *normalized_type = strcmp(task_type, "parse_file") == 0 ? "parse_source" : task_type;
    const size_t user_len = strlen(user), md5_len = strlen(md5), type_len = strlen(normalized_type), source_len = source ? strlen(source) : 6;
    char *eu = (char *)malloc(user_len * 2 + 1), *em = (char *)malloc(md5_len * 2 + 1), *et = (char *)malloc(type_len * 2 + 1), *es = (char *)malloc(source_len * 2 + 1);
    if (!eu || !em || !et || !es) { free(eu); free(em); free(et); free(es); return -1; }
    mysql_real_escape_string(conn, eu, user, (unsigned long)user_len);
    mysql_real_escape_string(conn, em, md5, (unsigned long)md5_len);
    mysql_real_escape_string(conn, et, normalized_type, (unsigned long)strlen(normalized_type));
    mysql_real_escape_string(conn, es, source ? source : "upload", (unsigned long)(source ? strlen(source) : 6));
    char sql[2048];
    snprintf(sql, sizeof(sql), "SELECT GET_LOCK(CONCAT('hydrastore-ai:',SHA2(CONCAT('%s',':','%s',':','%s'),256)),5)", eu, em, et);
    if (mysql_query(conn, sql) != 0) goto FAIL;
    MYSQL_RES *lock_result = mysql_store_result(conn);
    if (!lock_result) goto FAIL;
    MYSQL_ROW lock_row = mysql_fetch_row(lock_result);
    const int locked = lock_row && lock_row[0] && atoi(lock_row[0]) == 1;
    mysql_free_result(lock_result);
    if (!locked) goto FAIL;
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM ai_parse_task WHERE user='%s' AND md5='%s' AND task_type='%s' AND (status='pending' OR (status='running' AND lease_until IS NOT NULL AND lease_until>NOW()))", eu, em, et);
    if (mysql_query(conn, sql) != 0) goto RELEASE_FAIL;
    MYSQL_RES *result = mysql_store_result(conn);
    if (!result) goto RELEASE_FAIL;
    MYSQL_ROW row = mysql_fetch_row(result);
    const int duplicate = row && row[0] && atoi(row[0]) > 0;
    mysql_free_result(result);
    if (!duplicate) {
        snprintf(sql, sizeof(sql), "INSERT INTO ai_parse_task(user,md5,task_type,source,status,retry_count) VALUES('%s','%s','%s','%s','pending',0)", eu, em, et, es);
        if (mysql_query(conn, sql) != 0) goto RELEASE_FAIL;
    }
    snprintf(sql, sizeof(sql), "SELECT RELEASE_LOCK(CONCAT('hydrastore-ai:',SHA2(CONCAT('%s',':','%s',':','%s'),256)))", eu, em, et);
    mysql_query(conn, sql);
    free(eu); free(em); free(et); free(es); return 0;

RELEASE_FAIL:
    snprintf(sql, sizeof(sql), "SELECT RELEASE_LOCK(CONCAT('hydrastore-ai:',SHA2(CONCAT('%s',':','%s',':','%s'),256)))", eu, em, et);
    mysql_query(conn, sql);
FAIL:
    free(eu); free(em); free(et); free(es); return -1;
}

int enqueue_parse_task(MYSQL *conn, const char *user, const char *md5,
                       const char *type, const char *source) {
    (void)type;
    return enqueue_knowledge_task(conn, user, md5, "parse_source", source, 0);
}
