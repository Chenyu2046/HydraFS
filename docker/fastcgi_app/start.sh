#!/bin/bash

# FastDFS 客户端初始化需要其 base_path 在 chunk_merge 启动前存在。
mkdir -p /fastdfs_data_and_log/client

if [ "${STORAGE_GATEWAY_ONLY:-0}" = "1" ]; then
    echo -n "HydraStore Gateway："
    STORAGE_GATEWAY_WORKERS=${STORAGE_GATEWAY_WORKERS:-8}
    start_gateway_workers() {
        spawn-fcgi -a 0.0.0.0 -p 10020 -F "$STORAGE_GATEWAY_WORKERS" -f /app/bin_cgi/storage_gateway
        echo "OK"
    }
    start_gateway_workers
    while true; do
        worker_count=$(pgrep -fc '[/]app/bin_cgi/storage_gateway' || true)
        if [ "$worker_count" -lt "$STORAGE_GATEWAY_WORKERS" ]; then
            echo "HydraStore Gateway worker pool degraded: $worker_count/$STORAGE_GATEWAY_WORKERS; restarting" >&2
            pkill -TERM -f '[/]app/bin_cgi/storage_gateway' || true
            sleep 1
            start_gateway_workers
        fi
        sleep 2
    done
fi

if [ "${STORAGE_GC_ONLY:-0}" = "1" ]; then
    echo -n "HydraStore GC："
    /app/bin_cgi/storage_gc_worker
    exit $?
fi

# 启动 9 个 FastCGI 进程
echo -n "登录："
spawn-fcgi -a 0.0.0.0 -p 10000 -f /app/bin_cgi/login
echo -n "注册："
spawn-fcgi -a 0.0.0.0 -p 10001 -f /app/bin_cgi/register
echo -n "上传："
spawn-fcgi -a 0.0.0.0 -p 10002 -f /app/bin_cgi/upload
echo -n "MD5："
spawn-fcgi -a 0.0.0.0 -p 10003 -f /app/bin_cgi/md5
echo -n "MyFile："
spawn-fcgi -a 0.0.0.0 -p 10004 -f /app/bin_cgi/myfiles
echo -n "DealFile："
spawn-fcgi -a 0.0.0.0 -p 10005 -f /app/bin_cgi/dealfile
echo -n "ShareList："
spawn-fcgi -a 0.0.0.0 -p 10006 -f /app/bin_cgi/sharefiles
echo -n "DealShare："
spawn-fcgi -a 0.0.0.0 -p 10007 -f /app/bin_cgi/dealsharefile
echo -n "SharePicture："
spawn-fcgi -a 0.0.0.0 -p 10008 -f /app/bin_cgi/sharepicture

echo -n "ChunkInit："
spawn-fcgi -a 0.0.0.0 -p 10009 -f /app/bin_cgi/chunk_init
echo -n "ChunkUpload："
CHUNK_UPLOAD_WORKERS=${CHUNK_UPLOAD_WORKERS:-8}
spawn-fcgi -a 0.0.0.0 -p 10010 -F "$CHUNK_UPLOAD_WORKERS" -f /app/bin_cgi/chunk_upload

# chunk_merge 启动时需要连接 tracker，等待 tracker 就绪
echo -n "等待 tracker(172.30.0.3:22122) 就绪..."
for i in $(seq 1 30); do
    if fdfs_monitor /etc/fdfs/client.conf >/dev/null 2>&1; then
        echo "OK"
        break
    fi
    sleep 1
done
echo -n "ChunkMerge："
spawn-fcgi -a 0.0.0.0 -p 10011 -f /app/bin_cgi/chunk_merge

# AI 智能检索
mkdir -p /data/faiss
echo -n "AI："
spawn-fcgi -a 0.0.0.0 -p 10012 -f /app/bin_cgi/ai

# 知识层异步 worker（后台进程）
echo -n "KnowledgeWorker："
/app/bin_cgi/knowledge_worker &
echo "OK"

echo "所有 FastCGI 程序已启动"

# 保持容器运行
tail -f /dev/null
