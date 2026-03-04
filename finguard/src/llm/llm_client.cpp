// 引入 LLM 客户端声明
#include "llm/llm_client.h"

// 引入 drogon HTTP 框架
#include <drogon/drogon.h>
// 引入 JSON 解析库
#include <nlohmann/json.hpp>
// 引入 trantor 解析器
#include <trantor/net/Resolver.h>

// Windows 平台编译分支
#ifdef _WIN32
// 引入 Winsock 基础头
#include <winsock2.h>
// 引入地址解析头
#include <ws2tcpip.h>
// 链接 Winsock 库
#pragma comment(lib, "ws2_32.lib")
// 结束 Windows 分支
#endif

// 引入文件系统
#include <filesystem>
// 引入字符处理
#include <cctype>
// 引入智能指针
#include <memory>
// 引入 tuple
#include <tuple>
// 引入文件读写
#include <fstream>
// 引入字符串流
#include <sstream>
// 引入 C 标准 IO
#include <cstdio>
#include <mutex>
#include <thread>
#include <cstdlib>

#include "util/circuit_breaker.h"
#include "util/metrics_registry.h"
#include "util/reliability_config.h"
#include "util/token_bucket.h"

// 命名空间开始
namespace finguard::llm {

// 匿名命名空间开始
namespace {

std::string trim_copy(const std::string &value);

std::string get_env_trimmed(const char *name) {
    const char *raw = std::getenv(name);
    if (!raw) {
        return "";
    }
    return trim_copy(std::string(raw));
}

bool parse_env_bool(const std::string &value, bool fallback) {
    if (value.empty()) {
        return fallback;
    }
    std::string v = value;
    for (auto &c : v) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (v == "1" || v == "true" || v == "yes" || v == "y" || v == "on") {
        return true;
    }
    if (v == "0" || v == "false" || v == "no" || v == "n" || v == "off") {
        return false;
    }
    return fallback;
}

int parse_env_int(const std::string &value, int fallback) {
    if (value.empty()) {
        return fallback;
    }
    try {
        return std::stoi(value);
    } catch (const std::exception &) {
        return fallback;
    }
}

// 默认配置（当 llm.json 缺失时使用）
LlmConfig default_config() {
    // 构造配置对象
    LlmConfig cfg;
    // 设置默认 API 基地址
    cfg.api_base = "https://dashscope-us.aliyuncs.com/compatible-mode/v1";
    // 设置默认模型
    cfg.model = "qwen-plus";
    // 默认不启用 curl 降级
    cfg.use_curl_fallback = false;
    // 默认 curl 可执行文件
    cfg.curl_path = "curl.exe";
    // 默认无代理
    cfg.http_proxy = "";
    // 返回默认配置
    return cfg;
}

// 去掉首尾空白
std::string trim_copy(const std::string &value) {
    // 找到首个非空白位置
    const auto start = value.find_first_not_of(" \t\r\n");
    // 若全为空白，返回空串
    if (start == std::string::npos) {
        return "";
    }
    // 找到末尾非空白位置
    const auto end = value.find_last_not_of(" \t\r\n");
    // 返回截取后的字符串
    return value.substr(start, end - start + 1);
}

// 截断长字符串，避免把过长内容塞进 warning
std::string truncate_copy(const std::string &value, std::size_t max_len) {
    // 若长度未超限，直接返回
    if (value.size() <= max_len) {
        return value;
    }
    // 否则截断并追加省略号
    return value.substr(0, max_len) + "...";
}

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

// 将 api_base 拆分为 host / path 前缀 / 端口 / 是否 SSL
struct HostInfo {
    // 主机名
    std::string host;
    // 路径前缀
    std::string prefix;
    // 端口号
    uint16_t port = 80;
    // 是否使用 SSL
    bool use_ssl = false;
};

// 解析 api_base
HostInfo parse_base(const std::string &api_base) {
    // 结果对象
    HostInfo info;
    // 复制输入 URL
    std::string url = api_base;
    // 处理 https 前缀
    if (url.rfind("https://", 0) == 0) {
        // 标记 SSL
        info.use_ssl = true;
        // 默认端口 443
        info.port = 443;
        // 去掉协议前缀
        url = url.substr(8);
    } else if (url.rfind("http://", 0) == 0) {
        // 标记非 SSL
        info.use_ssl = false;
        // 默认端口 80
        info.port = 80;
        // 去掉协议前缀
        url = url.substr(7);
    }

    // 查找路径分隔符
    const auto path_pos = url.find('/');
    // 若存在路径
    if (path_pos != std::string::npos) {
        // 保存路径前缀
        info.prefix = url.substr(path_pos);
        // 主机部分截断
        url = url.substr(0, path_pos);
        // 若前缀以 / 结尾则去除
        if (!info.prefix.empty() && info.prefix.back() == '/') {
            info.prefix.pop_back();
        }
    }

    // 查找端口分隔符
    const auto port_pos = url.find(':');
    // 若存在端口
    if (port_pos != std::string::npos) {
        // 提取端口字符串
        const auto port_str = url.substr(port_pos + 1);
        // 转为整数
        const auto parsed = std::stoi(port_str);
        // 校验端口范围
        if (parsed > 0 && parsed < 65536) {
            // 写回端口
            info.port = static_cast<uint16_t>(parsed);
        }
        // 去掉端口部分
        url = url.substr(0, port_pos);
    }

    // 设置主机名
    info.host = url;
    // 返回解析结果
    return info;
}

// [P1 FIX] 强制使用 IPv4 解析主机名
// 根因: c-ares 优先返回 IPv6 地址，Windows 的 IPv6 路由可能失败
// 解决方案: 使用 getaddrinfo 显式指定 AF_INET (IPv4)
std::string resolve_host_ipv4(const std::string &hostname) {
// Windows 平台专用
#ifdef _WIN32
    // 初始化 Winsock（如果尚未初始化）
    WSADATA wsaData;
    // 启动 Winsock
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    // 检查启动结果
    if (wsaResult != 0) {
        // 记录失败日志
        LOG_WARN << "[P1 FIX] WSAStartup failed: " << wsaResult;
        // 返回空表示失败
        return "";  // 返回空表示失败，调用者将使用原始主机名
    }

    // 初始化 addrinfo 提示
    struct addrinfo hints = {};
    // 强制 IPv4
    hints.ai_family = AF_INET;      // 强制 IPv4
    // 指定 TCP 流
    hints.ai_socktype = SOCK_STREAM;
    // 指定 TCP 协议
    hints.ai_protocol = IPPROTO_TCP;

    // 解析结果指针
    struct addrinfo *result = nullptr;
    // 调用系统解析
    int ret = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);
    
