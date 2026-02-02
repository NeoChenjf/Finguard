
# 指定所需的最低 CMake 版本
cmake_minimum_required(VERSION 3.20)
# 定义项目名为 finguard，使用 C++ 语言
project(finguard LANGUAGES CXX)

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

# 定义可执行文件 finguard，包含以下源文件
add_executable(finguard
    src/main.cpp
    src/llm/llm_client.cpp
    src/risk/profile_store.cpp
    src/risk/rule_engine.cpp
    src/server/http_server.cpp
    src/server/routes.cpp
)

# 添加 src 目录为头文件搜索路径
target_include_directories(finguard PRIVATE src)

# 链接依赖库
target_link_libraries(finguard
    PRIVATE
        Drogon::Drogon           # Drogon 框架
        nlohmann_json::nlohmann_json # JSON 解析库
        spdlog::spdlog           # 日志库
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
    )
    target_link_libraries(finguard_tests PRIVATE GTest::gtest_main) # 链接 GTest
    include(GoogleTest)                        # 包含 GoogleTest CMake 模块
    gtest_discover_tests(finguard_tests)       # 自动发现并注册测试用例
endif()
