# Phase 1 — 可复现验收文档（精简与重组）

## **概览**
- **目标**：交付可构建、可启动的 C++ 后端骨架，并提供基础接口用于功能与合规验收（最小链路：构建 → 启动 → /health、/api/v1/plan）。
- **范围**：项目目录、CMake 配置、基础路由实现、规则占位与文档化。

## **交付物（Deliverables）**
- 源码：`finguard/` 下的 `src/`、`config/`、`CMakeLists.txt`。
- 可执行产物：通过 `cmake --build build` 生成的可执行（样例路径 `build/Debug/finguard.exe`）。
- 基础接口：`GET /health` 返回 `{ "status":"ok" }`；`POST /api/v1/plan` 返回 mock 建议。
- 验收证据：构建日志、接口响应副本（JSON）、服务启动/停止记录、审计文件（若已启用）。

## **验收准则（Acceptance Criteria）**
1. 能成功构建并产生可执行文件。
2. 可执行文件能启动并监听（默认端口 8080）；`GET /health` 返回 `{"status":"ok"}`。
3. `POST /api/v1/plan` 对示例画像返回合理的 mock/实际建议结构（包含 `proposed_portfolio`、`rationale`、`risk_report` 等键）。
4. （若启用）审计记录以 JSONL 形式写入 `uploads/audit/<request_id>.jsonl`，记录关键事件（request_received → auth_checked → kb_query → llm_invoked → decision → response_sent）。

## **快速复现步骤（手工）**
1) 构建

```powershell
cd d:\AI_Investment\finguard
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=d:/AI_Investment/tools/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

2) 启动（单终端）

```powershell
d:\AI_Investment\finguard\build\Debug\finguard.exe
```

3) 基础接口验证

```powershell
Invoke-RestMethod -Uri 'http://127.0.0.1:8080/health' -Method Get
Invoke-RestMethod -Uri 'http://127.0.0.1:8080/api/v1/plan' -Method Post -ContentType 'application/json' -Body '{"profile":{"age":35,"risk":"medium","capital":500000}}'
```

4) 可选：触发聊天流（SSE）并检查审计

```powershell
Invoke-RestMethod -Uri 'http://127.0.0.1:8080/api/v1/chat/stream' -Method Post -ContentType 'application/json' -Body '{"prompt":"我想要10年期的中等风险配置，本金50万，请给出组合与理由"}'
# 或用 curl -N 发起 SSE 请求
```

## **审计（Traceability）规范（建议）**
- 存储位置：`uploads/audit/`，每个请求一个 JSONL 文件，文件名以 `request_id` 命名。
- 必要事件：`request_received`、`auth_checked`、`kb_query`、`llm_invoked`（如果有）、`decision`、`response_sent`。
- 事件字段建议：`timestamp`（ISO8601）、`step`、`request_id`、`actor`、`input`/`output`（摘要）。
- 回放：提供 `tools/show_audit.py` 或 PowerShell 脚本读取 JSONL 并按时间顺序打印要点。

## **自动化运行记录（已执行 & 证据，2026-01-28）**
- 执行动作：构建 → 后台启动可执行 → 轮询 `/health` → 调用 `/api/v1/plan` → 捕获响应 → 停止服务。
- 采集到的接口响应：

`/health`:
```json
{"status":"ok"}
```

`/api/v1/plan`（示例）：
```json
{"proposed_portfolio":{"BND":0.4,"CASH":0.2,"GLD":0.1,"SPY":0.3},"rationale":"当前为 mock 输出，后续将接入画像解析与规则引擎。","rebalancing_actions":[],"risk_report":{"status":"PASS","triggered_rules":[]}}
```

- 重定向日志文件（路径示例）：
  - `d:\AI_Investment\tools\temp\finguard_out.log`（stdout 重定向）
  - `d:\AI_Investment\tools\temp\finguard_err.log`（stderr 重定向，若存在则需分析）

备注：当前自动化记录显示服务能快速启动并返回 mock 响应；未见 stderr 输出。

## **常见问题及排查要点**
- 若构建失败：检查 vcpkg、Drogon 是否在本机安装并配置正确（`DrogonConfig.cmake`）。
- 若看不到审计：确认 `uploads/audit/` 是否存在及服务有写权限；检查对应请求代码路径是否走到审计写入逻辑。
- 若审计时间戳异常：校验主机时钟与并发写逻辑（文件写入锁或进程间同步）。

## **下一步建议（优先级）**
1. 将“后台启动与轮询验收”步骤脚本化为 `tools/run_backend_check.ps1`（低风险，便于 CI 调用）。
2. 在确认可修改代码后，按审计规范在 `routes.cpp` 与 `llm_client.cpp` 插入事件写入，并实现 `GET /api/v1/audit/{request_id}` 接口（需要用户批准）。
3. 编写 `tools/show_audit.py` 用于人类可读的审计回放。

---
更新日志：对原文进行了结构化重写，保留原始验收证据与建议，精简重复段落，便于审阅与自动化接入。
