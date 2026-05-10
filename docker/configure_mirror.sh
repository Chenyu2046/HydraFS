#!/bin/bash
#
# 配置 Docker 镜像加速器（解决国内无法访问 Docker Hub 的问题）
# 使用方法: sudo ./configure_mirror.sh
#

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

if [ "$EUID" -ne 0 ]; then
    log_error "请使用 sudo 运行: sudo ./configure_mirror.sh"
    exit 1
fi

DAEMON_JSON="/etc/docker/daemon.json"

# 备份已有配置
if [ -f "$DAEMON_JSON" ]; then
    cp "$DAEMON_JSON" "${DAEMON_JSON}.bak.$(date +%s)"
    log_info "已备份原配置"
fi

log_info "正在配置 Docker 镜像加速器..."

cat > "$DAEMON_JSON" <<'EOF'
{
    "registry-mirrors": [
        "https://docker.1ms.run",
        "https://docker.xuanyuan.me",
        "https://docker.m.daocloud.io"
    ]
}
EOF

log_info "配置文件已写入: $DAEMON_JSON"

# 重启 Docker
log_info "正在重启 Docker 服务..."
systemctl daemon-reload
systemctl restart docker

# 验证
sleep 2
if systemctl is-active --quiet docker; then
    log_info "Docker 服务已重启"
    log_info "当前镜像加速器配置:"
    docker info 2>/dev/null | grep -A 5 "Registry Mirrors" || true
    echo ""
    log_info "配置完成! 请重新运行 docker compose build"
else
    log_error "Docker 重启失败，请检查配置"
    exit 1
fi
