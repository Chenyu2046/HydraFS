# AI云存储系统（AI——YunCunChu）— 项目技术文档

## 1. 项目概述

AI云存储系统是一个基于 **Docker 容器化部署** 的私有云文件管理平台，提供文件上传、下载、分享、以及 **AI 智能语义检索** 等功能。

### 技术栈

| 层级 | 技术 |
|------|------|
| 前端 | React 18 + Ant Design 5 + Emotion CSS-in-JS |
| 后端 | C/C++ FastCGI（spawn-fcgi） |
| Web 服务器 | Nginx 1.20.2（含 fastdfs-nginx-module） |
| 文件存储 | FastDFS v6.06（分布式文件系统） |
| 数据库 | MySQL 8.0 |
| 缓存 | Redis（内嵌在应用容器中） |
| AI 引擎 | 阿里百炼 DashScope API + FAISS v1.7.2 向量索引 |
| 容器化 | Docker Compose |
| 传输安全 | HTTPS（自签名证书） |

### 核心功能

- 用户注册/登录（加盐 MD5 密码哈希）
- 文件上传（普通上传 + 大文件分片上传，支持断点续传）
- 文件秒传（基于 MD5 去重）
- 文件下载、删除、分享
- 共享文件广场 + 下载排行榜
- 图床分享功能（生成提取码）
- AI 智能语义搜索（自然语言查找文件）
- AI 自动图片描述（Qwen-VL 多模态模型）

---

## 2. 系统架构

### 2.1 整体架构图

```
                          ┌──────────────────────────────┐
                          │      客户端（浏览器）          │
                          │   React 18 + Ant Design 5     │
                          └──────────────┬───────────────┘
                                         │ HTTPS (443)
                                         ▼
┌─────────────────────────────────────────────────────────────────────┐
│                  tc_fcgi_nginx_fastdfs (172.30.0.3)                  │
│  ┌─────────────────┐  ┌─────────────────┐  ┌────────────────────┐  │
│  │   Nginx 反向代理  │  │  FastDFS Tracker │  │  FastDFS Storage  │  │
│  │   端口 80/443     │  │  端口 22122      │  │  端口 23000        │  │
│  │  (含 SSL 终端)    │  │                 │  │  (文件实际存储)     │  │
│  └────────┬────────┘  └─────────────────┘  └────────────────────┘  │
│           │ FastCGI                                                  │
└───────────┼─────────────────────────────────────────────────────────┘
            │
            ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    tc_fcgi_app (172.30.0.4)                          │
│  ┌─────────────┐  ┌───────────────────────────────────────────┐    │
│  │   Redis      │  │  13 个 FastCGI 进程 (端口 10000-10012)    │    │
│  │  (本地缓存)  │  │  login / register / upload / md5 /        │    │
│  │             │  │  myfiles / dealfile / sharefiles /          │    │
│  │             │  │  dealsharefile / sharepicture /             │    │
│  │             │  │  chunk_init / chunk_upload / chunk_merge /  │    │
│  │             │  │  ai                                        │    │
│  └─────────────┘  └────────────────────┬──────────────────────┘    │
│                                         │                           │
└─────────────────────────────────────────┼───────────────────────────┘
                                          │
                    ┌─────────────────────┼─────────────────────┐
                    ▼                     ▼                     ▼
        ┌──────────────────┐   ┌──────────────────┐   ┌────────────────┐
        │  tc_fcgi_mysql    │   │  DashScope API   │   │  FAISS 索引     │
        │  (172.30.0.2)     │   │  (阿里百炼云端)   │   │  (本地内存+磁盘) │
        │  MySQL 8.0        │   │  Qwen-VL         │   │  /data/faiss/   │
        │  端口 3306         │   │  text-embedding  │   │  index.bin      │
        └──────────────────┘   └──────────────────┘   └────────────────┘
```

### 2.2 容器编排

项目通过 Docker Compose 编排 3 个容器，使用自定义桥接网络 `172.30.0.0/16`：