    // 若解析失败或无结果
    if (ret != 0 || result == nullptr) {
        // 记录失败信息
        LOG_WARN << "[P1 FIX] IPv4 resolution failed for " << hostname 
                 << ", error: " << gai_strerrorA(ret);
        // 释放结果
        if (result) freeaddrinfo(result);
        // 清理 Winsock
        WSACleanup();
        // 返回空
        return "";
    }

    // 提取第一个 IPv4 地址
    char ipstr[INET_ADDRSTRLEN] = {};
    // 转换为 sockaddr_in
    struct sockaddr_in *addr = reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
    // 转换为字符串
    inet_ntop(AF_INET, &(addr->sin_addr), ipstr, sizeof(ipstr));
    
    // 构造 IP 字符串
    std::string ip_address(ipstr);
    // 打印解析结果
    LOG_INFO << "[P1 FIX] Resolved " << hostname << " -> " << ip_address << " (IPv4)";
    
    // 释放解析结果
    freeaddrinfo(result);
    // 注意: 不调用 WSACleanup，因为 Drogon 可能还在使用 Winsock
    
    // 返回 IPv4 地址
    return ip_address;
// 非 Windows 平台
#else
    // 非 Windows 平台：返回空，使用默认解析
    return "";
// 结束平台分支
#endif
}

// 代理配置结构体
struct ProxyInfo {
    // 是否启用
    bool enabled = false;
    // 代理主机
    std::string host;
    // 代理端口
    uint16_t port = 0;
    // 是否使用 SSL
    bool use_ssl = false;
};

// 解析代理配置字符串
ProxyInfo parse_proxy(const std::string &proxy_raw) {
    // 初始化结构体
    ProxyInfo info;
    // 去掉首尾空白
    std::string value = trim_copy(proxy_raw);
    // 空值直接返回
    if (value.empty()) {
        return info;
    }
    // 处理 http 前缀
    if (value.rfind("http://", 0) == 0) {
        // 去掉前缀
        value = value.substr(7);
        // 非 SSL
        info.use_ssl = false;
    } else if (value.rfind("https://", 0) == 0) {
        // 去掉前缀
        value = value.substr(8);
        // SSL
        info.use_ssl = true;
    }
    // 查找路径分隔符
    const auto slash_pos = value.find('/');
    // 若包含路径则去掉
    if (slash_pos != std::string::npos) {
        value = value.substr(0, slash_pos);
    }
    // 查找端口分隔符
    const auto port_pos = value.find(':');
    // 若包含端口
    if (port_pos != std::string::npos) {
        // 提取主机
        info.host = value.substr(0, port_pos);
        // 提取端口字符串
        const auto port_str = value.substr(port_pos + 1);
        try {
            // 解析端口
            const auto parsed = std::stoi(port_str);
            // 校验范围
            if (parsed > 0 && parsed < 65536) {
                // 写回端口
                info.port = static_cast<uint16_t>(parsed);
            }
        } catch (const std::exception &) {
            // 异常时置零
            info.port = 0;
        }
    } else {
        // 仅主机名
        info.host = value;
    }
    // 若主机和端口均有效，则启用
    if (!info.host.empty() && info.port != 0) {
        info.enabled = true;
    }
    // 返回解析结果
    return info;
}

// 组合 base 与 path
std::string join_url(const std::string &base, const std::string &path) {
    // base 为空直接返回 path
    if (base.empty()) {
        return path;
    }
    // 若 base 以 / 结尾，去掉再拼接
    if (base.back() == '/') {
        return base.substr(0, base.size() - 1) + path;
    }
    // 直接拼接
    return base + path;
}

// 解析完成请求的 JSON 响应
bool parse_completion_payload(const std::string &body,
                              std::string &message,
                              LlmMetrics &metrics,
                              std::vector<std::string> &warnings) {
    try {
        // 解析 JSON 字符串
        const auto payload = nlohmann::json::parse(body);
        // 解析 choices
        if (payload.contains("choices") && payload["choices"].is_array() && !payload["choices"].empty()) {
            // 取第一个 choice
            auto &choice = payload["choices"][0];
            // 读取 message.content
            if (choice.contains("message") && choice["message"].contains("content")) {
                message = choice["message"]["content"].get<std::string>();
            }
        }
        // 解析 usage
        if (payload.contains("usage")) {
            // 引用 usage 对象
            auto &usage = payload["usage"];
            // 读取 prompt_tokens
            if (usage.contains("prompt_tokens")) {
                metrics.prompt_tokens = usage["prompt_tokens"].get<int>();
            }
            // 读取 completion_tokens
            if (usage.contains("completion_tokens")) {
                metrics.completion_tokens = usage["completion_tokens"].get<int>();
            }
            // 读取 total_tokens
            if (usage.contains("total_tokens")) {
                metrics.total_tokens = usage["total_tokens"].get<int>();
            }
            // 标记包含 usage
            metrics.has_usage = true;
        }
        // 解析成功
        return true;
    } catch (const std::exception &) {
        // 解析失败记录警告
        warnings.push_back("llm_parse_failed");
        // 返回失败
        return false;
    }
}

// 使用 curl 进行降级请求
bool curl_fallback_request(const LlmConfig &cfg,
                           const nlohmann::json &body,
                           std::string &message,
                           LlmMetrics &metrics,
                           std::vector<std::string> &warnings) {
    // 组装请求 URL（在 api_base 后追加 /chat/completions）
    const auto url = join_url(cfg.api_base, "/chat/completions");
    // 生成临时文件路径，用于写入请求体
    const auto temp_path = std::filesystem::temp_directory_path() / "finguard_qwen_payload.json";
    // 将请求体写入临时文件
    {
        // 打开临时文件用于写入
        std::ofstream out(temp_path);
        // 若无法写入，记录警告并返回失败
        if (!out) {
            warnings.push_back("curl_tempfile_failed");
            return false;
        }
        // 写入 JSON 请求体
        out << body.dump();
    }

    // 拼接 curl 命令
    std::ostringstream cmd;
    // curl 可执行文件路径
    cmd << cfg.curl_path
        // 静默模式
        << " -s"
        // HTTP 方法
        << " -X POST " << url
        // 认证头
        << " -H \"Authorization: Bearer " << cfg.api_key << "\""
        // JSON 内容类型
        << " -H \"Content-Type: application/json\""
        // 请求体从文件读取
        << " --data-binary @" << temp_path.string();
    // 如配置了代理则追加 proxy 参数
    if (!cfg.http_proxy.empty()) {
        cmd << " --proxy \"" << cfg.http_proxy << "\"";
    }

// Windows 平台
#ifdef _WIN32
    // Windows 使用 _popen 执行命令并读取输出
    FILE *pipe = _popen(cmd.str().c_str(), "r");
// 非 Windows 平台
#else
    // 非 Windows 使用 popen
    FILE *pipe = popen(cmd.str().c_str(), "r");
// 结束平台分支
#endif
    // 若进程创建失败，记录警告并返回
    if (!pipe) {
        warnings.push_back("curl_popen_failed");
        return false;
    }
    // 累积读取 curl 输出
    std::string output;
    // 读取缓冲区
    char buffer[4096];
    // 循环读取进程输出
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        // 拼接到输出字符串
        output += buffer;
    }
// Windows 平台
#ifdef _WIN32
    // 关闭管道并获取退出码（Windows）
    const int exit_code = _pclose(pipe);
// 非 Windows 平台
#else
    // 关闭管道并获取退出码（非 Windows）
    const int exit_code = pclose(pipe);
// 结束平台分支
#endif
    // 若退出码非 0，记录警告
    if (exit_code != 0) {
        warnings.push_back("curl_exit_" + std::to_string(exit_code));
    }
    // 如果输出为空，记录警告并返回失败
    if (output.empty()) {
        warnings.push_back("curl_empty_output");
        return false;
    }
    // 解析输出为模型响应
    return parse_completion_payload(output, message, metrics, warnings);
}

