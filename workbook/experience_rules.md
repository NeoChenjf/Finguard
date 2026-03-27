---
title: 开发经验规则速查
tags: [experience, best_practices]
last_updated: 2026-02-25
source: workbook
status: published
---

# 开发经验规则速查

本文件收录项目开发中总结的经验教训与最佳实践。仅保留不属于其他 Skill 文档的独立条目——身份/安全/沟通见 `role_and_principles.md`，阶段文档/Spec 流程见 `work_log_and_tracking.md`，文档格式/教学见 `docs_guidelines.md`，后端运维见 `automation.md`。

---

## 1. 后端服务与测试分离

- 启动服务与接口测试应分开进行，避免在测试中反复启动/关闭服务导致不稳定。
- 自动化测试优先使用 TestClient 或类似的测试客户端，而非直接启动完整服务。

## 2. API Key / 认证问题

- 变更密钥后务必重启服务（或调用 `/api/v1/settings` 动态更新）以使新密钥生效。
- 遇到 401/502 错误时，优先排查密钥有效性、额度与配置。
- 记录请求示例与响应供排查使用（响应中脱敏敏感信息）。

## 3. 日志与调试

- 避免直接将对象转为字符串输出，优先使用 JSON 序列化并脱敏敏感字段。
- 日志应包含上下文信息（trace_id、时间、severity），便于追溯。
- C++ 侧结构化日志使用 spdlog，字段包含 trace_id / route / status / latency_ms。

## 4. 配置文件注意事项

- YAML 配置文件必须为无 BOM 的 UTF-8，否则解析器会失败并退回默认值。
- 修改 `config/` 下文件后，确认 `build/config/` 同步更新（CMake post-build 自动复制，但手动调试时需注意）。
- 压测/验收场景下，配置值（如 rate_rps）需调大以避免限流干扰结果。

## 5. 编译与链接

- **LNK1168**（exe 被占用）：停止运行中的 finguard.exe 进程后重新构建。
- Debug 构建性能显著低于 Release，性能对比时注意区分构建类型。
- 新增源文件后须在 `CMakeLists.txt` 中注册，否则链接失败。

## 6. 压测与性能

- 压测工具：非流式用 Python `p5_bench.py`（asyncio + aiohttp）；流式 SSE 用同一工具的 SSE 模式。
- Mock LLM Server：`tools/mock_llm_server.py`（Port 28080），SSE 模拟每 chunk 100ms。
- 压测前确认配置宽松（rate_rps=5000+），避免限流导致全部 429。
- 每个优化点独立验证 + 回归压测，确保可回滚。

## 7. 面试准备文档迭代法

- 面试准备优先采用“外部模型出题 + 仓库内 Agent 找证据 + 人工回写文档 + 再回传追问”的闭环，而不是一次性写完大而全答案。
- 每轮只攻克 2 到 3 个知识点；先产出“白话解释 + 面试官版本”，再补项目代码证据和追问题。
- 对于 C++ / OS / Network 这类底层题，必须尽量绑定仓库中的真实实现、真实文件和真实设计取舍，避免只背八股。
- 如果外部模型给出的是知识点清单，先在仓库内落一份结构化 Markdown 骨架，再按主题逐段细化，而不是直接把聊天记录原样堆进主文档。
- 每轮迭代结束后，都应形成可回传的阶段性成果：当前版本文档、已补证据点、下一轮待追问点。

## 8. 前端开发 (React + Vite + TypeScript)

### 7.1 开发环境与依赖
- **交互式 CLI 阻塞**：工具有交互提示时（如 Vite 8 beta 选择），用管道输入跳过：`echo N | npx create-vite`。
- **配置文件版本差异**：框架升级可能改变配置方式（如 TailwindCSS v4 不再需要 `tailwind.config.js`），优先查阅官方迁移指南。
- **语法错误排查**：CSS 解析失败时，优先检查括号/分号是否匹配，逐行审查而非凭猜测。

### 7.2 CORS 与跨域请求
- **预检失败 (403/404)**：SPA 向不同端口的后端发 POST 时会触发 OPTIONS 预检，后端必须注册对应的 OPTIONS 方法或使用通配处理。
- **最佳实践**：开发环境用 Vite `proxy` 配置将 `/api` 等路径转发到后端，实现同源请求，彻底避免 CORS 问题。生产环境可改为实际后端地址。
- **动态 header**：前端发送可选 header（如 `X-API-Key`）时，应先判断值是否存在再添加，避免发送空字符串导致后端校验失败。