| 容器名称 | 服务 | IP 地址 | 对外端口 | 说明 |
|----------|------|---------|---------|------|
| `tc_fcgi_mysql` | MySQL 8.0 | 172.30.0.2 | 3307→3306 | 数据库，持久化卷 `mysql_data` |
| `tc_fcgi_nginx_fastdfs` | Nginx + FastDFS | 172.30.0.3 | 80→80, 443→443 | Web 服务器 + 文件存储，持久化卷 `fastdfs_data` |
| `tc_fcgi_app` | FastCGI + Redis | 172.30.0.4 | 10000-10012 | 后端业务逻辑 |

**启动顺序依赖**：MySQL → Nginx/FastDFS → FastCGI App（通过 healthcheck 保证）

### 2.3 请求处理流程

```
浏览器 → HTTPS 443 (Nginx)
    ├── 静态资源 (/, *.js, *.css) → /app/front/ (React 构建产物)
    ├── 文件下载 (/group[0-9]/M[0-9][0-9]) → FastDFS ngx_fastdfs_module
    └── API 请求 (/api/*) → FastCGI 转发到 172.30.0.4 对应端口
```

---

## 3. 项目目录结构

```
fastcgi_yuncunchu_docker/
├── README.md                   # 本文档
├── ai_search.md                # AI 检索功能详细文档
├── chunked_upload.md           # 大文件分片上传详细文档
├── .gitignore                  # Git 忽略规则（构建产物、日志、node_modules 等）
├── Makefile                    # 编译规则，定义所有 CGI 目标
├── src_cgi/                    # 后端 CGI 源码（C/C++）
│   ├── login_cgi.c             #   用户登录
│   ├── reg_cgi.c               #   用户注册
│   ├── upload_cgi.c            #   文件上传
│   ├── md5_cgi.c               #   文件秒传（MD5 校验）
│   ├── myfiles_cgi.c           #   用户文件列表
│   ├── dealfile_cgi.c          #   文件操作（删除、分享、取消分享、PV）
│   ├── sharefiles_cgi.c        #   共享文件列表
│   ├── dealsharefile_cgi.c     #   共享文件操作（取消分享、转存、PV）
│   ├── sharepicture_cgi.c      #   图床分享
│   ├── chunk_init_cgi.c        #   分片上传初始化
│   ├── chunk_upload_cgi.c      #   分片上传
│   ├── chunk_merge_cgi.c       #   分片合并
│   └── ai_cgi.cpp              #   AI 智能检索（C++）
├── common/                     # 公共模块
│   ├── cfg.c                   #   配置文件解析（读取 cfg.json）
│   ├── cJSON.c                 #   JSON 解析库
│   ├── deal_mysql.c            #   MySQL 操作封装
│   ├── redis_op.c              #   Redis 操作封装
│   ├── make_log.c              #   日志模块
│   ├── util_cgi.c              #   CGI 通用工具（URL 解析、字符串处理等）
│   ├── md5.c                   #   MD5 哈希算法实现
│   ├── des.c                   #   DES 加密（Token 生成）
│   ├── base64.c                #   Base64 编解码
│   ├── dashscope_api.cpp       #   阿里百炼 API 调用封装（C++）
│   └── faiss_wrapper.cpp       #   FAISS 向量索引封装（C++）
├── include/                    # 头文件
│   ├── cfg.h / cJSON.h / deal_mysql.h / redis_op.h / redis_keys.h
│   ├── make_log.h / util_cgi.h / md5.h / des.h / base64.h
│   ├── fdfs_api.h              #   FastDFS 客户端接口
│   ├── dashscope_api.h         #   DashScope API 接口
│   └── faiss_wrapper.h         #   FAISS 索引接口
├── conf/                       # 配置文件（开发环境）
│   ├── cfg.json                #   MySQL、Redis、FastDFS、AI 等配置
│   ├── redis.conf              #   Redis 配置文件
│   ├── tracker.conf            #   FastDFS Tracker 配置
│   ├── storage.conf            #   FastDFS Storage 配置
│   └── client.conf             #   FastDFS Client 配置
├── deps/                       # 预下载的第三方依赖源码包
│   ├── hiredis-v1.0.2.tar.gz
│   ├── libfastcommon-V1.0.43.tar.gz
│   ├── fastdfs-V6.06.tar.gz
│   ├── fastdfs-nginx-module-V1.22.tar.gz
│   └── faiss-v1.7.2.tar.gz
├── picture_bed/                # 前端 React 项目
│   ├── package.json            #   依赖声明
│   ├── package-lock.json       #   依赖锁定
│   ├── public/                 #   静态资源（index.html、favicon 等）
│   └── src/
│       ├── App.js              #   路由定义、全局布局
│       ├── index.js            #   入口文件
│       ├── theme.js            #   主题配置
│       ├── config/
│       │   └── index.js        #   API 地址配置
│       ├── contexts/
│       │   └── AuthContext.js   #   用户认证上下文（React Context）
│       ├── components/
│       │   └── NavBar.js       #   顶部导航栏组件
│       ├── pages/
│       │   ├── Login.js        #   登录/注册页面
│       │   ├── Home.js         #   首页仪表盘 + AI 搜索
│       │   ├── ImageList.js    #   图片管理（瀑布流展示）
│       │   ├── FileList.js     #   文件管理（表格展示 + 上传）
│       │   ├── SharedFiles.js  #   共享文件广场
│       │   └── TopDownloads.js #   下载排行榜
│       └── services/
│           ├── auth.js         #   登录/注册 API
│           ├── images.js       #   文件操作 API（上传、删除、分享等）
│           ├── share.js        #   共享文件 API
│           ├── ai.js           #   AI 检索 API（描述、搜索、重建索引）
│           └── dashboard.js    #   仪表盘数据 API
└── docker/                     # Docker 构建配置
    ├── docker-compose.yaml     #   容器编排定义
    ├── setup.sh                #   初始构建脚本
    ├── configure_mirror.sh     #   镜像源配置
    ├── mysql/
    │   ├── dockerfile          #   MySQL 镜像（基于 mysql:8.0）
    │   └── init.sql            #   数据库初始化 SQL（建库建表）
    ├── nginx_fastdfs/
    │   ├── dockerfile          #   Nginx+FastDFS 镜像（多阶段构建，含前端编译）
    │   ├── nginx.conf          #   Nginx 配置（HTTPS + FastCGI 路由）
    │   ├── start.sh            #   容器启动脚本（tracker→storage→nginx）
    │   ├── tracker.conf        #   FastDFS Tracker 配置
    │   ├── storage.conf        #   FastDFS Storage 配置
    │   ├── client.conf         #   FastDFS Client 配置
    │   └── mod_fastdfs.conf    #   fastdfs-nginx-module 配置
    └── fastcgi_app/
        ├── dockerfile          #   FastCGI 应用镜像（编译 C/C++ + 安装依赖）
        ├── start.sh            #   容器启动脚本（Redis + 13个 FastCGI 进程）
        └── cfg.json            #   Docker 环境配置文件
```

