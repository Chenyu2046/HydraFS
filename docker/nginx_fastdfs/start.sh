#!/bin/bash

# 启动 FastDFS tracker
echo "启动 tracker..."
/usr/bin/fdfs_trackerd /etc/fdfs/tracker.conf start
sleep 3

# 启动 FastDFS storage
echo "启动 storage..."
/usr/bin/fdfs_storaged /etc/fdfs/storage.conf start
echo "等待 storage 初始化 (首次启动需要创建子目录)..."
sleep 15

echo "查看 tracker 是否启动:"
lsof -i:22122 || echo "tracker 未启动"
echo "查看 storage 是否启动:"
lsof -i:23000 || echo "storage 未启动"

# 启动 nginx (前台运行，保持容器存活)
echo "启动 nginx..."
chmod +x /usr/local/nginx/sbin/nginx
/usr/local/nginx/sbin/nginx -g "daemon off;"
