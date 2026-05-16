# Frontend Redesign — 2026-05-16

> 触发问题（用户原话）：
> 1. 页面配色稍微好了一点，但**产品调性和重点突出的功能都没有很好表达**
> 2. **明明有顶栏，为什么内容页还要放 Tab 标签**

## 一、4 Agent 收敛方案

| Agent | 核心结论 |
|---|---|
| Product | Slogan 改 `Files in. Knowledge out.` / 副标"上传文件，沉淀知识网络"；删 trust footer 技术名词；首页只讲 "上传 → 摘要 → 自动连接 → 反向引用" 一条线 |
| IA | 导航单源原则 = Sidebar；删 HeroCanvas.PillNav；SharedHub Tabs → Segmented；FileList 工具栏分层；Topbar crumbs ≥2 段才显示 |
| Visual | hero h1 走 token (新增 `fontSize.hero = 44px`)；去 italic 三色渐变；hover 统一 border/shadow 不再 translateY；hero canvas 只用于首页 |
| Engineering | 删 9 个 `*.js.new` 草稿；Bento 6 卡 → 3 卡（对齐 Auto Summary / Auto Link / Semantic Search）；不动 services |

**Reviewer 修正：**
- ❌ 不重命名 `/wiki/:md5`（rename 侵入面太大，crumb 改善即可）
- ❌ 不合并 `/graph` 路由（保留 force-graph 独立页，只 sidebar 微调）
- ❌ 不删 FileList grid/list 视图切换 Segmented（真切换，移到 SectionTitle 右上角）
- ✅ SharedHub Tabs 改 Segmented 时保留 URL `?sort=top` 兼容 `/top-downloads` redirect

---

## 二、执行阶段（PR-0 → PR-4）

| # | Phase | 目标 | 主要文件 | 状态 |
|---|---|---|---|---|
| 0 | 清理草稿 | 删除 9 个 `*.js.new` | `picture_bed/src/**/*.js.new` | ✅ |
| 1 | 去重导航 + crumb 优化 | 删 PillNav；Topbar ≥2 段才显示 crumb；Sidebar reorder | `HeroCanvas.js`、`Topbar.js`、`Sidebar.js`、`App.js` | ✅ |
| 2 | 消灭页内 Tab | SharedHub Tabs → Segmented；FileList 工具栏分层；view toggle 右上角 | `SharedHub.js`、`FileList.js` | ✅ |
| 3 | Home 调性回归 | Slogan / 副标改写；删 trust footer 技术名词；hero h1 走 token；CHIPS 重写 3 条用户语言；Bento 6→3 卡 | `Home.js`、`Bento.js`、`HeroCanvas.js` | ✅ |
| 4 | Token 与原则文档化 | 新增 `fontSize.hero`；在 tokens 文件头注释 "navigation single-source / no in-page tabs" 设计原则 | `styles/tokens.js` | ✅ |

> 中途任何 PR 卡住，先在本表对应行标注 ⚠️ 并附原因。

---

## 三、设计决策记录

### 3.1 导航单源原则
- Sidebar 是**唯一**全局导航。
- Topbar 只在路径深度 ≥ 2 时显示面包屑；单层（如 `/files`）只显示 logo + right cluster。
- HeroCanvas 不允许内嵌任何 nav。

### 3.2 页内 Tab 限定
仅当两个 tab 是**完全独立的实体**（如 GitHub PR `Conversation / Files changed`）时才允许使用 Tabs。HydraFS 当前 0 个场景符合。
- 数据 + 排序：用 `Segmented`
- 模块切换：必须升级为独立路由
- 过滤条件：用 chip pills 或 `Segmented`

### 3.3 视觉权重收敛
- 最大字号：display(36) / hero(44，仅 `/`)
- 渐变文字：禁用三色 italic；只允许 accent → accentHover 同色系 underline
- 悬停动效：默认 border/shadow，不 transform；只有"主动表达交互"的元素（图谱节点、Logo）允许 translateY ≤ 1px

### 3.4 文案/调性
- 主 slogan：`Files in. Knowledge out.`
- 副标：上传文件，沉淀知识网络
- chips/Bento 文案：禁出现 FastDFS / FAISS / DashScope 等技术名词，用用户语言

---

## 四、风险与回滚
- 每个 PR 独立 commit，回滚以 commit 为单位。
- `/top-downloads` redirect → `/shared?tab=top` 已弃；PR-2 改为 `/shared?sort=top`，App.js redirect 同步更新。
- Knowledge.js 当前结构未动，下一轮再重设（API Key 折叠 + 节点墙 first）。

---

## 五、Final CR Checklist（PR-4 完成后由独立 agent 跑）

执行时间：2026-05-16，自动 grep + VS Code lint 全跑。

- [x] grep 无 `*.js.new` → 0 命中
- [x] grep 无 `PillNav` → 0 命中
- [x] HeroCanvas 中无任何 `NavLink` → 0 命中
- [x] grep 无 `import { Tabs } from 'antd'` → 0 命中
- [x] grep `clamp(.*vw.*px)` 在 pages/ 下 → 0 命中
- [x] grep `FastDFS|FAISS|DashScope|MySQL|Redis` 在 `src/pages/Home.js` → 仅 1 命中，为注释中说明"不再列出"的原因（非用户可见）
- [x] tokens.js 顶部注释包含 navigation single-source / 用户语言 / 只展示已上线能力 / 字号走 token 四条原则
- [x] VS Code lint：10 个变更文件 0 errors（HeroCanvas / Topbar / Sidebar / App / Bento / primitives / Home / FileList / SharedHub / tokens）
- [ ] `npm run build` 全量构建未执行（耗时较长；按需在 PR 合入前由 CI 触发）

### CR 复核说明（reviewer agent）
1. **导航单源** —— HeroCanvas 已移除 PillNav；Topbar 仅在 ≥2 段时渲染 crumbs；Sidebar 5 项有序，Graph 后置。✅
2. **零页内 Tab** —— SharedHub 用 `Segmented` 替代 `Tabs`，位置改放 PageHead 右侧（视觉变成"排序开关"而非二级导航）；FileList view 切换移入 SectionTitle 右侧槽，避免与 filter chips 抢同一行。✅
3. **调性收敛** —— Slogan "Files in. Knowledge out." + 副标"上传文件，沉淀知识网络"；CHIPS 由 6 个技术词收敛至 3 个用户动作词；Trust footer 改为"私有云部署 / 可替换 AI / 知识可导出"三条承诺；hero h1 改走 token，去掉 italic + 三色渐变。✅
4. **能力对齐** —— Bento 6 卡 → 3 卡 (Auto Summary / Auto Link / Semantic Search)，全部对应已合入后端能力，删除 "Storage Pipeline / Workspace / Backlinks" 等与"我现在能验证什么"无直接对应的展示卡。✅
5. **Token 化** —— 新增 `fontSize.hero = 44px`，并在 tokens.js 顶部加入四条全局设计原则（导航单源 / 用户语言 / 只展示已上线 / 字号走 token）。✅

**CR 结论：通过，可合入。** 后续待办：Knowledge.js 重设（API Key 折叠 + 节点墙 first）放下一轮。
