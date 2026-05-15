---
artifact: code-review
version: "1.0"
date: 2026-05-13
reviewer: self-CR (staff engineer perspective)
scope: Slice 1-6 全部变更，19 files
---

# CR 报告：双链知识云存储 MVP 实现

## 审查范围

`init.sql` / `knowledge_task.c` / `knowledge_worker.cpp` / `ai_cgi.cpp` new handlers / `upload_cgi.c` / `md5_cgi.c` / `myfiles_cgi.c` / `dealfile_cgi.c` / `Makefile` / `start.sh` / `dockerfile` / `ai.js` / `WikiDetail.js` / `FileList.js` / `Home.js` / `App.js`

---

## Findings

### P0-1: Worker 在 embedding 失败时仍标记 parse_status='success' 并写入 Wiki

- **位置**: `src_cgi/knowledge_worker.cpp` L554-565（process_one_task 中 embedding 失败后 write_user_ai_record 仍用 parse_status="success"）
- **问题**: 当 DashScope embedding API 调用失败或 API Key 不可用时，`vec` 置 NULL，但 `write_user_ai_record` 仍以 `parse_status="success"` + `status=2` 写入。用户看到"已解析"，文件有 Wiki 页面，但 FAISS 索引中没有该向量，搜索永远搜不到。
- **违反原则**: fail-fast — 静默降级，用户无法感知搜索能力缺失。
- **修复方向**: embedding 失败时 parse_status 应为 `"failed"`，error_msg=`"embedding failed"`，不创建 wiki_page（无搜索价值）。

### P0-2: Worker 无法响应 SIGTERM 正常退出

- **位置**: `src_cgi/knowledge_worker.cpp` L52 `int g_running = 1`，main() while(g_running) 循环，无 signal handler
- **问题**: Docker stop 发 SIGTERM → worker 忽略 → Docker 10s 后 SIGKILL。如果 worker 正在处理任务（status='running'），任务永久卡在 running，其他 worker 无法接手。
- **违反原则**: 一致性 — ai_parse_task 残留僵尸 running 记录。
- **修复方向**: 添加 SIGTERM/SIGINT handler 设置 g_running=0；或启动时执行 `UPDATE ai_parse_task SET status='pending' WHERE status='running'` 做崩溃恢复。

### P0-3: handle_task_failure 中 error_msg 未转义直接拼 SQL

- **位置**: `src_cgi/knowledge_worker.cpp` L483-484
- **问题**: `snprintf(sql, ..., "error_msg='%s'", error_msg)` — error_msg 来自外部输入（下载错误、文件路径）未做 `mysql_real_escape_string`，含单引号会导致 SQL 畸形。
- **违反原则**: fail-fast — SQL 畸形静默失败，任务卡在 running。
- **修复方向**: 对 error_msg 做 `mysql_real_escape_string` 后再拼 SQL。

### P1-1: enqueue_parse_task 防重查询有竞态窗口

- **位置**: `common/knowledge_task.c` L65-78
- **问题**: SELECT COUNT(*) 判断无 pending/running 任务 → INSERT。两个并发请求可同时通过检查，插入两条相同任务。
- **违反原则**: 一致性 — 不会损坏数据，但浪费 worker 资源。
- **修复方向**: 利用 MySQL `INSERT IGNORE` + 在 ai_parse_task 上添加 `UNIQUE KEY (user, md5, status)` 只对 pending/running 生效（需改为应用层检查或分区索引）。

### P1-2: make_summary 截断字节而非字符

- **位置**: `src_cgi/knowledge_worker.cpp` L238 `if (len > 300) len = 300`
- **问题**: len 是 `strlen()` 返回的字节数。中文 UTF-8 每字 3 字节，300 字节 ≈ 100 中文字。PRD FR-8 要求 100-300 字。
- **违反原则**: 需求对齐 — 实际产出只有约 100 字，低于 PRD 下限。
- **修复方向**: 改为按 UTF-8 字符计数截断，或 PRD 接受 300 字节。

### P1-3: handle_backlinks N+1 查询

