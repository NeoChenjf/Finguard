#include "llm/llm_client.h"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>

namespace finguard::llm {

namespace {

// 默认配置（当 llm.json 缺失时使用）
LlmConfig default_config() {
    LlmConfig cfg;
    cfg.api_base = "https://dashscope-us.aliyuncs.com/compatible-mode/v1";
    cfg.model = "qwen-plus";
    cfg.use_curl_fallback = false;
    cfg.curl_path = "curl.exe";
    return cfg;
}

// 去掉首尾空白
std::string trim_copy(const std::string &value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

// 截断长字符串，避免把过长内容塞进 warning
std::string truncate_copy(const std::string &value, std::size_t max_len) {
    if (value.size() <= max_len) {
        return value;
    }
    return value.substr(0, max_len) + "...";
}

// 将 api_base 拆分为 host / path 前缀 / 端口 / 是否 SSL
struct HostInfo {
    std::string host;
    std::string prefix;
    uint16_t port = 80;
    bool use_ssl = false;
};

HostInfo parse_base(const std::string &api_base) {
    HostInfo info;
    std::string url = api_base;
    if (url.rfind("https://", 0) == 0) {
        info.use_ssl = true;
        info.port = 443;
        url = url.substr(8);
    } else if (url.rfind("http://", 0) == 0) {
        info.use_ssl = false;
        info.port = 80;
        url = url.substr(7);
    }

    const auto path_pos = url.find('/');
    if (path_pos != std::string::npos) {
        info.prefix = url.substr(path_pos);
        url = url.substr(0, path_pos);
        if (!info.prefix.empty() && info.prefix.back() == '/') {
            info.prefix.pop_back();
        }
    }

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

std::string join_url(const std::string &base, const std::string &path) {
    if (base.empty()) {
        return path;
    }
    if (base.back() == '/') {
        return base.substr(0, base.size() - 1) + path;
    }
    return base + path;
}

bool parse_completion_payload(const std::string &body,
                              std::string &message,
                              LlmMetrics &metrics,
                              std::vector<std::string> &warnings) {
    try {
        const auto payload = nlohmann::json::parse(body);
        if (payload.contains("choices") && payload["choices"].is_array() && !payload["choices"].empty()) {
            auto &choice = payload["choices"][0];
            if (choice.contains("message") && choice["message"].contains("content")) {
                message = choice["message"]["content"].get<std::string>();
            }
        }
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
        warnings.push_back("llm_parse_failed");
        return false;
    }
}

bool curl_fallback_request(const LlmConfig &cfg,
                           const nlohmann::json &body,
                           std::string &message,
                           LlmMetrics &metrics,
                           std::vector<std::string> &warnings) {
    const auto url = join_url(cfg.api_base, "/chat/completions");
    const auto temp_path = std::filesystem::temp_directory_path() / "finguard_qwen_payload.json";
    {
        std::ofstream out(temp_path);
        if (!out) {
            warnings.push_back("curl_tempfile_failed");
            return false;
        }
        out << body.dump();
    }

    std::ostringstream cmd;
    cmd << cfg.curl_path
        << " -s"
        << " -X POST " << url
        << " -H \"Authorization: Bearer " << cfg.api_key << "\""
        << " -H \"Content-Type: application/json\""
        << " --data-binary @" << temp_path.string();

#ifdef _WIN32
    FILE *pipe = _popen(cmd.str().c_str(), "r");
#else
    FILE *pipe = popen(cmd.str().c_str(), "r");
#endif
    if (!pipe) {
        warnings.push_back("curl_popen_failed");
        return false;
    }
    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
#ifdef _WIN32
    const int exit_code = _pclose(pipe);
#else
    const int exit_code = pclose(pipe);
#endif
    if (exit_code != 0) {
        warnings.push_back("curl_exit_" + std::to_string(exit_code));
    }
    if (output.empty()) {
        warnings.push_back("curl_empty_output");
        return false;
    }
    return parse_completion_payload(output, message, metrics, warnings);
}

}

LlmConfig LlmClient::load_config() const {
    LlmConfig cfg = default_config();
    const std::filesystem::path path = std::filesystem::current_path() / "config" / "llm.json";
    if (!std::filesystem::exists(path)) {
        return cfg;
    }

    // 读取 JSON 配置
    std::ifstream in(path);
    if (!in) {
        return cfg;
    }

    nlohmann::json doc;
    try {
        in >> doc;
    } catch (const std::exception &) {
        return cfg;
    }

    // 允许部分字段缺失，使用默认值兜底
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

    return cfg;
}

StreamResult LlmClient::stream_chat(const std::string &prompt) const {
    StreamResult result;
    LlmConfig cfg = load_config();

    // 没有 API Key 时直接走降级逻辑
    if (cfg.api_key.empty()) {
        result.degraded = true;
        result.warnings.push_back("missing_api_key");
    }

    std::string message;
    if (cfg.api_key.empty()) {
        // mock：用 prompt 拼一段简单回复
        if (prompt.empty()) {
            message = "FinGuard mock stream response. Provide prompt in JSON {\"prompt\":\"...\"}.";
        } else {
            message = "FinGuard mock stream response for: " + prompt;
        }
    } else {
        nlohmann::json body;
        body["model"] = cfg.model;
        body["temperature"] = cfg.temperature;
        body["stream"] = false;
        body["messages"] = nlohmann::json::array({
            {{"role", "system"}, {"content", "You are FinGuard AI assistant."}},
            {{"role", "user"}, {"content", prompt.empty() ? "请输出一段示例流式回复" : prompt}}
        });

        // 组装 HttpClient：必须明确 host 与协议
        const auto info = parse_base(cfg.api_base);
        const std::string default_port = info.use_ssl ? "443" : "80";
        std::string host_url = (info.use_ssl ? "https://" : "http://") + info.host;
        if (!info.host.empty() && info.port != static_cast<uint16_t>(std::stoi(default_port))) {
            host_url += ":" + std::to_string(info.port);
        }
        auto client = drogon::HttpClient::newHttpClient(host_url);
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(drogon::Post);
        const std::string path = info.prefix.empty() ? "/chat/completions" : (info.prefix + "/chat/completions");
        req->setPath(path);
        req->addHeader("Authorization", "Bearer " + cfg.api_key);
        req->addHeader("Content-Type", "application/json");
        req->setBody(body.dump());

        const auto timeout_sec = static_cast<double>(cfg.timeout_ms) / 1000.0;
        // 同步发送（便于最小实现与错误可控）
        auto [res, resp] = client->sendRequest(req, timeout_sec);

        if (res != drogon::ReqResult::Ok || !resp) {
            result.degraded = true;
            result.error = "llm_request_failed";
            result.warnings.push_back("llm_request_failed");
            if (resp) {
                result.warnings.push_back("llm_http_status_" + std::to_string(resp->getStatusCode()));
            } else {
                result.warnings.push_back("llm_reqresult_" + std::to_string(static_cast<int>(res)));
            }
            if (cfg.use_curl_fallback) {
                std::vector<std::string> curl_warnings;
                if (curl_fallback_request(cfg, body, message, result.metrics, curl_warnings)) {
                    result.degraded = false;
                    result.warnings.clear();
                    result.warnings.push_back("curl_fallback_used");
                } else {
                    result.warnings.insert(result.warnings.end(), curl_warnings.begin(), curl_warnings.end());
                    message = "FinGuard fallback response due to LLM request failure.";
                }
            } else {
                message = "FinGuard fallback response due to LLM request failure.";
            }
        } else if (resp->getStatusCode() != drogon::k200OK) {
            result.degraded = true;
            result.error = "llm_bad_status";
            result.warnings.push_back("llm_bad_status_" + std::to_string(resp->getStatusCode()));
            const auto body_text = truncate_copy(std::string(resp->getBody()), 200);
            if (!body_text.empty()) {
                result.warnings.push_back("llm_body_" + body_text);
            }
            if (cfg.use_curl_fallback) {
                std::vector<std::string> curl_warnings;
                if (curl_fallback_request(cfg, body, message, result.metrics, curl_warnings)) {
                    result.degraded = false;
                    result.warnings.clear();
                    result.warnings.push_back("curl_fallback_used");
                } else {
                    result.warnings.insert(result.warnings.end(), curl_warnings.begin(), curl_warnings.end());
                    message = "FinGuard fallback response due to LLM HTTP status.";
                }
            } else {
                message = "FinGuard fallback response due to LLM HTTP status.";
            }
        } else {
            if (!parse_completion_payload(std::string(resp->getBody()), message, result.metrics, result.warnings)) {
                result.degraded = true;
                result.error = "llm_parse_failed";
                message = "FinGuard fallback response due to LLM parse failure.";
            }
        }
    }

    // 将文本拆成 token（按空格），模拟流式输出
    std::istringstream iss(message);
    std::string token;
    while (iss >> token) {
        result.tokens.push_back(token + " ");
    }

    return result;
}

}
