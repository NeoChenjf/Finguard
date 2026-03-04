# Next Plan

## 当前状态

- Phase 1-9 全部完成，验收闭环。
- Phase 10 启动日期：2026-02-27，Spec 已冻结，开始实施。

## 阶段总览

| 阶段 | 名称 | 状态 | 完成日期 |
|------|------|------|----------|
| Phase 1 | C++ 核心骨架与 LLM 代理 | **已完成** | — |
| Phase 2 | 风控闸门与规则引擎 | **已完成** | — |
| Phase 3 | 流式输出与 SSE | **已完成** | — |
| Phase 4 | 限流 / 熔断 / 缓存 / 可观测 | **已完成** | — |
| Phase 5 | 压测与性能优化 | **已完成** | — |
| Phase 6 | 测试加固与 Release 构建 | **已完成** | — |
| Phase 7 | 前端 MVP（Vite + React） | **已完成** | 2026-02-26 |
| Phase 8 | 桌面壳与本地启动器（Tauri） | **已完成** | 2026-02-26 |
| Phase 9 | 打包分发与最终交付 | **已完成** | 2026-02-27 |
| Phase 10 | Value Cell 量化价值分析系统 | **进行中** | — |

## Phase 10 已知阻塞问题

> Yahoo 429 已于 2026-03-03 修复（根因：User-Agent + Accept 头）。以下为剩余待办。

### 1. Alpha Vantage API Key 未配置

- **现象**：`config/valuation.json` 中 `alpha_vantage_api_key` 为空，Yahoo 失败时无法执行降级。
- **待解决**：注册 Alpha Vantage 免费 Key（25 次/天），写入配置。

### 2. Tavily 搜索 API Key 未配置

- **现象**：`config/valuation.json` 中 `tavily_api_key` 为空，定性搜索降级为空结果，LLM 缺少搜索语料。
- **影响**：定性分析（护城河、管理层、商业模式）质量降低，LLM 只能依赖内置知识。
- **待解决**：注册 Tavily API Key（免费 1000 次/月），写入配置并验证。

### 3. Tauri 安装包集成验收

- **待解决**：重新 Tauri build（含最新 finguard.exe + valuation.json），执行安装态验收清单。

## Phase 9 成果摘要

- **NSIS 安装包**：`FinGuard_0.1.0_x64-setup.exe`（4.4 MB），双击安装到 `%LOCALAPPDATA%/FinGuard/`。
- **MSI 安装包**：`FinGuard_0.1.0_x64_en-US.msi`（6.05 MB），备选分发格式。
- **bundle.resources**：10 个 DLL + config/ 目录随安装包分发，无需手动复制。
- **config 初始化**：首次启动自动将 config/ 从安装目录复制到 `%APPDATA%/finguard/config/`，后续不覆盖用户修改。
- **sidecar CWD 适配**：生产模式 CWD = `%APPDATA%/finguard/`，开发模式保持 exe 同目录。
- **验收**：V1-V6 全部 PASS。安装后 sidecar 自动拉起、/health 通过、关闭无残留。
- 详见 `phase9.md` Chapter 3 & 4。

## Phase 9 踩坑备忘（交接用）

1. **plugin-store 类型变更**：`@tauri-apps/plugin-store` v2.4.2 的 `load()` 要求 `defaults` 属性（required），旧代码 `{ autoSave: true }` 编译失败，需改为 `{ autoSave: true, defaults: {} }`。
2. **NSIS 下载超时**：Tauri 内置下载器从 GitHub 下载 NSIS 有网络问题时反复 timeout。解决：手动用 `curl.exe -L` 下载到 `%LOCALAPPDATA%/tauri/`，Tauri 会自动检测并使用。
3. **NSIS 目录命名**：手动下载解压后目录名为 `nsis-3.11/`，Tauri 查找目录名为 `NSIS/`，需重命名。实测 Tauri 如检测不符会重新下载覆盖。

## 下一步任务：Phase 10（Value Cell 量化价值分析系统）

### 目标

将 FinGuard 从通用 LLM 网关转型为"定性 + 定量"的量化价值投资分析系统，放弃 PDF RAG 路径，采用结构化数据 + 搜索引擎方案。

### 核心功能

1. **Yahoo Finance 数据模块**：获取 PE、PB、PEG、未来预期增长率等结构化财务指标。
2. **Search Wrapper 模块**：通过搜索引擎 API 获取目标公司护城河、管理层、商业模式定性信息。
3. **安全边际计算** (`calculateSafetyMargin`)：对比当前 PE 与 5 年历史均值，判定安全边际。
4. **评价逻辑**：(PE < 历史均值) AND (PEG < 1.0) → "安全边际区间"。
5. **Markdown 结构化输出**：定性评分 (0-10)、PEG 值、安全边际判定 (Yes/No)、核心投资结论。

### 执行策略

1. Spec 前置收敛（API 选型、数据模型、输出格式）
2. C++ 后端新增 `valuation/` 模块（yahoo_finance_client、search_wrapper、safety_margin）
3. 新增 `/api/v1/valuecell` 接口
4. 前端新增 Value Cell 分析页面
5. 端到端验收

### 待确认 Spec（已冻结）

- ✅ Yahoo Finance 非官方 API 作主源（`query1.finance.yahoo.com/v10/finance/quoteSummary`），Alpha Vantage 降级
- ✅ Tavily API 作搜索引擎（免费 1000次/月，结构化摘要，直接喂 LLM）
- ✅ 历史 PE 均值：近 4~8 季度 Trailing EPS 估算均值（EPS≤0 的季度跳过）
- ✅ 定性评分：LLM 直接打分（temperature=0，固定 prompt 模板）
- ✅ 配置文件：新建 `config/valuation.json`（`tavily_api_key`、`alpha_vantage_api_key`、超时等）
- 详见 `phase10.md` Chapter 1

## 关键技术栈速查

| 层 | 技术 | 版本 |
|----|------|------|
| 桌面壳 | Tauri | v2.10.2 |
| 桌面 Rust 侧 | Rust | 1.93.1 |
| 前端 | Vite + React + TypeScript + TailwindCSS v4 | Vite 7.3.1 / React 19.2.0 |
| 状态管理 | Zustand | 5.0.11 |
| 持久化 | tauri-plugin-store | 2.4.2 |
| Sidecar 管理 | tauri-plugin-shell | 2.3.5 |
| 后端 | C++ / Drogon | CMake Release |
| 后端 API | REST + SSE | port 8080 |

## 关键文件速查

| 文件 | 说明 |
|------|------|
| `项目书.md` | 总体项目规划 |
| `phase9.md` | Phase 9 完整记录（打包分发） |
| `phase8.md` | Phase 8 完整记录（Tauri 桌面壳） |
| `phase7.md` | Phase 7 完整记录（前端 MVP） |
| `frontend/src-tauri/src/lib.rs` | Tauri 核心逻辑（sidecar、config 初始化、托盘） |
| `frontend/src-tauri/build.rs` | 构建脚本（DLL + config 复制） |
| `frontend/src-tauri/tauri.conf.json` | Tauri 应用配置（含 bundle.resources） |
| `frontend/src/api/client.ts` | 后端 API 客户端 |
| `frontend/src/api/store.ts` | 配置持久化工具 |
| `finguard/src/server/routes.cpp` | 后端路由 |
| `finguard/config/llm.json` | 后端 LLM 配置 |
| `workbook/` | 员工手册 |

最后更新：2026-02-28
