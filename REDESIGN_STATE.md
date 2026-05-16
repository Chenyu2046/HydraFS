# 双链知识图谱：本次改动记录（2026-05-16）

本次围绕用户三个问题做的端到端改造：
1. 上传文件后无法自动连起来；
2. AI 摘要不通；
3. API Key 无效时前端没有任何提示。

---

## 一、改动概览（按步骤）

### 第 1 步：后端 DashScope 错误码透传
- `include/dashscope_api.h`
  - 新增 `dashscope_summarize_text()`：调用 qwen-turbo 生成 100~200 字中文摘要 + 3~5 个标签。
  - 新增进程级 last-error 查询：`dashscope_last_error_code/msg/http_code/is_api_key` 与 `dashscope_clear_last_error`。
- `common/dashscope_api.cpp`
  - 内部维护 `g_ds_err_code/msg/http_code` 三个 last-error 变量；
  - `capture_api_error()` 从响应解析 `code/message`，覆盖 HTTP 4xx 兜底；
  - `dashscope_describe_image` 与 `dashscope_get_embedding` 现在在失败时：
    - 捕获 HTTP 状态码（curl `CURLINFO_RESPONSE_CODE`）；
    - 把 API 的 `code/message` 写入 last-error；
    - 网络错误填入 `NetworkError`。
  - 新增 `dashscope_summarize_text()` 实现，约定模型输出严格 JSON `{summary,tags}`；
    - 自带容错：模型返回非 JSON 时回退把整段当作摘要文本；
    - 失败原因写入 last-error。
- `src_cgi/ai_cgi.cpp`
  - `handle_describe()` / `handle_search()` 在 `dashscope_get_embedding` 失败后：
    - 若 `dashscope_last_error_is_api_key()` 为真 → 返回 `code=2`、`msg` 带可读说明、`err_code` 字段；
    - 否则 → 返回 `code=1`，`msg` 拼接 last-error 文本，便于排查。
- `src_cgi/knowledge_worker.cpp`
  - embedding 失败时 `error_msg` 写入 `api_key_invalid: ...` 或 `embedding_failed: ...`，前端读 `user_file_ai_desc.error_msg` 即可看到原因。

### 第 2 步：前端 API Key 无效全局弹窗 + QuickUpload 不再静默
- `picture_bed/src/services/ai.js`
  - 新增 `setApiKeyInvalidListener(fn)` + 内部 5 秒节流；
  - 新增 `makeApiKeyError(data)` 与 `checkResponseCode(data)`；
  - 所有 AI 调用（`describeFile` / `describeFileByMd5` / `aiSearch`）遇到 `code=2` 都会抛带 `apiKeyInvalid=true` 的 Error 并触发全局监听；
  - `describeFile`（上传后异步调用）即使内部 catch 不上抛，也会触发一次全局弹窗。
- `picture_bed/src/App.js`
  - 在 `AppRoutes` 内注册全局监听，使用 antd `Modal.confirm` 弹窗，「去设置」按钮跳转 `/knowledge`。
- `picture_bed/src/components/QuickUpload.js`
  - 注释补充说明：上传后 AI 失败仍会触发统一弹窗。
- `picture_bed/src/pages/Knowledge.js`
  - 重建循环遇到 `apiKeyInvalid` 立刻中断并提示。
- `picture_bed/src/components/FileDrawer.js`
  - 手动「生成 AI 摘要」按钮捕获 `apiKeyInvalid`，避免重复 toast。
- `picture_bed/src/pages/Home.js`
  - AI 搜索失败时区分 `apiKeyInvalid`。

