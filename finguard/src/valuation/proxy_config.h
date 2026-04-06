#pragma once

// 外部 HTTP 请求的代理信息（用于 Yahoo Finance、Alpha Vantage、Finnhub 等）
// 格式与 llm_client 保持一致
#include <string>
#include <cstdint>

namespace finguard::valuation {

struct ProxyConfig {
    bool    enabled  = false;
    std::string host;
    uint16_t    port = 0;
    bool    use_ssl  = false; // 是否以 SSL 方式连接代理（通常为 false）
};

// 解析 "http://host:port" 或 "https://host:port" 格式
ProxyConfig parse_proxy(const std::string &proxy_str);

} // namespace finguard::valuation
