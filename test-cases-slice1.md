---
artifact: test-cases
version: "1.0"
slice: "1"
created: 2026-05-13
covers: docker/mysql/init.sql Schema 变更
---

# Slice 1 测试用例：数据库 Schema 变更

## 测试目标

验证 init.sql 新增的 3 张表和 2 张表的扩展列可在 Docker 环境中正确创建，且不影响现有功能。

## 前置条件

- Docker 环境可用
- 执行 `docker compose down -v && docker compose up -d --build`（全新安装场景）
- 或对存量数据库执行 init.sql（升级场景）

---

## TC-1.1：全新安装 — 新增表可创建（正常用例）

**步骤：**
1. 清理环境：`docker compose down -v`
2. 重新构建并启动：`docker compose up -d --build`
3. 等待 MySQL 健康检查通过
4. 进入 MySQL：`docker exec -it tc_fcgi_mysql mysql -u root -p123456 yuncunchu`
5. 执行：`SHOW TABLES LIKE '%ai_parse_task%';`
6. 执行：`SHOW TABLES LIKE '%wiki_page%';`
7. 执行：`SHOW TABLES LIKE '%wiki_link%';`

**预期结果：**
- `ai_parse_task` 存在
- `wiki_page` 存在
- `wiki_link` 存在

---

## TC-1.2：全新安装 — file_ai_desc 包含新列（正常用例）

**步骤：**
1. `docker exec -it tc_fcgi_mysql mysql -u root -p123456 yuncunchu`
2. `DESCRIBE file_ai_desc;`

**预期结果：**
- 包含 `summary` (text, YES)
- 包含 `tags_json` (text, YES)
- 包含 `outline_json` (longtext, YES)
- 包含 `parser_version` (varchar(32), YES, DEFAULT 'v1')
- 包含 `updated_at` (datetime, YES, DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP)
- 旧列全部保留（id, md5, description, embedding, faiss_id, model, status, create_time）

---

## TC-1.3：全新安装 — user_file_ai_desc 包含新列（正常用例）

**步骤：**
1. `DESCRIBE user_file_ai_desc;`

**预期结果：**
- 包含 `summary` (text, YES)
- 包含 `tags_json` (text, YES)
- 包含 `parse_status` (varchar(32), YES, DEFAULT 'pending')
- 包含 `error_msg` (text, YES)
- 包含 `updated_at` (datetime, YES)
- 旧列全部保留（id, user, md5, cache_id, description, embedding, faiss_id, model, status, create_time）

---

## TC-1.4：ai_parse_task 索引验证（正常用例）

**步骤：**
1. `SHOW INDEX FROM ai_parse_task;`

**预期结果：**
- `idx_user_status` 在 (user, status) 上
- `idx_md5_status` 在 (md5, status) 上
- `idx_status_created` 在 (status, created_at) 上
- 主键索引为 id

---

## TC-1.5：wiki_page 约束验证（正常用例）

**步骤：**
1. `SHOW INDEX FROM wiki_page;`
2. 尝试插入重复的主键记录：
```sql
INSERT INTO wiki_page (user, md5, title) VALUES ('testuser', 'abc123', 'Test Wiki');
INSERT INTO wiki_page (user, md5, title) VALUES ('testuser', 'abc123', 'Duplicate');
```

**预期结果：**
- `uq_user_md5` 唯一索引存在
- 第二句 INSERT 报错 `Duplicate entry`

---

## TC-1.6：wiki_link 约束验证（正常用例）

**步骤：**
1. `SHOW INDEX FROM wiki_link;`
2. 尝试插入重复的链接记录：
```sql
INSERT INTO wiki_link (user, src_md5, dst_name, link_type) VALUES ('u1', 'a1', 'FastDFS', 'explicit');
INSERT INTO wiki_link (user, src_md5, dst_name, link_type) VALUES ('u1', 'a1', 'FastDFS', 'explicit');
```

**预期结果：**
- `uq_user_src_dst_type` 唯一索引存在
- 第二句 INSERT 报错 `Duplicate entry`

---

## TC-1.7：存量数据库升级 — safe_add_column 幂等（异常/边界用例）

**步骤：**
1. 在已有旧版 init.sql（无新列）的数据库中，执行新 init.sql
2. 再次执行新 init.sql（验证第二次运行不报错）

**预期结果：**
- 第一次执行：新列成功添加
- 第二次执行：safe_add_column 跳过已存在列，不报错
- `DROP PROCEDURE IF EXISTS safe_add_column` 无副作用

---

## TC-1.8：旧表不受影响（回归用例）

**步骤：**
1. `SHOW TABLES;`

**预期结果：**
- user_info, file_info, user_file_list, user_file_count 等基础表存在且结构不变
- share_file_list, share_picture_list 存在且结构不变

