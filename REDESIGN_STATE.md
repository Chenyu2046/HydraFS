# HydraFS 视觉升级 · 状态记忆文档

> 用于 AI 助手会话断了之后快速恢复上下文。每完成一个 Phase 在末尾追加 changelog。
> 当前会话工作分支：`feature/front_opt`

---

## 1. 总体决策（已确认 · 不要再改）

- **信息架构**：从"左侧栏承载主功能"改为 **顶部主导航 + 上下文侧栏**
  - `Overview / Graph / Shared` → 全宽无侧栏
  - `Files / Knowledge` → 内嵌自己的上下文侧栏（不再用全局 Sidebar）
- **Logo 方向**：**A · 折叠节点**（黑曜石灵感 — 倾斜多边形 + 内部 3 节点连线 + 斜向高光）
- **品牌色**：保留现有 `accent` 紫，但加入 `obsidian` 深蓝紫色族作为 Logo / Hero 锚点
- **Motion 库选型**：**不引入 framer-motion**，用 CSS transition + IntersectionObserver 原生方案（零依赖增加，体积最优）
- **CR 节奏**：每个 Phase 独立提交 + push，CR 通过后再做下一个 Phase

---

## 2. Phase 计划与状态

| Phase | 内容 | 状态 | 提交 |
|---|---|---|---|
| **1** | 设计系统扩展 + Logo + Motion lib + Topbar 接入 Logo | ✅ 完成 | — |
| **2** | 信息架构改造：顶导 TopNav + AppShell 去除全局 Sidebar | ✅ 完成 | — |
| **3** | Hero 滚动视差 + Bento scroll-in 错峰淡入 | ✅ 完成 | — |
| **4** | 子页面打磨（Login Logo / Graph 空状态 / Knowledge 卡片 / 文案统一） | ✅ 完成 | — |
| **5** | Microcopy 字典 `lib/copy.js` + 全站替换 + `prefers-reduced-motion` | ✅ 完成 | — |

---

## 3. 设计系统扩展约定（Phase 1 落地）

### 颜色新增
```
obsidian: {
  600: '#1E1B4B'   // Logo 深色主体
  500: '#3730A3'   // 品牌主色（替代场景 accent）
  400: '#6366F1'   // 交互高亮
  300: '#A5B4FC'   // Logo 高光 / accent soft
}
```
- `obsidian` 是 **品牌锚点**，仅用于 Logo / Hero 标题渐变 / 主 CTA hover
- 现有 `accent` (`#7C5CFF` / `#5B5BD6`) 仍作 UI 交互色保留，**不破坏**已有组件

### 字体层级新增（在 fontSize 基础上加 `display`/`title`）
```
fontSize.display  '52px'   // Hero 主标题
fontSize.title    '34px'   // 分区主标
```
（保持向后兼容：旧的 `display: '36px'` → 改为 `'52px'`，旧 Home Hero 标题视觉自然变大；如果发现哪个组件用 display 不合适，按需加 inline override）

### Motion token 扩展
```
duration.story  '680ms'   // 大型进场（Hero / Graph 自转）
ease.spring     'cubic-bezier(.34,1.56,.64,1)'
```

### 文件位置
- `picture_bed/src/styles/tokens.js` — 扩展（不破坏）
- `picture_bed/src/lib/motion.js` — **新建**，导出统一的 CSS transition 字符串 + IntersectionObserver hook
- `picture_bed/src/components/Logo.js` — **新建**，SVG 黑曜石折叠节点

---

## 4. Phase 2 信息架构改造（待开始 · 设计稿）

### TopNav 结构（pill 风毛玻璃浮条）
```
[ Logo HydraFS ]   Overview  Files  Knowledge  Graph  Shared        [⌘K 搜索] [🌓] [Avatar▾]
```
- 高度 56px，圆角 28px，左右各 24px 留白
- sticky top:0，毛玻璃 backdrop-filter
- 当前路由项 underline + obsidian/400 强调
- ⌘K 全局命令栏（Phase 3 接入；Phase 2 先放占位）

