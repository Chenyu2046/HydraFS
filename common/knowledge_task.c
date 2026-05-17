/**
 * @file knowledge_task.c
 * @brief 知识层异步解析任务：入队、状态管理
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <mysql/mysql.h>

#include "knowledge_task.h"
#include "make_log.h"

#define TASK_LOG_MODULE "cgi"
#define TASK_LOG_PROC   "knowledge_task"

/* 可解析的文件类型列表（文本类 + pdf + 图片）
 * 图片走 Qwen-VL 路径，由 worker 调用多模态模型生成描述后再 embed
 */
static const char *PARSEABLE_TYPES[] = {
    "txt", "md", "csv", "json", "xml", "html", "htm", "log",
    "c", "cpp", "h", "hpp", "py", "js", "ts", "jsx", "tsx",
    "css", "java", "go", "rs", "rb", "php", "sh", "bat", "yaml", "yml",
    "pdf",
    /* 图片：worker 使用 dashscope_describe_image */
    "png", "jpg", "jpeg", "gif", "bmp", "webp",
    NULL
};

int is_parseable_type(const char *type)
{
    if (!type || strlen(type) == 0) return 0;

    for (int i = 0; PARSEABLE_TYPES[i] != NULL; i++) {
        if (strcasecmp(type, PARSEABLE_TYPES[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * 将字符串安全转义后执行 INSERT
 * 返回值：0=成功, -1=失败
 */
int enqueue_parse_task(MYSQL *conn, const char *user, const char *md5,
                       const char *type, const char *source)
{
    if (!conn || !user || !md5) return -1;

    char *esc_user = NULL;
    char *esc_md5 = NULL;
    char sql[1024] = {0};
    int ret = -1;

    /* 转义输入 */
    esc_user = (char *)malloc(strlen(user) * 2 + 1);
    esc_md5 = (char *)malloc(strlen(md5) * 2 + 1);
    if (!esc_user || !esc_md5) goto END;

    mysql_real_escape_string(conn, esc_user, user, (unsigned long)strlen(user));
    mysql_real_escape_string(conn, esc_md5, md5, (unsigned long)strlen(md5));

    /* 检查是否已存在 pending/running 任务（防重） */
    snprintf(sql, sizeof(sql),
             "SELECT COUNT(*) FROM ai_parse_task "
             "WHERE user='%s' AND md5='%s' AND status IN ('pending','running')",
             esc_user, esc_md5);

    if (mysql_query(conn, sql) == 0) {
        MYSQL_RES *res = mysql_store_result(conn);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[0] && atoi(row[0]) > 0) {
                LOG(TASK_LOG_MODULE, TASK_LOG_PROC,
                    "skip duplicate task: user=%s, md5=%.32s\n", user, md5);
                mysql_free_result(res);
                ret = 0;
                goto END;
            }
            mysql_free_result(res);
        }
    }

    /* 不可解析类型直接标记 skipped */
    const char *status = "pending";
    if (!is_parseable_type(type)) {
        status = "skipped";
        LOG(TASK_LOG_MODULE, TASK_LOG_PROC,
            "type '%s' not parseable, mark skipped: md5=%.32s\n",
            type ? type : "(null)", md5);
    }

    /* 写入任务 */
    snprintf(sql, sizeof(sql),
             "INSERT INTO ai_parse_task (user, md5, task_type, source, status) "
             "VALUES ('%s', '%s', 'parse_file', '%s', '%s')",
             esc_user, esc_md5,
             source ? source : "upload",
             status);

    if (mysql_query(conn, sql) != 0) {
        LOG(TASK_LOG_MODULE, TASK_LOG_PROC,
            "insert task failed: %s\n", mysql_error(conn));
        goto END;
    }

    LOG(TASK_LOG_MODULE, TASK_LOG_PROC,
        "task enqueued: user=%s, md5=%.32s, status=%s\n", user, md5, status);
    ret = 0;

END:
    if (esc_user) free(esc_user);
    if (esc_md5) free(esc_md5);
    return ret;
}