---

## TC-1.9：DEFAULT 值验证（边界用例）

**步骤：**
1. 不指定默认字段插入一条任务记录：
```sql
INSERT INTO ai_parse_task (user, md5) VALUES ('u1', 'testmd5');
SELECT status, retry_count, source, task_type FROM ai_parse_task WHERE user='u1';
```
2. 不指定默认字段插入一条 wiki_link：
```sql
INSERT INTO wiki_link (user, src_md5, dst_name) VALUES ('u1', 'm1', 'ConceptA');
SELECT link_type FROM wiki_link WHERE dst_name='ConceptA';
```

**预期结果：**
- ai_parse_task: status='pending', retry_count=0, source='upload', task_type='parse_file'
- wiki_link: link_type='explicit'

---

## TC-1.10：分片上传 HTTP API 兼容（回归用例）

**步骤：**
1. 对大于 10MB 的文件执行上传，或直接按顺序调用：
   - `POST /api/chunk_init`
   - `POST /api/chunk_upload?md5=<md5>&index=0`
   - `POST /api/chunk_merge`
2. 请求体字段沿用现有格式：`chunk_init` 使用 JSON，`chunk_upload` 使用二进制 body，`chunk_merge` 使用 JSON。

**预期结果：**
- 三个路径、URL 参数和 JSON 字段保持兼容，不需要调用方迁移 API。
- 成功响应仍返回 `{"code":0}` 或包含原有业务字段的成功响应。

---

## TC-1.11：前端 AIMD 初始窗口与上限（正常用例）

**步骤：**
1. 将测试分片大小调小，构造至少 12 个分片的文件。
2. Mock `fetch`，让 `/api/chunk_upload` 延迟成功返回。
3. 调用 `uploadChunked(file, user, onProgress)`，记录同时在途的 chunk upload 数。

**预期结果：**
- 首批并发上传数为 4。
- 首个分片完成前不会超过 4。
- 连续健康成功后并发窗口增长，但不超过 32。

---

## TC-1.12：AIMD 按 RTT/失败率/超时率退让（边界用例）

**步骤：**
1. Mock 多个 `/api/chunk_upload` 样本，制造以下任一退化条件：
   - 最近样本失败率大于 20%
   - 最近样本超时率大于 10%
   - 单次 RTT 超过成功平均 RTT 的 2 倍
2. 继续调度后续分片，记录窗口变化。

**预期结果：**
- 命中退化条件后窗口减半。
- 窗口不会低于 4。
- 恢复健康样本后窗口再按每次加 1 增长。

---

## TC-1.13：单片超时和重试不提前推进进度（异常用例）

**步骤：**
1. Mock 某个 `index` 的第一次 `/api/chunk_upload` 返回失败或触发 `AbortError`。
2. 第二次或后续重试返回 `{"code":0}`。
3. 记录该分片尝试次数和 `onProgress` 调用。

**预期结果：**
- 失败分片会重试，默认最多重试 3 次（最多 4 次尝试）。
- 该分片成功前不增加完成分片数。
- 全部分片成功后上传进度可到 90%，合并成功后到 100%。

---

## TC-1.14：chunk_upload 临时文件 + link no-overwrite 幂等写入（回归用例）

**步骤：**
1. 对同一 `md5`、同一 `index` 连续提交两次相同大小的分片。
2. 检查 `/tmp/chunks/{md5}/{index}` 文件存在且大小正确。
3. 观察第二次请求响应。

**预期结果：**
- 首次写入通过临时文件落盘，使用 `link(tmp_path, chunk_path)` no-overwrite 创建目标分片，成功后 `unlink(tmp_path)`。
- 第二次发现目标分片大小一致时直接视为成功，不重写完整分片。
- 两次请求都返回成功，不破坏已有分片。

---

## TC-1.15：Redis 上传进度原子更新（并发用例）

**步骤：**
1. 并发上传同一文件的多个不同分片。
2. 读取 Redis `chunk:{md5}` hash。
3. 检查 `uploaded_idx:{index}` 字段和 `uploaded` 字段。

**预期结果：**
- 每个成功分片都有对应 `uploaded_idx:{index}=1`。
- `uploaded` 字段包含已成功分片索引，且按数字升序去重。
- 并发更新下不丢失已上传索引。

---

## TC-1.16：chunk_upload 默认 8 workers（部署用例）

**步骤：**
1. 不设置 `CHUNK_UPLOAD_WORKERS` 启动 FastCGI 容器。
2. 查看启动脚本或进程参数。
3. 可选：设置 `CHUNK_UPLOAD_WORKERS=4` 后重启，验证覆盖生效。

**预期结果：**
- 默认通过 `spawn-fcgi -F 8` 启动 `chunk_upload`。
- 设置环境变量后使用指定 worker 数。
