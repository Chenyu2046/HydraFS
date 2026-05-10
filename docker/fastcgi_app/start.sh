#!/bin/bash

# 启动 Redis
redis-server --daemonize yes
echo "Redis 已启动"

# 等待 Redis 就绪
sleep 1

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
spawn-fcgi -a 0.0.0.0 -p 10010 -f /app/bin_cgi/chunk_upload

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

# 创建分片临时目录
mkdir -p /tmp/chunks

# 创建 FastDFS 客户端日志目录
mkdir -p /fastdfs_data_and_log/client

echo "所有 FastCGI 程序已启动"

# 保持容器运行
tail -f /dev/null