---

## 4. 后端 CGI 模块详解

后端采用 C/C++ 编写，通过 `spawn-fcgi` 启动为 FastCGI 常驻进程，每个模块监听独立端口。

### 4.1 模块端口分配

| 端口 | CGI 模块 | 源文件 | API 路由 | 功能说明 |
|------|----------|--------|---------|----------|
| 10000 | login | login_cgi.c | `/api/login` | 用户登录验证 |
| 10001 | register | reg_cgi.c | `/api/reg` | 用户注册 |
| 10002 | upload | upload_cgi.c | `/api/upload` | 文件上传（multipart/form-data） |
| 10003 | md5 | md5_cgi.c | `/api/md5` | 文件秒传（MD5 检测） |
| 10004 | myfiles | myfiles_cgi.c | `/api/myfiles` | 用户文件列表查询 |
| 10005 | dealfile | dealfile_cgi.c | `/api/dealfile` | 文件操作：删除/分享/取消分享/PV |
| 10006 | sharefiles | sharefiles_cgi.c | `/api/sharefiles` | 共享文件列表 |
| 10007 | dealsharefile | dealsharefile_cgi.c | `/api/dealsharefile` | 共享文件操作：转存/取消/PV |
| 10008 | sharepicture | sharepicture_cgi.c | `/api/sharepic` | 图床分享功能 |
| 10009 | chunk_init | chunk_init_cgi.c | `/api/chunk_init` | 分片上传初始化 |
| 10010 | chunk_upload | chunk_upload_cgi.c | `/api/chunk_upload` | 分片上传 |
| 10011 | chunk_merge | chunk_merge_cgi.c | `/api/chunk_merge` | 分片合并 |
| 10012 | ai | ai_cgi.cpp | `/api/ai` | AI 智能检索 |