### 上下文侧栏出现规则
| 路由 | 全局 Sidebar | 上下文 Sidebar |
|---|---|---|
| `/` Overview | ❌ | ❌ |
| `/files` | ❌ | ✅（文件夹/类型过滤） |
| `/knowledge` | ❌ | ✅（Provider/任务队列） |
| `/graph` | ❌ | ❌（floating toolbar 代替） |
| `/shared` | ❌ | ❌ |
| `/wiki/:md5` | ❌ | ✅（大纲/反链） |

### AppShell 改造要点
- 删除/隐藏 `Sidebar.js`
- AppShell 接收 `slot` 模式：`<AppShell aside={<FilesContextSidebar/>}>`
- mobile 端（≤768px）：上下文侧栏变 Drawer

---

## 5. Phase 3 首页 Hero 设计稿（待开始）

### 三屏滚动叙事
```
[屏 1 · 100vh]
  Floating TopNav（毛玻璃 pill）
  Display 标题: "你的文件，不该只是文件。"
                斜体高亮: "让它们彼此发现。"
  副标题（一句产品价值）
  ⌘K 命令栏（替代当前 Search 输入）
  背景: LiveGraph Canvas（缓慢自转的力导向图，pastel 节点 + 低 opacity 边）

[屏 2]
  Capabilities Bento (1 大 + 2 中 + 3 小)
  滚动触发: 卡片 IntersectionObserver 错峰淡入

[屏 3]
  左: 你的工作台（最近 / 处理中）
  右: Pipeline 演示 stepper（自动循环 done→active）
```

### Hero 滚动联动
- 标题 `translateY(0 → -40px)` + `opacity(1 → 0.3)`（滚到 30vh 时完成）
- 背景 Graph `scale(1 → 1.06)`（滚到 100vh 时完成）
- 用 `window.scroll` 监听 + `requestAnimationFrame` 节流

---

## 6. Microcopy 字典（Phase 5 集中替换 · 提前归档）

| key | 现状 | 升级后 |
|---|---|---|
| `empty.files` | 暂无数据 | 这里还很安静 — 上传一个文件试试？ |
| `loading.default` | 加载中... | 正在为你整理… |
| `upload.success` | 上传成功 | 已经放进你的工作台 ✓ |
| `upload.fail` | 上传失败 | 这个文件没能进来，要不要再试一次？ |
| `search.placeholder` | 请输入关键词 | 描述你想找的，比如"上周的会议纪要" |
| `auth.expired` | Token 失效 | 登录状态过期了，重新登录一下吧 |
| `empty.graph` | 知识图谱为空 | 你的知识地图还在形成… 上传几个关联的文件，看它们彼此找到对方。 |
| `empty.knowledge` | 暂无知识节点 | 还没有知识节点 — 上传一个 Markdown 试试？ |
| `provider.empty` | 未配置 Provider | 还没接入 AI — 选一个 Provider 开始？ |
| `process.fail` | 处理失败 | 这次没能理解这个文件，可能是格式不支持。详情 → |
| `search.noResult` | 没有结果 | 没找到相关的，换个说法试试？ |
| `upload.dragHint` | 拖拽文件到此处 | 把文件放到这里 — 我来帮你理解它 |

---

## 7. 严格红线（不要破坏）

- ❌ 不动 `services/*` 任何 API 调用
- ❌ 不动 `contexts/AuthContext.js` 鉴权流
- ❌ 不动 `App.js` 路由表（信息架构改造仅改 AppShell 内部布局）
- ❌ 不动 Upload / Chunk Upload 流水线
- ❌ 不新增运行时依赖（除非用户明确同意）
- ✅ 所有新组件 lint 干净，无 console.warn
- ✅ 每个 Phase 提交可独立回滚

---

## 8. Changelog（每完成一个 Phase 在此追加）

