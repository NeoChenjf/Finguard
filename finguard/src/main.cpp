// 程序入口：只负责启动 HTTP 服务
#include "server/http_server.h"

#include <filesystem>
#include <iostream>

// 自动探测 config/ 目录，确保 current_path 下有 config/
// 适配两种启动场景：
// 1. Tauri sidecar: CWD = %LOCALAPPDATA%/FinGuard/  (config/ 在此)
// 2. 开发调试:      CWD = build/Release/             (config/ 在 build/)
// 自动探测 config/ 目录，确保当前工作目录下有 config/ 文件夹
static void ensure_config_dir() {
    namespace fs = std::filesystem; // 引入 std::filesystem 命名空间，方便后续文件操作
    // 检查当前目录下是否存在 config 文件夹
    if (fs::exists(fs::current_path() / "config")) return; // 如果存在，直接返回

    // 检查上一级目录下是否存在 config 文件夹
    const auto parent = fs::current_path().parent_path(); // 获取当前目录的父目录
    if (fs::exists(parent / "config")) { // 如果父目录下有 config
        fs::current_path(parent); // 切换当前工作目录到父目录
        std::cout << "[main] CWD adjusted to " << fs::current_path().string() << std::endl; // 输出调整信息
        return;
    }

    // 检查可执行文件所在目录（Linux 下）
    // Windows 下 argv[0] 不可靠，但 filesystem 可用
    std::error_code ec; // 用于接收错误码
    auto exe_dir = fs::canonical("/proc/self/exe", ec); // 通过 /proc/self/exe 获取可执行文件路径（Linux 专有，/proc/self/exe 是 Linux 下的特殊路径，属于 Linux 命令/机制）
    if (ec) { // 如果获取失败，说明不是 Linux 或路径无效
        // Windows fallback: 用 _pgmptr 获取可执行文件路径
#ifdef _WIN32
        extern char *_pgmptr; // _pgmptr 是 Windows 下保存可执行文件路径的全局变量
        if (_pgmptr) {
            auto exe_path = fs::path(_pgmptr).parent_path(); // 获取可执行文件所在目录
            if (fs::exists(exe_path / "config")) { // 如果该目录下有 config
                fs::current_path(exe_path); // 切换到该目录
                std::cout << "[main] CWD adjusted to " << fs::current_path().string() << std::endl; // 输出调整信息
                return;
            }
            // 再查可执行文件的上一级目录
            if (fs::exists(exe_path.parent_path() / "config")) {
                fs::current_path(exe_path.parent_path()); // 切换到上一级目录
                std::cout << "[main] CWD adjusted to " << fs::current_path().string() << std::endl; // 输出调整信息
                return;
            }
        }
#endif
    }

    // 如果上述路径都没有找到 config，输出警告
    std::cerr << "[main] WARNING: config/ directory NOT found under "
              << fs::current_path().string() << std::endl;
}

// 主函数，程序入口
int main() {
    ensure_config_dir(); // 启动前确保 config 目录存在于当前工作目录

    // 启动 Drogon HTTP 服务（Web 服务框架）
    finguard::start_http_server();
    return 0; // 正常退出
}