### 4.2 公共模块说明

| 文件 | 说明 |
|------|------|
| `cfg.c` | 读取 `conf/cfg.json` 配置文件，按 section/key 获取值 |
| `cJSON.c` | 轻量级 JSON 解析库（第三方） |
| `deal_mysql.c` | MySQL 连接管理、查询执行、结果获取封装 |
| `redis_op.c` | Redis 连接管理、字符串/哈希操作封装 |
| `make_log.c` | 日志记录模块，按日期输出到 `logs/` 目录 |
| `util_cgi.c` | CGI 工具函数：URL 参数解析、字符串处理、Token 验证、multipart 解析等 |
| `md5.c` | MD5 哈希算法 C 实现 |
| `des.c` | DES 加密算法（用于生成登录 Token） |
| `base64.c` | Base64 编解码（Token 编码） |
| `dashscope_api.cpp` | 阿里百炼 API 封装：图片描述（Qwen-VL）+ 文本向量化（text-embedding-v3） |
| `faiss_wrapper.cpp` | FAISS 向量索引封装：创建/加载/添加/搜索/保存/重置索引 |

### 4.3 登录认证流程

```
前端                              后端
 │                                 │
 │  密码 → MD5(password)           │
 │  POST { user, pwd: MD5值 }     │
 │ ───────────────────────────────→│
 │                                 │  查询 user_info 获取 stored_hash + salt
 │                                 │  计算 MD5(salt + 收到的MD5)
 │                                 │  比较 computed_hash vs stored_hash
 │                                 │  匹配 → 生成 Token (DES加密+Base64)
 │                                 │  存入 Redis (key: token, value: user, TTL)
 │  ← { code:0, token: "xxx" }    │
 │                                 │
```

- 前端发送 `MD5(password)` 而非明文（HTTPS 保护传输层安全）
- 后端存储 `MD5(salt + MD5(password))`，salt 为 16 位随机十六进制字符串
- Token 基于 DES 加密 + Base64 编码生成，存入 Redis 设置过期时间

### 4.4 文件上传流程

**普通上传**（文件 <= 10MB）：

```
前端计算文件 MD5 → POST /api/md5（秒传检测）
    → 命中已存在文件：直接复用 file_info 记录并写入 user_file_list
    → 未命中：构造 FormData（file + user + md5 + size）
    → POST /api/upload (multipart/form-data)
    → 后端解析 multipart 数据 → 上传到 FastDFS → 获取 file_id 和 URL
    → 存入 file_info 表 + user_file_list 表
    → 异步调用 AI 生成文件描述
```

**分片上传**（文件 > 10MB）：

```
Step 1: POST /api/chunk_init  → 初始化，返回 uploadedChunks 已上传分片列表（断点续传）
Step 2: POST /api/chunk_upload?md5=xxx&index=N  → 逐个上传分片
Step 3: POST /api/chunk_merge  → 合并分片或按 MD5 直接复用已有文件 → 入库
```

- 分片大小：10MB
- 支持断点续传（已上传分片不重传）
- 上传进度：分片占 90%，合并占 10%

---

## 5. 前端架构

### 5.1 技术选型

| 库 | 版本 | 用途 |
|----|------|------|
| React | 18.x | UI 框架 |
| Ant Design | 5.23.x | UI 组件库 |
| React Router DOM | 7.x | 路由管理 |
| Emotion | 11.x | CSS-in-JS 样式 |
| SparkMD5 | 3.x | 客户端文件 MD5 计算 |
| Axios | 1.7.x | HTTP 请求 |

### 5.2 路由结构

| 路径 | 页面组件 | 说明 | 需要登录 |
|------|----------|------|----------|
| `/login` | Login.js | 登录/注册 | 否 |
| `/` | Home.js | 首页仪表盘 + AI 搜索 | 是 |
| `/images` | ImageList.js | 图片管理（卡片式瀑布流） | 是 |
| `/files` | FileList.js | 文件管理（表格+上传） | 是 |
| `/shared` | SharedFiles.js | 共享文件广场 | 是 |
| `/top-downloads` | TopDownloads.js | 下载排行榜 | 是 |

