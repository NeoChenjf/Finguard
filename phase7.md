# Phase 7 — 前端 MVP（Web UI）

## Chapter 1：Spec 前置计划（冻结）
说明：汇总本阶段关键 Spec 的最终决策与推荐理由，作为统一基线。

### 1) 技术栈选择
- **决策/推荐**：Vite + React + TypeScript + TailwindCSS。
- **理由**：React 生态成熟，TailwindCSS 适合快速构建现代、响应式的 UI，Vite 提供极速的开发体验。结合 Claude 3.5 Sonnet 的 UI 生成能力，能最高效地完成 MVP。
- **详情**：
  - 构建工具：Vite
  - 核心框架：React 18 + TypeScript
  - 样式方案：TailwindCSS + Lucide React (图标)
  - 路由管理：React Router v6

### 2) 页面路由与结构
- **决策/推荐**：采用单页面应用（SPA）架构，划分四个核心视图。
- **理由**：符合项目书对 MVP 的功能范围要求，逻辑清晰，易于后续扩展为 Tauri 桌面端。
- **详情**：
  - `/` (启动页)：检查后端 `8080` 端口的 `/health` 状态。若未配置 API Key 则引导至 `/settings`，否则进入主功能页。
  - `/settings` (设置页)：表单输入 API Key、模型 URL、模型名称。
  - `/plan` (配置建议页)：左侧表单输入（画像+持仓），右侧展示结果（建议权重、风控报告、操作清单）。
  - `/chat` (流式问答页)：类似 ChatGPT 的对话界面，支持多轮对话展示。

### 3) 状态管理与数据流
- **决策/推荐**：使用 Zustand 进行轻量级全局状态管理。
- **理由**：应用状态较简单（主要共享后端连接状态、当前配置信息），不需要 Redux 这样重的框架，Zustand API 简洁且支持 Hooks，非常适合当前体量。
- **详情**：
  - `useAppStore`：管理 `isBackendConnected`、`hasApiKey` 等全局状态。

### 4) SSE 流式请求处理 (POST)
- **决策/推荐**：使用 `@microsoft/fetch-event-source` 库或原生 `fetch` 配合 `ReadableStream` 处理 Server-Sent Events。
- **理由**：浏览器原生的 `EventSource` API 仅支持 `GET` 请求，而我们的 `/api/v1/chat/stream` 是 `POST` 请求（需要传递 JSON body），必须使用 `fetch` 方案。
- **详情**：
  - 解析 `text/event-stream` 格式。
  - 根据事件 `type` (`token` / `cite` / `metric` / `warning` / `done`) 实时更新 React 组件状态，实现打字机效果。

### 5) 前后端联调与跨域 (CORS)
- **决策/推荐**：前端运行在 Vite 默认端口（如 `5173`），直接请求后端 `http://localhost:8080`。
- **理由**：Phase 6 已经在后端全局开启了 CORS 支持（允许所有 Origin 和 Headers），前端无需配置复杂的代理，直接发起跨域请求即可。

---

## Chapter 2：Phase 7 计划

### 完成步骤：
1. **项目初始化**：在项目根目录创建 `frontend` 文件夹，使用 Vite 初始化 React + TS 项目，安装 TailwindCSS 及基础依赖（React Router, Zustand, Lucide Icons）。
2. **基础布局与路由搭建**：实现应用的整体 Layout（侧边栏导航）和四个基础页面的路由占位符。
3. **启动页与健康检查**：实现启动页逻辑，轮询后端 `http://localhost:8080/health` 接口，根据状态决定路由跳转。
4. **设置页开发**：
   - 调用 `GET /api/v1/settings` 回显当前配置（掩码显示 API Key）。
   - 实现表单提交，调用 `POST /api/v1/settings` 保存配置并提示成功。
5. **配置建议页开发**：
   - 实现用户画像与持仓的复杂表单输入。
   - 对接 `POST /api/v1/plan`。
   - 可视化展示返回的投资组合（表格/进度条）、风控报告（PASS/WARN/FAIL 徽章）与操作清单。
6. **流式问答页开发**：
   - 实现对话气泡界面与输入框。
   - 对接 `POST /api/v1/chat/stream`，处理 SSE 流式输出。
   - 特殊渲染：将 `cite`（引用）、`warning`（风控警告）、`metric`（耗时指标）以特殊 UI 样式嵌入到对话流中。
7. **UI 优化与响应式适配**：确保在 1280x720 及以上分辨率下显示正常，优化交互细节（如 Loading 骨架屏、错误 Toast 提示）。

### 验收项目：
1. **页面访问**：四个页面（启动、设置、配置建议、问答）均可正常访问，路由切换无报错。
2. **设置生效**：在设置页填入 API Key 并保存后，能够成功调用后端接口，且后续的配置建议和问答功能可正常使用。
3. **流式渲染**：问答页的 SSE 流式输出能够实时、平滑地渲染，无丢帧或阻塞现象，正确解析并展示 `cite`/`warning` 等自定义事件。
4. **响应式布局**：在 1280x720+ 分辨率下，界面布局合理，无元素重叠或溢出。

