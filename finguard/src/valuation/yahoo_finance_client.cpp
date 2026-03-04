#include "valuation/yahoo_finance_client.h"
#include "valuation/proxy_config.h"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace finguard::valuation {

namespace {

// ── 辅助：从 nlohmann JSON value 提取 double（兼容 raw 数值与 {"raw":x} 格式）
double extract_double(const nlohmann::json &j, const std::string &key,
                      double fallback = -1.0) {
    if (!j.contains(key)) return fallback;
    const auto &v = j.at(key);
    if (v.is_number()) return v.get<double>();
    if (v.is_object() && v.contains("raw") && v.at("raw").is_number())
        return v.at("raw").get<double>();
    return fallback;
}

// ── 通过 curl config 文件执行请求（规避 cmd.exe 的 & 转义问题）──
// 返回 {http_status, body}；失败返回 {-1, ""}
std::pair<int, std::string>
curl_get(const std::string &url, int timeout_sec,
         const ProxyConfig &proxy,
         const std::string &extra_header = "",
         const std::string &cookie_jar = "",
         const std::string &cookie_file = "") {

    // 写 curl config 文件
    const std::string cfg_path = "valuation_curl.cfg";
    {
        std::ofstream ofs(cfg_path, std::ios::binary);
        ofs << "url = \"" << url << "\"\n";
        ofs << "silent\n";
        ofs << "max-time = " << timeout_sec << "\n";
        ofs << "write-out = \"\\n%{http_code}\"\n";
        ofs << "header = \"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36\"\n";
        ofs << "header = \"Accept: */*\"\n";
        if (!extra_header.empty()) {
            ofs << "header = \"" << extra_header << "\"\n";
        }
        if (proxy.enabled) {
            ofs << "proxy = " << (proxy.use_ssl ? "https://" : "http://")
                << proxy.host << ":" << proxy.port << "\n";
        }
        if (!cookie_jar.empty()) {
            ofs << "cookie-jar = \"" << cookie_jar << "\"\n";
        }
        if (!cookie_file.empty()) {
            ofs << "cookie = \"" << cookie_file << "\"\n";
        }
    }

    const std::string cmd = "curl.exe -K \"" + cfg_path + "\" 2>NUL";
    spdlog::debug("[ValuationHTTP] exec config-based: url={}", url);

    // 执行并捕获输出
    std::string output;
    {
        std::array<char, 8192> buf{};
        std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(cmd.c_str(), "r"), _pclose);
        if (!pipe) {
            spdlog::error("[ValuationHTTP] popen failed for {}", url);
            return {-1, ""};
        }
        while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe.get()) != nullptr) {
            output += buf.data();
        }
    }

    // 从末尾解析 \n<http_code>
    // 去除尾部空白
    while (!output.empty() && (output.back() == '\r' || output.back() == '\n' || output.back() == ' '))
        output.pop_back();

    // 找最后一个换行
    const auto last_nl = output.rfind('\n');
    if (last_nl == std::string::npos) {
        // 可能整个输出只有状态码
        std::string trimmed;
        for (char c : output) if (c >= '0' && c <= '9') trimmed += c;
        if (!trimmed.empty()) {
            try { return {std::stoi(trimmed), ""}; } catch (...) {}
        }
        spdlog::warn("[ValuationHTTP] curl returned empty for {}", url);
        return {-1, ""};
    }

    const std::string code_str = output.substr(last_nl + 1);
    std::string trimmed_code;
    for (char c : code_str) if (c >= '0' && c <= '9') trimmed_code += c;

    int status = -1;
    try { status = std::stoi(trimmed_code); } catch (...) {}

    const std::string body = output.substr(0, last_nl);
    spdlog::debug("[ValuationHTTP] {} -> status={}, body_len={}", url, status, body.size());
    return {status, body};
}

// ── Yahoo crumb + cookie 管理 ──
struct YahooCrumb {
    std::string crumb;
    std::string cookie_file;
    bool valid = false;
};