### 5.3 状态管理

- **认证状态**：React Context（`AuthContext.js`），用户信息持久化到 `localStorage`
- **页面状态**：各组件内部 `useState` 管理
- **无全局状态库**（无 Redux/MobX）

### 5.4 页面功能说明

| 页面 | 核心功能 |
|------|----------|
| **Login** | 登录（MD5 加密密码）、注册（昵称/邮箱/手机号） |
| **Home** | 文件统计仪表盘（总文件数、下载次数、已分享、存储空间）、最近上传列表、AI 智能搜索（配置 API Key + 语义搜索 + 重建 AI 描述） |
| **ImageList** | 图片卡片瀑布流展示、上传、预览、分享、删除 |
| **FileList** | 文件表格展示、拖拽上传（支持大文件分片）、操作按钮 |
| **SharedFiles** | 共享文件广场、图片预览、转存到自己的文件列表 |
| **TopDownloads** | 共享文件按下载量排行、转存功能 |

### 5.5 API 调用封装

| 服务文件 | 封装的功能 |
|----------|-----------|
| `auth.js` | `loginUser()` 登录、`registerUser()` 注册 |
| `images.js` | `fetchUserImages()` 获取文件列表、`uploadImage()` 普通上传、`uploadChunked()` 分片上传、`deleteImage()` 删除、`shareFile()` / `cancelShareFile()` 分享操作、`pvFile()` 下载计数 |
| `share.js` | `fetchSharedFiles()` 共享列表、`fetchSharedFilesRanking()` 排行榜、`saveSharedFile()` 转存、`pvSharedFile()` 下载计数 |
| `ai.js` | `fetchApiKey()` / `saveApiKey()` API Key 管理、`describeFile()` / `describeFileByMd5()` AI 描述生成、`aiSearch()` 语义搜索、`rebuildIndex()` 重建索引 |

---

## 6. 数据库设计

数据库名：`yuncunchu`，字符集 UTF-8。

### 6.1 表结构

#### `user_info` — 用户信息表

| 字段 | 类型 | 说明 |
|------|------|------|
| id | bigint | 自增主键 |
| user_name | varchar(32) | 用户名（唯一） |
| nick_name | varchar(32) | 昵称（唯一，UTF8MB4） |
| password | varchar(32) | 密码哈希 `MD5(salt + MD5(password))` |
| salt | varchar(32) | 16 位随机十六进制盐值 |
| phone | varchar(16) | 手机号 |
| email | varchar(64) | 邮箱 |
| api_key | varchar(256) | 用户的 DashScope API Key |
| create_time | timestamp | 注册时间 |

#### `file_info` — 文件信息表

| 字段 | 类型 | 说明 |
|------|------|------|
| id | bigint | 自增主键 |
| md5 | varchar(256) | 文件 MD5 |
| file_id | varchar(256) | FastDFS 文件 ID |
| url | varchar(512) | 文件访问 URL |
| size | bigint | 文件大小（字节） |
| type | varchar(32) | 文件扩展名 |
| count | int | 引用计数（多用户共享同一文件） |

#### `user_file_list` — 用户文件关联表

| 字段 | 类型 | 说明 |
|------|------|------|
| id | int | 自增主键 |
| user | varchar(32) | 所属用户 |
| md5 | varchar(256) | 文件 MD5 |
| file_name | varchar(128) | 文件名 |
| shared_status | int | 共享状态（0=未共享，1=已共享） |
| pv | int | 下载次数 |
| create_time | timestamp | 上传时间 |

#### `share_file_list` — 共享文件列表

| 字段 | 类型 | 说明 |
|------|------|------|
| id | int | 自增主键 |
| user | varchar(32) | 分享者 |
| md5 | varchar(256) | 文件 MD5 |
| file_name | varchar(128) | 文件名 |
| pv | int | 下载量 |
| create_time | timestamp | 分享时间 |

#### `share_picture_list` — 图床分享列表

| 字段 | 类型 | 说明 |
|------|------|------|
| id | int | 自增主键 |
| user | varchar(32) | 所属用户 |
| filemd5 | varchar(256) | 文件 MD5 |
| file_name | varchar(128) | 文件名 |
| urlmd5 | varchar(256) | 图床 URL 的 MD5 |
| key | varchar(8) | 提取码 |
| pv | int | 下载量 |
| create_time | timestamp | 创建时间 |

