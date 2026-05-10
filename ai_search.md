# AI 搜索功能技术文档

## 目录

- [1. 系统架构总览](#1-系统架构总览)
- [2. 功能入口与部署](#2-功能入口与部署)
- [3. 数据结构](#3-数据结构)
- [4. API Key 管理机制](#4-api-key-管理机制)
- [5. describe 命令详解](#5-describe-命令详解)
- [6. search 命令详解](#6-search-命令详解)
- [7. rebuild 命令详解](#7-rebuild-命令详解)
- [8. 索引维护机制](#8-索引维护机制)
- [9. 缓存同步机制](#9-缓存同步机制)
- [10. FAISS 封装层](#10-faiss-封装层)
- [11. DashScope API 封装](#11-dashscope-api-封装)
- [12. 文件描述生成策略](#12-文件描述生成策略)
- [13. 文件删除时的 AI 清理流程](#13-文件删除时的-ai-清理流程)
- [14. 前端交互](#14-前端交互)
- [15. 配置说明](#15-配置说明)
- [16. 相关文件索引](#16-相关文件索引)

---

## 1. 系统架构总览

### 1.1 整体架构图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          浏览器 (React 前端)                            │
│                                                                         │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐                   │
│  │  AI 搜索 UI  │  │ API Key 管理 │  │  重建按钮    │                   │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘                   │
│         │                 │                  │                           │
│         │    localStorage │                  │                           │
│         ▼                 ▼                  ▼                           │
│  ┌─────────────────────────────────────────────────────────────┐        │
│  │              services/ai.js (API 服务层)                     │        │
│  │  aiSearch()  │  saveApiKey()  │  describeFileByMd5()        │        │
│  │  fetchApiKey()               │  rebuildIndex()              │        │
│  └──────────────────────┬──────────────────────────────────────┘        │
└─────────────────────────┼───────────────────────────────────────────────┘
                          │ HTTP POST
                          ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    Nginx (HTTPS 443 → FastCGI)                          │
│                                                                         │
│    location /api/ai  →  fastcgi_pass 172.30.0.4:10012                   │
│                         fastcgi_read_timeout 600s                       │
└─────────────────────────┼───────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                FastCGI 容器 (172.30.0.4)                                │
│                                                                         │
│  ┌──────────────────────────────────────────────┐                       │
│  │         ai_cgi.cpp  (端口 10012)              │                       │
│  │                                               │                       │
│  │  ┌────────────┐ ┌────────────┐ ┌────────────┐│                       │
│  │  │  describe   │ │  search    │ │  rebuild   ││                       │
│  │  └─────┬──────┘ └─────┬──────┘ └─────┬──────┘│                       │
│  │        │              │              │        │                       │
│  │        ▼              ▼              ▼        │                       │
│  │  ┌──────────────────────────────────────────┐ │                       │
│  │  │          公共组件                         │ │                       │
│  │  │  faiss_wrapper │ dashscope_api │ cJSON    │ │                       │
│  │  └────────────────────────────────────────┘   │                       │
│  └──────────────────────────────────────────────┘                       │
│                                                                         │
│  ┌────────────┐  ┌────────────────┐  ┌────────────────┐                 │
│  │   Redis    │  │  MySQL         │  │  FAISS 索引     │                 │
│  │ 127.0.0.1  │  │  127.0.0.1     │  │ /data/faiss/    │                 │
│  │ :6379      │  │  :3306         │  │   users/        │                 │
│  └────────────┘  └────────────────┘  └────────────────┘                 │
└─────────────────────────────────────────────────────────────────────────┘
                          │
                          │ HTTPS 调用
                          ▼
              ┌───────────────────────┐
              │  阿里云 DashScope API  │
              │                       │
              │  • text-embedding-v3  │
              │  • qwen-vl-plus       │
              └───────────────────────┘
```

### 1.2 数据流全景图

```
（一个预留的钩子，每次都静默失败；具体要不要真正实现需要大家自己评估，就是我上次一个文件，用不用自动生成embedding 向量进行存储，便于直接搜，而不是点击"重建 AI 描述"按钮，优点就是体验好，缺点就是消耗token）

用户上传文件
    │
    ├──→ file_info 表 (存储文件元数据)
    ├──→ user_file_list 表 (建立用户-文件归属)
    │
    └──→ 前端调用 describeFile(file, user)   ← 注意：未传 apiKey
              │
              └──→ 后端因 missing api_key 直接拒绝 (静默失败，不阻塞上传)
                   上传时不消耗任何 AI token


用户手动点击"重建 AI 描述"按钮 (需已保存 API Key):
    │
    ├──→ 逐文件调用 describeFileByMd5(force=true, skip_rebuild=true)
    │         │
    │         ├──→ 生成文件描述 (AI 或 文本提取)
    │         ├──→ 生成 embedding 向量 (DashScope)   ← 消耗 token
    │         ├──→ L2 归一化向量
    │         ├──→ 写入 file_ai_desc (全局缓存)
    │         └──→ 写入 user_file_ai_desc (用户记录)
    │
    └──→ 最后调用一次 rebuildIndex → 统一重建 FAISS 索引


用户搜索时 (需已保存 API Key):
    query文本 → embedding (消耗 token) → L2归一化 → FAISS TopK → 回查DB → 返回结果
```

---

## 2. 功能入口与部署

### 2.1 API 统一入口

所有 AI 搜索功能通过单一入口访问：

```
POST /api/ai?cmd=<命令>
```

| 命令 | 作用 | 是否需要 api_key |
|------|------|:---:|
| `describe` | 为文件生成 AI 描述和向量 | 是 |
| `search` | 语义搜索当前用户的文件 | 是 |
| `rebuild` | 重建当前用户的 FAISS 索引 | 否 |

### 2.2 后端程序

- 源文件：`src_cgi/ai_cgi.cpp`
- 编译产物：`/app/bin_cgi/ai`

### 2.3 Nginx 路由

```nginx
location /api/ai {
    # CORS 预检处理
    if ($request_method = 'OPTIONS') {
        add_header Access-Control-Allow-Origin $http_origin;
        add_header Access-Control-Allow-Methods 'GET, POST, OPTIONS';
        add_header Access-Control-Allow-Headers 'Content-Type, Authorization, Token';
        add_header Access-Control-Allow-Credentials true;
        add_header Access-Control-Max-Age 3600;
        return 204;
    }
    # CORS 响应头
    add_header Access-Control-Allow-Origin $http_origin always;
    add_header Access-Control-Allow-Methods '*' always;
    add_header Access-Control-Allow-Credentials true always;
    add_header Access-Control-Allow-Headers 'Content-Type, Authorization, Token' always;

    fastcgi_pass 172.30.0.4:10012;
    fastcgi_read_timeout 600;    # 10分钟超时，适应AI推理耗时
    include fastcgi.conf;
}
```

### 2.4 FastCGI 启动

```bash
# 确保索引目录存在
mkdir -p /data/faiss

# 启动 AI FastCGI 进程（单 worker）
spawn-fcgi -a 0.0.0.0 -p 10012 -f /app/bin_cgi/ai
```

单 worker 进程模式（未使用 `-F` 多 worker），请求在 FCGI accept 循环中串行处理。

### 2.5 请求处理主循环

```
main()
  │
  ├── read_cfg()              读取配置
  ├── curl_global_init()      初始化 cURL
  ├── ensure_dir_recursive()  确保 faiss_user_index_dir 存在
  ├── ensure_dir_recursive()  确保 faiss_lock_dir 存在
  │
  └── while (FCGI_Accept() >= 0)
        │
        ├── 读取 QUERY_STRING，提取 cmd 参数
        ├── 读取 POST body（最大 1MB）
        │
        ├── cmd == "describe"  → handle_describe(post_data)
        ├── cmd == "search"    → handle_search(post_data)
        ├── cmd == "rebuild"   → handle_rebuild(post_data)
        └── 未知 cmd           → {"code":1,"msg":"unknown cmd"}
```

---

## 3. 数据结构

### 3.1 MySQL 表结构

#### `file_info` — 文件基础信息（按 MD5 去重）

```sql
CREATE TABLE `file_info` (
  `id`    bigint(20)   NOT NULL AUTO_INCREMENT,
  `md5`   varchar(256) NOT NULL COMMENT '文件md5',
  `file_id` varchar(256) NOT NULL COMMENT '文件id:/group1/M00/00/00/xxx.png',
  `url`   varchar(512) NOT NULL COMMENT '文件url（FastDFS内网地址）',
  `size`  bigint(20)   DEFAULT '0' COMMENT '文件大小（字节）',
  `type`  varchar(32)  DEFAULT '' COMMENT '文件类型后缀',
  `count` int(11)      DEFAULT '0' COMMENT '引用计数',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_md5` (`md5`(191))
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
```

#### `user_file_list` — 用户-文件归属

```sql
CREATE TABLE `user_file_list` (
  `id`            int(11)      NOT NULL AUTO_INCREMENT,
  `user`          varchar(32)  NOT NULL COMMENT '所属用户',
  `md5`           varchar(256) NOT NULL COMMENT '文件md5',
  `create_time`   timestamp    NULL DEFAULT CURRENT_TIMESTAMP,
  `file_name`     varchar(128) DEFAULT NULL COMMENT '文件名',
  `shared_status` int(11)      DEFAULT NULL COMMENT '共享状态 0/1',
  `pv`            int(11)      DEFAULT NULL COMMENT '下载量',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
```

#### `file_ai_desc` — 全局 AI 缓存（按 md5 去重）

```sql
CREATE TABLE IF NOT EXISTS `file_ai_desc` (
  `id`          bigint       NOT NULL AUTO_INCREMENT,
  `md5`         varchar(256) NOT NULL COMMENT '对应 file_info.md5',
  `description` text         NOT NULL COMMENT 'AI 生成的文件描述',
  `embedding`   mediumblob   DEFAULT NULL COMMENT '向量序列化 float[1024]',
  `faiss_id`    int          DEFAULT -1 COMMENT 'FAISS 索引中的 ID',
  `model`       varchar(64)  DEFAULT '' COMMENT '使用的模型名',
  `status`      tinyint      DEFAULT 0 COMMENT '0=待处理 1=完成 2=失败',
  `create_time` timestamp    NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_md5` (`md5`(191))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

作用：相同 md5 的文件只做一次 AI 描述和 embedding，跨用户复用。

#### `user_file_ai_desc` — 用户级 AI 检索（按 user+md5 去重）

```sql
CREATE TABLE IF NOT EXISTS `user_file_ai_desc` (
  `id`          bigint       NOT NULL AUTO_INCREMENT,
  `user`        varchar(32)  NOT NULL COMMENT '所属用户',
  `md5`         varchar(256) NOT NULL COMMENT '文件md5',
  `cache_id`    bigint       DEFAULT NULL COMMENT '关联 file_ai_desc.id',
  `description` text         NOT NULL COMMENT '用户侧可检索的描述',
  `embedding`   mediumblob   DEFAULT NULL COMMENT '向量序列化 float[1024]',
  `faiss_id`    int          DEFAULT -1 COMMENT '用户私有 FAISS 索引中的 ID',
  `model`       varchar(64)  DEFAULT '' COMMENT '模型名',
  `status`      tinyint      DEFAULT 0 COMMENT '0=待处理 1=完成 2=失败',
  `create_time` timestamp    NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_user_md5` (`user`, `md5`(191)),
  KEY `idx_user_status` (`user`, `status`),
  KEY `idx_user_faiss` (`user`, `faiss_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

搜索时只查当前用户自己在这张表中的数据。

### 3.2 表关系 ER 图

```
┌──────────────┐      md5       ┌──────────────────┐
│  file_info   │◄──────────────►│  user_file_list   │
│              │                │                    │
│  md5 (UK)    │                │  user              │
│  url         │                │  md5               │
│  size        │                │  file_name         │
│  type        │                │  shared_status     │
│  count       │                │  pv                │
└──────┬───────┘                └─────────┬──────────┘
       │ md5                         user + md5
       │                                  │
       ▼                                  ▼
┌──────────────────┐   cache_id   ┌─────────────────────┐
│  file_ai_desc    │◄────────────►│ user_file_ai_desc    │
│  (全局 AI 缓存)  │              │ (用户级 AI 记录)      │
│                  │              │                       │
│  md5 (UK)        │              │  user + md5 (UK)      │
│  description     │              │  cache_id → file_ai   │
│  embedding       │              │  description          │
│  model           │              │  embedding            │
│  status          │              │  faiss_id             │
│  faiss_id        │              │  model                │
└──────────────────┘              │  status               │
                                  └───────────────────────┘
```

### 3.3 索引文件

每个用户一份独立的 FAISS 索引文件：

```
/data/faiss/users/<MD5(username)>.index.bin
```

- 索引类型：`faiss::IndexFlatIP`（内积索引）
- 配合 L2 归一化 = 余弦相似度
- 暴力搜索（无近似），精度100%

### 3.4 锁文件

```
/tmp/faiss_locks/<MD5(username)>.lock
```

- 写操作（append、rebuild）使用排他锁 `LOCK_EX`
- 读操作（search）使用共享锁 `LOCK_SH`
- 基于 `flock()` 系统调用

### 3.5 脏标记文件

```
/tmp/faiss_locks/<MD5(username)>.dirty
```

- 用户删除文件后写入，表示索引需要重建
- 下次 search 或 rebuild 时检测并处理

---

## 4. API Key 管理机制

### 4.1 存储方式

API Key 完全在浏览器端管理：

```
localStorage key: dashscope_api_key_<username>
```

- 不经过服务器存储，不上传到后端数据库
- 每次 API 请求时放入请求体的 `api_key` 字段
- 前端显示为 `Input.Password`，带 "保存" / "清除" 按钮

### 4.2 后端校验

```cpp
static const char *resolve_api_key(cJSON *apikey_item) {
    // 只从请求 JSON body 中读取
    // 非 NULL 且非空字符串则返回，否则返回 NULL
}
```

- `api_key` 缺失或为空 → `{"code":1,"msg":"missing api_key"}`
- `rebuild` 命令不需要 `api_key`（不调用 AI 接口）

### 4.3 API Key 流转时序

```
前端 Input.Password
    │
    ├── "保存" → localStorage.setItem(key, value)
    │              → 更新 apiKeySaved = true
    │              → 更新 apiKeyLoaded = value
    │
    ├── "清除" → localStorage.removeItem(key)
    │              → 重置所有状态
    │
    └── API 请求时
          │
          ├── 从 apiKeyLoaded 取值（而非 apiKeyInput）
          ├── 放入请求体 { ..., api_key: apiKeyLoaded }
          └── 后端 resolve_api_key() 提取并透传给 DashScope
```

---

## 5. describe 命令详解

### 5.1 请求格式

```json
{
  "user": "xxx",
  "token": "xxx",
  "md5": "xxx",
  "filename": "demo.png",
  "type": "png",
  "api_key": "sk-xxx",
  "force": false,
  "skip_rebuild": false
}
```

| 字段 | 必填 | 说明 |
|------|:---:|------|
| `user` | 是 | 用户名 |
| `token` | 是 | 登录 token |
| `md5` | 是 | 文件 MD5 |
| `filename` | 是 | 文件名 |
| `type` | 否 | 文件类型后缀 |
| `api_key` | 是 | DashScope API Key |
| `force` | 否 | 是否强制重新生成（默认 false） |
| `skip_rebuild` | 否 | 强制模式下是否跳过索引重建（默认 false） |

### 5.2 完整处理流程图

```
handle_describe(post_data)
    │
    ├── 1. cJSON_Parse(post_data)
    │     └── 失败 → {"code":1,"msg":"invalid json"}
    │
    ├── 2. 提取字段：user, token, md5, filename, type, api_key, force, skip_rebuild
    │     └── 缺少必填字段 → {"code":1,"msg":"missing fields"}
    │
    ├── 3. resolve_api_key()
    │     └── 为空 → {"code":1,"msg":"missing api_key"}
    │
    ├── 4. verify_token(user, token)
    │     └── 失败 → {"code":4,"msg":"token error"}
    │
    ├── 5. 连接 MySQL, SET NAMES utf8mb4
    │     └── 失败 → {"code":1,"msg":"db error"}
    │
    ├── 6. user_owns_file(conn, user, md5)
    │     │   SQL: SELECT file_name FROM user_file_list
    │     │        WHERE user='...' AND md5='...' LIMIT 1
    │     └── 失败 → {"code":1,"msg":"file not found or no permission"}
    │
    ├── 7. get_user_ai_status(conn, user, md5)
    │     │   SQL: SELECT status FROM user_file_ai_desc
    │     │        WHERE user='...' AND md5='...' LIMIT 1
    │     │
    │     └── status == 1 且非 force
    │           └── {"code":0,"msg":"already exists"}
    │
    ├── 8. [非 force] 检查全局缓存
    │     │   load_global_ai_cache(conn, md5)
    │     │   SQL: SELECT id, description, embedding, model, status
    │     │        FROM file_ai_desc WHERE md5='...' LIMIT 1
    │     │
    │     └── 缓存命中 (status == 1)
    │           │
    │           ├── copy_cache_to_user_ai(conn, user, md5, cache)
    │           │   SQL: REPLACE INTO user_file_ai_desc (...)
    │           │
    │           ├── append_user_faiss_entry(conn, user, md5)
    │           │     └── 失败 → 回退 rebuild_user_faiss_index()
    │           │
    │           └── {"code":0,"msg":"ok"}
    │
    ├── 9. generate_description_for_file(conn, md5, filename, type, api_key, ...)
    │     │   (见第12节详解)
    │     └── 失败 → {"code":1,"msg":"describe failed"}
    │
    ├── 10. dashscope_get_embedding(api_key, model, description, vector, dim)
    │      │
    │      └── 失败
    │            ├── replace_global_ai_cache(..., status=2)
    │            ├── replace_user_ai_record(..., status=2)
    │            └── {"code":1,"msg":"embedding failed"}
    │
    ├── 11. vector_l2_normalize(vector, dim)
    │
    ├── 12. replace_global_ai_cache(conn, md5, desc, vector, model, status=1)
    │      SQL: REPLACE INTO file_ai_desc (md5, description, embedding, faiss_id, model, status)
    │           VALUES ('...', '...', <BLOB>, -1, '...', 1)
    │
    ├── 13. load_global_ai_cache(conn, md5) → 获取 cache.id
    │
    ├── 14. replace_user_ai_record(conn, user, md5, cache.id, desc, vector, model, status=1)
    │      SQL: REPLACE INTO user_file_ai_desc (user, md5, cache_id, description,
    │           embedding, faiss_id, model, status) VALUES (...)
    │
    ├── 15. 维护 FAISS 索引
    │      │
    │      ├── [force 模式]
    │      │     ├── skip_rebuild == true → 只更新数据库，不动索引
    │      │     └── skip_rebuild == false → rebuild_user_faiss_index()
    │      │
    │      └── [普通模式]
    │            ├── append_user_faiss_entry() → 增量追加
    │            └── 失败 → 回退 rebuild_user_faiss_index()
    │
    └── 16. {"code":0,"msg":"ok"}
```

### 5.3 响应格式

成功：
```json
{"code": 0, "msg": "ok"}
```

已存在（非 force 模式）：
```json
{"code": 0, "msg": "already exists"}
```

失败示例：
```json
{"code": 1, "msg": "embedding failed"}
{"code": 4, "msg": "token error"}
```

### 5.4 批量重建流程

前端 "重建 AI 描述" 按钮触发的完整流程：

```
用户点击"重建 AI 描述"
    │
    ├── 1. fetchUserImages(user) → 获取所有文件列表
    │
    ├── 2. for (每个文件) {
    │     │   describeFileByMd5(md5, filename, type, user, apiKey,
    │     │                      skipRebuild = true)
    │     │   // force=true: 强制重新生成
    │     │   // skip_rebuild=true: 跳过索引重建
    │     │
    │     ├── 成功 → success++
    │     └── 失败 → 记录日志，继续下一个
    │   }
    │
    ├── 3. rebuildIndex(user)
    │     POST /api/ai?cmd=rebuild
    │     // 统一重建一次索引
    │
    └── 4. 显示结果：
          "AI 描述重建完成：{success}/{total} 个文件"
```

---

## 6. search 命令详解

### 6.1 请求格式

```json
{
  "user": "xxx",
  "token": "xxx",
  "query": "红色沙发上的猫",
  "api_key": "sk-xxx"
}
```

| 字段 | 必填 | 说明 |
|------|:---:|------|
| `user` | 是 | 用户名 |
| `token` | 是 | 登录 token |
| `query` | 是 | 自然语言搜索文本 |
| `api_key` | 是 | DashScope API Key |

### 6.2 完整处理流程图

```
handle_search(post_data)
    │
    ├── 1. JSON 解析 + 字段校验 (user, token, query, api_key)
    │     └── 缺少字段 → {"code":1,"msg":"missing fields / missing api_key / empty query"}
    │
    ├── 2. verify_token(user, token)
    │     └── 失败 → {"code":4,"msg":"token error"}
    │
    ├── 3. 连接 MySQL, SET NAMES utf8mb4
    │
    ├── 4. 检查脏标记
    │     │   is_user_index_dirty(user) — 检查 .dirty 文件是否存在
    │     │
    │     └── [脏]
    │           ├── rebuild_user_faiss_index(conn, user)
    │           └── clear_user_index_dirty(user)
    │
    ├── 5. 同步全局缓存
    │     │   sync_missing_user_ai_from_cache(conn, user)
    │     │   (详见第9节)
    │     │
    │     └── [同步到新记录 (返回值 > 0)]
    │           ├── append_pending_user_faiss_entries(conn, user)
    │           └── 失败 → rebuild_user_faiss_index(conn, user)
    │
    ├── 6. 生成查询向量
    │     │   dashscope_get_embedding(api_key, model, query, query_vec, dim)
    │     │   vector_l2_normalize(query_vec, dim)
    │     └── 失败 → {"code":1,"msg":"embedding failed"}
    │
    ├── 7. 获取共享锁
    │     │   acquire_user_lock(user, LOCK_SH)
    │     └── 失败 → {"code":1,"msg":"lock error"}
    │
    ├── 8. 初始化 FAISS 索引
    │     │   faiss_init(user_index_path, dim)
    │     └── 失败 → {"code":1,"msg":"index init failed"}
    │
    ├── 9. 空索引自动修复
    │     │   faiss_get_ntotal() == 0 ?
    │     │
    │     └── [索引为空]
    │           │   检查 DB 中是否有可用向量：
    │           │   SELECT COUNT(*) FROM user_file_ai_desc
    │           │   WHERE user='...' AND status=1 AND embedding IS NOT NULL
    │           │
    │           └── [DB 有向量]
    │                 ├── release_user_lock()
    │                 ├── rebuild_user_faiss_index()  // 自动获取排他锁
    │                 ├── acquire_user_lock(LOCK_SH)  // 重新获取共享锁
    │                 └── faiss_init()                 // 重新加载
    │
    ├── 10. FAISS TopK 搜索
    │      │   faiss_search(query_vec, dim, topk=10, ids, scores)
    │      │
    │      └── [索引仍为空]
    │            └── {"code":0,"count":0,"files":[]}
    │
    ├── 11. 结果过滤 + 数据库回查
    │      │
    │      │   for (i = 0; i < result_count; i++) {
    │      │       if (scores[i] < 0.45) continue;   // 阈值过滤
    │      │
    │      │       SQL: SELECT uad.md5, ufl.file_name, uad.description,
    │      │                   fi.url, fi.size, fi.type
    │      │            FROM user_file_ai_desc uad
    │      │            JOIN user_file_list ufl
    │      │              ON ufl.user = uad.user AND ufl.md5 = uad.md5
    │      │            JOIN file_info fi
    │      │              ON fi.md5 = uad.md5
    │      │            WHERE uad.user = '...'
    │      │              AND uad.faiss_id = <faiss_id>
    │      │              AND uad.status = 1
    │      │            LIMIT 1
    │      │
    │      │       → 组装 JSON 对象
    │      │   }
    │      │
    │      └── release_user_lock()
    │
    └── 12. 返回结果 JSON
```

### 6.3 搜索流程时序图

```
前端                     ai_cgi                DashScope             MySQL            FAISS
 │                         │                      │                    │                │
 │  POST search request    │                      │                    │                │
 │────────────────────────►│                      │                    │                │
 │                         │                      │                    │                │
 │                         │  verify_token        │                    │                │
 │                         │──────────────────────────────────────────►│                │
 │                         │◄─────────────────────────────────────────│                │
 │                         │                      │                    │                │
 │                         │  check dirty flag    │                    │                │
 │                         │─────────── access(.dirty) ────────────────────────────────►│
 │                         │◄──────────────────────────────────────────────────────────│
 │                         │                      │                    │                │
 │                         │  sync cache          │                    │                │
 │                         │──────────────────────────────────────────►│                │
 │                         │  INSERT...SELECT     │                    │                │
 │                         │◄─────────────────────────────────────────│                │
 │                         │                      │                    │                │
 │                         │  get query embedding │                    │                │
 │                         │─────────────────────►│                    │                │
 │                         │◄────────────────────│                    │                │
 │                         │                      │                    │                │
 │                         │  L2 normalize        │                    │                │
 │                         │                      │                    │                │
 │                         │  acquire LOCK_SH     │                    │                │
 │                         │──────────────────────────────────────────────────────────►│
 │                         │                      │                    │                │
 │                         │  faiss_search(topk=10)                    │                │
 │                         │──────────────────────────────────────────────────────────►│
 │                         │◄──────────────────────────────────────────────────────────│
 │                         │                      │                    │                │
 │                         │  回查 DB (faiss_id → 文件信息)            │                │
 │                         │──────────────────────────────────────────►│                │
 │                         │◄─────────────────────────────────────────│                │
 │                         │                      │                    │                │
 │                         │  release lock        │                    │                │
 │                         │──────────────────────────────────────────────────────────►│
 │                         │                      │                    │                │
 │   JSON response         │                      │                    │                │
 │◄────────────────────────│                      │                    │                │
```

### 6.4 返回格式

```json
{
  "code": 0,
  "count": 2,
  "files": [
    {
      "md5": "abc123...",
      "filename": "photo.png",
      "description": "一只猫在红色沙发上",
      "url": "http://172.30.0.3:80/group1/M00/...",
      "size": "1024",
      "type": "png",
      "score": 0.67
    }
  ]
}
```

### 6.5 搜索参数

| 参数 | 值 | 说明 |
|------|-----|------|
| TopK | 10 | 最多返回 10 个结果 |
| 相似度阈值 | 0.45 | 低于此分数的结果被过滤 |
| 向量维度 | 1024 | text-embedding-v3 输出维度 |
| 相似度度量 | 余弦相似度 | IndexFlatIP + L2归一化 |

### 6.6 搜索范围保障

搜索严格限定在当前用户范围内：

1. 只加载当前用户的 FAISS 索引文件 (`<MD5(user)>.index.bin`)
2. 只查 `user_file_ai_desc` 中 `user=当前用户` 的记录
3. SQL 中 `JOIN user_file_list` 确保只返回用户实际拥有的文件
4. `faiss_id` 是用户私有索引中的 ID，不同用户的 faiss_id 互不干扰

---

## 7. rebuild 命令详解

### 7.1 请求格式

```json
{
  "user": "xxx",
  "token": "xxx"
}
```

不需要 `api_key`，因为 rebuild 不调用 AI 接口。

### 7.2 处理流程

```
handle_rebuild(post_data)
    │
    ├── 1. JSON 解析 + 字段校验 (user, token)
    │
    ├── 2. verify_token(user, token)
    │     └── 失败 → {"code":4,"msg":"token error"}
    │
    ├── 3. 连接 MySQL, SET NAMES utf8mb4
    │
    ├── 4. sync_missing_user_ai_from_cache(conn, user)
    │     └── 失败 → {"code":1,"msg":"sync failed"}
    │
    ├── 5. rebuild_user_faiss_index(conn, user)
    │     └── 失败 → {"code":1,"msg":"rebuild failed"}
    │
    ├── 6. clear_user_index_dirty(user)
    │
    └── 7. {"code":0,"msg":"rebuilt","count":<N>}
```

### 7.3 响应格式

```json
{"code": 0, "msg": "rebuilt", "count": 42}
```

`count` 为索引中的向量总数。

---

## 8. 索引维护机制

### 8.1 三种索引操作对比

| 函数 | 场景 | 锁类型 | 开销 | 触发条件 |
|------|------|:---:|:---:|------|
| `append_user_faiss_entry` | describe 后追加单个向量 | 排他锁 | 轻量 | 普通 describe 成功后 |
| `append_pending_user_faiss_entries` | 批量补录 faiss_id < 0 的记录 | 排他锁 | 中等 | search 前同步缓存后 |
| `rebuild_user_faiss_index` | 全量重建索引 | 排他锁 | 最重 | 脏标记 / 增量失败回退 / 显式 rebuild |

策略：**能增量就不全量**，只有增量失败或明确要求时才全量 rebuild。

### 8.2 增量追加单个向量 (`append_user_faiss_entry`)

```
append_user_faiss_entry(conn, user, md5)
    │
    ├── ensure_dir_recursive(faiss_user_index_dir)
    │
    ├── SQL: SELECT id, embedding, faiss_id FROM user_file_ai_desc
    │        WHERE user='...' AND md5='...' AND status=1 LIMIT 1
    │
    ├── faiss_id >= 0 ?
    │     └── [是] 已在索引中 → return faiss_id (幂等)
    │
    ├── 校验 embedding 长度 == dim * sizeof(float)
    │
    ├── 复制 embedding → float[] 数组
    │
    ├── acquire_user_lock(user, LOCK_EX)
    │
    ├── faiss_init(user_index_path, dim)
    │
    ├── new_id = faiss_add(vec, dim)
    │     │  内部会做 L2 归一化
    │     │  若 auto_save=1 则自动持久化
    │
    ├── SQL: UPDATE user_file_ai_desc SET faiss_id=<new_id> WHERE id=<row_id>
    │
    ├── release_user_lock()
    │
    └── return new_id
```

### 8.3 批量补录待索引记录 (`append_pending_user_faiss_entries`)

```
append_pending_user_faiss_entries(conn, user)
    │
    ├── SQL: SELECT id, embedding FROM user_file_ai_desc
    │        WHERE user='...' AND status=1
    │        AND embedding IS NOT NULL AND faiss_id < 0
    │        ORDER BY id
    │
    ├── acquire_user_lock(user, LOCK_EX)  // 一次加锁
    │
    ├── faiss_init(user_index_path, dim)
    │
    ├── for (每行结果) {
    │     ├── 复制 embedding → float[]
    │     ├── faiss_add(vec, dim)
    │     ├── UPDATE user_file_ai_desc SET faiss_id=<new_id> WHERE id=<row_id>
    │     └── count++
    │   }
    │
    ├── release_user_lock()
    │
    └── return count
```

### 8.4 全量重建索引 (`rebuild_user_faiss_index`)

```
rebuild_user_faiss_index(conn, user)
    │
    ├── 1. ensure_dir_recursive(faiss_user_index_dir)
    │
    ├── 2. acquire_user_lock(user, LOCK_EX)
    │
    ├── 3. faiss_reset()
    │      │  delete g_index → NULL
    │      │  g_dimension = 0
    │      │  g_auto_save = 1
    │      └  memset(g_index_path, 0)
    │
    ├── 4. remove(index_path)  // 删除磁盘索引文件
    │
    ├── 5. faiss_init(index_path, dim)  // 创建空索引
    │
    ├── 6. faiss_set_auto_save(0)  // 批量插入时关闭自动保存
    │
    ├── 7. SQL: UPDATE user_file_ai_desc SET faiss_id=-1 WHERE user='...'
    │
    ├── 8. SQL: SELECT id, embedding FROM user_file_ai_desc
    │          WHERE user='...' AND status=1
    │          AND embedding IS NOT NULL ORDER BY id
    │
    ├── 9. for (每行) {
    │        ├── 复制 embedding → float[]
    │        ├── new_id = faiss_add(vec, dim)
    │        ├── UPDATE user_file_ai_desc SET faiss_id=<new_id> WHERE id=<row_id>
    │        └── count++
    │      }
    │
    ├── 10. faiss_set_auto_save(1)
    │
    ├── 11. faiss_save(index_path)  // 一次性写盘
    │
    ├── 12. release_user_lock()
    │
    └── 13. return count
```

### 8.5 索引维护决策树

```
                    ┌──────────────┐
                    │  触发场景？   │
                    └──────┬───────┘
                           │
          ┌────────────────┼────────────────┐
          ▼                ▼                ▼
     普通 describe    search 前同步     显式 rebuild/
          │           到新记录          脏标记修复
          │                │                │
          ▼                ▼                ▼
   append_single    append_pending    rebuild_full
          │                │
          │ 失败            │ 失败
          ▼                ▼
   rebuild_full      rebuild_full
```

---

## 9. 缓存同步机制

### 9.1 `sync_missing_user_ai_from_cache` 原理

当两个用户拥有相同文件（相同 MD5）时，如果用户 A 已经生成了 AI 描述，用户 B 无需重复调用 AI，直接复用全局缓存。

```sql
INSERT INTO user_file_ai_desc
  (user, md5, cache_id, description, embedding, faiss_id, model, status)
SELECT ufl.user, fad.md5, fad.id, fad.description, fad.embedding,
       -1, fad.model, fad.status
FROM user_file_list ufl
JOIN file_ai_desc fad
  ON fad.md5 = ufl.md5 AND fad.status = 1
LEFT JOIN user_file_ai_desc uad
  ON uad.user = ufl.user AND uad.md5 = ufl.md5
WHERE ufl.user = '<user>' AND uad.id IS NULL
```

### 9.2 同步逻辑图解

```
user_file_list (用户拥有的文件)
    │
    │  JOIN on md5
    ▼
file_ai_desc (全局缓存，status=1)
    │
    │  LEFT JOIN
    ▼
user_file_ai_desc (用户已有的记录)
    │
    │  WHERE uad.id IS NULL  (用户还没有的)
    ▼
INSERT → user_file_ai_desc (faiss_id = -1, 待入索引)
```

### 9.3 调用时机

| 调用者 | 时机 | 后续操作 |
|------|------|------|
| `handle_search` | 搜索前 | 有新记录 → `append_pending` → 失败回退 `rebuild` |
| `handle_rebuild` | 重建前 | 立即全量 rebuild |

### 9.4 效果

- 用户 A 上传文件 X 并生成 AI 描述 → 写入 `file_ai_desc`
- 用户 B 也有文件 X（相同 MD5）
- 用户 B 搜索时自动同步 → 写入 `user_file_ai_desc` + 追加到 B 的 FAISS 索引
- 无需用户 B 调用 describe，不消耗 AI token

---

## 10. FAISS 封装层

### 10.1 文件

- 头文件：`include/faiss_wrapper.h`
- 实现：`common/faiss_wrapper.cpp`

### 10.2 全局状态（进程内单例）

```cpp
static faiss::IndexFlatIP *g_index = NULL;    // 当前持有的索引
static int g_dimension = 0;                    // 向量维度
static char g_index_path[512] = {0};           // 索引文件路径
static int g_auto_save = 1;                    // add 后是否自动 save
```

同一时刻只持有一个索引。`faiss_init` 检查路径是否相同，不同则释放旧索引加载新索引。

### 10.3 函数接口详解

#### `faiss_init(path, dim)` — 初始化/加载索引

```
faiss_init(index_path, dimension)
    │
    ├── 路径和维度与当前相同？
    │     └── [是] return 0 (快速返回，无操作)
    │
    ├── g_index 不为空？
    │     └── [是] delete g_index; g_index = NULL
    │
    ├── 设置 g_dimension, g_index_path
    │
    ├── 索引文件存在？
    │     │
    │     ├── [是] try:
    │     │         idx = faiss::read_index(path)
    │     │         g_index = dynamic_cast<IndexFlatIP*>(idx)
    │     │         │
    │     │         └── cast 失败 → delete idx; 创建新 IndexFlatIP(dim)
    │     │
    │     │       catch: 创建新 IndexFlatIP(dim)
    │     │
    │     └── [否] g_index = new IndexFlatIP(dim)
    │
    └── return 0
```

#### `faiss_add(vec, dim)` — 添加向量

```
faiss_add(vector, dimension)
    │
    ├── g_index == NULL → return -1
    ├── dim != g_dimension → return -1
    │
    ├── vector_l2_normalize(vector, dim)  // 就地归一化，修改调用者的缓冲区
    │
    ├── faiss_id = (int)g_index->ntotal   // 新 ID = 当前向量数
    │
    ├── try: g_index->add(1, vector)
    │   catch: return -1
    │
    ├── g_auto_save ?
    │     └── [是] faiss_save(g_index_path)
    │
    └── return faiss_id
```

#### `faiss_search(query, dim, topk, out_ids, out_scores)` — 搜索

```
faiss_search(query, dimension, topk, out_ids, out_scores)
    │
    ├── g_index == NULL → return -1
    ├── g_index->ntotal == 0 → return 0 (空索引)
    │
    ├── actual_k = min(topk, g_index->ntotal)
    │
    ├── 分配临时数组 ids[actual_k], scores[actual_k]
    │
    ├── try: g_index->search(1, query, actual_k, scores, ids)
    │   catch: return -1
    │
    ├── 后过滤：只保留 ids[i] >= 0 的结果
    │   (FAISS 用 -1 表示无效结果)
    │
    └── return valid_count
```

**注意**：`faiss_search` 不会对 query 做 L2 归一化，调用者需要在外部归一化。这与 `faiss_add`（内部自动归一化）的行为不对称。

#### `vector_l2_normalize(vec, dim)` — L2 归一化

```cpp
void vector_l2_normalize(float *vector, int dimension) {
    float sum = 0.0f;
    for (int i = 0; i < dimension; i++)
        sum += vector[i] * vector[i];
    float norm = sqrtf(sum);
    if (norm > 1e-10f) {                    // 防零除
        for (int i = 0; i < dimension; i++)
            vector[i] /= norm;
    }
}
```

归一化后，IndexFlatIP 的内积 = 余弦相似度，分数范围 [-1, 1]。

#### 其他函数

| 函数 | 作用 |
|------|------|
| `faiss_save(path)` | 持久化索引到磁盘 (`faiss::write_index`) |
| `faiss_reset()` | 释放索引、清零所有全局状态、恢复 auto_save=1 |
| `faiss_get_ntotal()` | 返回 `g_index->ntotal`，索引为 NULL 返回 0 |
| `faiss_set_auto_save(flag)` | 设置 add 后是否自动 save（0/1） |

### 10.4 并发安全

FAISS 封装层本身**不包含任何锁**。并发安全由 `ai_cgi.cpp` 通过文件锁 (`flock`) 保障：

```
写操作 → acquire_user_lock(user, LOCK_EX)  // 排他锁
读操作 → acquire_user_lock(user, LOCK_SH)  // 共享锁
```

由于 `spawn-fcgi` 每个端口只有一个 worker 进程，且 FCGI accept 循环串行处理请求，实际上不存在进程内并发。`flock` 主要防止不同进程（如 dealfile 和 ai 同时操作同一用户索引）之间的冲突。

---

## 11. DashScope API 封装

### 11.1 文件

- 头文件：`include/dashscope_api.h`
- 实现：`common/dashscope_api.cpp`

### 11.2 API 端点

| 接口 | URL | 用途 |
|------|-----|------|
| 多模态图片描述 | `https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation` | 图片内容描述 |
| 文本向量化 | `https://dashscope.aliyuncs.com/api/v1/services/embeddings/text-embedding/text-embedding` | 生成 embedding |

### 11.3 `dashscope_describe_image` 流程

```
dashscope_describe_image(api_key, image_url, out_desc, max_len)
    │
    ├── 构造请求 JSON:
    │   {
    │     "model": "qwen-vl-plus",
    │     "input": {
    │       "messages": [{
    │         "role": "user",
    │         "content": [
    │           {"image": "<image_url>"},
    │           {"text": "请用中文详细描述这张图片的内容，
    │                     包括主要物体、场景、颜色、文字等信息。"}
    │         ]
    │       }]
    │     }
    │   }
    │
    ├── HTTP POST (curl)
    │   Headers: Content-Type: application/json
    │            Authorization: Bearer <api_key>
    │   Timeout: 60s
    │   SSL: 跳过证书验证
    │
    ├── 解析响应:
    │   output.choices[0].message.content
    │   │
    │   ├── content 是数组 → 取 content[0].text
    │   └── content 是字符串 → 直接使用
    │
    ├── 错误检测:
    │   └── 响应 JSON 中存在 "code" 字段 → API 错误
    │
    └── return 0 (成功) / -1 (失败)
```

### 11.4 `dashscope_get_embedding` 流程

```
dashscope_get_embedding(api_key, model, text, out_vector, dimension)
    │
    ├── 构造请求 JSON:
    │   {
    │     "model": "<model>",           // text-embedding-v3
    │     "input": {
    │       "texts": ["<text>"]          // 单文本
    │     },
    │     "parameters": {
    │       "dimension": <dimension>     // 1024
    │     }
    │   }
    │
    ├── HTTP POST (curl)
    │   Headers: Content-Type: application/json
    │            Authorization: Bearer <api_key>
    │   Timeout: 30s
    │   SSL: 跳过证书验证
    │
    ├── 解析响应:
    │   output.embeddings[0].embedding[]
    │   │
    │   ├── 检查数组长度 >= dimension
    │   └── 逐元素 (double → float) 复制到 out_vector
    │
    └── return 0 (成功) / -1 (失败)
```

### 11.5 内部 HTTP 客户端

使用 `CurlBuffer` 动态缓冲区接收响应：

```cpp
struct CurlBuffer {
    char *data;
    size_t size;
    size_t capacity;
};
```

- `curl_buffer_init()`：初始分配 4096 字节
- `curl_write_cb()`：自动倍增扩容
- `curl_buffer_free()`：释放缓冲区

### 11.6 无重试机制

两个函数均只发起一次 HTTP 请求，失败立即返回 -1。重试逻辑由调用方（或用户手动重试）负责。

---

## 12. 文件描述生成策略

`generate_description_for_file` 根据文件类型选择不同的描述生成策略。

### 12.1 策略分类与流程

```
generate_description_for_file(conn, md5, filename, type, api_key, desc, desc_len)
    │
    ├── is_image_type(type) ?
    │     │  支持：png, jpg, jpeg, gif, bmp, webp, svg
    │     │
    │     └── [是] ── 图片描述流程 ──
    │           │
    │           ├── SQL: SELECT url FROM file_info WHERE md5='...'
    │           │
    │           ├── 提取 URL 路径：strstr(db_url, "/group")
    │           │
    │           ├── 拼接公网地址：
    │           │   http://<public_server_ip>:<public_server_port>/<path>
    │           │   回退: http://<web_server_ip>:<web_server_port>/<path>
    │           │
    │           ├── dashscope_describe_image(api_key, url, desc, desc_len)
    │           │
    │           └── 失败降级 → "图片文件：<filename>"
    │
    ├── is_text_type(type) 或 type == "docx" ?
    │     │  文本支持：txt, md, csv, json, xml, html, htm, log,
    │     │           c, cpp, h, py, js, css, java
    │     │
    │     └── [是] ── 文本提取流程 ──
    │           │
    │           ├── SQL: SELECT url FROM file_info WHERE md5='...'
    │           │
    │           ├── 拼接内网地址（使用 web_server_ip，非 public_server）
    │           │
    │           ├── download_file(url, "/tmp/ai_desc_<md5>.<type>")
    │           │   cURL 下载，超时 30s
    │           │
    │           ├── type == "docx" ?
    │           │     ├── [是] extract_docx_text(path, content, 256KB)
    │           │     │        fork + exec: unzip -p <path> word/document.xml
    │           │     │        解析 <w:t> 标签提取文本
    │           │     │
    │           │     └── [否] read_text_file(path, content, 256KB)
    │           │              raw open/read 绕过 FCGI stdio
    │           │
    │           ├── 截取前 3000 字符
    │           │
    │           ├── 格式化：
    │           │   "文件名：<filename>\n文件内容：<content>"
    │           │
    │           ├── 删除临时文件
    │           │
    │           └── 提取失败降级 → "<type>类型的文件：<filename>"
    │
    └── [其他类型]
          │
          └── type 为空？
                ├── [是] "未知类型的文件：<filename>"
                └── [否] "<type>类型的文件：<filename>"
```

### 12.2 各类型处理对比

| 类型 | 描述方法 | 调用 AI | 内容来源 | 降级描述 |
|------|---------|:---:|---------|---------|
| 图片 | `dashscope_describe_image` | 是 | 图片 URL（公网） | `图片文件：<filename>` |
| 文本 | 直接读取内容 | 否 | 文件下载 + 读取 | `<type>类型的文件：<filename>` |
| docx | 提取 XML 文本 | 否 | unzip + 解析 `<w:t>` | `<type>类型的文件：<filename>` |
| 其他 | 类型标签 | 否 | 无 | `<type>类型的文件：<filename>` |

### 12.3 DOCX 文本提取细节

```cpp
extract_docx_text(docx_path, out_text, max_len)
    │
    ├── fork() 创建子进程
    │     子进程: execl("unzip", "-p", docx_path, "word/document.xml")
    │     父进程: 通过管道读取 stdout
    │
    ├── 读取 XML 内容（最大 256KB）
    │
    ├── 解析 <w:t> 标签：
    │     ├── 匹配 "<w:t>" 或 "<w:t "（属性形式）
    │     ├── 排除 <w:tab>, <w:tabs> 等
    │     │   (检查 t 后面的字符是 '>' 或 ' ')
    │     └── 提取 <w:t...> 到 </w:t> 之间的文本
    │
    └── return 0 (成功) / -1 (失败)
```

### 12.4 URL 转换逻辑

文件存储在 FastDFS 中，数据库中的 URL 包含内网地址（如 `http://172.30.0.3:80/group1/M00/...`）。生成描述时需要转换为可访问的地址：

```
数据库 URL: http://172.30.0.3:80/group1/M00/00/00/wKgeA2Zxxx.png
                                  ↑
                         strstr(url, "/group") 提取路径

图片（给 AI 看）:
  http://<public_server_ip>:<public_server_port>/group1/M00/00/00/wKgeA2Zxxx.png
  │
  └── public_server 未配置时回退到 web_server

文本/docx（服务器内下载）:
  http://<web_server_ip>:<web_server_port>/group1/M00/00/00/wKgeA2Zxxx.txt
  │
  └── 始终用 web_server（内部操作）
```

### 12.5 FastCGI 环境下的 I/O 规避

由于 FastCGI 会重定义 `stdio` 函数（`fread`/`fwrite`/`popen` 等），文件下载和内容读取使用底层系统调用绕过：

| 操作 | 使用的方式 | 原因 |
|------|-----------|------|
| 下载文件 | `open()` + cURL `write()` 回调 | 绕过 FCGI 的 `fwrite` 重定义 |
| 读文本文件 | `open()` / `read()` | 绕过 FCGI 的 `fread` |
| 解压 docx | `fork()` + `exec()` + `pipe()` | 绕过 FCGI 的 `popen` |

---

## 13. 文件删除时的 AI 清理流程

### 13.1 删除操作源文件

`src_cgi/dealfile_cgi.c` 中的 `del_file` 函数处理文件删除。

### 13.2 完整删除流程

```
del_file(user, md5, filename)
    │
    ├── 1. 检查文件共享状态
    │      └── 已共享 → 清理 share_file_list, Redis, 共享计数
    │
    ├── 2. 递减 user_file_count
    │
    ├── 3. 删除用户文件归属
    │      SQL: DELETE FROM user_file_list
    │           WHERE user='...' AND md5='...' AND file_name='...'
    │
    ├── 4. 清理 AI 记录 ★
    │      │
    │      │  SQL: DELETE FROM user_file_ai_desc
    │      │       WHERE user='...' AND md5='...'
    │      │
    │      └── mysql_affected_rows() > 0 ?
    │            │
    │            └── [是] mark_user_index_dirty(user)
    │                     │
    │                     ├── 计算 MD5(username) → hash
    │                     └── 创建文件: /tmp/faiss_locks/<hash>.dirty
    │
    └── 5. 递减 file_info.count
           └── count == 0 ?
                 ├── [是] 删除 file_info 行 + FastDFS 删除物理文件
                 └── [否] 仅更新 count
```

### 13.3 关键设计

1. **只删 `user_file_ai_desc`，不删 `file_ai_desc`**
   - `file_ai_desc` 是全局缓存，其他用户可能仍持有相同文件
   - 即使 `file_info.count` 降为 0，也不主动清理 `file_ai_desc`（允许孤儿记录存在）

2. **脏标记延迟修复**
   - 删除时不立即重建 FAISS 索引（避免阻塞删除操作）
   - 写入 `.dirty` 文件作为信号
   - 下次 search 或 rebuild 时检测到脏标记，触发全量重建

3. **修复过程不调用 AI，不消耗 token**
   - 重建时 `user_file_ai_desc` 中已无被删文件的记录
   - 只需重新从现有数据构建 FAISS 索引

### 13.4 脏标记修复时序

```
用户删除文件                          用户下次搜索
    │                                    │
    ├── DELETE user_file_ai_desc         ├── is_user_index_dirty() → true
    │                                    │
    ├── 创建 .dirty 文件                  ├── rebuild_user_faiss_index()
    │                                    │     (用 user_file_ai_desc 中剩余的
    │                                    │      记录重建索引)
    │                                    │
    │                                    ├── clear_user_index_dirty()
    │                                    │     (删除 .dirty 文件)
    │                                    │
    │                                    └── 继续正常搜索流程
    ▼                                    ▼
```

---

## 14. 前端交互

### 14.1 文件

- API 服务层：`picture_bed/src/services/ai.js`
- 搜索 UI：`picture_bed/src/pages/Home.js`

### 14.2 服务层函数

| 函数 | 说明 | 是否阻断上传流程 |
|------|------|:---:|
| `fetchApiKey(user)` | 从 localStorage 读取 API Key | — |
| `saveApiKey(key, user)` | 保存/清除 API Key 到 localStorage | — |
| `describeFile(file, user, apiKey)` | 上传后自动调用，失败返回 null 不抛异常 | 否 |
| `describeFileByMd5(md5, fn, type, user, apiKey, skipRebuild)` | 重建时逐个调用，`force=true` | 是（抛异常） |
| `aiSearch(query, user, apiKey)` | 语义搜索 | 是（抛异常） |
| `rebuildIndex(user)` | 重建 FAISS 索引（不需要 apiKey） | 是（抛异常） |

### 14.3 错误码约定

| code | 含义 | 前端处理 |
|------|------|---------|
| 0 | 成功 | 正常流程 |
| 4 | Token 过期 | 触发登出 (`err.tokenExpired = true`) |
| 其他非零 | 通用错误 | 显示 error message |

### 14.4 搜索 UI 组件结构

```
StyledCard (title: "AI 智能搜索")
│
├── API Key 区域
│   ├── Input.Password (placeholder: "阿里百炼 API Key (sk-...)")
│   ├── Button "保存" / "已保存" (disabled when saved & unchanged)
│   └── Button "清除" (visible when saved)
│   └── 提示文字: "Key 仅保存在浏览器本地"
│
├── 搜索区域
│   ├── Input.Search
│   │   placeholder: "描述你想找的文件，例如：红色沙发上的猫、上周的报告..."
│   │   disabled: !apiKeySaved
│   │   loading: searching
│   │   onSearch → handleAiSearch()
│   │
│   └── Button "重建 AI 描述" / "生成中..."
│       disabled: !apiKeySaved
│       loading: rebuilding
│       onClick → handleRebuildDescriptions()
│
├── [searching] Spin "AI 搜索中..."
│
└── [searchResults !== null && !searching]
    │
    ├── [空结果] Empty "未找到匹配的文件"
    │
    └── [有结果] List
        └── List.Item
            ├── 缩略图 (60x60)
            │   ├── 图片类型 → <img src={url} />
            │   └── 非图片   → <FileOutlined />
            │
            ├── 文件信息
            │   ├── 文件名 (粗体)
            │   └── 描述 (灰色, 12px, 最多2行)
            │
            ├── 相似度标签 Tag
            │   ├── score >= 0.6 → 绿色
            │   ├── score >= 0.4 → 蓝色
            │   └── score < 0.4  → 橙色
            │   显示: "相似度 {score*100}%"
            │
            └── 下载按钮 (新标签页打开)
```

### 14.5 前端搜索状态机

```
                    ┌────────────┐
                    │   初始状态  │  searchResults = null
                    │            │  searching = false
                    └─────┬──────┘
                          │ 用户点击搜索
                          ▼
                    ┌────────────┐
                    │  搜索中    │  searching = true
                    │            │  显示 Spin
                    └─────┬──────┘
                          │
              ┌───────────┼───────────┐
              ▼           ▼           ▼
        ┌──────────┐ ┌──────────┐ ┌──────────┐
        │ 有结果   │ │ 空结果   │ │  错误    │
        │          │ │          │ │          │
        │ 显示列表 │ │ 显示Empty│ │ 提示msg  │
        └──────────┘ └──────────┘ └──────────┘
```

### 14.6 重建流程前端视角

```
handleRebuildDescriptions()
    │
    ├── rebuilding = true
    │
    ├── fetchUserImages(user)     // 获取所有文件
    │
    ├── for (f of files) {
    │     try {
    │       describeFileByMd5(f.md5, f.file_name, f.type,
    │                          user, apiKeyLoaded, skipRebuild=true)
    │       success++
    │     } catch (e) {
    │       if (e.tokenExpired) throw e  // 向上抛，触发登出
    │       console.error(e)             // 单个失败不中断循环
    │     }
    │   }
    │
    ├── rebuildIndex(user)        // 一次性重建索引
    │
    ├── message.success("AI 描述重建完成：{success}/{total} 个文件")
    │
    └── rebuilding = false
```

---

## 15. 配置说明

### 15.1 配置文件

文件路径：`conf/cfg.json`

```json
{
  "redis": {
    "ip": "127.0.0.1",
    "port": "6379"
  },
  "mysql": {
    "ip": "127.0.0.1",
    "port": "3306",
    "database": "yuncunchu",
    "user": "root",
    "password": "123456"
  },
  "web_server": {
    "ip": "114.215.169.66",
    "port": "80"
  },
  "public_server": {
    "ip": "xxx.xx.xx.xx",
    "port": "8080"
  },
  "dashscope": {
    "api_key": "",
    "embedding_model": "text-embedding-v3",
    "vl_model": "qwen-vl-plus",
    "embedding_dimension": "1024"
  },
  "faiss": {
    "index_path": "/data/faiss/index.bin",
    "user_index_dir": "/data/faiss/users",
    "id_map_path": "/data/faiss/id_map.json"
  }
}
```

### 15.2 AI 相关配置项说明

| 配置路径 | 值 | 说明 |
|---------|-----|------|
| `dashscope.embedding_model` | `text-embedding-v3` | 阿里百炼文本向量化模型 |
| `dashscope.vl_model` | `qwen-vl-plus` | 通义千问视觉语言模型 |
| `dashscope.embedding_dimension` | `1024` | 向量维度 |
| `faiss.user_index_dir` | `/data/faiss/users` | 用户索引存储目录 |
| `web_server.ip/port` | 服务器地址 | 文件下载用（内网操作） |
| `public_server.ip/port` | 公网地址 | 图片描述用（AI 要能访问） |

### 15.3 全局变量对照

`read_cfg()` 函数将配置加载到以下全局变量中：

| 全局变量 | 配置来源 | 用途 |
|---------|---------|------|
| `mysql_user` | `mysql.user` | 数据库连接 |
| `mysql_pwd` | `mysql.password` | 数据库连接 |
| `mysql_db` | `mysql.database` | 数据库名 |
| `redis_ip` | `redis.ip` | Token 校验 |
| `redis_port` | `redis.port` | Token 校验 |
| `embedding_model` | `dashscope.embedding_model` | embedding 请求 |
| `vl_model` | `dashscope.vl_model` | 图片描述请求 |
| `embedding_dimension` | `dashscope.embedding_dimension` | 向量维度（默认 1024） |
| `faiss_index_path` | `faiss.index_path` | 全局索引路径 |
| `faiss_user_index_dir` | `faiss.user_index_dir` | 用户索引目录 |
| `web_server_ip/port` | `web_server.ip/port` | 文件下载地址 |
| `public_server_ip/port` | `public_server.ip/port` | 图片公网地址 |

---

## 16. 相关文件索引

### 16.1 后端

| 文件 | 作用 |
|------|------|
| `src_cgi/ai_cgi.cpp` | AI 后端主逻辑（describe/search/rebuild 三个命令处理） |
| `src_cgi/dealfile_cgi.c` | 删除文件时清理 AI 记录、写脏标记 |
| `include/faiss_wrapper.h` | FAISS 封装头文件（C 接口声明） |
| `common/faiss_wrapper.cpp` | FAISS 封装实现（IndexFlatIP、L2 归一化） |
| `include/dashscope_api.h` | DashScope API 封装头文件 |
| `common/dashscope_api.cpp` | DashScope API 实现（图片描述 + 文本 embedding） |

### 16.2 前端

| 文件 | 作用 |
|------|------|
| `picture_bed/src/services/ai.js` | AI 服务层（API Key 管理、搜索、描述、重建） |
| `picture_bed/src/pages/Home.js` | 搜索 UI 组件（搜索栏、结果列表、重建按钮） |

### 16.3 配置与部署

| 文件 | 作用 |
|------|------|
| `conf/cfg.json` | 全局配置（数据库、DashScope、FAISS 路径） |
| `docker/mysql/init.sql` | 数据库建表（含 `file_ai_desc`、`user_file_ai_desc`） |
| `docker/nginx_fastdfs/nginx.conf` | Nginx 路由（`/api/ai` → 10012，600s 超时） |
| `docker/fastcgi_app/start.sh` | FastCGI 启动（`spawn-fcgi -p 10012 -f ai`） |

### 16.4 运行时路径

| 路径 | 说明 |
|------|------|
| `/app/bin_cgi/ai` | AI FastCGI 编译产物 |
| `/data/faiss/users/<hash>.index.bin` | 用户 FAISS 索引文件 |
| `/tmp/faiss_locks/<hash>.lock` | 用户索引锁文件 |
| `/tmp/faiss_locks/<hash>.dirty` | 用户索引脏标记 |
| `/tmp/ai_desc_<md5>.<type>` | 文件描述生成临时文件 |
