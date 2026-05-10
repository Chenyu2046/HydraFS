/**
 * @file chunk_init_cgi.c
 * @brief  分片上传初始化CGI程序
 *         客户端发送文件元信息(文件名、大小、分片数、MD5)，
 *         后端存储到Redis中，创建临时目录，支持断点续传。
 */

#include "fcgi_config.h"
#include "fcgi_stdio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "make_log.h"
#include "util_cgi.h"
#include "cfg.h"
#include "cJSON.h"
#include "redis_op.h"

#define CHUNK_LOG_MODULE  "cgi"
#define CHUNK_LOG_PROC    "chunk_init"
#define CHUNK_TEMP_DIR    "/tmp/chunks"

static char redis_ip[30] = {0};
static char redis_port[10] = {0};

void read_cfg()
{
    get_cfg_value(CFG_PATH, "redis", "ip", redis_ip);
    get_cfg_value(CFG_PATH, "redis", "port", redis_port);
    LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC, "redis:[ip=%s,port=%s]", redis_ip, redis_port);
}

/*
 * 解析JSON请求:
 * {
 *   "user": "xxx",
 *   "token": "xxx",
 *   "filename": "xxx",
 *   "md5": "xxx",
 *   "size": 123456,
 *   "chunkCount": 3
 * }
 */
int parse_init_json(char *buf, char *user, char *token, char *filename,
                    char *file_md5, long *filesize, int *chunk_count)
{
    int ret = 0;
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL)
    {
        LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC, "cJSON_Parse err\n");
        return -1;
    }

    cJSON *item;

    item = cJSON_GetObjectItem(root, "user");
    if (!item) { ret = -1; goto END; }
    strcpy(user, item->valuestring);

    item = cJSON_GetObjectItem(root, "token");
    if (!item) { ret = -1; goto END; }
    strcpy(token, item->valuestring);

    item = cJSON_GetObjectItem(root, "filename");
    if (!item) { ret = -1; goto END; }
    strcpy(filename, item->valuestring);

    item = cJSON_GetObjectItem(root, "md5");
    if (!item) { ret = -1; goto END; }
    strcpy(file_md5, item->valuestring);

    item = cJSON_GetObjectItem(root, "size");
    if (!item) { ret = -1; goto END; }
    *filesize = (long)item->valuedouble;

    item = cJSON_GetObjectItem(root, "chunkCount");
    if (!item) { ret = -1; goto END; }
    *chunk_count = item->valueint;

END:
    cJSON_Delete(root);
    return ret;
}

int main()
{
    read_cfg();

    // 确保临时目录存在
    mkdir(CHUNK_TEMP_DIR, 0755);

    while (FCGI_Accept() >= 0)
    {
        char *contentLength = getenv("CONTENT_LENGTH");
        int len = 0;

        printf("Content-type: text/html\r\n\r\n");

        if (contentLength != NULL)
            len = atoi(contentLength);

        if (len <= 0)
        {
            printf("{\"code\":1}");
            continue;
        }

        char buf[4 * 1024] = {0};
        int ret = fread(buf, 1, len, stdin);
        if (ret == 0)
        {
            LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC, "fread stdin err\n");
            printf("{\"code\":1}");
            continue;
        }

        char user[128] = {0};
        char token[256] = {0};
        char filename[256] = {0};
        char file_md5[256] = {0};
        long filesize = 0;
        int chunk_count = 0;

        if (parse_init_json(buf, user, token, filename, file_md5, &filesize, &chunk_count) != 0)
        {
            LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC, "parse_init_json err\n");
            printf("{\"code\":1}");
            continue;
        }

        LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC,
            "user=%s, md5=%s, filename=%s, size=%ld, chunks=%d\n",
            user, file_md5, filename, filesize, chunk_count);

        // 验证token
        ret = verify_token(user, token);
        if (ret != 0)
        {
            printf("{\"code\":4}");
            continue;
        }

        // 连接Redis
        redisContext *redis_conn = rop_connectdb_nopwd(redis_ip, redis_port);
        if (redis_conn == NULL)
        {
            LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC, "redis connect err\n");
            printf("{\"code\":1}");
            continue;
        }

        char redis_key[512] = {0};
        sprintf(redis_key, "chunk:%s", file_md5);

        // 检查是否已经存在（断点续传）
        int key_exist = rop_is_key_exist(redis_conn, redis_key);
        if (key_exist == 1)
        {
            // 已存在，返回已上传的分片列表
            char uploaded[1024] = {0};
            int r = rop_hash_get(redis_conn, redis_key, "uploaded", uploaded);
            if (r != 0)
                uploaded[0] = '\0';

            LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC,
                "chunk key exists, uploaded=%s\n", uploaded);

            cJSON *resp = cJSON_CreateObject();
            cJSON_AddNumberToObject(resp, "code", 0);
            cJSON_AddNumberToObject(resp, "chunkCount", chunk_count);
            cJSON_AddStringToObject(resp, "uploadedChunks", uploaded);
            char *out = cJSON_PrintUnformatted(resp);
            printf("%s", out);
            free(out);
            cJSON_Delete(resp);

            rop_disconnect(redis_conn);
            continue;
        }

        // 新的分片上传，在Redis中初始化
        rop_hash_set(redis_conn, redis_key, "filename", filename);

        char size_str[64] = {0};
        sprintf(size_str, "%ld", filesize);
        rop_hash_set(redis_conn, redis_key, "filesize", size_str);

        char count_str[32] = {0};
        sprintf(count_str, "%d", chunk_count);
        rop_hash_set(redis_conn, redis_key, "chunk_count", count_str);

        rop_hash_set(redis_conn, redis_key, "user", user);
        rop_hash_set(redis_conn, redis_key, "uploaded", "");

        // 设置过期时间 24小时
        redisReply *reply = redisCommand(redis_conn, "EXPIRE %s 86400", redis_key);
        if (reply) freeReplyObject(reply);

        // 创建分片临时目录 /tmp/chunks/{md5}/
        char chunk_dir[512] = {0};
        sprintf(chunk_dir, "%s/%s", CHUNK_TEMP_DIR, file_md5);
        mkdir(chunk_dir, 0755);

        LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC,
            "chunk init OK, dir=%s, chunks=%d\n", chunk_dir, chunk_count);

        cJSON *resp = cJSON_CreateObject();
        cJSON_AddNumberToObject(resp, "code", 0);
        cJSON_AddNumberToObject(resp, "chunkCount", chunk_count);
        cJSON_AddStringToObject(resp, "uploadedChunks", "");
        char *out = cJSON_PrintUnformatted(resp);
        printf("%s", out);
        free(out);
        cJSON_Delete(resp);

        rop_disconnect(redis_conn);
    }

    return 0;
}
