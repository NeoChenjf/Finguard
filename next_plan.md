# FinGuard — Phase2 可执行计划书

## 概要

- 目标：在保证审计与降级可控的前提下，修复 Drogon 直连 LLM 的网络问题，移除长期依赖的 `curl` fallback，并把排障与运行规范纳入 `workbook`。 
- 时间窗口（建议）：小范围验证 1 周，修复与回归测试 1–2 周（视排障复杂度）。

## 当前状态

- 已完成：结构化 SSE、`X-API-Key` 鉴权、LLM client 的基本调用链、`curl` fallback 联调可用。
- 阻塞：Drogon HttpClient 在 Windows 环境下与目标 LLM endpoint 建连失败（`BadServerAddress`）。

## 目标与验收标准

- 主要目标：使 Drogon HttpClient 能稳定直连 LLM（Qwen/OpenAI 兼容 endpoint），并在失败时使用可控的降级策略。
- 验收标准（可量化）：
  1. Drogon 直连对同一 endpoint 返回非 `BadServerAddress`（2xx/4xx/5xx 均可被识别为正常回环，不再为连接级别错误）。
  2. `use_curl_fallback` 为配置开关，CI 能在开/关状态下通过相应断言。
  3. 排查与解决步骤写入 `workbook/` 的相应规则文件，并保留变更日志。

## 优先级任务清单（执行性）

高优先级（P0）
1. 注入 HTTP 代理配置并重试 Drogon 直连
   - 动作：在 `config/llm.json` 增加 `http_proxy` 字段；在 LLM client 读取并设置到 Drogon 请求选项。
   - 验收：直连成功或错误变更为可诊断的错误码（非 BadServerAddress）。
2. 将 `curl_fallback` 设为显式配置（`use_curl_fallback: true/false`）并在事件中标注原因
   - 动作：增加配置、日志与 SSE 中的 `warning` 字段。
   - 验收：能在不改业务代码的前提下通过配置切换降级行为。

中优先级（P1）
3. 强制 IPv4 优先与 Host header/SNI 设置
   - 动作：在请求构造中尝试设置 IPv4 socket/flags 或使用 IP 地址直连并设置 `Host` 请求头。
   - 验收：BadServerAddress 消失或被替换为后端返回的明确错误。
4. 启用 Drogon debug 日志并抓包定位（复现场景下）
   - 动作：脚本化抓包与日志收集步骤，输出可供分析的 trace 文件。
   - 验收：定位到失败阶段（DNS/TCP/TLS）并形成问题定位报告。

低优先级（P2）
5. 编写回放/断言测试并加入 CI
   - 动作：在 CI 中加入一次构建→启动→POST 测试（切换 `use_curl_fallback` 的断言）。
   - 验收：CI 测试在两种配置下给出预期结果并保存日志。

## 任务分工（建议）
- 技术负责人：TBD（建议由目前维护 `llm_client.cpp` 的开发者担任）。
- 验收与 QA：TBD（建议由测试工程师执行 CI 与回归测试）。
- 文档与规则录入：由 Agent（我）按 `workbook/README.md` 流程草拟并提交供人工复核。

## 时间估算（建议）
- P0 任务：1–3 天（视代理配置与重试复杂度）。
- P1 任务：2–5 天（抓包与定位可能需多次迭代）。
- P2 任务：1–3 天（CI 集成与断言脚本）。

## 风险与缓解
- 风险：定位网络栈差异可能需要系统层面或运维协助（如企业代理、网络策略）。
- 缓解：保持 `curl_fallback` 可用，优先做最小改动验证（代理配置），避免一次性大面积改动。

## 交付物（每项需在完成后在 `next_plan.md` 记录）
1. 修复或明确定位报告（Drogon 直连问题）
2. `config/llm.json` 更新（含 proxy、use_curl_fallback）
3. CI 集成测试（断言开/关 curl fallback）
4. `workbook/` 中新增/更新规则（诊断流程、抓包步骤、自动化脚本）

## 合规与文档要求
- 所有新增规则必须遵循 `workbook/README.md` 中的流程（YAML 元数据、去重规则、变更日志）。

## 下一步请求（需要你的确认）
1. 我先实现 P0 的 `http_proxy` 注入与 `use_curl_fallback` 配置开关，并提交 PR 吗？（需要你同意修改代码）
2. 还是先由我把“后端运行与排查”脚本化（`tools/run_backend_check.ps1`、抓包脚本）并把规则草稿加入 `workbook`？（不修改业务代码）

---
更新记录：将原 `next_plan.md` 由交接说明改写为可操作的 Phase2 计划书，包含优先级任务、验收标准与交付物。 
