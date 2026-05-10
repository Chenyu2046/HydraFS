#!/bin/bash
#
# 云存储项目 FastCGI 版 - 一键部署脚本
# 使用方法: chmod +x setup.sh && sudo ./setup.sh
#

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # 无颜色

log_info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# 检查是否以 root 权限运行
check_root() {
    if [ "$EUID" -ne 0 ]; then
        log_error "请使用 sudo 运行此脚本: sudo ./setup.sh"
        exit 1
    fi
}

# 检测系统类型
detect_os() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        OS=$ID
        OS_VERSION=$VERSION_ID
    else
        log_error "无法检测操作系统类型"
        exit 1
    fi
    log_info "检测到操作系统: $OS $OS_VERSION"
}

# 安装 Docker
install_docker() {
    if command -v docker &> /dev/null; then
        log_info "Docker 已安装: $(docker --version)"
        return 0
    fi

    log_info "正在安装 Docker..."

    case $OS in
        ubuntu|debian)
            apt-get update -y
            apt-get install -y ca-certificates curl gnupg lsb-release

            # 添加 Docker 官方 GPG key
            install -m 0755 -d /etc/apt/keyrings
            if [ ! -f /etc/apt/keyrings/docker.gpg ]; then
                curl -fsSL https://download.docker.com/linux/$OS/gpg | gpg --dearmor -o /etc/apt/keyrings/docker.gpg
                chmod a+r /etc/apt/keyrings/docker.gpg
            fi

            # 添加 Docker 仓库
            echo \
              "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/$OS \
              $(lsb_release -cs) stable" | tee /etc/apt/sources.list.d/docker.list > /dev/null

            apt-get update -y
            apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
            ;;
        centos|rhel|fedora)
            yum install -y yum-utils
            yum-config-manager --add-repo https://download.docker.com/linux/centos/docker-ce.repo
            yum install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
            ;;
        *)
            log_warn "未知系统，尝试使用官方安装脚本..."
            curl -fsSL https://get.docker.com | sh
            ;;
    esac

    log_info "Docker 安装完成"
}

# 启动 Docker 服务
start_docker() {
    if ! systemctl is-active --quiet docker; then
        log_info "正在启动 Docker 服务..."
        systemctl start docker
        systemctl enable docker
    fi
    log_info "Docker 服务运行中"
}

# 检查 docker compose 命令
check_compose() {
    # 优先使用新版 docker compose (插件模式)
    if docker compose version &> /dev/null; then
        COMPOSE_CMD="docker compose"
        log_info "使用 docker compose (插件模式): $(docker compose version)"
        return 0
    fi

    # 其次使用旧版 docker-compose
    if command -v docker-compose &> /dev/null; then
        COMPOSE_CMD="docker-compose"
        log_info "使用 docker-compose: $(docker-compose --version)"
        return 0
    fi

    # 都没有，安装 compose 插件
    log_info "正在安装 docker compose 插件..."
    case $OS in
        ubuntu|debian)
            apt-get install -y docker-compose-plugin 2>/dev/null || true
            ;;
        centos|rhel|fedora)
            yum install -y docker-compose-plugin 2>/dev/null || true
            ;;
    esac

    # 再次检查
    if docker compose version &> /dev/null; then
        COMPOSE_CMD="docker compose"
        log_info "docker compose 插件安装成功"
        return 0
    fi

    # 最后手动下载 docker-compose
    log_info "正在手动下载 docker-compose..."
    COMPOSE_VERSION=$(curl -s https://api.github.com/repos/docker/compose/releases/latest | grep -oP '"tag_name": "\K(.*)(?=")')
    if [ -z "$COMPOSE_VERSION" ]; then
        COMPOSE_VERSION="v2.24.0"
    fi
    curl -L "https://github.com/docker/compose/releases/download/${COMPOSE_VERSION}/docker-compose-$(uname -s)-$(uname -m)" \
        -o /usr/local/bin/docker-compose
    chmod +x /usr/local/bin/docker-compose
    COMPOSE_CMD="docker-compose"
    log_info "docker-compose 安装完成: $(docker-compose --version)"
}

