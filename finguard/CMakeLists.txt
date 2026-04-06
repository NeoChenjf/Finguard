
# 指定所需的最低 CMake 版本
cmake_minimum_required(VERSION 3.20)
# 定义项目名为 finguard，使用 C++ 语言
project(finguard LANGUAGES C CXX)

# 设置 C++ 标准为 C++17
set(CMAKE_CXX_STANDARD 17)
# 强制要求使用指定的 C++ 标准
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 导出编译命令（方便 clangd、ccls 等工具使用）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找依赖库（通过 vcpkg 或系统包管理器安装）
find_package(Drogon CONFIG REQUIRED)        # 查找 Drogon 框架
find_package(nlohmann_json CONFIG REQUIRED) # 查找 nlohmann_json 库
find_package(spdlog CONFIG REQUIRED)        # 查找 spdlog 日志库

# ========== 核心静态库（不含 main.cpp）==========
add_library(finguard_core STATIC
    third_party/sqlite/sqlite3.c
    src/data/fundamentals_db.cpp
    src/llm/llm_client.cpp
    src/llm/llm_client_common.cpp
    src/llm/llm_client_config.cpp
    src/llm/llm_client_stream.cpp
    src/risk/profile_store.cpp
    src/risk/rule_engine.cpp
    src/server/http_server.cpp
    src/server/routes.cpp
    src/server/route_support.cpp
    src/server/health_routes.cpp
    src/server/plan_routes.cpp
    src/server/profile_routes.cpp
    src/server/chat_routes.cpp
    src/server/system_routes.cpp
    src/server/valuecell_routes.cpp
    src/server/allocation_handler.cpp
    src/util/simple_yaml.cpp
    src/util/reliability_config.cpp
    src/util/token_bucket.cpp
    src/util/circuit_breaker.cpp
    src/util/metrics_registry.cpp
    src/util/concurrency_limiter.cpp
    # Phase 10: Value Cell 量化价值分析
    src/valuation/valuation_config.cpp
    src/valuation/proxy_config.cpp
    src/valuation/fundamentals_db_client.cpp
    src/valuation/yahoo_finance_client.cpp
    src/valuation/yahoo_finance_client_common.cpp
    src/valuation/yahoo_finance_quote_sources.cpp
    src/valuation/yahoo_finance_balance_sheet_sources.cpp
    src/valuation/yahoo_finance_net_income_sources.cpp
    src/valuation/analysis_workflow.cpp
    src/valuation/safety_margin.cpp
    src/valuation/valuation_handler.cpp
    # Asset Allocation Multi-Rules: 多规则资产配置框架
    src/core/allocation_rule.cpp
    src/core/rule_factory.cpp
    src/core/rules/shouzhe_rule.cpp
    src/core/rules/fixed_rules.cpp
)
target_include_directories(finguard_core PUBLIC src third_party/sqlite)
target_link_libraries(finguard_core
    PUBLIC
        Drogon::Drogon
        nlohmann_json::nlohmann_json
        spdlog::spdlog
)

# ========== 主程序 ==========
add_executable(finguard src/main.cpp)
target_link_libraries(finguard PRIVATE finguard_core)

# ========== Post-build: 自动复制配置文件到 build 目录 ==========
add_custom_command(TARGET finguard POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/llm.json"
        "$<TARGET_FILE_DIR:finguard>/../config/llm.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/rules.yaml"
        "$<TARGET_FILE_DIR:finguard>/../config/rules.yaml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/llm.json.example"
        "$<TARGET_FILE_DIR:finguard>/../config/llm.json.example"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/rate_limit.yaml"
        "$<TARGET_FILE_DIR:finguard>/../config/rate_limit.yaml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/timeout.yaml"
        "$<TARGET_FILE_DIR:finguard>/../config/timeout.yaml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/circuit_breaker.yaml"
        "$<TARGET_FILE_DIR:finguard>/../config/circuit_breaker.yaml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/observability.yaml"
        "$<TARGET_FILE_DIR:finguard>/../config/observability.yaml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/concurrency.yaml"
        "$<TARGET_FILE_DIR:finguard>/../config/concurrency.yaml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/valuation.json"
        "$<TARGET_FILE_DIR:finguard>/../config/valuation.json"
    COMMENT "Copying config files to build directory..."
)

# 定义选项 BUILD_TESTS，默认关闭
option(BUILD_TESTS "Build unit tests" OFF)
# 如果启用单元测试
if(BUILD_TESTS)
    enable_testing()                           # 启用 CTest 测试功能
    find_package(GTest CONFIG REQUIRED)        # 查找 GoogleTest
    add_executable(finguard_tests              # 定义测试可执行文件
        tests/test_rules.cpp
        tests/test_cache.cpp
        tests/test_llm_config.cpp
        tests/test_llm_helpers.cpp
        tests/test_token_bucket.cpp
        tests/test_circuit_breaker.cpp
        tests/test_valuation_metrics.cpp
        tests/test_route_support.cpp
        tests/test_fundamentals_db.cpp
    )
    target_link_libraries(finguard_tests
        PRIVATE
            finguard_core          # 链接核心静态库（被测代码）
            GTest::gtest_main      # GTest 主函数
    )
    include(GoogleTest)                        # 包含 GoogleTest CMake 模块
    gtest_discover_tests(finguard_tests)       # 自动发现并注册测试用例
endif()

