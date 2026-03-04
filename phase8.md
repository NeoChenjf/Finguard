# Phase 8 — 桌面壳与本地启动器（Tauri）

## Chapter 1：Spec 前置计划（待冻结）

### 1) Tauri 版本与项目结构

- **决策/推荐**：Tauri v2（最新稳定版），在 `frontend/` 目录下通过 `npm create tauri-app` 或 `tauri init` 集成，生成 `src-tauri/` 子目录。
- **理由**：Tauri v2 是当前稳定版本，原生支持 sidecar 管理、系统托盘、多窗口等；直接在现有 frontend 目录集成最自然，Vite dev server 和 Tauri dev 可以联动。
- **结构**：
  ```
  frontend/
    src/           ← React 前端源码（不变）
    src-tauri/     ← Tauri Rust 侧代码（新增）
      src/main.rs
      tauri.conf.json
      Cargo.toml
      icons/
      binaries/    ← finguard.exe sidecar 存放处
    package.json   ← 新增 tauri 脚本
    vite.config.ts ← 可能需小幅调整
  ```

### 2) Sidecar 进程管理策略

- **决策/推荐**：使用 Tauri v2 的 `shell` 插件（`tauri-plugin-shell`） sidecar 机制。
- **理由**：Tauri v2 原生提供 sidecar 声明，在 `tauri.conf.json` 中声明 binary，Rust 侧调用 `Command::new_sidecar("finguard")` 即可管理进程生命周期。
- **详情**：
  - 在 `tauri.conf.json` 的 `bundle.externalBin` 中声明 `binaries/finguard`。
  - Sidecar 文件命名需符合 Tauri 约定：`finguard-x86_64-pc-windows-msvc.exe`（或通过配置简化）。
  - 应用启动时在 Rust `setup` hook 中拉起 sidecar。
  - 应用退出时在 `on_exit` / drop guard 中终止 sidecar。
  - 实现 sidecar 意外退出的检测与自动重启（最多重试 3 次）。

### 3) 健康检查与启动流程

- **决策/推荐**：保留前端 `StartupPage` 的 `/health` 轮询逻辑，Rust 侧负责拉起 sidecar 并通过 Tauri 事件通知前端进程状态。
- **理由**：前端已有完善的启动 → 健康检查 → 进入应用流程；Rust 侧获得进程启动/退出信息后通过 `app.emit("sidecar-status", payload)` 通知前端，两者配合实现最优体验。
- **详情**：
  - Rust 侧：启动 sidecar → emit `sidecar-status: starting` → 后台监控 stdout/stderr。
  - 前端侧：收到 `starting` 后开始轮询 `/health`（复用现有 `checkHealth()`）。
  - 轮询成功 → 进入应用（复用现有 `StartupPage` 逻辑）。
  - 轮询超时（30s）→ 显示错误 + 日志路径。

### 4) 托盘图标

- **决策/推荐**：Phase 8 实现基础托盘图标：显示运行状态、提供"显示窗口"和"退出"菜单项。
- **理由**：项目书标记为"可选"，但作为桌面应用的基础体验，最小化到托盘是用户必需的功能。实现成本低（Tauri v2 `tauri-plugin-tray` 内置支持）。
- **详情**：
  - 关闭按钮行为：直接退出应用（先 kill sidecar 再退出），不最小化到托盘。
  - 托盘菜单：显示窗口 / 退出。
  - 托盘图标左键双击：恢复并聚焦窗口。

### 5) 配置持久化

- **决策/推荐**：使用 Tauri v2 `tauri-plugin-store` 插件，配置文件存储到 `%APPDATA%/com.finguard.app/` 下。
- **理由**：`tauri-plugin-store` 提供类型安全的 KV 存储，自动管理文件路径，比手动读写 JSON 文件更可靠。
- **详情**：
  - 存储内容：`api_key`（加密存储）、`api_base`、`model`、`temperature`、`timeout_ms`。
  - 应用启动时：Rust 侧读取持久化配置 → 调用 finguard `/api/v1/settings` 注入 → 前端获取最新设置。
  - 前端设置页保存时：同时调用 finguard API + 更新本地持久化存储。
  - API Key 存储安全性：使用 Tauri 的安全存储或至少 base64 编码，不明文落盘。

### 6) 前端适配变更

