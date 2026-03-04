#include "valuation/search_wrapper.h"
#include "valuation/proxy_config.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace finguard::valuation {

namespace {

// 将公司代码转为搜索查询语句（英文，针对全球市场）
std::string build_search_query(const std::string &symbol) {
    return symbol +
           " stock competitive advantage moat business model management team";
}

// 通过 curl.exe 发送 POST 请求
std::pair<int, std::string> do_post_curl(const std::string &url,
                                          const std::string &json_body,
                                          int timeout_sec,
                                          const ProxyConfig &proxy) {
    // 把 body 写入临时文件（避免命令行转义)
    const std::string tmpfile = "tavily_req_body.json";
    {
        std::ofstream ofs(tmpfile, std::ios::binary);
        ofs << json_body;
    }

    std::ostringstream cmd;
    cmd << "curl.exe -s -w \"\\n%{http_code}\"";
    cmd << " --max-time " << timeout_sec;
    cmd << " -X POST";
    cmd << " -H \"Content-Type: application/json\"";
    cmd << " -H \"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36\"";
    cmd << " -d @" << tmpfile;
    if (proxy.enabled) {
        cmd << " -x " << (proxy.use_ssl ? "https://" : "http://")
            << proxy.host << ":" << proxy.port;
    }
    cmd << " \"" << url << "\" 2>NUL";

    std::string output;
    {
        std::array<char, 8192> buf{};
        std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(cmd.str().c_str(), "r"), _pclose);
        if (!pipe) return {-1, ""};
        while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe.get()) != nullptr) {
            output += buf.data();
        }
    }

    const auto last_nl = output.rfind('\n');
    if (last_nl == std::string::npos || last_nl == 0) return {-1, ""};

    std::string trimmed_code;
    for (char c : output.substr(last_nl + 1)) if (c >= '0' && c <= '9') trimmed_code += c;
    int status = -1;
    try { status = std::stoi(trimmed_code); } catch (...) {}
    return {status, output.substr(0, last_nl)};
}

} // namespace (anonymous)

SearchResult fetch_qualitative_info(const std::string &symbol,
                                     const ValuationConfig &config) {
    SearchResult result;

    if (config.tavily_api_key.empty()) {
        result.error = "no_tavily_api_key";
        spdlog::warn("[SearchWrapper] Tavily API key not configured for {}", symbol);
        return result;
    }

    const ProxyConfig proxy = parse_proxy(config.http_proxy);
    if (proxy.enabled) {
        spdlog::info("[SearchWrapper] Using proxy {}:{} for {}", proxy.host, proxy.port, symbol);
    }

    // 构建 Tavily 请求体
    nlohmann::json body;
    body["api_key"]      = config.tavily_api_key;
    body["query"]        = build_search_query(symbol);
    body["search_depth"] = "basic";
    body["max_results"]  = config.search_max_results;
    body["include_answer"] = false;
    body["include_raw_content"] = false;

    const auto [status, resp_body] = do_post_curl(
        "https://api.tavily.com/search", body.dump(),
        config.yahoo_timeout_ms / 1000, proxy);

    if (status < 0) {
        result.error = "tavily_network_error";
        spdlog::error("[SearchWrapper] Tavily network error for {}", symbol);
        return result;
    }
    if (status != 200) {
        result.error = "tavily_http_" + std::to_string(status);
        spdlog::warn("[SearchWrapper] Tavily HTTP {} for {}", status, symbol);
        return result;
    }

    // 解析响应
    try {
        auto j = nlohmann::json::parse(resp_body);
        if (!j.contains("results") || !j.at("results").is_array()) {
            result.error = "tavily_no_results";
            return result;
        }

        std::ostringstream combined;
        int idx = 0;
        for (const auto &item : j.at("results")) {
            SearchSnippet snippet;
            snippet.title   = item.value("title", "");
            snippet.url     = item.value("url", "");
            snippet.content = item.value("content", "");
            if (snippet.content.empty()) continue;

            result.snippets.push_back(snippet);
            combined << "[" << (++idx) << "] " << snippet.title << "\n"
                     << snippet.content << "\n\n";

            if (idx >= config.search_max_results) break;
        }
        result.combined_text = combined.str();

    } catch (const std::exception &e) {
        result.error = std::string("tavily_parse_error: ") + e.what();
        spdlog::error("[SearchWrapper] Tavily parse error for {}: {}", symbol, e.what());
    }

    spdlog::info("[SearchWrapper] {} — {} snippets fetched", symbol,
                 result.snippets.size());
    return result;
}

} // namespace finguard::valuation