### 7.3 状态管理与数据流
- **会话状态丢失**：敏感信息（API Key）不应由前端长期记忆；后端应支持"无 key 时自动使用服务端配置"的 fallback 逻辑，避免刷新页面后 401。
- **全局状态同步**：表单提交成功后，立即更新 Zustand/Redux store 中对应状态，确保 UI 状态栏与实际配置一致。

### 7.4 业务逻辑对齐
- **需求抽象**：通用字段（如 `risk_level`、`horizon_months`）不适合领域特定场景，应根据业务规则文档（如 `守拙理念.md`、`rules.yaml`）设计专用表单字段。
- **动态约束**：前端上限/下限计算应与后端算配逻辑保持一致（如个股占比上限 = min(类型上限, equity)），避免前后端不一致导致用户困惑。
- **实时预览**：复杂配置页（如资产配置）应提供实时计算预览，帮助用户理解参数含义和最终效果。

### 7.5 数值精度与边界
- **浮点比较**：涉及百分比/比例的阈值判断（如 2.5% 下限）时，应加容差（如 `value < threshold - 0.001`）避免浮点精度导致误判。
- **归类逻辑**：自动归类用户输入时（如非标准 ETF 归为个股），维护显式白名单而非靠模糊匹配，确保行为可预测。

### 7.6 UI 交互优化
- **信息折叠**：大量次要信息（如多条告警）应默认折叠，提供"点击展开"交互，避免视觉杂乱。
- **渐进增强**：表单根据用户选择动态显示/隐藏字段（如"专业投资人"才显示"跑赢 SP500"），减少认知负担。

## 9. Tauri 桌面应用打包

### 8.1 Tauri 版本全局变量差异

- **Tauri v1** 注入 `window.__TAURI__`；**Tauri v2** 注入 `window.__TAURI_INTERNALS__`。
- 在 v2 项目中用 `'__TAURI__' in window` 检测，生产包中永远返回 `false`，导致 `API_BASE` 为空字符串，所有 fetch 打向 WebView 本地地址，表现为「无法连接到后端服务」。
- **正确写法**：`'__TAURI_INTERNALS__' in window`
- **隐蔽性**：`tauri dev` 开发模式下 Vite proxy 拦截所有 `/api` 请求并转发，`API_BASE` 根本不起作用，因此此 Bug 在开发环境**永远不会暴露**。

### 8.2 dev 与生产包的跨域行为根本差异

| 模式 | 前端来源 | `/api` 请求 | CORS 预检 |
|------|----------|-------------|-----------|
| `tauri dev` | Vite dev server | Vite proxy 转为同源 | **不触发** |
| 生产安装包 | `https://tauri.localhost` | 直接跨域访问 `http://127.0.0.1:8080` | **必触发** |

- **规则**：凡是 SPA 向 sidecar/本地服务发带 `Content-Type: application/json` 的 POST，**不能仅凭 `tauri dev` 通过就认为没有 CORS 问题**，必须用安装包做完整验收。
- **验证命令**：
  ```powershell
  # 期望 204；若返回 403 则 CORS 预检未正确配置
  curl.exe -s -o NUL -w "%{http_code}" -X OPTIONS `
    -H "Origin: https://tauri.localhost" `
    -H "Access-Control-Request-Method: POST" `
    -H "Access-Control-Request-Headers: Content-Type" `
    http://127.0.0.1:8080/api/v1/plan
  ```

### 8.3 Drogon CORS 正确配置

- **`registerPreRoutingAdvice` vs `registerPreHandlingAdvice`**：
  - PreHandling 运行在路由匹配**之后**：Drogon 在路由阶段对方法不匹配的请求已返回 403，PreHandling 来不及拦截。
  - **`registerPreRoutingAdvice` 运行在路由匹配之前**，是拦截 OPTIONS 预检的唯一有效位置。