### 第 3 步：AI 摘要 + 自动 tag（LLM）
- `src_cgi/knowledge_worker.cpp`
  - 新增 `merge_tag_arrays()`：合并 `[[wikilink]]` 抽取 + LLM 抽取的 tag，去重，上限 8 个。
  - `process_one_task()` 流程调整：
    1. 先用 `make_summary` 做截断兜底；
    2. 提取显式 `[[xxx]]` 到 `explicit_tags_json`；
    3. 调 `dashscope_summarize_text()` 生成 AI 摘要 + tag，成功则替换 `summary`；
    4. `merge_tag_arrays()` 合并显式 + AI tags 写入最终 `tags_json`；
    5. 写 `wiki_page` / `wiki_link` —— 自动 tag 也会进 `wiki_link` 表。
  - 新增配置 `dashscope.summary_model`，默认 `qwen-turbo`。
- `conf/cfg.json` / `docker/fastcgi_app/cfg.json`
  - 增加 `"summary_model": "qwen-turbo"` 字段（不配也会用默认值）。

### 第 4 步：基于 embedding 的语义自动链接（auto-link）
- `src_cgi/ai_cgi.cpp` 的 `handle_related()` 在原有 fallback 链路最后再加一层：
  1. 共享 `[[显式概念]]`（原本）；
  2. tag 重合（原本，但因第 3 步 tag 会更丰富，命中率提升）；
  3. **新增：基于 embedding 余弦相似度** 取 top-3（阈值 0.6），返回结构 `reason="语义相似度 0.78"`。
  - 直接读 `user_file_ai_desc.embedding` BLOB 做内积（已 L2 归一化），不动 schema，不加新表。
  - 用户文件数 < 500 暴力可接受，> 500 截断；后续可改成走 FAISS。

---

## 二、用户三个问题的对应解决方式

| 用户原话 | 根因 | 解决路径 |
|---|---|---|
| 上传文件无法自动连起来 | worker 只识别 `[[xxx]]`；普通 PDF/代码无任何显式标记 | 第 3 步：LLM 自动抽 3~5 个概念，进入 `wiki_link` → 自动出现在「相关文件 / 反向链接」；第 4 步：embedding 相似度做语义兜底 |
| AI 摘要不通 | 原 `make_summary` 是文本前 300 字硬截断 | 第 3 步：调 `qwen-turbo` 生成中文摘要，失败回退截断 |
| API Key 不可用没提示 | DashScope 错误码不上抛、前端 catch 全部静默 | 第 1 步：后端透传 `code=2` + `err_code`；第 2 步：前端 services 节流抛错 + App 顶层 `Modal.confirm` |

---

## 三、部署 / 验证清单

1. **重新编译后端**（必须在 Linux 容器内）：
   ```sh
   make clean && make
   ```
   产物：`bin_cgi/ai`、`bin_cgi/knowledge_worker`（其它 CGI 也会重链 `dashscope_api.o`）。

2. **重启进程**：
   ```sh
   pkill -f knowledge_worker; ./bin_cgi/knowledge_worker &
   # spawn-fcgi 等照常重启 ai
   ```

3. **配置可选**：
   - `conf/cfg.json` 可改 `dashscope.summary_model` 为 `qwen-plus` 提升摘要质量（成本更高）。

4. **前端**：
   ```sh
   cd picture_bed && npm run build
   ```

5. **验证步骤**：
   - 故意把 API Key 改成乱码，触发任意 AI 操作 → 应弹窗 + 跳转。
   - 上传一个普通 PDF/MD（无 `[[xxx]]`），等 5~10s 后打开 FileDrawer：
     - 「AI Summary」应为模型生成的 200 字以内中文；
     - tags 应有 3~5 个 AI 抽出的概念。
   - 上传两个主题相近的文件，打开第二个的 FileDrawer / WikiDetail，「相关文件」里应自动出现第一个，原因显示 `共享概念：xxx` 或 `语义相似度 0.xx`。

---

## 四、未做 / 待办（如果后续要加）

- 真正的 `wiki_link(link_type='semantic')` 落表 + 反向链接联动（当前 semantic 只出现在 related，不进 backlinks）。
- 关系图页面（`pages/Graph.js`）拉取 semantic 边可视化。
- worker 增量 tag 模型选择策略（短文本走 turbo，长文本走 plus）。
- `ai_parse_task.error_msg` 推送到前端文件列表 hover 提示。
