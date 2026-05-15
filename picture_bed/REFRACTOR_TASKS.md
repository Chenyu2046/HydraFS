# Frontend Refactor — Task Ledger

> 方案 A「Graphite」深色科技流 + 融合方案 C「图谱作 Hero」
> 双主题（dark default / light optional）+ react-force-graph-2d + mock 兜底
> 保持所有 services/* 与后端接口不变

## 全局原则
- 所有 token 走 `src/styles/tokens.js`（dark + light 两套），不在组件内写死颜色
- antd 主题动态切换（algorithm: darkAlgorithm | defaultAlgorithm）
- emotion ThemeProvider 提供 `useTheme()`
- 主题状态持久化到 localStorage（key: `hydra.theme`）
- 真实接口失败或为空时回退到 `src/mock/*` 仅用于视觉
- 不引入新业务依赖；唯一新增运行时依赖：`react-force-graph-2d`（仅 Graph 页懒加载）

## 阶段进度

| # | Phase | 文件 | 状态 |
|---|---|---|---|
| 0 | 建台账 + 安装依赖 | this file, package.json | ✅ |
| 1 | 设计 token + 主题上下文 + 全局样式 | styles/tokens.js, contexts/ThemeContext.js, styles/global.css, index.js | ✅ |
| 2 | AppShell + Sidebar（替换 NavBar） | components/AppShell.js, components/Sidebar.js, components/Topbar.js, App.js | ✅ |
| 3 | Overview 首页重构 | pages/Home.js + 子组件（Hero, StatBar, CapabilityCard, MiniGraph, AIPipeline, RecentNodes, QuickUpload） | ✅ |
| 4 | Files 页统一 + 右侧 Drawer | pages/FileList.js, components/FileDrawer.js, components/FileGrid.js | ✅ |
| 5 | Knowledge 页（新增） | pages/Knowledge.js | ✅ |
| 6 | Graph 页（新增，react-force-graph-2d 懒加载） | pages/Graph.js, mock/graph.js | ✅ |
| 7 | WikiDetail 重排 | pages/WikiDetail.js | ✅ |
| 8 | Login 重设计 | pages/Login.js | ✅ |
| 9 | Shared + TopDownloads 合并为 Tab | pages/SharedHub.js | ✅ |
| 10 | 清理：删除 NavBar / ImageList / SharedFiles / TopDownloads | components/NavBar.js, pages/ImageList.js, pages/SharedFiles.js, pages/TopDownloads.js | ✅ |

## 设计 Token 摘要

### Dark（默认）
- bg: `#0B0B0F` / panel: `#111114` / panel-2: `#16161B`
- border: `#1F1F25` / border-strong: `#2A2A33`
- text: `#EDEDEF` / text-2: `#8A8A93` / text-3: `#5A5A63`
- accent: `#7C5CFF` (靛紫单一强调) / accent-soft: rgba(124,92,255,0.12)
- success `#3FB950` / warn `#F0883E` / danger `#F85149`

### Light
- bg: `#FAFAF7` / panel: `#FFFFFF` / panel-2: `#F4F4EE`
- border: `#EAEAE3` / border-strong: `#D8D8D0`
- text: `#1A1A1A` / text-2: `#666` / text-3: `#999`
- accent: `#5B5BD6` / accent-soft: rgba(91,91,214,0.10)

### 通用
- radius: card 12 / btn 8 / tag 6 / input 8
- spacing: 4 8 12 16 24 32 48 64
- shadow-1: 0 1px 2px rgba(0,0,0,.06)
- shadow-2: 0 8px 24px rgba(0,0,0,.10)
- ease: cubic-bezier(0.16, 1, 0.3, 1)
- font: Inter, -apple-system, "Segoe UI", Roboto, sans-serif
- font-mono: "JetBrains Mono", ui-monospace, Menlo, monospace

---

## CR Must-Fix（已完成）

- [x] **M1** `Home.js` 移除 `Math.random()` 伪造 AI 状态 → `wiki_ready` 直接映射 `done` / `pending`
- [x] **M2** `Home.js` 真实账号不再混入 `MOCK_STATS`：空账号显式 `demo: true` 标记，已登录账号 0 就是 0
- [x] **M3** `mock/graph.js` `buildGraphFromFiles` 不再随机合成边，改为只返回节点 + `links: []`；`Graph.js` 新增 `RELATIONS PENDING` Pill
- [x] **M4** 删除 4 个 `*.new` 残留 + 根 `.gitignore` 增加 `*.new` / `*.bak` / `*.orig`