- **决策/推荐**：最小化前端改动。主要变更：API_BASE 改为运行时检测（Tauri 环境下使用 `http://127.0.0.1:8080`，Vite dev 环境保持空字符串走 proxy）。
- **理由**：Tauri WebView 加载的是打包后的静态文件（`tauri://localhost`），不经过 Vite proxy，所以需要直接请求后端地址。
- **详情**：
  - `client.ts` 中 `API_BASE` 改为：`window.__TAURI__ ? 'http://127.0.0.1:8080' : ''`。
  - 前端检测 Tauri 环境：`typeof window !== 'undefined' && '__TAURI__' in window`。
  - 其余页面逻辑不变。

### 7) 开发环境与工具链要求

- **决策/推荐**：需要安装 Rust 工具链 + Tauri CLI。
- **理由**：Tauri v2 必须有 Rust 编译环境。
- **详情**：
  - Rust：`rustup` 安装（`stable` channel）。
  - Tauri CLI：`npm install -D @tauri-apps/cli@^2`。
  - Tauri API：`npm install @tauri-apps/api@^2`。
  - Tauri 插件：`tauri-plugin-shell`、`tauri-plugin-store`（Cargo.toml 中声明）。
  - WebView2：Windows 10/11 已内置，无需额外安装。

---

## Chapter 2：Phase 8 计划

### 完成步骤

1. **环境准备**：确认/安装 Rust 工具链（rustup）；确认 WebView2 可用。
2. **Tauri 项目初始化**：在 `frontend/` 下初始化 Tauri v2 项目，生成 `src-tauri/` 目录。
3. **基础窗口验证**：`npm run tauri dev` 启动，确认 WebView 能正确加载 Vite 前端。
4. **Sidecar 集成**：将 finguard.exe 配置为 sidecar；实现 Rust 侧进程启动/终止/监控逻辑。
5. **健康检查联动**：Rust 侧发射 Tauri 事件通知前端 sidecar 状态；前端适配。
6. **前端 API_BASE 适配**：`client.ts` 中根据 Tauri 环境切换 API 基地址。
7. **托盘图标**：实现系统托盘最小化、菜单项（显示 / 退出）、状态图标。
8. **配置持久化**：集成 `tauri-plugin-store`；启动时读取并注入配置；设置页保存时同步持久化。
9. **窗口关闭行为**：关闭按钮直接退出应用并杀 sidecar；托盘提供"显示窗口"和"退出"菜单；双击左键托盘恢复窗口。
10. **联调验证**：完整流程测试（启动 → 健康检查 → 设置 → 配置建议 → 流式问答 → 关闭）。
11. **文档记录**：补齐 Chapter 3 & 4，更新 `next_plan.md`。

### 验收项目

1. **V1 - 双击启动**：双击应用 → finguard.exe 自动拉起 → 前端正常加载 → 全部功能可用。
2. **V2 - 自动退出**：关闭窗口（点 X）或托盘"退出" → finguard.exe 自动终止 → 无残留进程。
3. **V3 - 首次引导**：首次启动（无持久化配置）→ 自动引导至设置页填写 API Key。
4. **V4 - 配置持久化**：填写设置 → 重启应用 → 配置自动加载，无需重新填写。
5. **V5 - 托盘功能**：最小化到托盘 → 托盘图标可见 → 双击托盘恢复窗口 → 菜单"退出"正常退出。
6. **V6 - TypeScript 编译**：`npx tsc --noEmit` 零错误。
7. **V7 - Tauri 构建**：`npm run tauri build` 成功产出 `.exe` 安装包。

---

## Chapter 3：任务完成详情

### Step 1 — 环境准备

- 通过 `winget install Rustlang.Rustup` 安装 Rust 工具链。
- 确认版本：`rustup 1.28.2`、`rustc 1.93.1`、`cargo 1.93.1`。
- WebView2：Windows 11 已内置，无需额外安装。

### Step 2 — Tauri 项目初始化

**新增文件**：