- **通配路由优先级陷阱**：`/{path}` 通配符路由优先级低于精确路由。已有精确 POST/GET 路由的路径，OPTIONS 请求会匹配精确路由但因方法不允许返回 403，而非走通配 OPTIONS 处理器。
- **标准配置**（`http_server.cpp`）：
  ```cpp
  // OPTIONS 预检：必须用 PreRoutingAdvice（路由匹配前）
  drogon::app().registerPreRoutingAdvice(
      [](const drogon::HttpRequestPtr &req, auto &&done, auto &&pass) {
          if (req->method() == drogon::Options) {
              auto resp = drogon::HttpResponse::newHttpResponse();
              resp->setStatusCode(drogon::k204NoContent);
              resp->addHeader("Access-Control-Allow-Origin", "*");
              resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
              resp->addHeader("Access-Control-Allow-Headers",
                              "Content-Type, X-User-Id, X-Trace-Id, X-Api-Key");
              resp->addHeader("Access-Control-Max-Age", "86400");
              done(resp); return;
          }
          pass();
      });
  // 非 OPTIONS 响应补充 CORS 头：PostHandlingAdvice
  drogon::app().registerPostHandlingAdvice(
      [](const drogon::HttpRequestPtr &req, const drogon::HttpResponsePtr &resp) {
          resp->addHeader("Access-Control-Allow-Origin", "*");
      });
  ```

### 8.4 安装包验收检查清单

每次重建安装包后，**必须用安装包**（而非 `tauri dev`）完成以下检查：

1. 启动后 sidecar 进程（`finguard.exe`）出现在任务管理器。
2. `GET /health` → 200。
3. `OPTIONS /api/v1/plan`（带 Origin: https://tauri.localhost）→ 204。
4. 前端打开"配置建议"页面并提交 → 无 `Failed to fetch`。
5. 前端打开"设置"页面 → 能正常加载当前配置。
6. 关闭应用 → `finguard.exe` 无残留进程。

---

## 10. 外部 API 调用（Yahoo Finance / 搜索引擎）

### 9.1 User-Agent 必须模拟浏览器

- Yahoo Finance 非官方 API 对轻量 UA（如 `Mozilla/5.0 (compatible; FinGuard/1.0)`）返回 429 限流。
- **必须**使用完整浏览器 UA：`Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36`
- 适用于所有通过 `curl.exe` 或 HTTP client 发出的请求。

### 9.2 Accept 头与响应格式匹配

- 非 JSON 端点（如 Yahoo crumb 返回纯文本）不能发送 `Accept: application/json`，否则返回 406 Not Acceptable。
- **规则**：默认使用 `Accept: */*`，仅在明确知道端点返回 JSON 时才用 `Accept: application/json`。

### 9.3 Yahoo Finance API 字段位置

- `trailingPE` 在 `summaryDetail` 模块（不在 `defaultKeyStatistics`）
- `priceToBook` 在 `defaultKeyStatistics` 模块（不在 `financialData`）
- `pegRatio` 对很多股票为空对象 `{}`，须自算 fallback：`PEG = trailingPE / (earningsGrowth × 100)`
- 请求时须包含 `summaryDetail` 模块：`modules=defaultKeyStatistics,financialData,summaryDetail`

### 9.4 Yahoo 中国大陆封锁

- Yahoo 自 2021-11-01 起封锁中国大陆访问，直连返回封锁页面。
- **必须**通过代理（HTTP CONNECT 隧道）访问，且代理出口 IP 不在中国大陆。

---

## 11. 代码组织与模块化规范

### 10.1 前置代码阅读

- **改动前必读**：修改或扩展某个模块时，必须先通读相关的现有实现，理解：
  - 现有接口设计与约定
  - 依赖关系与调用链路
  - 错误处理与边界条件
  - 使用的设计模式与抽象
- **规则**：不允许在未理解代码结构的情况下直接添加代码，否则易造成重复实现或违反现有设计。

### 10.2 最小改动原则

- **改动范围要小**：每次修改应只涉及必要的代码，避免过度重构或大规模重组。
- **新增优于修改**：优先通过新增文件/函数/类来实现新功能，而非修改现有代码；保留原有代码的稳定性。
- **向后兼容**：新功能不应破坏现有接口；若需接口变更，需在 Spec 中明确讨论并更新所有调用方。
- **示例**：
  - ❌ 不好：直接修改 `AllocationRule` 基类，添加 N 个新参数
  - ✅ 好：新建 `ExtendedAllocationRule` 继承自 `AllocationRule`，或在工厂类中添加新逻辑

### 10.3 单一职责原则