#### `user_file_count` — 用户文件数量表

| 字段 | 类型 | 说明 |
|------|------|------|
| id | int | 自增主键 |
| user | varchar(128) | 用户名（唯一） |
| count | int | 文件数量 |

#### `file_ai_desc` — AI 文件描述与向量表

| 字段 | 类型 | 说明 |
|------|------|------|
| id | bigint | 自增主键 |
| md5 | varchar(256) | 文件 MD5（唯一索引） |
| description | text | AI 生成的文件描述 |
| embedding | mediumblob | 1024 维 float 向量（4096 字节） |
| faiss_id | int | FAISS 索引中的 ID |
| model | varchar(64) | 使用的 AI 模型名 |
| status | tinyint | 0=待处理 1=完成 2=失败 |
| create_time | timestamp | 创建时间 |

### 6.2 表关系

```
user_info.user_name ──1:N──→ user_file_list.user
file_info.md5 ──1:N──→ user_file_list.md5
file_info.md5 ──1:1──→ file_ai_desc.md5
user_file_list ──1:1──→ share_file_list (通过 user + md5)
```

---

## 7. 安全机制

### 7.1 密码安全

```
注册流程：
  前端: MD5(password) → 发送到后端
  后端: 生成 salt (16位随机hex) → 计算 MD5(salt + MD5(password)) → 存储 hash + salt

登录流程：
  前端: MD5(password) → 发送到后端
  后端: 查询 salt → 计算 MD5(salt + 收到的MD5) → 与存储的 hash 对比
```

### 7.2 传输安全

- 全站 HTTPS（自签名 RSA 2048 证书，10 年有效期）
- HTTP 80 端口自动 301 重定向到 HTTPS 443
- SSL 协议：TLSv1.2 / TLSv1.3
- 密码套件：HIGH:!aNULL:!MD5

### 7.3 认证机制

- 登录成功后生成 Token（DES 加密 + Base64 编码）
- Token 存储在 Redis 中（设有过期时间）
- 每个需要认证的 API 请求都需要携带 Token
- Token 验证失败返回 code=4，前端自动跳转登录页

---

## 8. AI 智能检索

AI 检索是本项目的亮点功能，详细技术文档请参考 [ai_search.md](ai_search.md)。

### 8.1 功能概述

| 功能 | 模型 | 说明 |
|------|------|------|
| 图片描述 | qwen-vl-plus | 自动识别图片内容生成中文描述 |
| 文本向量化 | text-embedding-v3 | 将文本转为 1024 维向量 |
| 向量检索 | FAISS IndexFlatIP | 余弦相似度搜索，Top-10，阈值 0.45 |

### 8.2 支持的文件类型

- **图片**（调用 AI 描述）：png, jpg, jpeg, gif, bmp, webp, svg
- **文本文件**（提取内容）：txt, md, csv, json, xml, html, log, c, cpp, h, py, js, css, java
- **文档**（特殊解析）：docx（解压 XML 提取文本）
- **其他**：使用文件名+类型作为描述

### 8.3 API Key 管理

- 每用户独立配置阿里百炼 API Key
- 优先级：请求体 > 数据库 > 全局配置文件
- 存储在 MySQL `user_info.api_key` 字段

---

## 9. 配置与部署

### 9.1 环境要求

- Docker 20.10+
- Docker Compose v2+
- 服务器需有公网 IP（AI 图片描述需要 DashScope 能访问到图片 URL）
- 建议磁盘空间 > 20GB（FastDFS 预留 10% 空间）

### 9.2 配置文件

#### `docker/fastcgi_app/cfg.json` — 核心配置

```json
{
  "redis": {
    "ip": "127.0.0.1",          // Redis 运行在应用容器内
    "port": "6379"
  },
  "mysql": {
    "ip": "172.30.0.2",         // MySQL 容器 IP
    "port": "3306",
    "database": "yuncunchu",
    "user": "root",
    "password": "123456"        // ⚠️ 生产环境请修改
  },
  "dfs_path": {
    "client": "/etc/fdfs/client.conf"
  },
  "web_server": {
    "ip": "172.30.0.3",         // Nginx 容器 IP（内部）
    "port": "80"
  },
  "public_server": {
    "ip": "xxx.xx.xx.xx",     // ⚠️ 修改为你的公网 IP
    "port": "8080"              // 公网端口
  },
  "dashscope": {
    "api_key": "",              // 全局默认 API Key（可选）
    "embedding_model": "text-embedding-v3",
    "vl_model": "qwen-vl-plus",
    "embedding_dimension": "1024"
  },
  "faiss": {
    "index_path": "/data/faiss/index.bin"
  }
}
```