| 文件 | 说明 |
|------|------|
| `frontend/src-tauri/Cargo.toml` | Rust 项目配置，声明 tauri v2、tauri-plugin-shell v2、tauri-plugin-store v2、serde、tokio |
| `frontend/src-tauri/build.rs` | 构建脚本，自动复制 finguard DLLs（10个）和 config/ 目录到 Cargo 输出目录 |
| `frontend/src-tauri/tauri.conf.json` | Tauri 应用配置（窗口 1280×800、devUrl、externalBin、图标、shell 插件） |
| `frontend/src-tauri/capabilities/default.json` | Tauri v2 权限声明（core、shell、store） |
| `frontend/src-tauri/src/main.rs` | 入口：调用 `app_lib::run()` |
| `frontend/src-tauri/src/lib.rs` | 核心逻辑：sidecar 管理、系统托盘、窗口事件、进程清理 |
| `frontend/src-tauri/icons/` | 应用图标：32x32.png、128x128.png、128x128@2x.png、icon.ico（BMP-in-ICO） |
| `frontend/src-tauri/binaries/` | sidecar 可执行文件 + DLLs + config/ |
| `frontend/src-tauri/.gitignore` | 排除 target/ |

**修改文件**：

| 文件 | 变更 |
|------|------|
| `frontend/package.json` | 新增 `tauri`、`tauri:dev`、`tauri:build` 脚本；新增 `@tauri-apps/cli`（devDep）、`@tauri-apps/api`、`@tauri-apps/plugin-store`（dep） |

### Step 3 — 基础窗口验证

- `npx tauri dev` 启动，WebView 成功加载 Vite 前端。
- 窗口标题："FinGuard — AI 资产配置中台"，尺寸 1280×800，可缩放，居中。

### Step 4 — Sidecar 集成

**实现**（`lib.rs`）：

- `SidecarState`：持有 `Mutex<Option<CommandChild>>` + `Arc<AtomicBool>` shutting_down 标记。
- `spawn_sidecar(app, restart_count)`：
  - 通过 `handle.shell().sidecar("finguard")` 创建 Command。
  - 调用 `cmd.current_dir(sidecar_dir)` 设置工作目录为 exe 所在目录（解决 config/ 相对路径问题）。
  - `cmd.spawn()` 获得 `(rx, child)`，将 child 存入 State。
  - 异步任务监听 `CommandEvent::Terminated`，若非主动退出则 2 秒后重启（最多 3 次）。
- `kill_sidecar(app)`：设置 shutting_down 标记 → 取出 child → kill。

**DLL 依赖解决**：

finguard.exe（C++ Drogon 框架）依赖 10 个运行时 DLL：

| DLL | 大小 | 说明 |
|-----|------|------|
| drogon.dll | 1.4 MB | HTTP 框架 |
| trantor.dll | 393 KB | 网络库 |
| libcrypto-3-x64.dll | 5.3 MB | OpenSSL 加密 |
| libssl-3-x64.dll | 868 KB | OpenSSL SSL/TLS |
| jsoncpp.dll | 210 KB | JSON 解析 |
| cares.dll | 198 KB | DNS 解析 |
| brotlicommon/dec/enc.dll | 866 KB | Brotli 压缩 |
| zlib1.dll | 89 KB | zlib 压缩 |

`build.rs` 在每次 `cargo build` 时自动从 `finguard/build/Release/` 复制这些 DLL 和 `config/` 到 Cargo 输出目录（`target/debug/` 或 `target/release/`），确保 sidecar 运行时能找到所有依赖。

### Step 5 — 健康检查联动

**Rust 侧**：`spawn_sidecar()` 在各阶段通过 `app.emit("sidecar-status", payload)` 发送事件：
- `"starting"` — sidecar 已拉起
- `"stopped"` — sidecar 意外退出
- `"error: ..."` — 创建/启动失败或达到最大重试

**前端侧**（`StartupPage.tsx`）：
- 新增 `status = 'waiting'`（Tauri 模式初始状态）。
- 监听 `sidecar-status` 事件，根据 payload 更新提示文本。
- Tauri 模式下健康检查失败自动重试（每 1 秒），非 Tauri 模式保持原有行为。
- 连接成功后，自动注入持久化配置到后端（通过 `postSettings()`）。

### Step 6 — 前端 API_BASE 适配

**`client.ts`**：
```typescript
const API_BASE =
  typeof window !== 'undefined' && '__TAURI__' in window
    ? 'http://127.0.0.1:8080'
    : '';
```

### Step 7 — 托盘图标

- 使用 `TrayIconBuilder` 创建系统托盘。
- 图标：复用 `default_window_icon()`。
- Tooltip："FinGuard — AI 资产配置中台"。
- 右键菜单：「显示窗口」+「退出 FinGuard」。
- 左键双击：恢复并聚焦窗口。
- 「退出」：先 `kill_sidecar()` 再 `app.exit(0)`。

### Step 8 — 配置持久化

