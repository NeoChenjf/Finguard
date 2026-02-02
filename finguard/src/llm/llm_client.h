#pragma once

// LLM 接入的最小抽象：负责读取配置并请求外部模型

#include <string>
#include <vector>

namespace finguard::llm {

// 计费/用量相关统计
struct LlmMetrics {
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;
    bool has_usage = false;
};

// LLM 连接配置（来自 config/llm.json）
struct LlmConfig {
    std::string api_base;
    std::string api_key;
    std::string model;
    double temperature = 0.7;
    int timeout_ms = 30000;
    bool use_curl_fallback = false;
    std::string curl_path = "curl.exe";
    std::string http_proxy;
};

// 流式结果（事件由 routes.cpp 组装为 SSE）
struct StreamResult {
    std::vector<std::string> tokens;
    std::vector<std::string> cites;
    std::vector<std::string> warnings;
    LlmMetrics metrics;
    bool degraded = false;
    std::string error;
};

class LlmClient {
public:
    // 读取配置文件，若不存在则使用默认值
    LlmConfig load_config() const;
    // 发起一次请求并返回拆分后的 token（当前实现为非真正流式）
    StreamResult stream_chat(const std::string &prompt) const;
};

}