### Phase 1 · 2026-05-16
- `styles/tokens.js`: 新增 `obsidian` 色族；`fontSize` 新增 `display(52px)` / `title(34px)`；`duration` 新增 `story(680ms)`；`ease` 新增 `spring`
- `components/Logo.js`: 新建，SVG 黑曜石折叠节点（24/32/48 三种尺寸）+ wordmark 可选
- `lib/motion.js`: 新建，导出 `transitions` / `useInView` / `useScrollProgress` 工具
- `components/Topbar.js`: 字符 "HydraFS" → `<Logo withWordmark size={20} />`
- 范围严格控制：未动路由、未动 services、未动 Sidebar

### Phase 2 · 2026-05-16
- `components/Topbar.js` 完全重写：合并主导航五项 + Logo + ⌘K 搜索占位 + 主题切换 + Avatar；pill 风毛玻璃 nav 条
- `components/AppShell.js` 完全重写：移除左侧 Sidebar（`margin-left:64px` 去掉），新增 `aside` slot（子页面可注入上下文面板），保留 `flushContent` / `transparentTopbar`
- `components/Sidebar.js` 保留文件但不再被任何路由引用（安全，可后续删除）
- `App.js` 路由表保持不变；旧的 `crumbs` prop 仍传入 AppShell 但被忽略（向后兼容）
- 移动端 `<880px` 顶部 nav 自动隐藏（后续可加汉堡 Drawer）

### Phase 3 · 2026-05-16
- `pages/Home.js` Hero 新增滚动视差：
  - `HeroLeft` 整体 `translateY(0 → -32px)` + `opacity(1 → 0.35)`，滚动 0~600px 区间
  - `MiniGraph` 容器 `scale(1 → 1.05)`，慢速放大制造深度感
  - 使用 `useWindowScrollY` 节流（rAF，性能安全）
- `components/Bento.js` 6 张卡片改为 scroll-in 错峰淡入：
  - Grid 用 `useInView({ threshold: 0.05 })` 翻转 `data-inview`
  - 每张 Card 通过 `:nth-of-type(n)` 设置 `transition-delay: 0/70/140/210/280/350ms`
  - 只动 opacity（不动 transform，避免与 hover translateY(-3px) 冲突）
  - 添加 `@media (prefers-reduced-motion: reduce)` 兜底，opacity 立即为 1
- Workspace 区（QuickUpload + Recent Nodes + AI Pipeline）整体 `useInView` 触发 `fadeUp`

### Phase 4 · 2026-05-16
- `pages/Login.js`：Hero 区品牌头 H 方块 → `<Logo size={30} withWordmark>`；导入 copy 字典
- `pages/Graph.js`：副标题、节点搜索 placeholder、空状态、选择提示文案全部从 `copy.*` 取
- `pages/Knowledge.js`：所有 message / placeholder / 空状态文案全部从 `copy.*` 取，包括 `provider.rebuildDone(ok, total)` 函数式文案
- `pages/FileList.js`：error/success 文案、搜索 placeholder、空状态文案统一替换
- `pages/SharedHub.js`：转存成功/失败/已存在、空状态文案统一替换

### Phase 5 · 2026-05-16
- `lib/copy.js` 新建：完整微文案字典（auth / loading / empty / upload / action / search / provider / graph / generic 共 9 个分组）
- 所有触达用户的文字均带"你/我"对话感
- `Bento.js` / `motion.js` 均尊重 `prefers-reduced-motion`
- 删除 / 失败类文案统一改为"建议下一步"（如："要不要再试一次？"）
- 空状态语气从"通知"改为"邀请"（如："你的知识地图还在形成…"）

### 不在本轮范围（后续可选）
- ⌘K 全局命令面板（Topbar 已留位 + Tooltip）
- 移动端汉堡导航 Drawer
- Files 页面 Insight 视图（grid/list 之外的第三视图）
- 全站空状态插画 SVG（目前仍用 antd Empty 占位）
- 删除 `components/Sidebar.js` 文件本体（当前仅去引用）
