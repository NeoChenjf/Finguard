# FinGuard — Phase 3 启动与交接计划（给下一位 Agent）

## 1. 核心阅读（启动前必看）
1. **`phase2.md`**：记录了二阶段的最终完成状态与 P1 深度排查结论。
2. **`学习文档/20260201/`**：包含《概念学习笔记》和《二阶段深度排查手册》，详细解释了 IPv6 路由、c-ares、SNI 等关键难题。
3. **`llm.json`**：确认 `use_curl_fallback: true` 且 `api_key` 正确。

---

## 2. 当前项目状态（截至 2026-02-01）

### ✅ 已完成（Phase 2 成果）
- **SSE 流式接口**：`/api/v1/chat/stream` 已实现，支持结构化输出。
- **鉴权机制**：`X-API-Key` 鉴权已生效，与服务端的 `api_key` 匹配。
- **LLM 通信闭环**：通过 `curl` fallback 机制，已成功获取 Qwen 回复（268 tokens 验证通过）。
- **环境清理**：`tools/temp/` 已清理，仅保留核心诊断总结 `P1_DIAGNOSTIC_PACKAGE_FOR_OPUS.md`。

### ⚠️ 技术负债（P1 排查结果）
- **Drogon HttpClient 直连失败**：根因为 c-ares 库优先返回不可达的 IPv6 地址，且 Drogon 不支持手动 IP 连接时设置 SNI。
- **现状**：**接受 fallback 模式作为当前交付标准**。不需要在此问题上继续纠缠，直接推进 Phase 3。

---

## 3. Phase 3：风险规则引擎（下一阶段目标）

**核心任务**：在 AI 回复返回给用户之前，插入一层“金融风险校验”。

### A. 规则定义 (YAML)
- 创建 `finguard/config/rules.yaml`。
- 定义初始规则（例如）：
  - `max_single_asset_percent`: 30%（单资产持仓限制）
  - `forbidden_keywords`: ["博彩", "高杠杆"]（违禁词过滤）
  - `risk_level_check`: 根据用户画像分级。

### B. 核心代码职责
- **`RuleEngine` 类**：负责加载 YAML，提供 `check_request()` 和 `check_response()` 接口。
- **集成点**：在 `routes.cpp` 的流式处理循环中，每收到一个 token 或在最终发送前进行规则判定。

### C. 验收标准
1. 发送包含“博彩”字详的 Prompt，AI 回答应被拦截或触发 Warning。
2. 提供非法持仓比例建议时，SSE 流中应出现 `warning` 类型的事件。

---

## 4. 明日的立即行动项
1. **初始化配置**：在 `finguard/config/` 下创建初始的 `rules.yaml`。
2. **骨架搭建**：创建 `finguard/src/risk/rule_engine.h/cpp`，实现基本的配置读取。
3. **路由集成**：在 `/api/v1/chat/stream` 处理器中注入规则引擎的实例，先跑通一个简单的“全文拦截”逻辑。

---

## 5. 开发规范提醒
- **日志**：所有规则触发必须留下 `LOG_INFO` 日志，并在 SSE 输出中携带 `warning`。
- **构建**：每次修改 C++ 代码后，执行 `cmake --build build --config Debug`。
- **验证**：使用 PowerShell 的 `Invoke-RestMethod` 或 `curl.exe` 验证流式输出。

> “Phase 2 的技术难题已化作文档，带着这些经验，开启 Phase 3 的业务逻辑构建吧。”