YahooCrumb fetch_yahoo_crumb(int timeout_sec, const ProxyConfig &proxy) {
    YahooCrumb yc;
    yc.cookie_file = "yahoo_cookies.txt";

    // Step 1: 访问 fc.yahoo.com 获取 cookie
    const auto [s1, b1] = curl_get("https://fc.yahoo.com/", timeout_sec, proxy,
                                    "", yc.cookie_file, "");
    spdlog::debug("[YahooCrumb] Step1 cookie fetch: status={}", s1);

    // Step 2: 用 cookie 获取 crumb（带重试）
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) {
            const int delay_ms = attempt * 2000; // 2s, 4s
            spdlog::info("[YahooCrumb] Retry {} after {}ms...", attempt, delay_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
        const auto [s2, crumb] = curl_get(
            "https://query2.finance.yahoo.com/v1/test/getcrumb",
            timeout_sec, proxy, "", "", yc.cookie_file);

        if (s2 == 200 && !crumb.empty()) {
            yc.crumb = crumb;
            while (!yc.crumb.empty() && (yc.crumb.back() == '\r' || yc.crumb.back() == '\n' || yc.crumb.back() == ' '))
                yc.crumb.pop_back();
            yc.valid = !yc.crumb.empty();
            if (yc.valid) {
                spdlog::info("[YahooCrumb] Got crumb: {}... (len={})",
                             yc.crumb.substr(0, std::min<size_t>(8, yc.crumb.size())),
                             yc.crumb.size());
                return yc;
            }
        } else if (s2 == 429) {
            spdlog::warn("[YahooCrumb] Rate limited (429) on attempt {}", attempt);
        } else {
            spdlog::warn("[YahooCrumb] Crumb fetch failed: status={}", s2);
        }
    }
    spdlog::warn("[YahooCrumb] All crumb attempts failed");
    return yc;
}

// ── 带 crumb 缓存的获取器 ──
static std::mutex s_crumb_mutex;
static YahooCrumb s_cached_crumb;
static std::chrono::steady_clock::time_point s_crumb_time{};

YahooCrumb get_or_refresh_crumb(int timeout_sec, const ProxyConfig &proxy) {
    std::lock_guard<std::mutex> lock(s_crumb_mutex);
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - s_crumb_time).count();
    if (s_cached_crumb.valid && elapsed < 25) {
        spdlog::debug("[YahooCrumb] Using cached crumb (age={}min)", elapsed);
        return s_cached_crumb;
    }
    s_cached_crumb = fetch_yahoo_crumb(timeout_sec, proxy);
    s_crumb_time = now;
    return s_cached_crumb;
}

// 对 crumb 进行 URL 编码（处理特殊字符如 /）
std::string url_encode(const std::string &s) {
    std::ostringstream oss;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << c;
        } else {
            oss << '%' << std::uppercase << std::hex
                << ((c >> 4) & 0x0F) << (c & 0x0F);
        }
    }
    return oss.str();
}

// ── 带 crumb 的 Yahoo GET ──
std::pair<int, std::string>
yahoo_get(const std::string &url, int timeout_sec,
          const ProxyConfig &proxy, const YahooCrumb &crumb) {
    std::string full_url = url;
    if (crumb.valid) {
        full_url += (url.find('?') != std::string::npos ? "&" : "?");
        full_url += "crumb=" + url_encode(crumb.crumb);
    }
    return curl_get(full_url, timeout_sec, proxy, "", "", crumb.cookie_file);
}

// ── Yahoo Finance 主源：quoteSummary 接口 ──
struct QuoteSummaryResult {
    double trailing_pe = -1.0;
    double forward_pe = -1.0;
    double price_to_book = -1.0;
    double peg_ratio = -1.0;
    double trailing_eps = 0.0;
    double earnings_growth = 0.0;
    double current_price = 0.0;
    std::string error;
};

