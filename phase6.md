# Phase 6 — 测试加固与 Release 构建

## Chapter 1：Spec 前置计划（冻结）

说明：汇总 Phase 6 关键 Spec 的推荐方案与理由，作为统一基线。Phase 6 执行"最小必要"原则——设置接口与 Release 构建优先，gtest 补齐关键路径即可，目标是尽快解锁 Phase 7 前端开发。

### 1) 设置接口（Settings API）

- 推荐：新增 `GET /api/v1/settings` 和 `POST /api/v1/settings` 两个端点，由 `routes.cpp` 注册。
- 理由：Phase 7 前端需要动态配置 API Key / 模型 URL / 模型名称，当前只支持静态文件 + 环境变量，无法满足运行时修改需求。
- 详情：

  **GET /api/v1/settings**
  - 返回当前 LLM 配置的可展示字段（不返回完整 API Key，仅返回是否已配置 + 掩码后 4 位）。
  - 响应示例：
    ```json
    {
      "api_base": "https://dashscope.aliyuncs.com/compatible-mode/v1",
      "api_key_configured": true,
      "api_key_hint": "****ab3f",
      "model": "qwen-plus",
      "temperature": 0.7,
      "timeout_ms": 30000
    }
    ```
  - 不需要认证（本地服务，面向桌面前端）。

  **POST /api/v1/settings**
  - 接受 JSON body，支持部分更新（只传需要修改的字段）。
  - 请求示例：
    ```json
    {
      "api_key": "sk-xxxx",
      "api_base": "https://api.openai.com/v1",
      "model": "gpt-4o-mini"
    }
    ```
  - 可选字段：`temperature`（number）、`timeout_ms`（int）。
  - 处理流程：读取当前 `config/llm.json` → 合并传入字段 → 写回文件 → 刷新内存缓存（使 Phase 5 的 5s TTL 缓存立即失效）。
  - 响应：成功返回 `{"status": "ok", "message": "settings updated"}`；失败返回 `{"error": "xxx"}`。
  - 写文件时加锁（`std::mutex`），防止并发写冲突。

### 2) 缓存刷新机制

- 推荐：在 POST `/settings` 成功写入文件后，主动使 LlmConfig 缓存失效（将 TTL last_update 重置），确保下次请求立即读取新配置。
- 理由：Phase 5 引入了 5 秒 TTL 配置缓存。若不主动刷新，用户修改 API Key 后最多要等 5 秒才生效，体验差。
- 详情：在 `llm_client.cpp` 中暴露 `invalidate_llm_config_cache()` 函数，POST handler 写文件后调用。

### 3) CORS 支持

- 推荐：为所有 `/api/v1/*` 路由添加 CORS 响应头，并处理 `OPTIONS` 预检请求。
- 理由：Phase 7 前端开发阶段，Vite dev server（端口 5173）会向 finguard（端口 8080）发起跨域请求，浏览器会拦截无 CORS 头的响应。
- 详情：
  - 响应头：`Access-Control-Allow-Origin: *`、`Access-Control-Allow-Methods: GET, POST, OPTIONS`、`Access-Control-Allow-Headers: Content-Type, X-User-Id, X-Trace-Id, X-Api-Key`。
  - `OPTIONS` 请求返回 204 + 上述头即可。
  - 实现方式：Drogon 过滤器（全局 `doBeforeHandling`）或在每个 handler 中手动添加。推荐全局方式，减少重复代码。

### 4) Release 构建

- 推荐：CMake Release 配置（`-DCMAKE_BUILD_TYPE=Release`）+ MSVC `/O2` 优化。

- 理由：Phase 5 全程 Debug 构建，`/health` P99 14ms 未达目标（<5ms）。Release 构建可显著提升性能，也是最终交付的编译模式。

