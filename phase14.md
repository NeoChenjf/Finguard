# Phase 14 - llm_client.cpp 第一波治理

## Chapter 1: Spec pre-plan (frozen)

### 1) 本轮治理目标
- Decision / recommendation: 优先治理 `finguard/src/llm/llm_client.cpp`，将其从配置加载 + HTTP 请求 + stream 编排 + fallback + metrics 混合文件收敛为内部模块分层。
- Why: 当前 `llm_client.cpp` 是后端最大代码热点，且位于核心调用链上，维护风险高。
- Details: 本轮只做结构治理，不改 `LlmClient` public API，不改请求/响应语义，不改降级与缓存行为。

### 2) 外部兼容性冻结
- Decision / recommendation: 保持 `LlmClient::load_config()`、`LlmClient::stream_chat(...)`、`invalidate_llm_config_cache()` 对外兼容。
- Why: 本轮目标是拆热点，不是 LLM 接入策略改版。
- Details: config 读取顺序、env 覆盖、curl fallback、circuit breaker、rate limit、warning/error 字段默认保持现状。

### 3) 结构边界冻结
- Decision / recommendation: 将 LLM 客户端拆成三层：shared/common helpers、config/cache、stream/orchestration。
- Why: 把 URL/代理/JSON 解析、配置缓存、请求编排从单文件里分开，降低认知负担。
- Details:
  - `llm_client.h` 继续只暴露 public API
  - `llm_client.cpp` 缩成 public API 薄入口
  - shared helper 负责 trim/env/url/proxy/JSON/fallback
  - config 层负责默认配置、文件/env 覆盖、全局缓存
  - stream 层负责 request building、retry、降级、token 化输出

## Chapter 2: Phase 14 plan
Steps:
1. 读取 `llm_client.cpp/.h` 与现有测试入口。
2. 新增 llm internal header，定义内部 helper 与结构体边界。
3. 拆出 common/config/stream 三类内部实现文件。
4. 保持 public API 不变，将 `llm_client.cpp` 收缩为薄入口。
5. 补 helper 级回归测试。
6. 执行 Release build、`finguard_tests.exe`、前端 build。
7. 更新 phase 文档与 `next_plan.md`。

Acceptance items:
1. `llm_client.cpp` 不再承担全部细节。
2. config/cache、request helper、stream orchestration 能在 10 分钟内定位。
3. 构建与测试通过。
4. LLM public API 与兼容行为保持不变。

## Chapter 3: Execution details

### Step execution log
- 已读取并确认 Phase 14 相关实现与接线文件：
  - `finguard/src/llm/llm_client.cpp`
  - `finguard/src/llm/llm_client_internal.h`
  - `finguard/src/llm/llm_client_common.cpp`
  - `finguard/src/llm/llm_client_config.cpp`
  - `finguard/src/llm/llm_client_stream.cpp`
  - `finguard/tests/test_llm_helpers.cpp`
  - `finguard/CMakeLists.txt`
- 已完成 LLM client 第一波拆分落地：
  - `llm_client.cpp` 缩成 public API 薄入口说明文件
  - `llm_client_common.cpp` 收拢 trim/env/url/proxy/JSON/curl fallback 等公共 helper
  - `llm_client_config.cpp` 收拢默认配置、文件/env 读取、TTL 缓存与 public config wrapper
  - `llm_client_stream.cpp` 收拢 `stream_chat(...)` 的 request building、retry、fallback、token 化编排
- 已补 helper 级回归测试：
  - `parse_base(...)`
  - `parse_proxy(...)`
  - `join_url(...)`
- 已补齐构建接线：
  - `finguard/CMakeLists.txt` 新增 3 个 LLM 内部实现文件
  - `finguard/CMakeLists.txt` 新增 `tests/test_llm_helpers.cpp`
- 已执行回归验证：
  - 2026-03-29 `cmake --build build --config Release` 通过
  - 2026-03-29 `ctest -C Release --output-on-failure` 47/47 通过
  - 2026-03-29 `frontend/npm.cmd run build` 通过

### Problems and fixes
- 问题 1：拆分后新文件未接入 `finguard_core` 与测试目标。
  - 修复：更新 `finguard/CMakeLists.txt`，补入 `llm_client_common.cpp`、`llm_client_config.cpp`、`llm_client_stream.cpp` 以及 `test_llm_helpers.cpp`。
- 问题 2：`llm_client_config.cpp` 使用 `std::chrono` 但缺少头文件。
  - 修复：补充 `#include <chrono>`。
- 问题 3：`llm_client_stream.cpp` 使用 `trantor::Resolver::isCAresUsed()`，需显式包含 Resolver 头。
  - 修复：补充 `#include <trantor/net/Resolver.h>`。
- 问题 4：CMake 接线时出现换行粘连，导致源文件列表格式损坏。
  - 修复：重新整理 `CMakeLists.txt` 对应条目并回归构建验证。

## Chapter 4: Acceptance details

### Acceptance checklist
- [x] `llm_client.cpp` 瘦身完成
- [x] common/config/stream 拆分完成
- [x] helper 测试补充完成
- [x] Release build 通过
- [x] `finguard_tests.exe` 全绿
- [x] 前端 build 通过

### Artifact list
- `finguard/src/llm/llm_client.cpp`
- `finguard/src/llm/llm_client_internal.h`
- `finguard/src/llm/llm_client_common.cpp`
- `finguard/src/llm/llm_client_config.cpp`
- `finguard/src/llm/llm_client_stream.cpp`
- `finguard/tests/test_llm_helpers.cpp`
- `finguard/CMakeLists.txt`
- `next_plan.md`

### Change log
- 2026-03-29: 启动 Phase 14，开始治理 LLM 客户端热点。
- 2026-03-29: 完成 `llm_client.cpp` 第一波治理，拆分 common/config/stream 三层并通过后端与前端回归。