---

## Chapter 3：任务完成详情

### 步骤执行记录

- **步骤 1：项目初始化** —— 已完成
  - 使用 `npx create-vite@latest frontend --template react-ts` 创建项目。
  - Vite 最新版（v7.3.1）在交互式提示中需通过管道 `echo N |` 跳过 Vite 8 beta 选择。
  - `npm install` 安装 178 个基础包，0 漏洞。

- **步骤 2：依赖安装** —— 已完成
  - `npm install react-router-dom zustand lucide-react @microsoft/fetch-event-source` — 路由、状态管理、图标、SSE 处理。
  - `npm install -D tailwindcss @tailwindcss/vite` — TailwindCSS v4 + Vite 插件。
  - 采用 TailwindCSS v4 的 `@import "tailwindcss"` 新语法（无需 `tailwind.config.js`）。

- **步骤 3：基础布局与路由搭建** —— 已完成
  - 实现 `Layout.tsx`（左侧栏导航 + 右侧主内容区）。
  - 配置 React Router v6：`/chat`、`/plan`、`/settings` 三个路由 + 默认重定向。
  - 未连接后端时自动展示 `StartupPage`（全屏启动页）。

- **步骤 4：启动页与健康检查** —— 已完成
  - 轮询 `http://localhost:8080/health`，3 秒超时。
  - 连接成功后自动获取 `/api/v1/settings` 检查 API Key 配置状态。
  - 提供"重试连接"按钮。

- **步骤 5：设置页开发** —— 已完成
  - `GET /api/v1/settings` 回显当前配置（API Key 掩码显示）。
  - 表单支持部分更新（留空 API Key 则不修改）。
  - 保存后即时刷新全局状态，底部栏 API Key 警告自动消失。
  - 支持密码可见/隐藏切换。

- **步骤 6：配置建议页开发** —— 已完成
  - 左右双栏布局：左侧输入（用户画像、持仓列表、约束条件），右侧结果。
  - 持仓列表支持动态增删，实时计算总权重占比。
  - 结果可视化：进度条展示建议权重、PASS/WARN/FAIL 徽章。

- **步骤 7：流式问答页开发** —— 已完成
  - 对话气泡界面（用户蓝色、AI 灰色）。
  - 使用原生 `fetch` + `ReadableStream` 处理 POST SSE，逐 token 渲染（打字机效果）。
  - `cite`、`warning`、`metric` 事件以独立 UI 组件渲染在消息末尾。
  - 支持 `AbortController` 中断流式请求（红色停止按钮）。
  - Enter 发送、Shift+Enter 换行。

### 问题与解决

- **问题 1**：`npx create-vite@latest` 弹出"Use Vite 8 beta?"交互提示，阻塞自动化脚本。
  - 定位：Vite 7.x 开始在 CLI 中加入了 beta 版本选择提示。
  - 处理：通过 `echo N | npx create-vite@latest ...` 管道输入跳过。

- **问题 2**：TailwindCSS v4 配置方式变更。
  - 定位：v4 不再需要 `tailwind.config.js`，改为在 CSS 中直接 `@import "tailwindcss"`。
  - 处理：使用 `@tailwindcss/vite` 插件 + CSS import 新语法。

- **问题 3**：`index.css` 中多余的 `}` 导致 TailwindCSS 解析失败 ("Missing opening {")。
  - 定位：拷贝代码时产生额外闭合括号。
  - 处理：逐行检查 CSS 文件，删除多余括号后重启 Vite 即恢复。

- **问题 4**：PlanPage 用户画像字段不符合基金理念（`risk_level`/`horizon_months` 等通用字段 → 年龄/投资人类型/经验年限等专用字段）。
  - 定位：初版使用通用问卷模型，未对齐"守拙价值多元化基金理念"。
  - 处理：根据 `rules.yaml` 的 `questionnaire_schema` 和 `守拙价值多元化基金理念.md` 重新设计表单：
    - 年龄滑块（18-70）决定债券占比
    - 投资人类型（小白/经验丰富/专业投资人）三选一
    - 动态显示资格自评（经验年限、年化收益、跑赢 SP500）
    - 个股占比滑块（上限随类型变化）
    - 实时配置预览（黄金 10% + 债券 = 年龄十位数 + 指数比 1:3:16）

- **问题 5**：债券 ETF 选择 (TLT → VGIT)。
  - 定位：用户需求调整，从长期国债 (TLT) 改为中期国债 (VGIT)。
  - 处理：全局替换 4 处引用（PlanPage 默认持仓、预览标签、守拙理念文档、rules.yaml 后端配置）。

- **问题 6**：前端 POST `/api/v1/plan` 报 "Failed to fetch"。
  - 定位：浏览器同源策略限制，`localhost:5173` 向 `localhost:8080` 发 POST 触发 CORS 预检 (OPTIONS)，后端精确路由 `/api/v1/plan` 未注册 OPTIONS 方法导致 403。
  - 处理：在 Vite 配置中添加 `proxy`，将 `/api` 和 `/health` 转发到 `http://localhost:8080`，前端改用空 `API_BASE` 实现同源请求，彻底避免 CORS 问题。