#### 部署前必须修改的配置

代码中所有 `xxx.xx.xx.xx` 均为公网 IP 占位符，部署前需替换为你的服务器实际公网 IP：

| 文件 | 位置 | 说明 |
|------|------|------|
| `docker/fastcgi_app/cfg.json` | `public_server.ip` | AI 图片描述需要公网可访问的图片 URL |
| `docker/nginx_fastdfs/dockerfile` | openssl 命令中的两处 IP | SSL 证书绑定的 IP 地址 |
| `conf/cfg.json` | `public_server.ip` | 本地开发环境配置（非 Docker 部署时使用） |

可选修改：

| 配置项 | 文件 | 说明 |
|--------|------|------|
| MySQL 密码 | `docker/fastcgi_app/cfg.json` + `docker/mysql/dockerfile` | 生产环境建议修改默认密码 `123456` |

### 9.3 一键部署步骤

```bash
# 1. 克隆项目
git clone <repo-url>
cd fastcgi_yuncunchu_docker

# 2. 修改配置（⚠️ 部署前必须）
#    将以下文件中的 xxx.xx.xx.xx 替换为你的服务器公网 IP：
#    - docker/fastcgi_app/cfg.json → public_server.ip（AI 图片描述需要公网可访问的 URL）
#    - docker/nginx_fastdfs/dockerfile → SSL 证书中的 IP 地址（两处）
#    如果不配置 AI 功能，仅第二项影响浏览器证书警告，其他功能正常使用

# 3. 构建并启动
cd docker
docker compose up -d --build

# 4. 查看容器状态
docker compose ps

# 5. 查看日志
docker compose logs -f
```

### 9.4 构建过程说明

`docker compose up --build` 会依次执行：

1. **MySQL 容器**：基于 `mysql:8.0`，执行 `init.sql` 初始化数据库和表
2. **Nginx+FastDFS 容器**（多阶段构建）：
   - 阶段 1：`node:18-alpine` 中 `npm install && npm run build` 编译 React 前端
   - 阶段 2：`ubuntu:20.04` 中编译安装 libfastcommon → FastDFS → fastdfs-nginx-module → Nginx → 生成 SSL 证书 → 复制前端构建产物
3. **FastCGI 应用容器**：`ubuntu:20.04` 中编译安装 hiredis → libfastcommon → FastDFS → FAISS → 编译项目 C/C++ 源码

### 9.5 服务启动流程

容器内部启动顺序：

**Nginx+FastDFS 容器**（`start.sh`）：
```
fdfs_trackerd 启动 → 等待 3 秒
fdfs_storaged 启动 → 等待 15 秒（首次需创建子目录）
nginx 前台启动（保持容器存活）
```

**FastCGI 应用容器**（`start.sh`）：
```
redis-server 启动（后台）
spawn-fcgi 启动 13 个 FastCGI 进程（端口 10000-10012）
mkdir 创建必要目录
tail -f /dev/null（保持容器存活）
```

### 9.6 常用运维命令

```bash
# 进入 docker 目录
cd docker

# 重启所有容器
docker compose restart

# 仅重启应用容器（代码更新后）
docker compose restart fastcgi_app

# 重建并启动（代码变更后）
docker compose up -d --build

# 查看应用容器日志
docker compose logs -f fastcgi_app

# 进入应用容器调试
docker exec -it tc_fcgi_app bash

# 进入 MySQL 容器
docker exec -it tc_fcgi_mysql mysql -u root -p123456 yuncunchu

# 查看 FastCGI 进程状态
docker exec tc_fcgi_app lsof -i -P | grep LISTEN

# 手动重启某个 CGI 进程（如 upload 崩溃）
docker exec tc_fcgi_app spawn-fcgi -a 0.0.0.0 -p 10002 -f /app/bin_cgi/upload

# 查看磁盘使用情况（FastDFS 预留 10%）
docker exec tc_fcgi_nginx_fastdfs df -h

# 清理 Docker 占用空间
docker system prune -a
docker volume prune
```

