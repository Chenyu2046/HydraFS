/**
 * @file chunk_upload_cgi.c
 * @brief  接收单个分片数据并保存到临时目录
 *         URL参数: ?md5=xxx&index=0
 *         POST body: 原始分片二进制数据
 */

#include "fcgi_config.h"
#include "fcgi_stdio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "make_log.h"
#include "util_cgi.h"
#include "cfg.h"
#include "redis_op.h"

#define CHUNK_LOG_MODULE  "cgi"
#define CHUNK_LOG_PROC    "chunk_upload"
#define CHUNK_TEMP_DIR    "/tmp/chunks"

static char redis_ip[30] = {0};
static char redis_port[10] = {0};

void read_cfg()
{
    get_cfg_value(CFG_PATH, "redis", "ip", redis_ip);
    get_cfg_value(CFG_PATH, "redis", "port", redis_port);
    LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC, "redis:[ip=%s,port=%s]", redis_ip, redis_port);
}

int main()
{
    read_cfg();

    while (FCGI_Accept() >= 0)
    {
        char *contentLength = getenv("CONTENT_LENGTH");
        char *queryString = getenv("QUERY_STRING");
        long len = 0;
        int ret = 0;

        printf("Content-type: text/html\r\n\r\n");

        if (contentLength != NULL)
            len = strtol(contentLength, NULL, 10);

        if (len <= 0 || queryString == NULL)
        {
            LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC, "no data or no query string\n");
            printf("{\"code\":1}");
            continue;
        }

        // 从URL参数获取 md5 和 index
        char file_md5[256] = {0};
        char index_str[32] = {0};
        int value_len = 0;

        if (query_parse_key_value(queryString, "md5", file_md5, &value_len) != 0)
        {
            LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC, "parse md5 from query err\n");
            printf("{\"code\":1}");
            continue;
        }

        if (query_parse_key_value(queryString, "index", index_str, &value_len) != 0)
        {
            LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC, "parse index from query err\n");
            printf("{\"code\":1}");
            continue;
        }

        int chunk_index = atoi(index_str);

        LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC,
            "receiving chunk: md5=%s, index=%d, size=%ld\n",
            file_md5, chunk_index, len);

        // 读取分片数据
        char *chunk_buf = (char *)malloc(len);
        if (chunk_buf == NULL)
        {
            LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC, "malloc %ld err\n", len);
            printf("{\"code\":1}");
            continue;
        }

        long total_read = 0;
        while (total_read < len)
        {
            int n = fread(chunk_buf + total_read, 1, len - total_read, stdin);
            if (n <= 0) break;
            total_read += n;
        }

        if (total_read != len)
        {
            LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC,
                "fread incomplete: expected=%ld, got=%ld\n", len, total_read);
            free(chunk_buf);
            printf("{\"code\":1}");
            continue;
        }

        // 写入文件 /tmp/chunks/{md5}/{index}
        char chunk_path[512] = {0};
        sprintf(chunk_path, "%s/%s/%d", CHUNK_TEMP_DIR, file_md5, chunk_index);

        int fd = open(chunk_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0)
        {
            LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC,
                "open %s err: %s\n", chunk_path, strerror(errno));
            free(chunk_buf);
            printf("{\"code\":1}");
            continue;
        }

        long total_written = 0;
        while (total_written < len)
        {
            int n = write(fd, chunk_buf + total_written, len - total_written);
            if (n <= 0) break;
            total_written += n;
        }
        close(fd);
        free(chunk_buf);

        if (total_written != len)
        {
            LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC, "write incomplete\n");
            printf("{\"code\":1}");
            continue;
        }

        // 更新Redis中已上传的分片索引
        redisContext *redis_conn = rop_connectdb_nopwd(redis_ip, redis_port);
        if (redis_conn != NULL)
        {
            char redis_key[512] = {0};
            sprintf(redis_key, "chunk:%s", file_md5);

            char uploaded[1024] = {0};
            int r = rop_hash_get(redis_conn, redis_key, "uploaded", uploaded);

            if (r == 0 && strlen(uploaded) > 0)
            {
                // 追加 ",index"
                char new_uploaded[1024] = {0};
                sprintf(new_uploaded, "%s,%d", uploaded, chunk_index);
                rop_hash_set(redis_conn, redis_key, "uploaded", new_uploaded);
            }
            else
            {
                // 首个分片
                char new_uploaded[32] = {0};
                sprintf(new_uploaded, "%d", chunk_index);
                rop_hash_set(redis_conn, redis_key, "uploaded", new_uploaded);
            }

            rop_disconnect(redis_conn);
        }

        LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC,
            "chunk saved: %s (%ld bytes)\n", chunk_path, len);

        printf("{\"code\":0}");
    }

    return 0;
}