- **问题 7**：后端 `/api/v1/plan` 返回固定 mock 数据，不响应画像参数变化。
  - 定位：占位符代码未移除，写死返回 SPY/BND/GLD/CASH。
  - 处理：实现完整守拙理念算配逻辑：
    - 黄金固定 10%
    - 债券 = 年龄十位数%
    - 股票 = 余量
    - 个股占比按类型限制（小白 0%、经验≤50%、专业≤equity）
    - 指数比 港:A:美 = 1:3:16
    - 资格检查（经验年限、年化收益、跑赢 SP500）
    - 单一资产下限 2.5% 检查（含浮点容差）

- **问题 8**：个股比例语义混乱（"占股票部分" vs "占总配置"）。
  - 定位：用户反馈个股统计不直观，希望改为占总配置百分比便于计算。
  - 处理：
    - 前端：`computeAllocation()` 改为直接扣除个股占比，不再乘股票部分
    - 后端：算配逻辑 `index_equity = equity - pick_pct`（而非 `equity × (1-pick_pct)`）
    - 文档：`守拙价值多元化基金理念.md` 同步更新语义定义
    - 上限动态计算：经验丰富 = min(50%, equity)；专业 = equity

- **问题 9**：持仓中"个股"字段无法识别（用户填 AAPL、TSLA，后端仍显示个股仓位=0）。
  - 定位：后端调仓比对逻辑只对比标准 ETF key，非标准 ETF 被忽略。
  - 处理：维护标准 ETF 白名单 `{VOO, 沪深300, 恒生指数, VGIT, GLD}`，持仓中不在白名单的自动累加到"个股仓位"，与目标个股占比对比后生成调仓建议。

- **问题 10**：投研问答返回 "unauthorized"。
  - 定位：ChatPage 从 Zustand store 的 `apiKey` 取值，但该字段默认为空（用户刷新页面后丢失，后端出于安全也不返回完整 key）。
  - 处理：后端 `/api/v1/chat/stream` 改为"若请求头无 `X-API-Key` 或为空，自动使用服务端 `llm.json` 中已配置的 key"；前端 `streamChat()` 当 `apiKey` 为空时不发送该 header。

- **问题 11**：港股占比刚好 2.5% 时仍触发 WARN。
  - 定位：浮点精度问题，`0.025 < 0.025` 可能为真。
  - 处理：增加容差判断 `hk < min_asset - 0.001`，只有真正 < 2.4% 才触发。

- **问题 12**：ChatPage 中大量告警信息堆叠，视觉杂乱。
  - 定位：每条告警独立显示，多条时占据大片空间。
  - 处理：实现 `WarningCollapse` 组件，默认折叠显示"N 条风控提示"，点击后展开详情，再点收起，箭头带旋转动画。

---

## Chapter 4：验收详情

### 验收清单

| # | 验收项 | 状态 | 说明 |
|---|--------|------|------|
| 1 | 页面访问 | ✅ PASS | 四个页面均可访问，路由切换无报错，TypeScript 编译零错误 |
| 2 | 设置生效 | ✅ PASS | POST /settings 成功写入配置，GET /settings 正确返回掩码后 API Key |
| 3 | 流式渲染 | ✅ PASS | SSE 事件正确解析，token/cite/warning/metric/done 五种类型均正确渲染 |
| 4 | 响应式布局 | ✅ PASS | 1280x720+ 分辨率下布局无溢出，侧边栏+主内容区双栏正常显示 |
| 5 | 后端联调 | ✅ PASS | /health → ok, /settings → model=qwen-plus, /plan → risk=PASS, 前端 → 200 |

### 产物列表

| 文件 | 说明 |
|------|------|
| `frontend/package.json` | 前端项目配置与依赖声明 |
| `frontend/vite.config.ts` | Vite 构建配置（含 TailwindCSS 插件） |
| `frontend/index.html` | 入口 HTML（中文标题、深色背景） |
| `frontend/src/main.tsx` | React 入口 |
| `frontend/src/App.tsx` | 根组件：路由定义、启动检查逻辑 |
| `frontend/src/index.css` | TailwindCSS 导入 + 自定义滚动条/光标动画 |
| `frontend/src/api/client.ts` | 后端 API 客户端（health, settings, plan, chat/stream） |
| `frontend/src/store/useAppStore.ts` | Zustand 全局状态管理 |
| `frontend/src/components/Layout.tsx` | 应用布局（侧边栏导航 + 内容区） |
| `frontend/src/pages/StartupPage.tsx` | 启动页：后端连接检测与引导 |
| `frontend/src/pages/SettingsPage.tsx` | 设置页：API Key/模型/URL 配置 |
| `frontend/src/pages/PlanPage.tsx` | 配置建议页：画像输入 + 结果可视化 |
| `frontend/src/pages/ChatPage.tsx` | 流式问答页：SSE 打字机效果 + 元数据渲染 |

### 变更日志
- [2026-02-26] Phase 7 完成：前端 MVP 全部四个页面实现，通过联调验收。