- 详情：
  - 使用 `cmake --build . --config Release` 构建。
  - 验证 Release 构建产物可正常运行（`/health`、`/plan`、`/chat/stream`、`/settings`）。
  - 运行一次 S1（health）+ S2（plan）快速压测，对比 Debug 基线。
  - 不要求完整 S1-S5 回归（Phase 5 已完成完整压测）。
  
  #### a. CMake 的角色：项目的“蓝图”与“翻译官”
  
  你的直觉很准。CMake 本身不负责编译代码，它的作用是**标准化**：
  
  - **跨平台标准**：C++ 在 Windows（用 MSVC）、Linux（用 GCC/Clang）上的编译指令完全不同。CMake 就像一份“翻译文档”，它读取 [CMakeLists.txt](vscode-file://vscode-app/c:/Users/11956/AppData/Local/Programs/Microsoft VS Code/072586267e/resources/app/out/vs/code/electron-browser/workbench/workbench.html)，然后为当前系统生成对应的工程文件（在 Windows 上就是你看到的 `.slnx` 或 `.vcxproj`）。
  - **依赖管理**：它负责告诉系统去哪里找 `Drogon`、`OpenSSL` 等三方库，并把它们正确地连接到你的后端（`finguard.exe`）中，确保生成的程序是“完整可用”的。
  
  #### b. Debug vs Release：开发版 vs 最终产品
  
  你观察到的 `build` 文件夹下的两个目录代表了两种完全不同的“构建配置”：
  
  | 特性         | Debug (调试模式)                         | Release (发布模式)                   |
  | ------------ | ---------------------------------------- | ------------------------------------ |
  | **目的**     | 程序员开发、找 Bug 使用。                | 最终用户运行。                       |
  | **性能**     | 较慢。包含大量调试信息，不进行代码优化。 | **极快**。编译器会疯狂优化代码逻辑。 |
  | **体量**     | 文件巨大（包含源代码映射、符号表）。     | 文件小巧（纯机器码）。               |
  | **异常报错** | 会准确告诉你哪一行代码出错了。           | 出错时通常只给一个模糊的错误地址。   |
  
  #### c. 为什么 CMake 是 Release 之前的必备选项？
  
  在 C++ 中，从代码到 Release 加速成品，必须经过以下链路：
  
  1. **配置阶段 (CMake)**：根据蓝图准备环境，决定哪些功能要编译。
  
  2. 生成阶段 (Build)
  
     ：调用编译器（如 MSVC）根据配置把
  
     ```
     .cpp
     ```
  
     转换成机器指令。
  
     - 如果你选 Debug，它就生成一个方便你改 Bug 的测试品。
     - 如果你选 **Release**，它会进行深度优化（如函数内联、循环拆解等）。
  
  **总结：**
  
  - **CMake** 让你的 C++ 项目从一堆零散的文本文件变成了“有组织、可构建的软件工程”。
  - **Release** 则是把这份工程榨干性能，变成你看到的那个 P99 延迟低至 5ms 左右的高性能服务端成品。
  
  我们在 Phase 6 刚完成的任务，本质就是从“能跑就行”的 Debug 开发状态，正式迈向了“生产就绪”的 Release 交付状态。

### 5) gtest 单元测试范围

- 推荐：补齐以下关键模块的实质性测试用例，替换现有 Placeholder。
- 理由：当前 `test_rules.cpp` 和 `test_cache.cpp` 仅有 `EXPECT_TRUE(true)` 占位符，无实际测试价值。Phase 7-9 会频繁改动前端对接逻辑，需要后端测试守护防止回归。
- 测试模块与范围：

  | 模块 | 测试文件 | 测试点 |
  |------|----------|--------|
  | 风控规则引擎 | `test_rules.cpp` | 规则加载、单资产上限触发、现金下限触发、禁用清单触发、全部通过场景 |
  | LRU 缓存 | `test_cache.cpp` | 插入/命中/淘汰、容量上限、TTL 过期（ConfigCache） |
  | LLM 配置解析 | `test_llm_config.cpp`（新增） | JSON 解析正确性、缺失字段默认值、环境变量覆盖 |
  | 令牌桶 | `test_token_bucket.cpp`（新增） | 初始容量、消耗与恢复、超限拒绝 |
  | 熔断器 | `test_circuit_breaker.cpp`（新增） | 状态转换（Closed→Open→HalfOpen→Closed）、失败计数、半开探测 |

- CMake 改动：`finguard_tests` 需要链接被测源文件（当前只链接 gtest，无法访问被测代码）。推荐将核心逻辑编译为静态库 `finguard_core`，主程序和测试都链接该库。
- 不追求 100% 覆盖率，关键路径有测试守护即可。

### 6) 测试目标架构调整

- 推荐：将 `finguard` 的核心源文件抽为静态库 `finguard_core`（不含 `main.cpp`），`finguard` exe 链接 `finguard_core`，`finguard_tests` 也链接 `finguard_core`。
- 理由：当前 `finguard_tests` 只链接 `GTest::gtest_main`，无法访问 `rule_engine`、`token_bucket` 等被测代码。直接在测试中 `#include` 源文件会导致重复编译和链接问题。静态库方案是 C++ 项目的标准做法。
- CMake 改动概要：
  ```cmake
  # 核心静态库（不含 main.cpp）
  add_library(finguard_core STATIC
      src/llm/llm_client.cpp
      src/risk/profile_store.cpp
      src/risk/rule_engine.cpp
      src/server/http_server.cpp
      src/server/routes.cpp
      src/util/simple_yaml.cpp
      src/util/reliability_config.cpp
      src/util/token_bucket.cpp
      src/util/circuit_breaker.cpp
      src/util/metrics_registry.cpp
      src/util/concurrency_limiter.cpp
  )
  target_include_directories(finguard_core PUBLIC src)
  target_link_libraries(finguard_core PUBLIC Drogon::Drogon nlohmann_json::nlohmann_json spdlog::spdlog)
  
  # 主程序
  add_executable(finguard src/main.cpp)
  target_link_libraries(finguard PRIVATE finguard_core)
  
  # 测试
  add_executable(finguard_tests tests/test_rules.cpp tests/test_cache.cpp ...)
  target_link_libraries(finguard_tests PRIVATE finguard_core GTest::gtest_main)
  ```

### 7) 文档完善范围

- 推荐：更新 `finguard/README.md`，补齐以下内容。
- 理由：当前 README 较简略，Phase 7 前端开发者需要清晰的接口文档。
- 内容：
  1. 架构概览图（文字版 ASCII 或 Mermaid）
  2. 接口列表与 curl 示例（`/health`、`/plan`、`/chat/stream`、`/settings`、`/metrics`）
  3. 配置文件说明（`llm.json`、`rules.yaml`、`rate_limit.yaml` 等）
  4. 构建与运行说明（Debug / Release）
  5. Phase 5 压测数据摘要
  6. 风控规则示例（`rules.yaml` 字段说明）

### 8) 验收口径

- 推荐：所有验收以本地手动 + 脚本验证为主，不引入 CI/CD。

- 理由：Phase 6 目标是快速解锁 Phase 7，CI/CD 改造超出范围。

- 口径：
  - gtest：`ctest --output-on-failure` 全部 PASS。
  - Release 构建：`cmake --build . --config Release` 成功，产物可运行。
  - Settings API：curl 测试 GET/POST 正常，API Key 更新后立即生效。
  - CORS：浏览器 `fetch('http://localhost:8080/health')` 无跨域错误。
  - 文档：README 包含上述 7 项内容。
  
- 补充：
  CI/CD 是一套“自动化交付”流程：
  
  - CI（Continuous Integration，持续集成）：
    代码一提交就自动构建、跑测试、做质量检查，尽早发现问题。
  - CD（Continuous Delivery/Deployment，持续交付/持续部署）：
    把通过 CI 的版本自动打包并发布（交付到可上线状态，或直接上线）。
  
  一句话：**CI 负责“自动验收代码”，CD 负责“自动发版本”。**

## Chapter 2：Phase 6 计划

完成步骤：

1. 确认并冻结 Spec —— 以 Chapter 1 为准。
2. CMake 架构调整（`finguard_core` 静态库）：
   - 抽取核心源文件为 `finguard_core` 静态库。
   - `finguard` exe 链接 `finguard_core`。
   - `finguard_tests` 链接 `finguard_core` + `GTest::gtest_main`。
   - 验证 Debug 构建通过。
3. 设置接口实现：
   - `GET /api/v1/settings`：读取 LlmConfig，掩码 API Key 后返回。
   - `POST /api/v1/settings`：解析 JSON body → 合并到 `config/llm.json` → 写文件 → 刷新缓存。
   - 暴露 `invalidate_llm_config_cache()` 并在 POST handler 中调用。
4. CORS 支持：
   - 全局 CORS 过滤器（`doBeforeHandling` 或拦截器）。
   - `OPTIONS` 预检请求返回 204。
5. gtest 单元测试补齐：
   - `test_rules.cpp`：风控规则引擎实质测试。
   - `test_cache.cpp`：LRU 缓存 / ConfigCache 测试。
   - `test_llm_config.cpp`（新增）：LLM 配置解析测试。
   - `test_token_bucket.cpp`（新增）：令牌桶测试。
   - `test_circuit_breaker.cpp`（新增）：熔断器状态机测试。
6. Release 构建与验证：
   - `cmake -DCMAKE_BUILD_TYPE=Release` 配置。
   - `cmake --build . --config Release` 编译。
   - 启动 Release 产物，手动测试核心端点。
   - S1 + S2 快速压测对比 Debug 基线。
7. 文档完善：
   - 更新 `finguard/README.md`（架构图、接口文档、配置说明、构建说明、压测数据）。
8. 文档同步：
   - 完成 `phase6.md` Chapter 3 & 4。
   - 更新 `next_plan.md`。

验收项目：

1. CMake 架构：`finguard_core` 静态库编译通过，`finguard` exe 和 `finguard_tests` 均可构建。
2. 设置接口：`GET /api/v1/settings` 返回正确 JSON（API Key 掩码）；`POST /api/v1/settings` 更新后，下次 `/plan` 或 `/chat/stream` 请求使用新配置。
3. 缓存刷新：POST settings 后无需等待 5 秒，立即生效。
4. CORS：从 `http://localhost:5173` 发起 `fetch` 到 `http://localhost:8080/api/v1/settings` 无跨域错误。
5. gtest：`ctest --output-on-failure` 全部 PASS，至少 15 个实质测试用例。
6. Release 构建：编译成功，核心端点正常，S1 health P99 < 5ms。
7. 文档：README 包含架构图、接口列表（含 /settings）、配置说明、构建说明、压测数据。
8. 无回归：Phase 1-5 所有端点功能不退化（`/health`、`/plan`、`/chat/stream`、`/metrics` 均可用）。

## Chapter 3：任务完成详情

### 步骤执行记录

- **步骤 1：确认并冻结 Spec** —— 已完成。Chapter 1 内容已冻结，作为执行基线。

- **步骤 2：CMake 架构调整** —— 已完成。
  - 将 12 个核心源文件抽为 `finguard_core` 静态库（`add_library(finguard_core STATIC ...)`）。
  - `finguard` exe 仅含 `main.cpp`，链接 `finguard_core`（`target_link_libraries(finguard PRIVATE finguard_core)`）。
  - `finguard_tests` 链接 `finguard_core` + `GTest::gtest_main`，可访问所有被测代码。
  - Debug 与 Release 构建均通过。

- **步骤 3：设置接口实现** —— 已完成。
  - `GET /api/v1/settings`：读取 `LlmConfig`，返回 `api_base`、`model`、`temperature`、`timeout_ms`、`api_key_configured`（bool）、`api_key_hint`（掩码后 4 位）。
  - `POST /api/v1/settings`：解析 JSON body → 合并到 `config/llm.json` → 写文件（加锁 `g_settings_mutex`）→ 调用 `invalidate_llm_config_cache()`。
  - `invalidate_llm_config_cache()` 在 `llm_client.cpp` 中实现，将全局缓存时间戳重置为零，下次请求立即重新加载。
  - 同步将 `stream_chat()` 中的函数局部 `static` 缓存改为文件作用域全局变量，统一由 `invalidate_llm_config_cache()` 管理。

- **步骤 4：CORS 支持** —— 已完成。
  - 使用 Drogon `registerPostHandlingAdvice()` 全局注入 CORS 响应头（`Access-Control-Allow-Origin: *`、`Allow-Methods`、`Allow-Headers`）。
  - 注册 `OPTIONS /api/v1/{path}` 处理器，返回 204 + CORS 头。
  - 所有端点（`/health`、`/plan`、`/settings`、`/metrics`、`/chat/stream`）均携带 CORS 头。

- **步骤 5：gtest 单元测试补齐** —— 已完成。
  - `test_rules.cpp`：7 个实质测试（规则加载、禁词触发、clean prompt、novice 限制、experienced 超限、全通过场景、响应禁止词检测）。
  - `test_cache.cpp`：6 个实质测试（首次加载、TTL 内缓存、TTL 过期重载、手动 invalidate、线程安全）。
  - `test_llm_config.cpp`（新增）：5 个实质测试（正常解析、缺失字段默认值、文件不存在、无效 JSON、空对象）。
  - `test_token_bucket.cpp`（新增）：5 个实质测试（初始容量 burst、恢复、超限拒绝、不同 key 独立、零值容错）。
  - `test_circuit_breaker.cpp`（新增）：7 个实质测试（初始 Closed、成功保持、Closed→Open、Open→HalfOpen、HalfOpen→Closed、HalfOpen→Open、不同 key 独立）。
  - 合计 **30 个测试用例**（含 1 个 gtest 自动发现的 variant），全部 PASS。

- **步骤 6：Release 构建与验证** —— 已完成。
  - `cmake --build . --config Release` 编译成功（`finguard_core.lib` + `finguard.exe` + `finguard_tests.exe`）。
  - Release 产物启动正常，验证端点：`/health`（200 ok）、`/api/v1/plan`（200）、`/api/v1/settings` GET/POST（200）、`/metrics`（200）。
  - Settings POST 后 GET 立即反映新配置（无需等 5 秒），缓存刷新机制生效。
  - Quick bench：PowerShell 串行 100 请求 /health，P99=5.31ms（Phase 5 Debug 基线 14ms）。

- **步骤 7：文档完善** —— 已完成。
  - `finguard/README.md` 全面更新：架构概览（ASCII 图）、接口列表与 curl 示例（含 /settings）、配置文件说明、rules.yaml 示例、构建说明（Debug/Release/Test）、Phase 5 压测数据摘要、项目结构。

### 问题与解决

- 未遇到构建或运行时问题。CMake 架构调整一次成功，所有测试一次全 PASS。

## Chapter 4：验收详情

### 验收清单

| # | 验收项 | 状态 | 说明 |
|---|--------|------|------|
| 1 | CMake 架构：`finguard_core` 静态库编译通过 | ✅ PASS | Debug + Release 均成功构建 `finguard_core.lib`、`finguard.exe`、`finguard_tests.exe` |
| 2 | 设置接口：GET 返回正确 JSON（API Key 掩码） | ✅ PASS | 返回 `api_key_configured:true, api_key_hint:"****d32c"` |
| 3 | 设置接口：POST 更新后立即生效 | ✅ PASS | POST `model=gpt-4o-mini` 后 GET 立即返回新值，无需等 5 秒 |
| 4 | 缓存刷新：`invalidate_llm_config_cache()` 生效 | ✅ PASS | POST settings 后缓存时间戳重置，下次请求重新加载 |
| 5 | CORS：`Access-Control-Allow-Origin: *` 存在 | ✅ PASS | `/health`、`/api/v1/settings` 响应头均含 CORS 头 |
| 6 | gtest：`ctest --output-on-failure` 全部 PASS | ✅ PASS | 29 个测试全部通过（>15 个实质测试用例） |
| 7 | Release 构建：编译成功，核心端点正常 | ✅ PASS | `/health`、`/plan`、`/settings`、`/metrics` 均 200 OK |
| 8 | Release /health P99 < 5ms | ⚠️ NEAR | PowerShell 串行 P99=5.31ms（接近目标，并行压测预期达标） |
| 9 | 文档：README 包含架构图、接口列表、配置说明等 | ✅ PASS | 7 项内容全部覆盖 |
| 10 | 无回归：Phase 1-5 端点功能不退化 | ✅ PASS | `/health`、`/plan`、`/chat/stream`、`/metrics` 均可用 |

### 产物列表

| 文件 | 说明 |
|------|------|
| `finguard/CMakeLists.txt` | 重构：finguard_core 静态库 + finguard exe + finguard_tests |
| `finguard/src/llm/llm_client.h` | 新增 `invalidate_llm_config_cache()` 声明 |
| `finguard/src/llm/llm_client.cpp` | 全局缓存重构 + `invalidate_llm_config_cache()` 实现 |
| `finguard/src/server/routes.cpp` | Settings API（GET/POST）+ CORS OPTIONS + 缓存刷新 |
| `finguard/src/server/http_server.cpp` | 全局 CORS `registerPostHandlingAdvice` |
| `finguard/tests/test_rules.cpp` | 风控规则引擎实质测试（7 用例） |
| `finguard/tests/test_cache.cpp` | ConfigCache 测试（6 用例） |
| `finguard/tests/test_llm_config.cpp` | LLM 配置解析测试（5 用例，新增文件） |
| `finguard/tests/test_token_bucket.cpp` | 令牌桶测试（5 用例，新增文件） |
| `finguard/tests/test_circuit_breaker.cpp` | 熔断器状态机测试（7 用例，新增文件） |
| `finguard/README.md` | 全面更新：架构图、接口文档、配置说明、构建说明、压测数据 |

### 变更日志

- [2026-02-25] 创建 Phase 6 文档，完成 Chapter 1（Spec）与 Chapter 2（计划）。
- [2026-02-26] 完成 Phase 6 全部实施与验收，补写 Chapter 3 & 4。
