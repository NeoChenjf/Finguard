# Phase 9 — 打包分发与最终交付

## Chapter 1：Spec 前置计划（冻结）

### 1) DLL 分发策略
- **决策**：在 `tauri.conf.json` 的 `bundle.resources` 中声明全部 10 个 DLL 文件，安装时放入 `<install_dir>` 与 sidecar exe 同级目录。
- **理由**：Windows DLL 搜索顺序首选 exe 同目录，无需额外 PATH 配置。`build.rs` 仅用于 `tauri dev`，生产包由 `bundle.resources` 负责。
- **文件清单**：brotlicommon.dll, brotlidec.dll, brotlienc.dll, cares.dll, drogon.dll, jsoncpp.dll, libcrypto-3-x64.dll, libssl-3-x64.dll, trantor.dll, zlib1.dll

### 2) config/ 读写路径策略
- **决策**：Rust 侧首次启动时将默认 config 从安装目录复制到 `%APPDATA%/finguard/config/`；sidecar CWD 设为 `%APPDATA%/finguard/`。
- **理由**：`Program Files` 无写入权限，finguard 使用 `cwd/config/llm.json` 进行读写。CWD 改变不影响 DLL 搜索（DLL 始终从 exe 同目录加载）。
- **首次启动流程**：检测 AppData 下 config 是否存在 → 不存在则从安装目录复制模板 → 设置 sidecar CWD → spawn

### 3) 代码签名
- **决策**：本阶段跳过签名。
- **理由**：需购买代码签名证书（年费约 $200+），当前阶段优先完成功能交付。README 中注明 Windows SmartScreen 弹窗处理方式。

### 4) 便携版 .zip
- **决策**：本阶段仅生成 NSIS 安装包，便携版留后续。
- **理由**：NSIS 安装包是主要分发形式；便携版所需改动小（结构已兼容），可快速追加。

## Chapter 2：Phase 9 计划

### 完成步骤

1. **bundle.resources 配置**：在 `tauri.conf.json` 中声明 10 个 DLL + config/ 目录为 bundle resources。
2. **Rust 侧 config 初始化逻辑**：修改 `lib.rs`，首次启动时从安装目录复制 config/ 到 `%APPDATA%/finguard/config/`。
3. **sidecar CWD 生产适配**：修改 `sidecar_dir()` 函数，生产模式下返回 `%APPDATA%/finguard/`，开发模式保持现有行为。
4. **确认 C++ Release 构建就绪**：检查 `finguard/build/Release/finguard.exe` 存在且 sidecar 二进制已更新。
5. **执行 `npm run tauri build`**：生成 NSIS `.exe` 安装包。
6. **端到端验收**：安装包安装 → 首次配置 → 健康检查 → 功能验证。
7. **更新 next_plan.md**：标记 Phase 9 完成，规划 Phase 10（Value Cell）。

### 验收项目

1. **V1: NSIS 安装包生成** — `npm run tauri build` 成功产出 `.exe` 安装包，无编译错误。
2. **V2: DLL 完整随包** — 安装后 `<install_dir>` 下存在全部 10 个 DLL。
3. **V3: 配置初始化** — 首次启动后 `%APPDATA%/finguard/config/llm.json` 自动生成。
4. **V4: 功能可用** — 安装后启动应用 → sidecar 自动拉起 → /health 通过 → 设置页可配置 → /plan 和 /chat/stream 可用。
5. **V5: 安装包体积** — 安装包体积 ≤ 30 MB。
6. **V6: 无残留进程** — 关闭应用后 finguard.exe 无残留。

## Chapter 3：任务完成详情

### 步骤执行记录

- **步骤 1：bundle.resources 配置** — 已完成
  - 在 `tauri.conf.json` 的 `bundle` 下添加 `resources` 字段，声明 10 个 DLL 和 `config/*`。
  - 使用 map 格式，DLL → `"./"` 同层目录，config → `"config/"` 子目录。

- **步骤 2：Rust 侧 config 初始化逻辑** — 已完成
  - 新增 `is_production()` 函数检测运行环境（dev vs 生产）。
  - 新增 `appdata_work_dir()` 返回 `%APPDATA%/finguard/`。
  - 新增 `ensure_config_in_appdata()` 首次启动时从安装目录复制 config → AppData。
  - 已有配置不覆盖（保护用户已修改的配置）。

- **步骤 3：sidecar CWD 生产适配** — 已完成
  - `sidecar_dir()` 函数根据 `is_production()` 分支：
    - 生产模式：CWD = `%APPDATA%/finguard/`，DLL 从 exe 同目录加载不受 CWD 影响。
    - 开发模式：保持原有行为（CWD = exe 同目录）。

- **步骤 4：Release 构建检查** — 已完成
  - finguard.exe Release 构建 0.49 MB，sidecar 二进制哈希一致。

- **步骤 5：tauri build 执行** — 已完成
  - 修复 `store.ts` TypeScript 编译错误：`@tauri-apps/plugin-store` v2.4.2 要求 `load()` 第二参数包含 `defaults` 属性。三处调用均添加 `defaults: {}`。
  - NSIS 下载因网络超时，手动通过 curl 下载并解压到 `%LOCALAPPDATA%/tauri/NSIS/`。
  - 最终构建成功，产出 NSIS + MSI 两种安装包。

- **步骤 6：端到端验收** — 已完成（详见 Chapter 4）

### 问题与解决