- **位置**: `src_cgi/ai_cgi.cpp` handle_backlinks 函数
- **问题**: 先查 wiki_link 获取当前文件的所有概念（1 次查询），然后对每个概念再查一次反向引用。一个有 20 个 [[link]] 的文件产生 21 次 MySQL 查询。
- **违反原则**: 简洁实现 — 可用单条 SQL 完成。
- **修复方向**: 
```sql
SELECT wl.dst_name, wl2.src_md5, ufl.file_name
FROM wiki_link wl
JOIN wiki_link wl2 ON wl2.user=wl.user AND wl2.dst_name=wl.dst_name AND wl2.src_md5!=wl.src_md5
JOIN user_file_list ufl ON ufl.user=wl2.user AND ufl.md5=wl2.src_md5
WHERE wl.user=? AND wl.src_md5=?
```

### P2-1: write_wiki_links 单概念单 SQL，无事务包裹

- **位置**: `src_cgi/knowledge_worker.cpp` L417-460
- **问题**: 循环内每个 wikilink 执行一条 REPLACE，中途某条失败则前几条已写入。
- **违反原则**: 一致性。MVP 阶段 wikilink 数量通常 <10，影响小。
- **修复方向**: 可接受（MVP），但应记录 fail 数并在日志中输出。

### P2-2: Worker 启动时 MySQL 连接失败直接退出

- **位置**: `src_cgi/knowledge_worker.cpp` main() L490-495
- **问题**: `msql_conn` 失败直接 `return 1`。在 Docker Compose 中容器启动顺序虽有 healthcheck 但 worker 仍有概率早于 MySQL 就绪。
- **违反原则**: fail-fast 过度 — 应该重试。
- **修复方向**: 循环重试 10 次，每次 sleep 3s。

### P2-3: extract_wikilinks + make_tags 双重 JSON 序列化

- **位置**: `src_cgi/knowledge_worker.cpp` L215-278
- **问题**: extract_wikilinks 输出 `[a,b,c]`（无引号），make_tags 再转为 `["a","b","c"]`，多一次解析。wikilink 名含引号时 JSON 损坏。
- **违反原则**: 简洁实现 — 两段代码做一件事。
- **修复方向**: 合并为一个函数，直接生成 `["concept1","concept2"]`。

### P2-4: init.sql safe_add_column 使用 DATABASE() 函数

- **位置**: `docker/mysql/init.sql` L115-116
- **问题**: `WHERE TABLE_SCHEMA = DATABASE()` — 依赖当前连接默认数据库。如果 Docker entrypoint 执行时未切换到 yuncunchu，会查询错误的 INFORMATION_SCHEMA。
- **违反原则**: 清晰性 — 依赖隐式上下文。
- **修复方向**: 改为 `TABLE_SCHEMA = 'yuncunchu'`。

### P2-5: 前端 WikiDetail 缺少 links 字段时的静默降级

- **位置**: `picture_bed/src/pages/WikiDetail.js`
- **问题**: `wiki.links` 为 undefined 时静默不渲染，用户无法区分"无链接"和"接口未返回"。
- **违反原则**: 清晰性。低风险，用户体验差。
- **修复方向**: 默认值 `(wiki.links || [])`。

---

## Residual Risks

| Risk | 影响 |
|------|------|
| Worker 崩溃后 ai_parse_task 残留 running 任务 | 其他 worker 无法接手，需手动清理 |
| 多 worker 并发：mark_task_running 的 SELECT→UPDATE 之间有竞态窗口 | 同一任务可能被两个 worker 同时取到 |
| pdftotext 未安装时静默失败（stderr → /dev/null） | 生成空文本 Wiki，质量极差 |
| write_global_cache 使用 REPLACE，多用户并发可能覆盖 | 内容相同（同 md5），风险低 |

## Unverified Paths

- Worker 处理 PDF 文件完整流程
- 多用户秒传同一文件后的并发入队
- 删除文件后 wiki_page/wiki_link 实际清理
- 前端 WikiDetail 在 links 为 undefined 时的渲染
- init.sql 在存量数据库上的 safe_add_column 执行
- worker 被 kill -9 后残留任务的恢复
- 中文文件名/概念名在 JSON 字段中的存储和前端解析

## Overengineering

- `write_wiki_links` 中的 `ok/fail` 计数器未使用 → 删除
- `make_tags` 函数完全可合并到 `extract_wikilinks` → 简化
