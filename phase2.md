# Phase 2 文档

## 1. 二阶段总览
- 目标：接入 SSE 流式接口并完成基础 LLM client 接入（Qwen OpenAI 兼容 API），支持结构化事件输出与 API Key 鉴权。
- 范围：
  - `/api/v1/chat/stream` 结构化 SSE（type + payload）
  - LLM client 真实 API 接入（OpenAI 兼容接口形式）
  - API Key 鉴权（请求级校验）
  - 事件类型：token / cite / metric / warning / done

## 2. Todo List
- 已完成
  - Phase 2 方案讨论与决策（API 接入 / 结构化 SSE / API Key / 完整事件验收）。
  - 新增 `config/llm.json` 配置文件（Qwen API Base / Key / Model）。
  - LLM client 接入 OpenAI 兼容 API（含失败降级与超时）。
  - SSE 结构化事件输出（token/cite/metric/warning/done）。
  - API Key 鉴权校验（`X-API-Key`）。
  - README 更新（鉴权与事件格式说明）。
  - 通过 curl fallback 打通联调（SSE 可返回真实模型内容）。
- 未完成
  - 修复 Drogon HttpClient 直连 Qwen（仍返回 BadServerAddress）。

## 3. 二阶段工作日志
- 2026-01-21：根据区域（成都）将 Qwen API Base 调整为中国站点 `https://dashscope.aliyuncs.com/compatible-mode/v1`。
- 2026-01-21：最小验证：curl 直连 Qwen 兼容接口返回 200 OK。
- 2026-01-21：服务进程内请求仍失败（ReqResult::BadServerAddress / llm_reqresult_3）。
- 2026-01-21：尝试防火墙放行（控制面板）后问题仍存在。
- 2026-01-21：阅读专家 review.md（编码疑似非 UTF-8，部分乱码），核心建议为：优先排查代理/IPv6/TLS/DNS 差异。
- 2026-01-21：实现 curl fallback（子进程请求），SSE 返回真实内容，warning 为 `curl_fallback_used`。

### 验收与结果（2026-01-21 追加）
- 验收目标：结构化 SSE 流式输出与鉴权可用；支持 token/cite/metric/warning/done。
- 执行步骤：
  1) `cmake --build build`
  2) 启动服务：`d:\AI_Investment\finguard\build\Debug\finguard.exe`
  3) `POST /api/v1/chat/stream`（带 `X-API-Key`）验证 SSE
- 结果：
  - SSE 事件结构正常，包含 token/cite/metric/warning/done。
  - 通过 curl fallback 返回真实内容（warning: `curl_fallback_used`）。
- 结论：
  - Phase 2 已可用，但 Drogon 直连仍需修复。

### 阻塞问题汇总（2026-01-21）
- 服务进程内请求 Qwen 失败：Drogon HttpClient 返回 BadServerAddress（llm_reqresult_3）。
- 同机 curl 直连 Qwen 兼容接口返回 200 OK，说明 Key 与 endpoint 正常。
- 防火墙放行后问题仍存在，怀疑 DNS/代理/证书链导致服务进程无法连接。
### 专家建议记录（2026-01-21）
- 结论：问题不在 Qwen/Key/协议，而在 Windows 下 Drogon HttpClient 的连接路径与 curl 不一致。
- 优先级建议：
  1) 显式设置 HTTP 代理（Drogon 不会自动读取系统代理）；
  2) 强制 IPv4 + Host header，绕过 DNS/IPv6；
  3) 开启 Drogon debug 日志 + 抓包定位 TLS/DNS/连接阶段。
- 建议验证方式：给错误代理地址观察错误变化；对比 curl 与 Drogon 的 DNS/TCP/TLS 行为。
### 暂停说明（2026-01-21）
- 用户决定暂不继续排查（代理注入/IPv4 强制/DNS/TLS），后续再推进。
- 当前可用方案：curl fallback 已能返回真实内容，但 Drogon 直连仍失败。