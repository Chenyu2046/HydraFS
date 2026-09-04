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

# Nginx 配置中包含两个 Gateway 的上游地址；等待其 DNS 别名出现，避免
# Compose 并行启动时因为容器尚未加入网络而启动失败。
echo "等待 HydraStore Gateway 加入网络..."
for gateway in storage_gateway_1 storage_gateway_2; do
    for attempt in $(seq 1 60); do
        if getent hosts "$gateway" >/dev/null 2>&1; then
            break
        fi
        sleep 1
    done
    getent hosts "$gateway" || {
        echo "Gateway DNS 未就绪: $gateway"
        exit 1
    }
done

# 启动 nginx (前台运行，保持容器存活)
echo "启动 nginx..."
chmod +x /usr/local/nginx/sbin/nginx
/usr/local/nginx/sbin/nginx -g "daemon off;"