QuoteSummaryResult fetch_quote_summary(const std::string &symbol, int timeout_ms,
                                        const ProxyConfig &proxy,
                                        const YahooCrumb &crumb) {
    QuoteSummaryResult result;

    int status = -1;
    std::string body;

    // 带重试的请求（处理 429 限速）
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) {
            const int delay_ms = attempt * 3000; // 3s, 6s
            spdlog::info("[YahooQuote] Retry {} for {} after {}ms", attempt, symbol, delay_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }

        std::tie(status, body) = yahoo_get(
            "https://query1.finance.yahoo.com/v10/finance/quoteSummary/" + symbol +
            "?modules=defaultKeyStatistics,financialData,summaryDetail&formatted=false&lang=en-US&region=US",
            timeout_ms / 1000, proxy, crumb);

        if (status == 200 && !body.empty()) break;
        if (status != 429) break; // 非 429 不重试
    }

    if (status < 0 || body.empty()) {
        result.error = "yahoo_quotesummary_network_error";
        return result;
    }
    if (status != 200) {
        result.error = "yahoo_quotesummary_http_" + std::to_string(status);
        return result;
    }

    try {
        auto j = nlohmann::json::parse(body);
        const auto &qs = j.at("quoteSummary");
        if (!qs.at("result").is_array() || qs.at("result").empty()) {
            result.error = "yahoo_quotesummary_empty_result";
            return result;
        }
        const auto &entry = qs.at("result").at(0);

        // defaultKeyStatistics: forwardPE, pegRatio, trailingEps, priceToBook
        if (entry.contains("defaultKeyStatistics")) {
            const auto &stat = entry.at("defaultKeyStatistics");
            result.forward_pe   = extract_double(stat, "forwardPE");
            result.peg_ratio    = extract_double(stat, "pegRatio");
            result.trailing_eps = extract_double(stat, "trailingEps", 0.0);
            result.price_to_book = extract_double(stat, "priceToBook");
        }
        // summaryDetail: trailingPE (主要来源)
        if (entry.contains("summaryDetail")) {
            const auto &sd = entry.at("summaryDetail");
            result.trailing_pe = extract_double(sd, "trailingPE");
            // fallback: 如果 defaultKeyStatistics 没有 forwardPE
            if (result.forward_pe < 0.0)
                result.forward_pe = extract_double(sd, "forwardPE");
        }
        // financialData: earningsGrowth, currentPrice
        if (entry.contains("financialData")) {
            const auto &fin = entry.at("financialData");
            result.earnings_growth = extract_double(fin, "earningsGrowth", 0.0);
            result.current_price   = extract_double(fin, "currentPrice", 0.0);
        }
        // PEG fallback: 如果 Yahoo 没提供 pegRatio，用 trailingPE / (earningsGrowth * 100) 自算
        if (result.peg_ratio < 0.0 && result.trailing_pe > 0.0 && result.earnings_growth > 0.01) {
            result.peg_ratio = result.trailing_pe / (result.earnings_growth * 100.0);
            spdlog::info("[YahooQuote] PEG auto-calc for {}: {:.2f} = {:.1f} / ({:.1f}%)",
                         symbol, result.peg_ratio, result.trailing_pe, result.earnings_growth * 100.0);
        }
    } catch (const std::exception &e) {
        result.error = std::string("yahoo_quotesummary_parse_error: ") + e.what();
    }
    return result;
}

// ── Yahoo Finance 价格历史：v8/finance/chart 接口 ──
struct PriceHistoryResult {
    std::vector<double> quarterly_closes;
    std::string error;
};

PriceHistoryResult fetch_price_history(const std::string &symbol, int timeout_ms,
                                        int quarters, const ProxyConfig &proxy,
                                        const YahooCrumb &crumb) {
    PriceHistoryResult result;
    const std::string range = (quarters <= 4) ? "1y" : "2y";

    const auto [status, body] = yahoo_get(
        "https://query1.finance.yahoo.com/v8/finance/chart/" + symbol +
        "?interval=3mo&range=" + range + "&includeAdjustedClose=true",
        timeout_ms / 1000, proxy, crumb);

    if (status != 200 || body.empty()) {
        result.error = "yahoo_chart_network_error";
        return result;
    }

    try {
        auto j = nlohmann::json::parse(body);
        const auto &chart_result = j.at("chart").at("result").at(0);
        const auto &indicators   = chart_result.at("indicators");

        std::vector<double> closes;
        if (indicators.contains("adjclose") &&
            !indicators.at("adjclose").empty() &&
            indicators.at("adjclose").at(0).contains("adjclose")) {
            for (const auto &v : indicators.at("adjclose").at(0).at("adjclose")) {
                if (v.is_number()) closes.push_back(v.get<double>());
            }
        } else if (indicators.contains("quote") &&
                   !indicators.at("quote").empty() &&
                   indicators.at("quote").at(0).contains("close")) {
            for (const auto &v : indicators.at("quote").at(0).at("close")) {
                if (v.is_number()) closes.push_back(v.get<double>());
            }
        }

        if (closes.empty()) {
            result.error = "yahoo_chart_no_close_data";
            return result;
        }

        if (static_cast<int>(closes.size()) > quarters) {
            closes.erase(closes.begin(), closes.begin() +
                         (static_cast<int>(closes.size()) - quarters));
        }
        result.quarterly_closes = std::move(closes);

    } catch (const std::exception &e) {
        result.error = std::string("yahoo_chart_parse_error: ") + e.what();
    }
    return result;
}

