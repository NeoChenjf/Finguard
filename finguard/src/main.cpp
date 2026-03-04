// 程序入口：只负责启动 HTTP 服务
#include "server/http_server.h"

#include <filesystem>
#include <iostream>

// 自动探测 config/ 目录，确保 current_path 下有 config/
// 适配两种启动场景：
// 1. Tauri sidecar: CWD = %LOCALAPPDATA%/FinGuard/  (config/ 在此)
// 2. 开发调试:      CWD = build/Release/             (config/ 在 build/)
static void ensure_config_dir() {
    namespace fs = std::filesystem;
    if (fs::exists(fs::current_path() / "config")) return; // 已找到

    // 检查上一级目录
    const auto parent = fs::current_path().parent_path();
    if (fs::exists(parent / "config")) {
        fs::current_path(parent);
        std::cout << "[main] CWD adjusted to " << fs::current_path().string() << std::endl;
        return;
    }

    // 检查 exe 所在目录
    // Windows: argv[0] 不可靠，但 filesystem 可用
    std::error_code ec;
    auto exe_dir = fs::canonical("/proc/self/exe", ec); // Linux
    if (ec) {
        // Windows fallback: 用 _pgmptr
#ifdef _WIN32
        extern char *_pgmptr;
        if (_pgmptr) {
            auto exe_path = fs::path(_pgmptr).parent_path();
            if (fs::exists(exe_path / "config")) {
                fs::current_path(exe_path);
                std::cout << "[main] CWD adjusted to " << fs::current_path().string() << std::endl;
                return;
            }
            // 再查 exe 上一级
            if (fs::exists(exe_path.parent_path() / "config")) {
                fs::current_path(exe_path.parent_path());
                std::cout << "[main] CWD adjusted to " << fs::current_path().string() << std::endl;
                return;
            }
        }
#endif
    }

    std::cerr << "[main] WARNING: config/ directory NOT found under "
              << fs::current_path().string() << std::endl;
}

int main() {
    ensure_config_dir();

    // 启动 Drogon HTTP 服务
    finguard::start_http_server();
    return 0;
}
