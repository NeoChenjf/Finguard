// 包含内部头文件
#include "llm/llm_client_internal.h"

// 引入 Drogon 框架和网络解析库
#include <drogon/drogon.h>
#include <trantor/net/Resolver.h>

// 以下为平台相关头文件，_WIN32 为 Windows，未定义时为 Linux/Unix
#ifdef _WIN32
#include <winsock2.h>      // Windows 下的 socket 头文件
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib") // 链接 ws2_32 库
#endif

#include <cctype>          // 字符处理
#include <cstdio>          // C 标准 IO
#include <cstdlib>         // C 标准库
#include <exception>       // 异常处理
#include <filesystem>      // 文件系统操作
#include <fstream>         // 文件流
#include <sstream>         // 字符串流

// 命名空间封装
namespace finguard::llm::internal {

// 获取环境变量并去除首尾空白
std::string get_env_trimmed(const char *name) {
    const char *raw = std::getenv(name); // 获取环境变量
    if (!raw) {
        return ""; // 没有则返回空字符串
    }
    return trim_copy(std::string(raw)); // 去除首尾空白
}

// 解析环境变量为布尔值，支持多种写法
bool parse_env_bool(const std::string &value, bool fallback) {
    if (value.empty()) {
        return fallback; // 空则返回默认值
    }
    std::string v = value;
    for (auto &c : v) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); // 转小写
    }
    // 支持多种 true 写法
    if (v == "1" || v == "true" || v == "yes" || v == "y" || v == "on") {
        return true;
    }
    // 支持多种 false 写法
    if (v == "0" || v == "false" || v == "no" || v == "n" || v == "off") {
        return false;
    }
    return fallback; // 其他情况返回默认值
}

// 解析环境变量为整数，异常时返回默认值
int parse_env_int(const std::string &value, int fallback) {
    if (value.empty()) {
        return fallback;
    }
    try {
        return std::stoi(value); // 转换为 int
    } catch (const std::exception &) {
        return fallback; // 转换失败返回默认值
    }
}

// 返回默认 LLM 配置
LlmConfig default_config() {
    LlmConfig cfg;
    cfg.api_base = "https://dashscope-us.aliyuncs.com/compatible-mode/v1"; // 默认 API 地址
    cfg.model = "qwen-plus"; // 默认模型
    cfg.use_curl_fallback = false; // 是否用 curl 兜底
    cfg.curl_path = "curl.exe"; // curl 路径
    cfg.http_proxy = ""; // 代理
    return cfg;
}

// 去除字符串首尾空白
std::string trim_copy(const std::string &value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

// 截断字符串到最大长度，超出部分加 ...
std::string truncate_copy(const std::string &value, std::size_t max_len) {
    if (value.size() <= max_len) {
        return value;
    }
    return value.substr(0, max_len) + "...";
}

// 根据模型名判断模型家族
std::string model_family(const std::string &model) {
    std::string out = model;
    for (auto &c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (out.find("qwen") != std::string::npos) {
        return "qwen";
    }
    if (out.find("deepseek") != std::string::npos) {
        return "deepseek";
    }
    return "default";
}

// 解析 API base，提取主机、端口、前缀等
HostInfo parse_base(const std::string &api_base) {
    HostInfo info;
    std::string url = api_base;
    // 判断协议
    if (url.rfind("https://", 0) == 0) {
        info.use_ssl = true;
        info.port = 443;
        url = url.substr(8);
    } else if (url.rfind("http://", 0) == 0) {
        info.use_ssl = false;
        info.port = 80;
        url = url.substr(7);
    }

    // 提取路径前缀
    const auto path_pos = url.find('/');
    if (path_pos != std::string::npos) {
        info.prefix = url.substr(path_pos);
        url = url.substr(0, path_pos);
        if (!info.prefix.empty() && info.prefix.back() == '/') {
            info.prefix.pop_back();
        }
    }

    // 提取端口
    const auto port_pos = url.find(':');
    if (port_pos != std::string::npos) {
        const auto port_str = url.substr(port_pos + 1);
        const auto parsed = std::stoi(port_str);
        if (parsed > 0 && parsed < 65536) {
            info.port = static_cast<uint16_t>(parsed);
        }
        url = url.substr(0, port_pos);
    }

    info.host = url;
    return info;
}

// 解析主机名为 IPv4 地址（仅 Windows 实现）
std::string resolve_host_ipv4(const std::string &hostname) {
#ifdef _WIN32
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaResult != 0) {
        LOG_WARN << "[P1 FIX] WSAStartup failed: " << wsaResult;
        return "";
    }

    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *result = nullptr;
    int ret = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);
    if (ret != 0 || result == nullptr) {
        LOG_WARN << "[P1 FIX] IPv4 resolution failed for " << hostname
                 << ", error: " << gai_strerrorA(ret);
        if (result) freeaddrinfo(result);
        WSACleanup();
        return "";
    }

    char ipstr[INET_ADDRSTRLEN] = {};
    struct sockaddr_in *addr = reinterpret_cast<struct sockaddr_in *>(result->ai_addr);
    inet_ntop(AF_INET, &(addr->sin_addr), ipstr, sizeof(ipstr));

    std::string ip_address(ipstr);
    LOG_INFO << "[P1 FIX] Resolved " << hostname << " -> " << ip_address << " (IPv4)";

    freeaddrinfo(result);
    return ip_address;
#else
    return ""; // 非 Windows 返回空
#endif
}