// 结束匿名命名空间
}

// ── P6: 全局 LLM 配置缓存（供 invalidate 使用）──
static std::mutex g_llm_cfg_mtx;
static LlmConfig g_llm_cfg;
static std::chrono::steady_clock::time_point g_llm_cfg_loaded_at{};

void invalidate_llm_config_cache() {
    std::lock_guard<std::mutex> lock(g_llm_cfg_mtx);
    g_llm_cfg_loaded_at = std::chrono::steady_clock::time_point{};
}

// 读取配置文件
LlmConfig LlmClient::load_config() const {
    // 先使用默认配置
    LlmConfig cfg = default_config();
    // 配置文件路径
    const std::filesystem::path path = std::filesystem::current_path() / "config" / "llm.json";

    // 读取 JSON 配置（若文件缺失/不可读/解析失败，则保留默认值继续走 env 覆盖）
    if (std::filesystem::exists(path)) {
        std::ifstream in(path);
        if (in) {
            nlohmann::json doc;
            try {
                in >> doc;
                if (doc.contains("api_base") && doc["api_base"].is_string()) {
                    cfg.api_base = trim_copy(doc["api_base"].get<std::string>());
                }
                if (doc.contains("api_key") && doc["api_key"].is_string()) {
                    cfg.api_key = trim_copy(doc["api_key"].get<std::string>());
                }
                if (doc.contains("model") && doc["model"].is_string()) {
                    cfg.model = trim_copy(doc["model"].get<std::string>());
                }
                if (doc.contains("temperature") && doc["temperature"].is_number()) {
                    cfg.temperature = doc["temperature"].get<double>();
                }
                if (doc.contains("timeout_ms") && doc["timeout_ms"].is_number_integer()) {
                    cfg.timeout_ms = doc["timeout_ms"].get<int>();
                }
                if (doc.contains("use_curl_fallback") && doc["use_curl_fallback"].is_boolean()) {
                    cfg.use_curl_fallback = doc["use_curl_fallback"].get<bool>();
                }
                if (doc.contains("curl_path") && doc["curl_path"].is_string()) {
                    cfg.curl_path = trim_copy(doc["curl_path"].get<std::string>());
                }
                if (doc.contains("http_proxy") && doc["http_proxy"].is_string()) {
                    cfg.http_proxy = trim_copy(doc["http_proxy"].get<std::string>());
                }
            } catch (const std::exception &) {
                // ignore parse errors; keep defaults
            }
        }
    }

    // 环境变量覆盖（用于本地验收/调试，避免直接修改 llm.json）
    // 注意：不要在日志中打印真实 key
    const auto env_api_base = get_env_trimmed("FINGUARD_LLM_API_BASE");
    if (!env_api_base.empty()) {
        cfg.api_base = env_api_base;
    }
    const auto env_api_key = get_env_trimmed("FINGUARD_LLM_API_KEY");
    if (!env_api_key.empty()) {
        cfg.api_key = env_api_key;
    }
    const auto env_model = get_env_trimmed("FINGUARD_LLM_MODEL");
    if (!env_model.empty()) {
        cfg.model = env_model;
    }
    const auto env_timeout = get_env_trimmed("FINGUARD_LLM_TIMEOUT_MS");
    if (!env_timeout.empty()) {
        cfg.timeout_ms = parse_env_int(env_timeout, cfg.timeout_ms);
    }
    const auto env_proxy = get_env_trimmed("FINGUARD_LLM_HTTP_PROXY");
    if (!env_proxy.empty()) {
        cfg.http_proxy = env_proxy;
    }
    const auto env_use_curl = get_env_trimmed("FINGUARD_LLM_USE_CURL_FALLBACK");
    if (!env_use_curl.empty()) {
        cfg.use_curl_fallback = parse_env_bool(env_use_curl, cfg.use_curl_fallback);
    }

    // 返回最终配置
    return cfg;
}

