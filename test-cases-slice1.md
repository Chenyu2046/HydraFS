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