// ── Alpha Vantage 降级 ──
FinancialMetrics fetch_from_alpha_vantage(const std::string &symbol,
                                           const ValuationConfig &config,
                                           const ProxyConfig &proxy) {
    FinancialMetrics result;
    result.symbol = symbol;

    if (config.alpha_vantage_api_key.empty()) {
        result.error = "alpha_vantage_api_key_not_configured";
        return result;
    }

    const auto [status, body] = curl_get(
        "https://www.alphavantage.co/query?function=OVERVIEW&symbol=" + symbol +
        "&apikey=" + config.alpha_vantage_api_key,
        config.yahoo_timeout_ms / 1000, proxy);

    if (status != 200 || body.empty()) {
        result.error = "alpha_vantage_http_error";
        return result;
    }

    try {
        auto j = nlohmann::json::parse(body);

        auto parse_av_double = [&](const std::string &key) -> double {
            if (!j.contains(key)) return -1.0;
            const auto &v = j.at(key);
            if (v.is_number()) return v.get<double>();
            if (v.is_string()) {
                const std::string s = v.get<std::string>();
                if (s == "None" || s == "-") return -1.0;
                try { return std::stod(s); } catch (...) { return -1.0; }
            }
            return -1.0;
        };

        result.trailing_pe     = parse_av_double("TrailingPE");
        result.forward_pe      = parse_av_double("ForwardPE");
        result.peg_ratio       = parse_av_double("PEGRatio");
        result.price_to_book   = parse_av_double("PriceToBookRatio");
        result.trailing_eps    = parse_av_double("EPS");
        result.earnings_growth = parse_av_double("RevenueGrowthTTMYOY");
        result.has_sufficient_data = (result.trailing_pe > 0.0) && (result.peg_ratio > 0.0);
        result.data_source = "alpha_vantage";

    } catch (const std::exception &e) {
        result.error = std::string("alpha_vantage_parse_error: ") + e.what();
    }
    return result;
}

} // namespace (anonymous)

// ── 公共接口实现 ──
FinancialMetrics fetch_financial_metrics(const std::string &symbol,
                                          const ValuationConfig &config) {
    FinancialMetrics metrics;
    metrics.symbol = symbol;

    const ProxyConfig proxy = parse_proxy(config.http_proxy);
    if (proxy.enabled) {
        spdlog::info("[ValuationClient] Using proxy {}:{} for {}", proxy.host, proxy.port, symbol);
    }

    // Step 0: 获取 Yahoo crumb & cookie（带静态缓存，25 分钟有效）
    const auto crumb = get_or_refresh_crumb(config.yahoo_timeout_ms / 1000, proxy);
    if (!crumb.valid) {
        spdlog::warn("[ValuationClient] Yahoo crumb failed, will try anyway");
    }

    // Step 1: 获取 quote summary（当前估值指标）
    const auto qs = fetch_quote_summary(symbol, config.yahoo_timeout_ms, proxy, crumb);
    if (!qs.error.empty()) {
        spdlog::warn("[ValuationClient] Yahoo quoteSummary failed for {}: {}", symbol, qs.error);
        metrics = fetch_from_alpha_vantage(symbol, config, proxy);
        return metrics;
    }

    metrics.trailing_pe     = qs.trailing_pe;
    metrics.forward_pe      = qs.forward_pe;
    metrics.peg_ratio       = qs.peg_ratio;
    metrics.price_to_book   = qs.price_to_book;
    metrics.trailing_eps    = qs.trailing_eps;
    metrics.earnings_growth = qs.earnings_growth;
    metrics.data_source     = "yahoo_finance";

    // Step 2: 获取价格历史，计算历史 PE 均值
    if (metrics.trailing_eps > 0.01) {
        const auto ph = fetch_price_history(symbol, config.yahoo_timeout_ms,
                                             config.pe_history_quarters, proxy, crumb);
        if (ph.error.empty() && !ph.quarterly_closes.empty()) {
            std::vector<double> pe_series;
            for (double close : ph.quarterly_closes) {
                const double implied_pe = close / metrics.trailing_eps;
                if (implied_pe > 0.0 && implied_pe < 1000.0) {
                    pe_series.push_back(implied_pe);
                }
            }
            if (!pe_series.empty()) {
                const double sum = std::accumulate(pe_series.begin(), pe_series.end(), 0.0);
                metrics.historical_pe_mean = sum / static_cast<double>(pe_series.size());
                metrics.historical_pe_series = pe_series;
                metrics.pe_history_quarters_used = static_cast<int>(pe_series.size());
            }
        } else {
            spdlog::warn("[ValuationClient] Yahoo chart failed for {}: {}", symbol, ph.error);
        }
    } else {
        spdlog::warn("[ValuationClient] trailingEps={:.4f} for {} — skipping PE history",
                     metrics.trailing_eps, symbol);
    }

    metrics.has_sufficient_data =
        (metrics.trailing_pe > 0.0) &&
        (metrics.peg_ratio > 0.0) &&
        (metrics.historical_pe_mean > 0.0);

    spdlog::info(
        "[ValuationClient] {} — trailingPE={:.1f}, historicalPEMean={:.1f}, "
        "PEG={:.2f}, PB={:.2f}, source={}, sufficient={}",
        symbol, metrics.trailing_pe, metrics.historical_pe_mean,
        metrics.peg_ratio, metrics.price_to_book, metrics.data_source,
        metrics.has_sufficient_data);

    return metrics;
}

} // namespace finguard::valuation
