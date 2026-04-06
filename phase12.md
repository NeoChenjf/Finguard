# Phase 12 - routes.cpp 第一波治理

## Chapter 1: Spec pre-plan (frozen)

### 1) 本轮治理目标
- Decision / recommendation: 优先治理 `finguard/src/server/routes.cpp`，将其收敛为薄注册入口。
- Why: 当前 `routes.cpp` 混合了路由注册、业务 handler、限流、并发控制、trace、metrics、CORS、settings 写锁等多类职责，是后端新的集中热点。
- Details: 本轮只做结构治理，不改接口、不改返回 shape、不改行为语义。

### 2) 外部兼容性冻结
- Decision / recommendation: 保持现有路由路径、HTTP 方法、请求/响应 JSON、状态码、trace header、限流行为、CORS 行为不变。
- Why: 本轮目标是拆热点，不是接口改版。
- Details: `/health`、`/metrics`、`/api/v1/plan`、`/api/v1/profile/upsert`、`/api/v1/chat/stream`、`/api/v1/settings`、`/api/v1/valuecell`、`OPTIONS /api/v1/{path}` 全部保持兼容。

### 3) 结构边界冻结
- Decision / recommendation: `routes.cpp` 只保留 `setup_routes()` 与组装调用；路由按领域拆组，横切辅助独立收敛。
- Why: 避免继续把新接口和横切基础逻辑堆回一个文件。
- Details:
  - 路由组至少拆为：health/system、plan、profile、chat、valuecell
  - 横切辅助至少抽出：trace id / user id / entry key、rate limit body、metrics logging、CORS helper、共享 limiter / mutex
  - `valuation_handler.cpp` 保持独立，不回流到 routes 层

## Chapter 2: Phase 12 plan
Steps:
1. 读取 `routes.cpp`、相关 handler、构建与测试入口。
2. 新增 routes internal/support 模块，承载横切辅助与共享状态。
3. 按领域拆出路由注册文件，保留原行为与调用链。
4. 将 `routes.cpp` 收缩为薄注册入口。
5. 补充 helper 级回归测试。
6. 执行 Release build、`finguard_tests.exe`、前端 `npm run build`。
7. 更新 phase 文档与 `next_plan.md`。

Acceptance items:
1. `routes.cpp` 明显收缩为薄注册入口。
2. 可在 10 分钟内定位各路由组与横切辅助入口。
3. 构建与测试通过。
4. 外部接口与行为保持兼容。

## Chapter 3: Execution details

### Step execution log
- 新增横切内部模块：
  - `finguard/src/server/routes_internal.h`
  - `finguard/src/server/route_support.cpp`
- 新增路由分组文件：
  - `finguard/src/server/health_routes.cpp`
  - `finguard/src/server/plan_routes.cpp`
  - `finguard/src/server/profile_routes.cpp`
  - `finguard/src/server/chat_routes.cpp`
  - `finguard/src/server/system_routes.cpp`
  - `finguard/src/server/valuecell_routes.cpp`
- `finguard/src/server/routes.cpp` 已缩成薄注册入口，仅负责调用各路由组注册函数。
- `finguard/CMakeLists.txt` 已注册新增源文件与回归测试文件。
- 新增 `finguard/tests/test_route_support.cpp`，覆盖：
  - trace id 透传 / fallback
  - entry key 生成
  - 429 body shape
  - CORS header shape

### Problems and fixes
- 问题: 新拆分后，`plan_routes.cpp` / `chat_routes.cpp` / `profile_routes.cpp` 仅依赖 `routes_internal.h` 的前向声明，MSVC 在调用 `TokenBucket` / `ConcurrencyLimiter` 成员函数时报未定义类型错误。
- 修复: 在 `routes_internal.h` 中直接包含 `util/token_bucket.h` 与 `util/concurrency_limiter.h`，确保调用点可见完整类型。
- 说明: 本轮未改动任何路由 wire shape、状态码、header 语义与业务调用链，只做职责拆分与 helper 级回归测试。

## Chapter 4: Acceptance details

### Acceptance checklist
- [x] `routes.cpp` 瘦身完成（收缩到约 12 行）
- [x] 路由组拆分完成
- [x] 横切辅助收敛完成
- [x] Release build 通过
- [x] `finguard_tests.exe` 全绿（44 tests passed）
- [x] 前端 `npm run build` 通过

### Artifact list
- `finguard/src/server/routes.cpp`
- `finguard/src/server/routes_internal.h`
- `finguard/src/server/route_support.cpp`
- `finguard/src/server/health_routes.cpp`
- `finguard/src/server/plan_routes.cpp`
- `finguard/src/server/profile_routes.cpp`
- `finguard/src/server/chat_routes.cpp`
- `finguard/src/server/system_routes.cpp`
- `finguard/src/server/valuecell_routes.cpp`
- `finguard/tests/test_route_support.cpp`
- `finguard/CMakeLists.txt`

### Change log
- 2026-03-29: 完成 Phase 12 第一波 routes 层结构治理，将总路由层收敛为薄注册入口，并补 helper 级回归测试。