- **一个文件一个功能**：
  - `.h` 文件中声明单一的类或相关类族（如 `rule_interface.h` 只声明 `AllocationRule` 及其异常类）
  - `.cpp` 文件中实现该类或类族的所有方法
  - 不混合多个独立功能到一个文件（如不应将"规则引擎"和"HTTP路由"混在一起）

- **文件划分指南**：
  - **接口定义**：`core/allocation_rule.h` —— 基类接口
  - **具体实现**：`core/rules/shouzhe_rule.cpp` —— 单个规则类
  - **工厂逻辑**：`core/rule_factory.h/cpp` —— 规则创建与管理
  - **工具函数**：`util/allocation_utils.h/cpp` —— 辅助计算函数

### 10.4 代码组织的文件树规范

```
finguard/src/
├── core/                          # 核心业务逻辑
│   ├── allocation_rule.h          # 规则基类接口
│   ├── rules/                     # 具体规则实现（每个规则一个文件）
│   │   ├── shouzhe_rule.h/cpp
│   │   ├── bridgewater_rule.h/cpp
│   │   ├── permanent_rule.h/cpp
│   │   └── swensen_rule.h/cpp
│   ├── rule_factory.h/cpp         # 规则工厂类
│   └── allocation_types.h         # 共享的数据结构（AllocationResult 等）
│
├── util/                          # 通用工具
│   ├── allocation_utils.h/cpp     # 配置计算辅助函数
│   └── ...
│
├── server/                        # HTTP 服务层
│   ├── routes.cpp                 # 路由定义
│   └── handlers/                  # 各端点业务逻辑（可选细分）
│
└── ...
```

### 10.5 文件与职责对应关系

| 文件 | 职责 | 允许包含 | 禁止包含 |
|------|------|--------|---------|
| `allocation_rule.h` | 规则接口定义 | 虚函数、枚举、异常类 | 具体实现、HTTP路由 |
| `shouzhe_rule.cpp` | 守拙理念规则逻辑 | 年龄计算、分层逻辑 | 其他规则、HTTP响应 |
| `rule_factory.cpp` | 规则实例化与选择 | switch/if 选择逻辑 | 具体规则计算 |
| `routes.cpp` | HTTP 路由与请求处理 | 路由注册、请求解析 | 业务计算逻辑（应委托给 rule engine） |
| `allocation_utils.cpp` | 通用计算工具 | 索引分配、配置合并 | HTTP相关、规则选择 |

### 10.6 代码审查检查清单

添加新功能或修改现有代码时，每次提交前检查：

- [ ] 是否理解了现有相关代码的设计意图
- [ ] 修改范围是否最小（仅涉及必要的文件）
- [ ] 是否遵循单一职责原则（一个文件对应一个功能）
- [ ] 是否保持向后兼容（现有接口是否变更）
- [ ] 是否添加了相应的单元测试
- [ ] 文件与职责对应关系是否清晰（看名字能推测出功能）
- [ ] 是否有不必要的代码重复

---

变更日志- [2026-03-26] 新增 **第 11 章：代码组织与模块化规范**，来源資產配置多規則框架建設：前置代码阅读、最小改动原则、单一职责、文件划分准则、审查清单。- [2026-03-09] 自动加入：沉淀腾讯 BPO 面试准备方法，采用“Gemini 出题 + Coding Agent 找仓库证据 + 文档回写 + 再追问”的闭环迭代策略。
- [2026-03-03] 新增 **第 9 章：外部 API 调用**，来源 Phase 10 Yahoo 429 修复经验：User-Agent 模拟、Accept 头匹配、字段位置映射、中国大陆封锁。
- [2026-02-27] 新增 **第 8 章：Tauri 桌面应用打包**，来源 Phase 9 安装包验收发现的两个 Bug：Tauri v2 `__TAURI_INTERNALS__` 检测、dev/生产跨域行为根本差异、Drogon `registerPreRoutingAdvice` vs `PreHandlingAdvice` 陷阱、安装包验收检查清单。
- [2026-02-26] 新增 **第 7 章：前端开发**，总结 Phase 7 中解决的 12 个实际问题的通用规则：交互式 CLI 跳过、CORS 预检处理、Vite proxy 同源方案、状态管理 fallback、业务字段对齐、浮点精度容差、信息折叠 UI 等。
- [2026-02-25] 全面整理：移除过时条目（微信小程序、Python 后端相关）；移除已迁入其他 Skill 文档的重复规则；新增配置/编译/压测经验条目。