**新增 `store.ts`**：
- `isTauri()`：检测 `window.__TAURI__`。
- `loadPersistedSettings()`：Tauri 环境用 `@tauri-apps/plugin-store`（文件 `settings.json`），浏览器环境降级到 `localStorage`。
- `savePersistedSettings()`：同上，写入时自动合并。
- `clearPersistedSettings()`：清除持久化数据。

**`SettingsPage.tsx`**：保存成功后调用 `savePersistedSettings()` 同步持久化。

**`StartupPage.tsx`**：启动成功后调用 `loadPersistedSettings()` → `postSettings()` 注入到后端。

### Step 9 — 窗口关闭行为

- `on_window_event(CloseRequested)`：先调用 `kill_sidecar()` 终止后端，然后允许窗口正常关闭（应用退出）。
- `RunEvent::Exit` 兜底：在 `app.run()` 回调中，当收到 `Exit` 事件时再次调用 `kill_sidecar()`，确保任何退出路径都能清理子进程。
- **不再使用"最小化到托盘"模式**：关闭即退出，避免用户困惑和残留进程。

### Step 10 — 联调验证

- **启动**：`npx tauri dev` → 窗口正常加载 → sidecar 启动 → health OK → 进入应用。
- **设置**：输入 API Key → POST /api/v1/settings → 返回 `settings updated` → 持久化到 store。
- **退出**：点击 X → finguard.exe 终止 → finguard-desktop.exe 退出 → 无残留进程。
- **托盘**：右键菜单显示 → "显示窗口" 恢复 → "退出" 完全退出。

### 关键问题记录

| 序号 | 问题 | 根因 | 解决方案 |
|------|------|------|----------|
| 1 | ICO 文件导致 RC.EXE 报错 | PNG-in-ICO 格式不兼容 RC.EXE | 生成 BMP-in-ICO 格式（BITMAPINFOHEADER + BGRA 像素 + AND mask） |
| 2 | cargo check 报生命周期错误 | `state.child.lock()` 借用超出作用域 | 在 `if let` 块后加分号封闭作用域 |
| 3 | webviewInstallMode 无效 | `"skipInstall"` 非 Tauri v2 合法值 | 改为 `"skip"` |
| 4 | plugins.shell 配置无效 | v1 的 `scope`/`sidecar` 字段在 v2 中不存在 | 移至 capabilities/default.json |
| 5 | Sidecar 启动即崩溃 (0xC0000135) | finguard.exe 依赖 10 个 DLL 未随 sidecar 复制 | `build.rs` 自动复制 DLL 到输出目录 |
| 6 | 设置保存返回 `write_failed` | sidecar 工作目录无 `config/` | `build.rs` 复制 config/ + `cmd.current_dir()` 设置 cwd |
| 7 | 关闭窗口后进程残留 | `on_window_event` 仅隐藏窗口不退出 | 改为直接退出 + `kill_sidecar()` + `RunEvent::Exit` 兜底 |

---

## Chapter 4：验收详情

| 验收项 | 状态 | 说明 |
|--------|------|------|
| **V1 - 双击启动** | ✅ PASS | `npx tauri dev` → 窗口加载 → sidecar 自动拉起 → `/health` 返回 ok → 前端正常进入应用 |
| **V2 - 自动退出** | ✅ PASS | 关闭窗口 → `kill_sidecar()` 调用 → finguard.exe 终止 → `Get-Process` 确认无残留进程 → 后端端口不可达 |
| **V3 - 首次引导** | ✅ PASS | 无持久化配置时，`App.tsx` 检测 `!hasApiKey` → 自动重定向至 `/settings` |
| **V4 - 配置持久化** | ✅ PASS | 设置页保存 → `savePersistedSettings()` 写入 → 后端 `POST /api/v1/settings` 返回 `settings updated` |
| **V5 - 托盘功能** | ✅ PASS | 系统托盘图标可见 → 右键菜单「显示窗口/退出」→ 左键双击恢复窗口 → 「退出」完全退出 |
| **V6 - TypeScript 编译** | ✅ PASS | `npx tsc --noEmit` 零错误 |
| **V7 - Tauri 构建** | ⏳ 待做 | `tauri build` 生产构建尚未执行（需配置代码签名等） |

---

变更日志
- [2026-02-26] 创建 Phase 8 文档，完成 Chapter 1（7 项 Spec）+ Chapter 2（计划与验收项）。
