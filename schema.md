---
artifact: schema
version: "1.0"
created: 2026-05-13
source: dual_link_knowledge_cloud_storage_prd.md, tech-spec.md
---

# Schema：双链知识云存储 MVP

## 1. 设计原则

- 复用 `file_ai_desc`（全局缓存）和 `user_file_ai_desc`（用户私有记录）的双层结构
- MVP 只为 Wiki 和双链增加最少必要表
- 状态字段使用 VARCHAR（而非 TINYINT），便于直接可读和后续扩展

## 2. 状态枚举

### 2.1 解析状态 (parse_status)

| 值 | 含义 | 设置时机 |
|----|------|---------|
| `pending` | 待处理 | 任务入队时 |
| `running` | 处理中 | worker 取到任务后 |
| `success` | 成功 | 解析 + Wiki 生成完成 |
| `failed` | 失败 | 解析或 AI 调用失败 |
| `skipped` | 跳过 | 文件类型不支持解析 |

### 2.2 Wiki 状态

| 值 | 含义 |
|----|------|
| `active` | 正常可见 |
| `deleted` | 已软删除（用户文件被删后标记） |

### 2.3 任务触发来源 (source)

| 值 | 含义 |
|----|------|
| `upload` | 普通上传完成后入队 |
| `md5_hit` | 秒传命中后入队 |

### 2.4 链接类型 (link_type)

| 值 | 含义 |
|----|------|
| `explicit` | 显式双链 `[[concept]]` |

## 3. 表结构

### 3.1 扩展后的 file_ai_desc（全局缓存）

| 字段 | 类型 | 说明 |
|------|------|------|
| id | bigint | 自增主键 |
| md5 | varchar(256) | 文件 MD5（唯一） |
| description | text | AI 生成的文件描述 |
| embedding | mediumblob | 1024 维 float 向量 |
| faiss_id | int | FAISS 索引 ID |
| model | varchar(64) | 使用的模型名 |
| status | tinyint | 0=待处理 1=完成 2=失败 |
| create_time | timestamp | 创建时间 |
| **summary** | **text** | **AI 摘要（100-300 字）** |
| **tags_json** | **text** | **标签 JSON 数组** |
| **outline_json** | **longtext** | **内容大纲 JSON 数组** |
| **parser_version** | **varchar(32)** | **解析器版本，默认 'v1'** |
| **updated_at** | **datetime** | **最后更新时间** |

**tags_json 格式：** `["FastDFS", "分布式存储", "上传链路"]`

**outline_json 格式：** `["1. 基本架构", "2. Tracker 调度", "3. Storage 写入"]`

### 3.2 扩展后的 user_file_ai_desc（用户私有）

| 字段 | 类型 | 说明 |
|------|------|------|
| id | bigint | 自增主键 |
| user | varchar(32) | 所属用户 |
| md5 | varchar(256) | 文件 MD5 |
| cache_id | bigint | 关联 file_ai_desc.id |
| description | text | 可检索描述 |
| embedding | mediumblob | 向量 |
| faiss_id | int | 用户私有 FAISS ID |
| model | varchar(64) | 模型名 |
| status | tinyint | 0=待处理 1=完成 2=失败 |
| create_time | timestamp | 创建时间 |
| **summary** | **text** | **AI 摘要** |
| **tags_json** | **text** | **标签 JSON 数组** |
| **parse_status** | **varchar(32)** | **解析状态** |
| **error_msg** | **text** | **失败原因** |
| **updated_at** | **datetime** | **最后更新时间** |

### 3.3 ai_parse_task（新增）

| 字段 | 类型 | 说明 |
|------|------|------|
| id | bigint | 自增主键 |
| user | varchar(32) | 所属用户 |
| md5 | varchar(256) | 文件 MD5 |
| task_type | varchar(32) | 任务类型，默认 'parse_file' |
| source | varchar(32) | 触发来源：upload / md5_hit |
| status | varchar(32) | 任务状态：pending/running/success/failed/skipped |
| retry_count | int | 已重试次数 |
| error_msg | text | 失败原因 |
| worker_id | varchar(64) | 执行 worker 标识 |
| created_at | datetime | 创建时间 |
| updated_at | datetime | 最后更新时间 |