# 配置 Docker 镜像加速器（解决国内网络问题）
setup_mirror() {
    DAEMON_JSON="/etc/docker/daemon.json"

    # 检查是否已配置镜像加速
    if [ -f "$DAEMON_JSON" ] && grep -q "registry-mirrors" "$DAEMON_JSON"; then
        log_info "Docker 镜像加速器已配置"
        return 0
    fi

    # 测试 Docker Hub 连通性
    if curl -s --connect-timeout 5 https://registry-1.docker.io/v2/ &>/dev/null; then
        log_info "Docker Hub 可直接访问，跳过镜像加速配置"
        return 0
    fi

    log_warn "Docker Hub 无法直接访问，正在配置镜像加速器..."

    # 备份已有配置
    if [ -f "$DAEMON_JSON" ]; then
        cp "$DAEMON_JSON" "${DAEMON_JSON}.bak.$(date +%s)"
    fi

    mkdir -p /etc/docker
    cat > "$DAEMON_JSON" <<'MIRROR_EOF'
{
    "registry-mirrors": [
        "https://docker.1ms.run",
        "https://docker.xuanyuan.me",
        "https://docker.m.daocloud.io"
    ]
}
MIRROR_EOF

    # 重启 Docker 使配置生效
    systemctl daemon-reload
    systemctl restart docker
    sleep 2

    if systemctl is-active --quiet docker; then
        log_info "Docker 镜像加速器配置完成"
    else
        log_error "Docker 重启失败，请检查配置"
        exit 1
    fi
}

# 将当前用户添加到 docker 组
setup_user_group() {
    REAL_USER=${SUDO_USER:-$USER}
    if [ "$REAL_USER" != "root" ]; then
        if ! groups "$REAL_USER" | grep -q docker; then
            log_info "将用户 $REAL_USER 添加到 docker 组..."
            usermod -aG docker "$REAL_USER"
            log_warn "用户组变更将在重新登录后生效"
        fi
    fi
}

# 构建并启动服务
deploy() {
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    cd "$SCRIPT_DIR"

    log_info "当前目录: $(pwd)"

    # 检查 docker-compose.yaml 是否存在
    if [ ! -f "docker-compose.yaml" ]; then
        log_error "未找到 docker-compose.yaml 文件"
        exit 1
    fi

    log_info "正在构建镜像 (首次构建可能需要较长时间)..."
    $COMPOSE_CMD build

    log_info "正在启动服务..."
    $COMPOSE_CMD up -d

    log_info "等待服务就绪..."
    sleep 10

    # 检查服务状态
    log_info "服务状态:"
    $COMPOSE_CMD ps
}

# 打印部署信息
print_info() {
    echo ""
    echo "========================================"
    log_info "部署完成!"
    echo "========================================"
    echo ""
    echo "  服务信息:"
    echo "  ----------------------------------------"
    echo "  前端页面:   http://<服务器IP>:8080"
    echo "  MySQL:      172.30.0.2:3306 (用户: root, 密码: 123456)"
    echo "  Nginx:      172.30.0.3:80"
    echo "  FastCGI:    172.30.0.4:10000-10008"
    echo "  ----------------------------------------"
    echo ""
    echo "  常用命令:"
    echo "  ----------------------------------------"
    echo "  查看状态:   cd $(pwd) && $COMPOSE_CMD ps"
    echo "  查看日志:   cd $(pwd) && $COMPOSE_CMD logs -f"
    echo "  停止服务:   cd $(pwd) && $COMPOSE_CMD down"
    echo "  重启服务:   cd $(pwd) && $COMPOSE_CMD restart"
    echo "  重新构建:   cd $(pwd) && $COMPOSE_CMD up -d --build"
    echo "  ----------------------------------------"
    echo ""
}

# ======================== 主流程 ========================
main() {
    echo ""
    echo "========================================"
    echo "  云存储项目 FastCGI 版 - 一键部署"
    echo "========================================"
    echo ""

    check_root
    detect_os
    install_docker
    start_docker
    check_compose
    setup_mirror
    setup_user_group
    deploy
    print_info
}

main "$@"
