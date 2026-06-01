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

static int is_complete_chunk(char *chunk_path, long len)
{
    struct stat st;

    if (stat(chunk_path, &st) != 0)
    {
        return 0;
    }

    return S_ISREG(st.st_mode) && st.st_size == len;
}

static int write_chunk_atomic(char *chunk_dir, char *chunk_path, char *chunk_buf, long len)
{
    char tmp_path[512] = {0};
    int fd = -1;
    long total_written = 0;

    snprintf(tmp_path, sizeof(tmp_path), "%s/.chunk_upload_%d_XXXXXX", chunk_dir, getpid());
    fd = mkstemp(tmp_path);
    if (fd < 0)
    {
        LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC,
            "mkstemp %s err: %s\n", tmp_path, strerror(errno));
        return -1;
    }

    while (total_written < len)
    {
        ssize_t n = write(fd, chunk_buf + total_written, len - total_written);
        if (n <= 0)
        {
            LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC,
                "write tmp chunk err: %s\n", strerror(errno));
            close(fd);
            unlink(tmp_path);
            return -1;
        }
        total_written += n;
    }

    if (fsync(fd) != 0)
    {
        LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC,
            "fsync %s err: %s\n", tmp_path, strerror(errno));
        close(fd);
        unlink(tmp_path);
        return -1;
    }

    if (close(fd) != 0)
    {
        LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC,
            "close %s err: %s\n", tmp_path, strerror(errno));
        unlink(tmp_path);
        return -1;
    }

    if (is_complete_chunk(chunk_path, len))
    {
        unlink(tmp_path);
        return 0;
    }

    if (link(tmp_path, chunk_path) != 0)
    {
        if (errno == EEXIST && is_complete_chunk(chunk_path, len))
        {
            unlink(tmp_path);
            return 0;
        }

        LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC,
            "link %s to %s err: %s\n", tmp_path, chunk_path, strerror(errno));
        unlink(tmp_path);
        return -1;
    }

    unlink(tmp_path);
    return 0;
}

static int update_uploaded_chunks(redisContext *redis_conn, char *redis_key, int chunk_index)
{
    redisReply *reply = NULL;
    const char *script =
        "local idx=ARGV[1] "
        "redis.call('HSET', KEYS[1], 'uploaded_idx:'..idx, '1') "
        "local seen={} "
        "local uploaded=redis.call('HGET', KEYS[1], 'uploaded') "
        "if uploaded and uploaded~='' then "
        "  for token in string.gmatch(uploaded, '[^,]+') do seen[token]=true end "
        "end "
        "local fields=redis.call('HKEYS', KEYS[1]) "
        "for _,field in ipairs(fields) do "
        "  local chunk=string.match(field, '^uploaded_idx:(%d+)$') "
        "  if chunk then seen[chunk]=true end "
        "end "
        "local arr={} "
        "for chunk,_ in pairs(seen) do table.insert(arr, chunk) end "
        "table.sort(arr, function(a,b) return tonumber(a)<tonumber(b) end) "
        "redis.call('HSET', KEYS[1], 'uploaded', table.concat(arr, ',')) "
        "return 1";

    reply = redisCommand(redis_conn, "EVAL %b 1 %s %d", script, strlen(script), redis_key, chunk_index);
    if (reply == NULL || reply->type == REDIS_REPLY_ERROR)
    {
        LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC,
            "update uploaded chunks err: %s\n",
            reply != NULL ? reply->str : redis_conn->errstr);
        if (reply != NULL)
        {
            freeReplyObject(reply);
        }
        return -1;
    }

    freeReplyObject(reply);
    return 0;
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
        char chunk_dir[512] = {0};
        char chunk_path[512] = {0};
        sprintf(chunk_dir, "%s/%s", CHUNK_TEMP_DIR, file_md5);
        mkdir(chunk_dir, 0755);
        sprintf(chunk_path, "%s/%d", chunk_dir, chunk_index);

        if (is_complete_chunk(chunk_path, len))
        {
            LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC,
                "chunk already exists: %s (%ld bytes)\n", chunk_path, len);
        }
        else if (write_chunk_atomic(chunk_dir, chunk_path, chunk_buf, len) != 0)
        {
            free(chunk_buf);
            printf("{\"code\":1}");
            continue;
        }
        free(chunk_buf);

        // 更新Redis中已上传的分片索引
        redisContext *redis_conn = rop_connectdb_nopwd(redis_ip, redis_port);
        if (redis_conn == NULL)
        {
            LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC, "redis connect err\n");
            printf("{\"code\":1}");
            continue;
        }

        {
            char redis_key[512] = {0};
            sprintf(redis_key, "chunk:%s", file_md5);

            if (update_uploaded_chunks(redis_conn, redis_key, chunk_index) != 0)
            {
                rop_disconnect(redis_conn);
                printf("{\"code\":1}");
                continue;
            }
            rop_disconnect(redis_conn);
        }

        LOG(CHUNK_LOG_MODULE, CHUNK_LOG_PROC,
            "chunk saved: %s (%ld bytes)\n", chunk_path, len);

        printf("{\"code\":0}");
    }

    return 0;
}
