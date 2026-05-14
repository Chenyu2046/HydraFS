#!/bin/bash

# 获取脚本所在目录的绝对路径，确保在任何地方执行都能正确定位
PROJECT_ROOT=$(cd "$(dirname "$0")" && pwd)
cd "$PROJECT_ROOT/docker"

echo "开始构建并更新前端容器 (nginx_fastdfs)..."

# 重新构建 frontend/nginx 镜像
sudo docker compose build nginx_fastdfs

# 停止并重新启动容器
sudo docker compose stop nginx_fastdfs
sudo docker compose up -d nginx_fastdfs

echo "==========================================="
echo "前端编译和部署已完成！请在浏览器强制刷新页面。"
echo "==========================================="
