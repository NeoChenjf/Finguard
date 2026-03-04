#include "valuation/proxy_config.h"

#include <string>
#include <stdexcept>

namespace finguard::valuation {

ProxyConfig parse_proxy(const std::string &proxy_str) {
    ProxyConfig cfg;
    std::string url = proxy_str;

    // 去首尾空白
    const auto trim = [](std::string s) -> std::string {
        const auto a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        const auto b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    };
    url = trim(url);
    if (url.empty()) return cfg;

    // 协议
    if (url.rfind("https://", 0) == 0) {
        cfg.use_ssl = true;
        url = url.substr(8);
    } else if (url.rfind("http://", 0) == 0) {
        cfg.use_ssl = false;
        url = url.substr(7);
    }

    // 去掉路径部分
    const auto slash = url.find('/');
    if (slash != std::string::npos) url = url.substr(0, slash);

    // 端口
    const auto colon = url.find(':');
    if (colon != std::string::npos) {
        cfg.host = url.substr(0, colon);
        try {
            const int p = std::stoi(url.substr(colon + 1));
            if (p > 0 && p < 65536) cfg.port = static_cast<uint16_t>(p);
        } catch (...) {}
    } else {
        cfg.host = url;
    }

    if (!cfg.host.empty() && cfg.port != 0) cfg.enabled = true;
    return cfg;
}

} // namespace finguard::valuation