索引：
- `idx_user_status (user, status)` — 按用户查待处理/失败任务
- `idx_md5_status (md5(191), status)` — 按 md5 查任务（去重）
- `idx_status_created (status, created_at)` — worker 按创建时间轮询

### 3.4 wiki_page（新增）

| 字段 | 类型 | 说明 |
|------|------|------|
| id | bigint | 自增主键 |
| user | varchar(32) | 所属用户 |
| md5 | varchar(256) | 源文件 MD5 |
| title | varchar(255) | Wiki 标题 |
| summary | text | AI 摘要 |
| tags_json | text | 标签 JSON 数组 |
| outline_json | longtext | 内容大纲 JSON 数组 |
| status | varchar(32) | active / deleted |
| created_at | datetime | 创建时间 |
| updated_at | datetime | 最后更新时间 |

唯一约束：`(user, md5)` — 每个用户每个文件只有一个 Wiki 页面。

### 3.5 wiki_link（新增）

| 字段 | 类型 | 说明 |
|------|------|------|
| id | bigint | 自增主键 |
| user | varchar(32) | 所属用户 |
| src_md5 | varchar(256) | 源文件 MD5 |
| dst_name | varchar(255) | 目标概念名 |
| link_type | varchar(32) | 默认 'explicit' |
| anchor_text | varchar(255) | 锚文本 |
| created_at | datetime | 创建时间 |

唯一约束：`(user, src_md5, dst_name, link_type)` — 防止相同文件到同一概念的重复链接。

索引：
- `idx_user_dst (user, dst_name)` — 反向链接查询（哪些文件引用了概念 X）
- `idx_user_src (user, src_md5)` — 正向查询（文件 X 引用了哪些概念）

## 4. 任务状态机

```
pending ──→ running ──→ success
  │                      │
  │                      └──→ (终态)
  │
  ├──→ skipped（不支持的文件类型）
  │
  └──→ failed
        │
        ├── retry_count < 3 ──→ pending（等待重试）
        └── retry_count >= 3 ──→ failed（终态）
```

## 5. 索引设计理由

| 索引 | 查询场景 |
|------|---------|
| `idx_user_status` on ai_parse_task | worker 轮询：`WHERE user=X AND status='pending' ORDER BY created_at LIMIT 1` |
| `idx_md5_status` on ai_parse_task | 去重检查：`WHERE md5=X AND status='pending'` |
| `idx_status_created` on ai_parse_task | 全局轮询：`WHERE status='pending' ORDER BY created_at` |
| `uq_user_md5` on wiki_page | 按用户+md5 定位 Wiki 页面 |
| `idx_user_dst` on wiki_link | 反向链接：`WHERE user=X AND dst_name='FastDFS'` |
| `idx_user_src` on wiki_link | 正向链接：`WHERE user=X AND src_md5='abc'` |

## 6. 与现有表的关系

```
file_info.md5 ──1:1──→ file_ai_desc.md5 （全局缓存）
file_info.md5 ──1:N──→ user_file_list.md5 （用户归属）
user_file_list(user, md5) ──1:1──→ user_file_ai_desc(user, md5) （用户 AI 记录）
user_file_ai_desc(user, md5) ──1:1──→ wiki_page(user, md5) （用户 Wiki）
wiki_page(user, md5) ──1:N──→ wiki_link(user, src_md5) （双链关系）
ai_parse_task(user, md5) ──N:1──→ 由 upload/md5 创建，由 worker 消费
```

## 7. 升级兼容性

- 新安装（无存量数据）：`CREATE TABLE IF NOT EXISTS` 直接建含新列的表
- 存量数据库：`safe_add_column` 存储过程安全添加列（列已存在则跳过）
- 新表：`CREATE TABLE IF NOT EXISTS` 幂等，可重复执行
