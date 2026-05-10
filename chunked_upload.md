# 大文件分片断点续传功能说明

## 概述

当上传文件大于 10MB 时，系统自动启用分片上传模式。文件被切割为 10MB 的分片逐个上传，支持断点续传（上传中断后可从已完成的分片继续），上传完成后服务端通过 FastDFS Appender API 合并分片。

## 架构设计

```
前端 (React)                          后端 (C FastCGI)
─────────────                         ────────────────

1. 计算文件MD5
2. POST /api/chunk_init ──────────►  chunk_init_cgi (port 10009)
   {user,token,filename,              - 在Redis中记录分片元数据
    md5,size,chunkCount}               - 创建临时目录 /tmp/chunks/{md5}/
   ◄────── {code:0, uploaded:"0,1,3"}  - 返回已上传分片索引(用于续传)

3. 跳过已上传分片
4. POST /api/chunk_upload?md5=x&index=i  chunk_upload_cgi (port 10010)
   Body: 二进制分片数据 ─────────►    - 保存到 /tmp/chunks/{md5}/{index}
   ◄────── {code:0}                    - 更新Redis已上传索引
   (重复直到所有分片上传完毕)

5. POST /api/chunk_merge ────────►   chunk_merge_cgi (port 10011)
   {user,token,md5,filename}          - 验证所有分片完整
                                      - FastDFS: 分片0 → upload_appender
                                      - FastDFS: 分片1~N → append
                                      - 每片合并后立即删除临时文件
                                      - 写入MySQL (file_info + user_file_list)
                                      - 清理Redis记录和临时目录
   ◄────── {code:0}
```

## API 接口

### 1. 分片初始化 — `POST /api/chunk_init`

**请求体 (JSON):**

| 字段 | 类型 | 说明 |
|------|------|------|
| user | string | 用户名 |
| token | string | 登录token |
| filename | string | 原始文件名 |
| md5 | string | 整个文件的MD5 |
| size | number | 文件总大小(字节) |
| chunkCount | number | 分片总数 |

**响应:**

```json
// 首次上传
{"code": 0}

// 断点续传（已有部分分片）
{"code": 0, "uploaded": "0,1,3,4"}
```

| code | 含义 |
|------|------|
| 0 | 成功 |
| 1 | 参数错误/服务端错误 |
| 4 | token验证失败 |

### 2. 分片上传 — `POST /api/chunk_upload?md5=xxx&index=N`

**URL参数:**

| 参数 | 说明 |
|------|------|
| md5 | 文件MD5 |
| index | 分片索引(从0开始) |

**请求体:** 原始二进制数据 (`Content-Type: application/octet-stream`)

**响应:**

```json
{"code": 0}
```

### 3. 分片合并 — `POST /api/chunk_merge`

**请求体 (JSON):**

| 字段 | 类型 | 说明 |
|------|------|------|
| user | string | 用户名 |
| token | string | 登录token |
| md5 | string | 文件MD5 |
| filename | string | 原始文件名 |

**响应:**

```json
{"code": 0}
```

## 断点续传流程

```
用户选择文件 → 计算MD5 → 调用 chunk_init
                                │
                    ┌───────────┴───────────┐
                    │ Redis中已有该MD5记录？  │
                    └───────────┬───────────┘
                          │           │
                         是          否
                          │           │
                  返回已上传索引   创建新记录
                  "0,1,3,4"      uploaded=""
                          │           │
                          └─────┬─────┘
                                │
                    前端跳过已完成分片
                    只上传缺失的分片(如2,5,6...)
                                │
                    全部分片上传完成
                                │
                    调用 chunk_merge 合并
```

## Redis 数据结构

使用 Hash 类型，key 为 `chunk:{md5}`：

| 字段 | 值 | 说明 |
|------|-----|------|
| filename | test.zip | 原始文件名 |
| filesize | 52428800 | 文件总大小 |
| chunk_count | 5 | 分片总数 |
| user | zhangsan | 上传用户 |
| uploaded | 0,1,2,3 | 已上传分片索引(逗号分隔) |

TTL: 24小时（过期自动清理）

## 临时文件存储

```
/tmp/chunks/
└── {file_md5}/
    ├── 0        # 分片0 (10MB)
    ├── 1        # 分片1 (10MB)
    ├── 2        # 分片2 (10MB)
    └── 3        # 分片3 (最后一片，可能不足10MB)
```

合并过程中每个分片追加到 FastDFS 后立即 `unlink()` 删除，合并完成后 `rmdir()` 删除目录。

## 前端实现要点

**自动检测:** `uploadImage()` 函数判断文件大小，超过 `CHUNK_THRESHOLD`(10MB) 自动走分片上传。

**MD5计算:** 使用 SparkMD5 分块读取计算，避免大文件一次性加载到内存。

**进度条:** 分片上传占 90%，合并占 10%。每完成一个分片更新进度。

**配置项 (`src/config/index.js`):**

```js
CHUNK_SIZE: 10 * 1024 * 1024,      // 每个分片大小: 10MB
CHUNK_THRESHOLD: 10 * 1024 * 1024   // 启用分片的阈值: 10MB
```

## 服务端口分配

| CGI程序 | 端口 | 说明 |
|---------|------|------|
| chunk_init | 10009 | 分片初始化 |
| chunk_upload | 10010 | 分片接收 |
| chunk_merge | 10011 | 分片合并 |

## Nginx 配置要点

- `client_max_body_size 12m` — 允许单个分片(10MB) + HTTP头部开销
- `chunk_upload` 设置 `fastcgi_read_timeout 300` — 大分片上传超时5分钟
- `chunk_merge` 设置 `fastcgi_read_timeout 600` — 合并操作超时10分钟
- 三个 location 块均配置完整 CORS 头

## 涉及的源文件

```
fastcgi_yuncunchu_docker/
├── src_cgi/
│   ├── chunk_init_cgi.c       # 分片初始化CGI
│   ├── chunk_upload_cgi.c     # 分片上传CGI
│   └── chunk_merge_cgi.c      # 分片合并CGI
├── Makefile                   # 编译规则(chunk_init/upload/merge目标)
└── docker/
    ├── docker-compose.yaml    # 端口映射 10000-10011
    ├── fastcgi_app/
    │   ├── dockerfile         # EXPOSE 10000-10011
    │   └── start.sh           # spawn-fcgi 启动3个新进程
    └── nginx_fastdfs/
        └── nginx.conf         # 3个新location块 + 超时配置

picture_bed/src/
├── config/index.js            # CHUNK_* 端点和阈值配置
├── services/images.js         # uploadChunked() 分片上传逻辑
└── pages/
    ├── ImageList.js           # 图片上传进度条
    └── FileList.js            # 文件上传进度条
```