### 9.7 访问地址

部署完成后通过以下地址访问：

```
https://<你的公网IP>/
```

- HTTP 80 端口会自动重定向到 HTTPS 443
- 首次访问自签名证书需在浏览器中手动信任

---

## 10. API 接口汇总

### 10.1 认证相关

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/login` | POST | 用户登录，返回 Token |
| `/api/reg` | POST | 用户注册 |

### 10.2 文件管理

| 接口 | 方法 | 参数 | 说明 |
|------|------|------|------|
| `/api/upload` | POST | multipart/form-data | 普通文件上传 |
| `/api/md5` | POST | JSON | 文件秒传检测，命中时直接复用已有文件 |
| `/api/myfiles?cmd=normal` | POST | JSON | 获取用户文件列表 |
| `/api/dealfile?cmd=del` | POST | JSON | 删除文件 |
| `/api/dealfile?cmd=share` | POST | JSON | 分享文件 |
| `/api/dealfile?cmd=pv` | POST | JSON | 更新下载计数 |

### 10.3 分片上传

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/chunk_init` | POST | 初始化分片上传，返回 `uploadedChunks` 已上传分片列表 |
| `/api/chunk_upload?md5=xxx&index=N` | POST | 上传单个分片（body=二进制数据） |
| `/api/chunk_merge` | POST | 合并所有分片 |

### 10.4 共享文件

| 接口 | 方法 | 参数 | 说明 |
|------|------|------|------|
| `/api/sharefiles?cmd=normal` | POST | JSON | 共享文件列表 |
| `/api/sharefiles?cmd=pvdesc` | POST | JSON | 下载量排行 |
| `/api/dealsharefile?cmd=save` | POST | JSON | 转存文件 |
| `/api/dealsharefile?cmd=cancel` | POST | JSON | 取消分享 |
| `/api/dealsharefile?cmd=pv` | POST | JSON | 更新下载计数 |
| `/api/sharepic` | POST | JSON | 图床分享 |

### 10.5 AI 智能检索

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/ai?cmd=describe` | POST | 生成文件 AI 描述 + 向量 |
| `/api/ai?cmd=search` | POST | 语义搜索 |
| `/api/ai?cmd=rebuild` | POST | 重建 FAISS 索引 |
| `/api/ai?cmd=get_apikey` | POST | 获取用户 API Key |
| `/api/ai?cmd=set_apikey` | POST | 保存用户 API Key |

---

## 11. 数据持久化

| 数据 | 持久化方式 | Docker Volume |
|------|-----------|---------------|
| MySQL 数据库 | Docker 命名卷 | `mysql_data:/var/lib/mysql` |
| FastDFS 文件 | Docker 命名卷 | `fastdfs_data:/fastdfs_data_and_log` |
| FAISS 索引 | 容器内磁盘 `/data/faiss/` + MySQL 备份 | 容器重建需 rebuild |
| Redis 缓存 | 仅内存（Token 等临时数据） | 容器重启后丢失 |
| 运行日志 | 容器内 `/app/logs/` | 容器重建后丢失 |

---

## 12. 第三方依赖

项目依赖的第三方库源码预先下载在 `deps/` 目录，Docker 构建时编译安装：

| 库 | 版本 | 用途 |
|----|------|------|
| hiredis | 1.0.2 | Redis C 客户端 |
| libfastcommon | 1.0.43 | FastDFS 基础库 |
| FastDFS | 6.06 | 分布式文件系统 |
| fastdfs-nginx-module | 1.22 | Nginx 访问 FastDFS 文件 |
| FAISS | 1.7.2 | Facebook 向量相似度搜索库 |
| Nginx | 1.20.2 | Web 服务器（构建时在线下载） |
| OpenBLAS | 系统包 | FAISS 线性代数后端 |
| libcurl | 系统包 | HTTP 请求（DashScope API 调用） |
| MySQL Client | 系统包 | MySQL C API |
| libfcgi | 系统包 | FastCGI 协议库 |