// 发起请求并返回流式结果
StreamResult LlmClient::stream_chat(const std::string &prompt) const {
    // 初始化结果结构
    StreamResult result;
    // 读取配置 (P5: use cached LLM config, P6: use global cache for invalidation support)
    LlmConfig cfg;
    {
        std::lock_guard<std::mutex> lock(g_llm_cfg_mtx);
        const auto now = std::chrono::steady_clock::now();
        if (now - g_llm_cfg_loaded_at > std::chrono::seconds(5)) {
            g_llm_cfg = load_config();
            g_llm_cfg_loaded_at = now;
        }
        cfg = g_llm_cfg;
    }
    const auto family = model_family(cfg.model);

    // P5 perf: use cached config loaders (5s TTL) instead of per-request file I/O
    const auto rate_cfg = util::cached_rate_limit_config();
    const auto timeout_cfg = util::cached_timeout_config();
    const auto cb_cfg = util::cached_circuit_breaker_config();

    static util::TokenBucket model_bucket;
    static std::unique_ptr<util::CircuitBreaker> circuit_breaker;
    static util::CircuitBreakerConfig last_cb_cfg;
    if (!circuit_breaker || last_cb_cfg.error_rate_threshold != cb_cfg.error_rate_threshold
        || last_cb_cfg.window_seconds != cb_cfg.window_seconds
        || last_cb_cfg.half_open_max_trials != cb_cfg.half_open_max_trials
        || last_cb_cfg.min_samples != cb_cfg.min_samples) {
        circuit_breaker = std::make_unique<util::CircuitBreaker>(cb_cfg);
        last_cb_cfg = cb_cfg;
    }

    const auto model_it = rate_cfg.model_limits.find(family);
    const auto model_limit = (model_it != rate_cfg.model_limits.end()) ? model_it->second : util::RateLimitModelConfig{};
    if (!model_bucket.allow("model:" + family, model_limit.rate_rps, model_limit.capacity)) {
        util::global_metrics().record_rate_limit_reject();
        result.degraded = true;
        result.error = "model_rate_limited";
        result.warnings.push_back("model_rate_limited");
        result.full_text = "FinGuard fallback response due to model rate limiting.";
        return result;
    }

    if (!circuit_breaker->allow("model:" + family)) {
        util::global_metrics().record_circuit_breaker_trip();
        result.degraded = true;
        result.error = "circuit_breaker_open";
        result.warnings.push_back("circuit_breaker_open");
        result.full_text = "FinGuard fallback response due to circuit breaker.";
        return result;
    }

    // 没有 API Key 时直接走降级逻辑
    if (cfg.api_key.empty()) {
        result.degraded = true;
        result.warnings.push_back("missing_api_key");
    }

    // 输出消息内容
    std::string message;
    // 若缺少 API Key
    if (cfg.api_key.empty()) {
        // mock：用 prompt 拼一段简单回复
        if (prompt.empty()) {
            message = "FinGuard mock stream response. Provide prompt in JSON {\"prompt\":\"...\"}.";
        } else {
            message = "FinGuard mock stream response for: " + prompt;
        }
    } else {
        int timeout_ms = timeout_cfg.external_call_timeout_ms;
        int max_retries = timeout_cfg.external_call_max_retries;
        const auto route_it = timeout_cfg.route_overrides.find("chat_stream");
        if (route_it != timeout_cfg.route_overrides.end()) {
            timeout_ms = route_it->second.timeout_ms;
            max_retries = route_it->second.max_retries;
        }
        const auto model_timeout_it = timeout_cfg.model_overrides.find(family);
        if (model_timeout_it != timeout_cfg.model_overrides.end()) {
            timeout_ms = model_timeout_it->second.timeout_ms;
            max_retries = model_timeout_it->second.max_retries;
        }
        cfg.timeout_ms = timeout_ms;

        // 构造请求 JSON
        nlohmann::json body;
        // 设置模型
        body["model"] = cfg.model;
        // 设置温度
        body["temperature"] = cfg.temperature;
        // 禁用真正流式
        body["stream"] = false;
        // 强制模板要求的 System Prompt
        const std::string system_prompt = 
            "You are FinGuard AI investment analysis assistant. "
            "When analyzing a company, you MUST follow this exact template structure:\n\n"
            "【公司信息】\nCompany name / Stock code / Industry / Main products\n\n"
            "【财务状况（定量指标，含好价格判断）】\n"
            "Include: 5-year CAGR, ROE, Debt ratio, PEG, Cash flow trend. "
            "ALWAYS use explicit numbers with labels (e.g., 'ROE 18%', 'PEG 2.3', 'Debt ratio 45%', '5-year CAGR 12%')\n\n"
            "【商业模式与护城河】\nBusiness model / Competitive advantage / Moat strength\n\n"
            "【管理层结构与风格】\nManagement stability / Governance / Integrity signals\n\n"
            "【优点与风险点】\nList 3 main advantages / List 3 main risks\n\n"
            "【预期收益（模型估算，必须注明不确定性）】\n"
            "Formula: Expected = (ROE * (1 + CAGR_5y) + 1) * (PE_long_term / PE_current) - 1\n"
            "MUST include specific numerical values (e.g., '(0.18 * 1.12 + 1) * (18 / 22) - 1 = 8.5%') and uncertainty range.\n\n"
            "【结论（分析性总结）】\n1-3 key takeaways. NO allocation ratios, NO buy/sell signals, NO profit promises.\n\n"
            "【合规与数据声明】\n"
            "This analysis does NOT constitute investment advice and does NOT include allocation ratios or buy/sell signals. "
            "Data from public sources may be outdated or inaccurate.\n\n"
            "STRICT CONSTRAINTS: "
            "1. NEVER recommend allocation percentages, positions, or trading actions. "
            "2. NEVER give buy/sell signals or profit guarantees. "
            "3. ALL financial metrics MUST show explicit numbers (e.g., 'ROE 18%' not just '18%'). "
            "4. ALWAYS include both metric label and value. "
            "5. Include uncertainty disclaimers in Expected Return section.";
        // 组装消息数组
        body["messages"] = nlohmann::json::array({
            {{"role", "system"}, {"content", system_prompt}},
            {{"role", "user"}, {"content", prompt.empty() ? "请输出一段示例流式回复" : prompt}}
        });

        // 组装 HttpClient：必须明确 host 与协议
        const auto info = parse_base(cfg.api_base);
        // 解析代理
        const auto proxy = parse_proxy(cfg.http_proxy);
        // 默认端口字符串
        const std::string default_port = info.use_ssl ? "443" : "80";
        // 构造 host URL
        std::string host_url = (info.use_ssl ? "https://" : "http://") + info.host;
        // 若端口非默认则追加
        if (!info.host.empty() && info.port != static_cast<uint16_t>(std::stoi(default_port))) {
            host_url += ":" + std::to_string(info.port);
        }
        // 代理配置无效提示
        if (!cfg.http_proxy.empty() && !proxy.enabled) {
            LOG_WARN << "LLM proxy config invalid; ignoring http_proxy=" << cfg.http_proxy;
        }
        // 打印代理/直连信息
        if (proxy.enabled) {
            LOG_INFO << "LLM proxy enabled: proxy=" << proxy.host << ":" << proxy.port
                     << " proxy_ssl=" << (proxy.use_ssl ? "true" : "false")
                     << " target=" << host_url;
        } else {
            LOG_INFO << "LLM direct connect: target=" << host_url;
        }
        
        // [P1 DIAGNOSTIC] Check c-ares and DNS resolver status
        // 注意: 当 c-ares 启用时, 它可能返回 IPv6 地址, 导致在某些 Windows 环境下连接失败
        // 如果遇到 "Network failure" 或 "BadServerAddress", 请启用 use_curl_fallback=true
        LOG_INFO << "[P1 DNS] c-ares enabled: " << (trantor::Resolver::isCAresUsed() ? "YES" : "NO");
        LOG_INFO << "[P1 DNS] Attempting to create HttpClient for host: " << info.host 
                 << " port: " << info.port << " ssl: " << (info.use_ssl ? "true" : "false");
        
        // 创建 HttpClient
        // Note: per-request client creation is intentional — Drogon's sync
        // sendRequest() serializes on a shared client, which degrades RPS
        // when the backend (LLM/mock) has non-trivial latency. Creating
        // a fresh client per request allows maximum concurrency via separate
        // TCP connections.
        drogon::HttpClientPtr client;
        if (proxy.enabled) {
            client = drogon::HttpClient::newHttpClient(proxy.host, proxy.port, proxy.use_ssl);
        } else {
            client = drogon::HttpClient::newHttpClient(host_url);
        }
        // 创建请求对象
        auto req = drogon::HttpRequest::newHttpRequest();
        // 设置方法为 POST
        req->setMethod(drogon::Post);
        // 组装路径
        const std::string path = info.prefix.empty() ? "/chat/completions" : (info.prefix + "/chat/completions");
        // 若使用代理，需要完整 URL 和 Host 头
        if (proxy.enabled) {
            const std::string scheme = info.use_ssl ? "https://" : "http://";
            std::string target = scheme + info.host;
            std::string host_header = info.host;
            if (!info.host.empty() && info.port != static_cast<uint16_t>(std::stoi(default_port))) {
                target += ":" + std::to_string(info.port);
                host_header += ":" + std::to_string(info.port);
            }
            req->setPath(target + path);
            req->addHeader("Host", host_header);
        } else {
            req->setPath(path);
        }
        // 添加授权头
        req->addHeader("Authorization", "Bearer " + cfg.api_key);
        // 添加内容类型
        req->addHeader("Content-Type", "application/json");
        // 设置请求体
        req->setBody(body.dump());

        // 计算超时秒数
        const auto timeout_sec = static_cast<double>(cfg.timeout_ms) / 1000.0;
        LOG_INFO << "LLM request dispatch: api_base=" << cfg.api_base
                 << " timeout_sec=" << timeout_sec
                 << " use_proxy=" << (proxy.enabled ? "true" : "false");
        // Sync request with retry for minimal behavior and controllable errors.
        drogon::ReqResult res = drogon::ReqResult::Ok;
        drogon::HttpResponsePtr resp;
        for (int attempt = 0; attempt <= max_retries; ++attempt) {
            const auto attempt_start = std::chrono::steady_clock::now();
            std::tie(res, resp) = client->sendRequest(req, timeout_sec);
            const auto attempt_latency_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - attempt_start).count();
            util::global_metrics().record_external_call_latency(attempt_latency_ms);
            if (res == drogon::ReqResult::Ok && resp && resp->getStatusCode() == drogon::k200OK) {
                break;
            }
            // 输出重试日志，便于验收“超时与重试次数可见”
            LOG_WARN << "LLM attempt failed; attempt=" << attempt
                     << " max_retries=" << max_retries
                     << " req_result=" << drogon::to_string(res)
                     << " http_status=" << (resp ? std::to_string(resp->getStatusCode()) : "none")
                     << " attempt_latency_ms=" << attempt_latency_ms;
            if (attempt < max_retries) {
                std::this_thread::sleep_for(std::chrono::milliseconds(timeout_cfg.external_call_backoff_ms));
            }
        }

        // 请求失败或无响应
        if (res != drogon::ReqResult::Ok || !resp) {
            result.degraded = true;
            result.error = "llm_request_failed";
            result.warnings.push_back("llm_request_failed");
            circuit_breaker->record_failure("model:" + family);
            if (resp) {
                result.warnings.push_back("llm_http_status_" + std::to_string(resp->getStatusCode()));
            } else {
                result.warnings.push_back("llm_reqresult_" + std::to_string(static_cast<int>(res)));
            }
            
            // [P1 DIAGNOSTIC] Enhanced logging for deep investigation
            LOG_ERROR << "LLM request failed: result=" << drogon::to_string(res)
                      << " result_code=" << static_cast<int>(res)
                      << " has_response=" << (resp ? "true" : "false")
                      << " use_proxy=" << (proxy.enabled ? "true" : "false")
                      << " target_host=" << info.host
                      << " target_port=" << info.port
                      << " use_ssl=" << (info.use_ssl ? "true" : "false");
            
            // [P1 DIAGNOSTIC] Log response details if available
            if (resp) {
                LOG_ERROR << "LLM HTTP response: status=" << resp->getStatusCode()
                          << " content_type=" << resp->getContentType()
                          << " body_size=" << resp->getBody().size();
            }
            // 若允许使用 curl 降级
            if (cfg.use_curl_fallback) {
                std::vector<std::string> curl_warnings;
                if (curl_fallback_request(cfg, body, message, result.metrics, curl_warnings)) {
                    result.degraded = false;
                    result.warnings.clear();
                    result.warnings.push_back("curl_fallback_used");
                    circuit_breaker->record_success("model:" + family);
                    LOG_WARN << "LLM fallback used: curl_fallback_used";
                } else {
                    result.warnings.insert(result.warnings.end(), curl_warnings.begin(), curl_warnings.end());
                    message = "FinGuard fallback response due to LLM request failure.";
                    LOG_ERROR << "LLM fallback failed; returning mock response.";
                }
            } else {
                message = "FinGuard fallback response due to LLM request failure.";
            }
        } else if (resp->getStatusCode() != drogon::k200OK) {
            // 非 200 响应
            result.degraded = true;
            result.error = "llm_bad_status";
            result.warnings.push_back("llm_bad_status_" + std::to_string(resp->getStatusCode()));
            const auto body_text = truncate_copy(std::string(resp->getBody()), 200);
            if (!body_text.empty()) {
                result.warnings.push_back("llm_body_" + body_text);
            }
            LOG_ERROR << "LLM bad status: status=" << resp->getStatusCode()
                      << " use_proxy=" << (proxy.enabled ? "true" : "false");
            if (resp->getStatusCode() >= 500) {
                circuit_breaker->record_failure("model:" + family);
            }
            // 若允许使用 curl 降级
            if (cfg.use_curl_fallback) {
                std::vector<std::string> curl_warnings;
                if (curl_fallback_request(cfg, body, message, result.metrics, curl_warnings)) {
                    result.degraded = false;
                    result.warnings.clear();
                    result.warnings.push_back("curl_fallback_used");
                    circuit_breaker->record_success("model:" + family);
                    LOG_WARN << "LLM fallback used: curl_fallback_used";
                } else {
                    result.warnings.insert(result.warnings.end(), curl_warnings.begin(), curl_warnings.end());
                    message = "FinGuard fallback response due to LLM HTTP status.";
                    LOG_ERROR << "LLM fallback failed; returning mock response.";
                }
            } else {
                message = "FinGuard fallback response due to LLM HTTP status.";
            }
        } else {
            // 200 OK 响应
            if (!parse_completion_payload(std::string(resp->getBody()), message, result.metrics, result.warnings)) {
                result.degraded = true;
                result.error = "llm_parse_failed";
                message = "FinGuard fallback response due to LLM parse failure.";
                LOG_ERROR << "LLM parse failed.";
            } else {
                circuit_breaker->record_success("model:" + family);
                LOG_INFO << "LLM request succeeded: status=" << resp->getStatusCode()
                         << " use_proxy=" << (proxy.enabled ? "true" : "false");
            }
        }
    }

    // 保存完整文本，供响应侧规则检查
    result.full_text = message;

    // 将文本拆成 token（按空格），模拟流式输出
    std::istringstream iss(message);
    // 单个 token
    std::string token;
    // 按空格提取
    while (iss >> token) {
        result.tokens.push_back(token + " ");
    }

    // 返回结果
    return result;
}

// 命名空间结束
}
