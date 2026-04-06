# Phase 11 - ValueCell 后端第一波屎山治理

## Chapter 1: Spec pre-plan (frozen)

### 1) 本轮治理范围
- Decision / recommendation: 只治理 ValueCell 后端热点 `finguard/src/valuation/yahoo_finance_client.cpp`。
- Why: 当前最大维护热点集中在单一超大文件，先拆热点比全仓库重写风险更低、收益更高。
- Details: 不处理前端页面拆分，不处理 `routes.cpp`，不改产品 spec、字段口径、评分规则、页面表现。

### 2) 外部接口兼容性
- Decision / recommendation: 保持 `/api/v1/valuecell`、`fetch_financial_metrics(symbol, config)`、`recompute_financial_metrics(...)` 对外兼容。
- Why: 本轮目标是结构治理，不应引入接口层回归。
- Details: warning code、fallback 顺序、缓存语义、BRK alias 归一逻辑保持原样。

### 3) 内部分层冻结
- Decision / recommendation: ValueCell 后端按 5 类职责拆分：HTTP/Session 基础层、Quote Source、Balance Sheet Source、Net Income Source、Pipeline/Orchestration。
- Why: 把 HTTP、缓存、fallback、派生计算从单文件里拆开，降低认知负担。
- Details:
  - `yahoo_finance_client.h` 继续只暴露 public API
  - `yahoo_finance_client.cpp` 缩成 orchestration + recompute + price history 薄入口
  - 新内部文件控制在 300~550 行，避免再造 1000+ 行热点文件

## Chapter 2: Phase 11 plan
Steps:
1. 读取当前 ValueCell 后端实现、测试入口、phase/next_plan 文档。
2. 新增内部 header 与 source 文件，抽出共享 helper、quote source、balance sheet source、net income source。
3. 保持 public API 不变，重写 `yahoo_finance_client.cpp` 为薄编排入口。
4. 补充 BRK alias 与 quote merge/backfill 回归测试。
5. 执行 Release build、`finguard_tests.exe`、前端 `npm run build`。
6. 更新 phase 文档与交接文档。

Acceptance items:
1. `yahoo_finance_client.cpp` 不再承担所有 source 细节。
2. Quote / debt ratio / net income / orchestration 能在 10 分钟内定位。
3. Release build 通过。
4. `finguard_tests.exe` 全绿。
5. 前端 `npm run build` 通过。

## Chapter 3: Execution details

### Step execution log
- 新增内部文件：
  - `finguard/src/valuation/yahoo_finance_client_internal.h`
  - `finguard/src/valuation/yahoo_finance_client_common.cpp`
  - `finguard/src/valuation/yahoo_finance_quote_sources.cpp`
  - `finguard/src/valuation/yahoo_finance_balance_sheet_sources.cpp`
  - `finguard/src/valuation/yahoo_finance_net_income_sources.cpp`
- `finguard/src/valuation/yahoo_finance_client.cpp` 重写为薄入口：
  - 保留 `recompute_financial_metrics(...)`
  - 保留 `fetch_financial_metrics(...)`
  - 保留 price history 获取
  - 将 quote/balance sheet/net income/source helper 挪到独立文件
- `finguard/CMakeLists.txt` 已注册新增源文件。
- `finguard/tests/test_valuation_metrics.cpp` 已补两类编排回归：
  - BRK alias 归一兼容
  - quote merge/backfill 与主源保持逻辑

### Problems and fixes
- 问题: 新拆分后 `net_income` 源文件缺少 `<thread>` / `<chrono>` 头，首次 Release build 失败。
- 修复: 补齐缺失头文件后重新构建，通过编译。
- 说明: 本轮未改动实际网络链路与接口 shape，只做职责拆分与测试补强。

## Chapter 4: Acceptance details

### Acceptance checklist
- [x] `yahoo_finance_client.cpp` 从约 2000 行收缩到约 460 行
- [x] Quote source 独立定位：`yahoo_finance_quote_sources.cpp`
- [x] Debt ratio / balance sheet 独立定位：`yahoo_finance_balance_sheet_sources.cpp`
- [x] Net income 独立定位：`yahoo_finance_net_income_sources.cpp`
- [x] Shared helper / cache / crumb / alias 独立定位：`yahoo_finance_client_common.cpp`
- [x] Release build 通过
- [x] `finguard_tests.exe` 全绿（39 tests passed）
- [x] 前端 `npm run build` 通过

### Artifact list
- `finguard/src/valuation/yahoo_finance_client_internal.h`
- `finguard/src/valuation/yahoo_finance_client.cpp`
- `finguard/src/valuation/yahoo_finance_client_common.cpp`
- `finguard/src/valuation/yahoo_finance_quote_sources.cpp`
- `finguard/src/valuation/yahoo_finance_balance_sheet_sources.cpp`
- `finguard/src/valuation/yahoo_finance_net_income_sources.cpp`
- `finguard/tests/test_valuation_metrics.cpp`
- `finguard/CMakeLists.txt`

### Change log
- 2026-03-29: 完成 ValueCell 后端第一波结构治理，保持外部接口不变，完成核心热点拆分与回归验证。