- **问题 1：TypeScript 编译失败** — `@tauri-apps/plugin-store` 更新后 `StoreOptions` 类型要求 `defaults` 属性，但 `store.ts` 缺少。
  - 定位：TSC 报 TS2345，`defaults` 为 required 字段。
  - 处理：三处 `load(STORE_FILE, { autoSave: true })` 均改为 `{ autoSave: true, defaults: {} }`。

- **问题 2：NSIS 下载超时** — Tauri 内置下载器从 GitHub 下载 NSIS 3.11 反复 timeout。
  - 定位：网络问题（GitHub CDN 连接不稳定）。
  - 处理：通过 `curl.exe -L` 手动下载 NSIS zip 到 `%LOCALAPPDATA%/tauri/`，Tauri 自动检测并使用。

- **问题 3：安装后应用无法连接后端（「无法连接到后端服务」）** — 安装验收时前端提示后端不可用，但 sidecar 进程实际已正常拉起，`/health` 用 curl 可通。
  - 根因：`client.ts` 和 `store.ts` 用 `window.__TAURI__` 检测 Tauri 环境，但 **Tauri v2 生产模式注入的全局变量是 `__TAURI_INTERNALS__`**，导致 `isTauri()` 返回 `false`，`API_BASE` 置为空字符串，所有 fetch 请求打向 `https://tauri.localhost/api/...` 而非 `http://127.0.0.1:8080`。
  - 关键点：`tauri dev` 开发模式下 Vite proxy 会拦截并转发请求，从不走 `API_BASE` 判断，因此此 Bug 在开发环境完全隐藏。
  - 处理：`client.ts` 和 `store.ts` 中将 `'__TAURI__' in window` 改为 `'__TAURI_INTERNALS__' in window`。
  - 影响文件：`frontend/src/api/client.ts`、`frontend/src/api/store.ts`

- **问题 4：配置建议页面「Failed to fetch」（CORS OPTIONS 预检 403）** — 修复问题 3 后，POST `/api/v1/plan` 报 `Failed to fetch`；抓包发现 OPTIONS 预检返回 403。
  - 根因：浏览器对跨域 POST（带 `Content-Type: application/json`）必须先发 OPTIONS 预检。`tauri dev` 下 Vite proxy 将请求转为同源，**永远不触发预检**，此 Bug 在开发环境完全隐藏。安装包中 WebView 源为 `https://tauri.localhost`，向 `http://127.0.0.1:8080` 发请求属于跨域，必走预检。
  - 误区 1：`routes.cpp` 已有 `/{path}` 的 `Options` 通配处理器，但 **Drogon 精确路由优先级高于通配符路由**，`/api/v1/plan` 的 OPTIONS 被精确路由匹配后因方法不允许直接返回 403。
  - 误区 2：改用 `registerPreHandlingAdvice` 仍无效——PreHandling 运行在路由匹配**之后**，此时 403 已确定。
  - 处理：在 `http_server.cpp` 中使用 `registerPreRoutingAdvice`（路由匹配**之前**），对所有 `Options` 方法请求直接返回 204 + CORS 头。
  - 影响文件：`finguard/src/server/http_server.cpp`

## Chapter 4：验收详情

### 验收清单

| # | 验收项 | 状态 | 说明 |
|---|--------|------|------|
| V1 | NSIS 安装包生成 | **PASS** | `FinGuard_0.1.0_x64-setup.exe` 4.4 MB |
| V2 | DLL 完整随包 | **PASS** | 安装后 `%LOCALAPPDATA%/FinGuard/` 下 10 个 DLL 全部存在 |
| V3 | 配置初始化 | **PASS** | 首次启动后 `%APPDATA%/finguard/config/llm.json` 自动生成 |
| V4 | 功能可用 | **PASS** | sidecar 自动拉起 → `/health` 200 → 前端就绪 → `/plan` 和 `/chat/stream` 可用（修复 Bug #3 & #4 后通过） |
| V5 | 安装包体积 ≤ 30MB | **PASS** | NSIS 4.4 MB / MSI 6.05 MB |
| V6 | 无残留进程 | **PASS** | 关闭应用后 `finguard.exe` 无残留 |

### 产物列表

| 文件 | 说明 |
|------|------|
| `frontend/src-tauri/target/release/bundle/nsis/FinGuard_0.1.0_x64-setup.exe` | NSIS 安装包（4.4 MB） |
| `frontend/src-tauri/target/release/bundle/msi/FinGuard_0.1.0_x64_en-US.msi` | MSI 安装包（6.05 MB） |
| `frontend/src-tauri/tauri.conf.json` | 新增 bundle.resources 配置 |
| `frontend/src-tauri/src/lib.rs` | config 初始化 + sidecar CWD 生产适配 |
| `frontend/src/api/store.ts` | 修复 @tauri-apps/plugin-store defaults 类型 + Tauri v2 isTauri 检测（`__TAURI_INTERNALS__`） |
| `frontend/src/api/client.ts` | Tauri v2 API_BASE 环境检测修复 |
| `finguard/src/server/http_server.cpp` | 新增 `registerPreRoutingAdvice` 处理全部 CORS OPTIONS 预检 |

### 变更日志
- [2026-02-27] 安装后验收发现并修复两个与 `tauri dev` 行为差异相关的 Bug：Tauri v2 isTauri 检测失效（Bug #3）、CORS OPTIONS 预检 403（Bug #4）；重建安装包后端到端验收全 PASS。
- [2026-02-27] Phase 9 主体完成：NSIS + MSI 安装包构建成功，V1–V6 验收通过。