// 解析代理字符串，提取主机、端口、协议
ProxyInfo parse_proxy(const std::string &proxy_raw) {
    ProxyInfo info;
    std::string value = trim_copy(proxy_raw);
    if (value.empty()) {
        return info;
    }
    // 判断协议
    if (value.rfind("http://", 0) == 0) {
        value = value.substr(7);
        info.use_ssl = false;
    } else if (value.rfind("https://", 0) == 0) {
        value = value.substr(8);
        info.use_ssl = true;
    }
    // 去掉路径部分
    const auto slash_pos = value.find('/');
    if (slash_pos != std::string::npos) {
        value = value.substr(0, slash_pos);
    }
    // 提取端口
    const auto port_pos = value.find(':');
    if (port_pos != std::string::npos) {
        info.host = value.substr(0, port_pos);
        const auto port_str = value.substr(port_pos + 1);
        try {
            const auto parsed = std::stoi(port_str);
            if (parsed > 0 && parsed < 65536) {
                info.port = static_cast<uint16_t>(parsed);
            }
        } catch (const std::exception &) {
            info.port = 0;
        }
    } else {
        info.host = value;
    }
    if (!info.host.empty() && info.port != 0) {
        info.enabled = true;
    }
    return info;
}

// 拼接 base 和 path，避免重复斜杠
std::string join_url(const std::string &base, const std::string &path) {
    if (base.empty()) {
        return path;
    }
    if (base.back() == '/') {
        return base.substr(0, base.size() - 1) + path;
    }
    return base + path;
}

// 解析 LLM 返回的 JSON，提取 message 和 token 统计
bool parse_completion_payload(const std::string &body,
                              std::string &message,
                              LlmMetrics &metrics,
                              std::vector<std::string> &warnings) {
    try {
        const auto payload = nlohmann::json::parse(body); // 解析 JSON
        // 提取 message 内容
        if (payload.contains("choices") && payload["choices"].is_array() &&
            !payload["choices"].empty()) {
            auto &choice = payload["choices"][0];
            if (choice.contains("message") && choice["message"].contains("content")) {
                message = choice["message"]["content"].get<std::string>();
            }
        }
        // 提取 token 统计
        if (payload.contains("usage")) {
            auto &usage = payload["usage"];
            if (usage.contains("prompt_tokens")) {
                metrics.prompt_tokens = usage["prompt_tokens"].get<int>();
            }
            if (usage.contains("completion_tokens")) {
                metrics.completion_tokens = usage["completion_tokens"].get<int>();
            }
            if (usage.contains("total_tokens")) {
                metrics.total_tokens = usage["total_tokens"].get<int>();
            }
            metrics.has_usage = true;
        }
        return true;
    } catch (const std::exception &) {
        warnings.push_back("llm_parse_failed"); // 解析失败
        return false;
    }
}

// 用 curl 命令行兜底发起 LLM 请求，适用于主流程失败时
bool curl_fallback_request(const LlmConfig &cfg,
                           const nlohmann::json &body,
                           std::string &message,
                           LlmMetrics &metrics,
                           std::vector<std::string> &warnings) {
    // 拼接请求 URL
    const auto url = join_url(cfg.api_base, "/chat/completions");
    // 临时文件保存请求体
    const auto temp_path = std::filesystem::temp_directory_path() / "finguard_qwen_payload.json";
    {
        std::ofstream out(temp_path);
        if (!out) {
            warnings.push_back("curl_tempfile_failed");
            return false;
        }
        out << body.dump(); // 写入 JSON
    }

    // 构造 curl 命令
    std::ostringstream cmd;
    cmd << cfg.curl_path << " -s"
        << " -X POST " << url
        << " -H \"Authorization: Bearer " << cfg.api_key << "\""
        << " -H \"Content-Type: application/json\""
        << " --data-binary @" << temp_path.string();
    if (!cfg.http_proxy.empty()) {
        cmd << " --proxy \"" << cfg.http_proxy << "\"";
    }

    // 以下为平台相关的进程调用，Linux 下用 popen/pclose，Windows 下用 _popen/_pclose
#ifdef _WIN32
    FILE *pipe = _popen(cmd.str().c_str(), "r"); // Windows 下用 _popen
#else
    FILE *pipe = popen(cmd.str().c_str(), "r");   // Linux 下用 popen（Linux 相关）
#endif
    if (!pipe) {
        warnings.push_back("curl_popen_failed");
        return false;
    }

    // 读取 curl 输出
    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
#ifdef _WIN32
    const int exit_code = _pclose(pipe);
#else
    const int exit_code = pclose(pipe); // Linux 下用 pclose（Linux 相关）
#endif
    if (exit_code != 0) {
        warnings.push_back("curl_exit_" + std::to_string(exit_code));
    }
    if (output.empty()) {
        warnings.push_back("curl_empty_output");
        return false;
    }
    // 解析返回内容
    return parse_completion_payload(output, message, metrics, warnings);
}

// 命名空间结束
} // namespace finguard::llm::internal
