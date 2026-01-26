# FinGuard next_plan（交接文档）

## 当前状态

- Phase 1：构建与运行验收已完成（接口可访问）。
- Phase 2：结构化 SSE、API Key 鉴权与 LLM client API 接入已完成；Drogon 直连失败，但已用 curl fallback 打通联调。
- 当前选择：暂停继续排查，后续再推进。

## 当前阶段与目标

- 阶段名称：第二阶段（流式问答与模型接入）
- 本阶段目标：修复 Drogon HttpClient 直连 Qwen，去除 curl fallback 依赖。

## 具体任务清单（可执行，待恢复后执行）

1. 显式设置 HTTP 代理（优先）

   - 产出：为 Drogon HttpClient 注入代理（从配置读取）
   - 验收标准：请求成功且 warning 不含 `llm_reqresult_3`
2. 强制 IPv4 + Host header（次优）

   - 产出：使用 IPv4 直连并设置 Host/SNI
   - 验收标准：BadServerAddress 消失或转为可诊断错误
3. 启用 debug 日志 + 抓包定位

   - 产出：Drogon debug 日志（resolve/connect/SSL）
   - 验收标准：定位失败阶段（DNS / TCP / TLS）
4. 保留 curl fallback 作为降级路径（可选）

   - 产出：配置开关（`use_curl_fallback`）
   - 验收标准：可一键关闭/开启

## 关键注意事项

- 文档与对外说明保持“上架产品”语气，避免学生思维表达。
- 中文文档保存为 UTF-8（带 BOM）以避免乱码。
- 变更完成后在 `next_plan.md` 更新“已完成/未开始”并记录新增文件。

## 未决问题（需要用户确认）

- 恢复推进时优先选择“代理注入”还是“强制 IPv4”。

## 参考文件

- `workbook.md`
- `项目书.md`
- `phase1.md`
- `phase2.md`
- `next_plan.md`
