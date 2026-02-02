# FinGuard — Phase 3 交接文档（任何 Agent 可无缝接手）

本文档旨在让新接手的 Agent 在**不依赖上下文**的情况下，像“老员工”一样继续推进 Phase 3。

---

## 0. 快速入口（必读 5 分钟）
1. `phase2.md`：Phase 2 已完成内容 + P1 深度排查结论（Drogon IPv6 / SNI）。
2. `守拙价值多元化基金理念.md`：资产配置与选股规则的业务来源（强规则基础）。
3. `finguard/config/rules.yaml`：当前规则配置（关键词、画像、个股比例、警示模板等）。
4. `phase3.md`：Phase 3 进展与日志。
5. `学习文档/20260202/study.md`：教学内容与“教教我”默认更新规则。

---

## 1. 当前项目状态（截至 2026-02-02）
### 已稳定可用
- SSE 接口 `/api/v1/chat/stream` 已实现，输出 token/cite/metric/warning/done。
- `X-API-Key` 鉴权已生效，与 `config/llm.json` 的 api_key 匹配。
- LLM 通过 curl fallback 通路可正常输出真实 Qwen 回复。

### Phase 3 已完成
- 规则配置文件 `finguard/config/rules.yaml` 已按“守拙价值多元化基金理念”整理。
- 问卷档案接口已新增：`POST /api/v1/profile/upsert`（Header: `X-User-Id`）。
- 本地档案存储实现：`config/profiles.json`（文件存储，数据库为技术债务）。
- RuleEngine 基础实现已接入 `/api/v1/chat/stream`（请求侧警示）。

### 仍需完成
- `RuleEngine::check_response()`：响应侧规则（资产分配、选股指标等）。
- 警示输出结构完善（更明确的原因 + 触发词）。
- 验收脚本/命令标准化。

---

## 2. 技术债务（必须知晓）
- Drogon HttpClient 直连失败（IPv6 + SNI 问题）
  - 结论：**接受 curl fallback 作为当前交付标准**。
  - 参考：`phase2.md` + `学习文档/20260201/`。

---

## 3. 关键规则与业务原则（强规则 + 弱 LLM）
### 业务立场
- LLM 只做“公司分析与解释文本”，不负责决定配置。
- 配置与风控规则由后端 RuleEngine 决定。

### 规则来源
- `守拙价值多元化基金理念.md`

### 规则配置文件
- `finguard/config/rules.yaml`
  - 关键词触发：博彩、高杠杆、ST、期货、做空等。
  - 画像与个股比例限制：novice/experienced/professional。
  - 默认年龄：35（对应 30–40 岁区间）。
  - warning 模板（含触发词提示）。

---

## 4. 已新增代码与入口
### 新增文件
- `finguard/src/risk/profile_store.h`
- `finguard/src/risk/profile_store.cpp`
- `finguard/src/risk/rule_engine.h`
- `finguard/src/risk/rule_engine.cpp`

### 相关集成点
- `finguard/src/server/routes.cpp`
  - 新增接口：`/api/v1/profile/upsert`
  - `/api/v1/chat/stream` 中调用 RuleEngine 并合并 warnings

### 构建入口
- `finguard/CMakeLists.txt` 已加入 `profile_store.cpp` 和 `rule_engine.cpp`

---

## 5. 关键接口说明
### 5.1 问卷写入接口（已实现）
- `POST /api/v1/profile/upsert`
- Header: `X-User-Id: <账号>`
- Body:
```json
{
  "questionnaire": {
    "age": 35,
    "investor_profile": "experienced",
    "experience_years_band": "5-10",
    "annualized_return_band": "10-20",
    "beat_sp500_10y": "no",
    "individual_stock_percent": 0.30
  }
}
```
- 存储：`config/profiles.json`

### 5.2 SSE 流式问答（已实现 + 接入规则）
- `POST /api/v1/chat/stream`
- Header: `X-API-Key`, `X-User-Id`
- Body:
```json
{
  "prompt": "......"
}
```
- 规则引擎会：
  - 检查关键词
  - 检查画像与个股比例
  - 若缺失档案会输出 warning

---

## 6. 当前规则引擎状态
### 已完成
- `RuleEngine::load_config()`：轻量 YAML 解析（只读必要字段）。
- `RuleEngine::check_request()`：关键词、画像与个股比例检查。

### 计划完善
- `RuleEngine::check_response()`：
  - 资产分配计算与校验
  - 选股指标（PEG、负债率、ROE、现金流）
  - 输出结构化原因

---

## 7. 验收建议（建议写成脚本）
1. 写入问卷
```powershell
curl.exe -X POST "http://localhost:8080/api/v1/profile/upsert" ^
  -H "Content-Type: application/json" ^
  -H "X-User-Id: NeoChen" ^
  -d "{\"questionnaire\":{\"age\":35,\"investor_profile\":\"experienced\",\"individual_stock_percent\":0.3}}"
```

2. 触发关键词 warning
```powershell
curl.exe -X POST "http://localhost:8080/api/v1/chat/stream" ^
  -H "Content-Type: application/json" ^
  -H "X-API-Key: <api_key>" ^
  -H "X-User-Id: NeoChen" ^
  -d "{\"prompt\":\"我想用高杠杆做空\"}"
```

---

## 8. 开发规范（必须遵守）
- 新功能优先“新增函数/头文件 + 引入”，避免大改旧代码。
- “教教我”默认更新 `学习文档/20260202/study.md`。
- 关键规则变更需同步更新 `phase3.md` 与 `next_plan.md`。

---

## 9. 接手后的下一步路线图
1. 实现 `check_response()` 并完善 warning 输出结构。
2. 统一 warning 结构（含触发词 + 原因 + 理性提示）。
3. 若需要个性化解释，可选“档案摘要注入 system prompt”。

---

## 10. 需要提醒用户的事情（Agent 提示清单）
- 先阅读 `学习文档/20260202/明日前置学习路线.md`，确认理解新增代码的学习路径。
- 确认是否要继续完善 `check_response()`（资产分配 + 个股指标）或先补验收脚本。
- 若要个性化解释，讨论是否采用“档案摘要注入 system prompt”。

---

## 11. 参考索引（路径清单）
- 规则配置：`finguard/config/rules.yaml`
- 问卷存储：`config/profiles.json`
- 规则引擎：`finguard/src/risk/rule_engine.*`
- 档案存储：`finguard/src/risk/profile_store.*`
- SSE 路由：`finguard/src/server/routes.cpp`
- 业务理念：`守拙价值多元化基金理念.md`
- Phase 3 进度：`phase3.md`

---

## 11. 交接总结（一句话）
Phase 3 已完成“问卷档案 + 规则引擎请求侧接入”，下一步是补齐响应侧规则与标准化验收。
