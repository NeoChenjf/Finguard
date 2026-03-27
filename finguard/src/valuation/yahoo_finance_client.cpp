#include "valuation/yahoo_finance_client.h"
#include "valuation/proxy_config.h"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <string>
#include <thread>
#include <type_traits>
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

double normalize_ratio_metric(double value) {
    if (value < 0.0) return value;
    if (value > 1.5 && value <= 100.0) return value / 100.0;
    return value;
}

std::string trim_copy(std::string s) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string normalize_symbol_alias(std::string symbol) {
    symbol = trim_copy(std::move(symbol));
    std::transform(symbol.begin(), symbol.end(), symbol.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });

    if (symbol == "BRK" || symbol == "BRKB" || symbol == "BRK.B" ||
        symbol == "BRK/B" || symbol == "BRK B") {
        return "BRK-B";
    }

    return symbol;
}

void push_warning_once(FinancialMetrics &metrics, const std::string &warning) {
    if (std::find(metrics.warnings.begin(), metrics.warnings.end(), warning) ==
        metrics.warnings.end()) {
        metrics.warnings.push_back(warning);
    }
}

static std::atomic<long long> s_alpha_rate_limited_until{0};

bool alpha_rate_limited_active() {
    const auto now = static_cast<long long>(std::time(nullptr));
    return now < s_alpha_rate_limited_until.load();
}

void mark_alpha_rate_limited(int cooldown_sec = 1800) {
    const auto now = static_cast<long long>(std::time(nullptr));
    s_alpha_rate_limited_until.store(now + cooldown_sec);
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
    static std::atomic<unsigned long long> s_cfg_counter{0};
    const auto cfg_id = ++s_cfg_counter;
    const std::string cfg_path =
        "valuation_curl_" + std::to_string(cfg_id) + ".cfg";
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
            std::error_code ec;
            std::filesystem::remove(cfg_path, ec);
            return {-1, ""};
        }
        while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe.get()) != nullptr) {
            output += buf.data();
        }
    }
    {
        std::error_code ec;
        std::filesystem::remove(cfg_path, ec);
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
    const int effective_timeout_sec =
        timeout_sec < 2 ? 2 : (timeout_sec > 3 ? 3 : timeout_sec);

    // Step 1: 访问 fc.yahoo.com 获取 cookie
    const auto [s1, b1] = curl_get("https://fc.yahoo.com/", effective_timeout_sec, proxy,
                                    "", yc.cookie_file, "");
    spdlog::debug("[YahooCrumb] Step1 cookie fetch: status={}", s1);
    if (s1 != 200) {
        spdlog::warn("[YahooCrumb] Cookie fetch unavailable (status={}), skip Yahoo crumb", s1);
        return yc;
    }

    // Step 2: 用 cookie 获取 crumb（带重试）
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) {
            const int delay_ms = attempt * 2000; // 2s, 4s
            spdlog::info("[YahooCrumb] Retry {} after {}ms...", attempt, delay_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
        const auto [s2, crumb] = curl_get(
            "https://query2.finance.yahoo.com/v1/test/getcrumb",
            effective_timeout_sec, proxy, "", "", yc.cookie_file);

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
        } else if (s2 == 403) {
            spdlog::warn("[YahooCrumb] Crumb fetch blocked with 403 on attempt {}", attempt);
            return yc;
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
    if (!s_cached_crumb.valid && elapsed < 5) {
        spdlog::debug("[YahooCrumb] Reusing cached invalid crumb state (age={}min)", elapsed);
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
    double avg_roe_5y = -1.0;
    double trailing_eps = 0.0;
    double earnings_growth = 0.0;
    double current_price = 0.0;
    double book_value_per_share = -1.0;
    std::string error;
};

struct BalanceSheetSnapshot {
    double total_assets = -1.0;
    double total_liabilities = -1.0;
    std::string source = "unknown";
    std::string updated_at;
    std::string error;
};

struct FinnhubQuoteMetricsResult {
    double current_price = -1.0;
    double trailing_pe = -1.0;
    double price_to_book = -1.0;
    double avg_roe_5y = -1.0;
    double trailing_eps = -1.0;
    double book_value_per_share = -1.0;
    double peg_ratio = -1.0;
    std::string source = "finnhub";
    std::string updated_at;
    std::string error;
};

struct NetIncomeHistoryResult {
    std::vector<FinancialMetrics::NetProfitPoint> annual_points;
    std::vector<double> annual_net_income;
    std::string source = "unknown";
    std::string updated_at;
    std::string error;
};

struct EquityHistoryResult {
    std::vector<FinancialMetrics::EquityPoint> annual_points;
    std::string source = "unknown";
    std::string updated_at;
    std::string error;
};

std::string now_iso_utc() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::pair<std::string, double> extract_latest_timeseries_value(
    const nlohmann::json &result_arr,
    const std::vector<std::string> &candidate_keys) {
    std::pair<std::string, double> latest{"", -1.0};

    if (!result_arr.is_array()) {
        return latest;
    }

    for (const auto &item : result_arr) {
        if (!item.is_object()) {
            continue;
        }

        for (const auto &key : candidate_keys) {
            if (!item.contains(key) || !item.at(key).is_array()) {
                continue;
            }

            for (const auto &entry : item.at(key)) {
                if (!entry.contains("asOfDate") || !entry.at("asOfDate").is_string() ||
                    !entry.contains("reportedValue")) {
                    continue;
                }
                const auto &rv = entry.at("reportedValue");
                if (!rv.is_object() || !rv.contains("raw") || !rv.at("raw").is_number()) {
                    continue;
                }

                const std::string as_of = entry.at("asOfDate").get<std::string>();
                const double value = rv.at("raw").get<double>();
                if (value <= 0.0) {
                    continue;
                }

                if (latest.first.empty() || as_of > latest.first) {
                    latest = {as_of, value};
                }
            }
        }
    }

    return latest;
}

template <typename PointT>
std::vector<PointT> extract_timeseries_points(
    const nlohmann::json &result_arr,
    const std::vector<std::string> &candidate_keys,
    const std::string &source_name,
    double min_valid_value = 0.0) {
    std::unordered_map<int, PointT> by_year;

    if (!result_arr.is_array()) {
        return {};
    }

    for (const auto &item : result_arr) {
        if (!item.is_object()) {
            continue;
        }

        for (const auto &key : candidate_keys) {
            if (!item.contains(key) || !item.at(key).is_array()) {
                continue;
            }

            for (const auto &entry : item.at(key)) {
                if (!entry.contains("asOfDate") || !entry.at("asOfDate").is_string() ||
                    !entry.contains("reportedValue")) {
                    continue;
                }

                const auto &rv = entry.at("reportedValue");
                if (!rv.is_object() || !rv.contains("raw") || !rv.at("raw").is_number()) {
                    continue;
                }

                const std::string as_of = entry.at("asOfDate").get<std::string>();
                if (as_of.size() < 4) {
                    continue;
                }

                int year = 0;
                try {
                    year = std::stoi(as_of.substr(0, 4));
                } catch (...) {
                    year = 0;
                }
                if (year <= 0) {
                    continue;
                }

                const double value = rv.at("raw").get<double>();
                if (value <= min_valid_value) {
                    continue;
                }

                auto &slot = by_year[year];
                slot.year = year;
                slot.source = source_name;
                if constexpr (std::is_same_v<PointT, FinancialMetrics::NetProfitPoint>) {
                    slot.net_income = value;
                } else {
                    slot.equity = value;
                }
            }
        }
    }

    std::vector<PointT> rows;
    rows.reserve(by_year.size());
    for (const auto &[year, point] : by_year) {
        (void)year;
        rows.push_back(point);
    }

    std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
        return a.year < b.year;
    });
    return rows;
}

std::filesystem::path net_income_cache_path(const std::string &symbol) {
    auto path = std::filesystem::current_path() / "uploads" / "tmp";
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return path / ("valuation_net_income_" + symbol + ".json");
}

std::filesystem::path quote_metrics_cache_path(const std::string &symbol) {
    auto path = std::filesystem::current_path() / "uploads" / "tmp";
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return path / ("valuation_quote_metrics_" + symbol + ".json");
}

std::string now_iso_utc();

NetIncomeHistoryResult load_net_income_cache(const std::string &symbol, int ttl_hours) {
    NetIncomeHistoryResult out;
    const auto path = net_income_cache_path(symbol);
    std::ifstream f(path);
    if (!f.is_open()) {
        out.error = "cache_not_found";
        return out;
    }

    try {
        nlohmann::json j;
        f >> j;

        const auto now_ts = static_cast<long long>(std::time(nullptr));
        const auto updated_ts = j.value("updated_ts", 0LL);
        if (updated_ts <= 0) {
            out.error = "cache_missing_updated_ts";
            return out;
        }
        const auto age_hours = (now_ts - updated_ts) / 3600;
        if (age_hours > ttl_hours) {
            out.error = "cache_expired";
            return out;
        }

        if (!j.contains("rows") || !j.at("rows").is_array()) {
            out.error = "cache_rows_missing";
            return out;
        }

        for (const auto &row : j.at("rows")) {
            FinancialMetrics::NetProfitPoint p;
            p.year = row.value("year", 0);
            p.net_income = row.value("net_income", 0.0);
            p.source = row.value("source", "cache");
            if (p.year > 0) {
                out.annual_points.push_back(p);
                out.annual_net_income.push_back(p.net_income);
            }
        }

        if (out.annual_points.empty()) {
            out.error = "cache_empty";
            return out;
        }

        if (out.annual_points.size() > 8) {
            out.annual_points.erase(out.annual_points.begin(),
                                    out.annual_points.begin() +
                                        (static_cast<int>(out.annual_points.size()) - 8));
            out.annual_net_income.erase(out.annual_net_income.begin(),
                                        out.annual_net_income.begin() +
                                            (static_cast<int>(out.annual_net_income.size()) - 8));
        }

        out.source = "cache";
        out.updated_at = j.value("updated_at", now_iso_utc());
        out.error.clear();
    } catch (const std::exception &e) {
        out.error = std::string("cache_parse_error: ") + e.what();
    }

    return out;
}

void save_net_income_cache(const std::string &symbol,
                           const NetIncomeHistoryResult &data) {
    if (data.annual_points.empty()) return;

    nlohmann::json j;
    j["symbol"] = symbol;
    j["updated_at"] = data.updated_at.empty() ? now_iso_utc() : data.updated_at;
    j["updated_ts"] = static_cast<long long>(std::time(nullptr));
    j["source"] = data.source;
    j["rows"] = nlohmann::json::array();
    for (const auto &p : data.annual_points) {
        j["rows"].push_back({
            {"year", p.year},
            {"net_income", p.net_income},
            {"source", p.source.empty() ? data.source : p.source}
        });
    }

    const auto path = net_income_cache_path(symbol);
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (ofs.is_open()) {
        ofs << j.dump(2);
    }
}

FinnhubQuoteMetricsResult load_quote_metrics_cache(const std::string &symbol,
                                                   int ttl_hours) {
    FinnhubQuoteMetricsResult out;
    const auto path = quote_metrics_cache_path(symbol);
    std::ifstream f(path);
    if (!f.is_open()) {
        out.error = "quote_metrics_cache_not_found";
        return out;
    }

    try {
        nlohmann::json j;
        f >> j;

        const auto now_ts = static_cast<long long>(std::time(nullptr));
        const auto updated_ts = j.value("updated_ts", 0LL);
        if (updated_ts <= 0) {
            out.error = "quote_metrics_cache_missing_updated_ts";
            return out;
        }

        const auto age_hours = (now_ts - updated_ts) / 3600;
        if (age_hours > ttl_hours) {
            out.error = "quote_metrics_cache_expired";
            return out;
        }

        out.current_price = j.value("current_price", -1.0);
        out.trailing_pe = j.value("trailing_pe", -1.0);
        out.price_to_book = j.value("price_to_book", -1.0);
        out.avg_roe_5y = normalize_ratio_metric(j.value("avg_roe_5y", -1.0));
        out.trailing_eps = j.value("trailing_eps", -1.0);
        out.book_value_per_share = j.value("book_value_per_share", -1.0);
        out.peg_ratio = j.value("peg_ratio", -1.0);
        out.source = "cache";
        out.updated_at = j.value("updated_at", now_iso_utc());
        out.error.clear();
    } catch (const std::exception &e) {
        out.error = std::string("quote_metrics_cache_parse_error: ") + e.what();
    }

    return out;
}

void save_quote_metrics_cache(const std::string &symbol,
                              const FinnhubQuoteMetricsResult &data) {
    if (data.trailing_pe <= 0.0 && data.price_to_book <= 0.0 &&
        data.avg_roe_5y < 0.0 && data.trailing_eps <= 0.0 &&
        data.book_value_per_share <= 0.0) {
        return;
    }

    nlohmann::json j;
    j["symbol"] = symbol;
    j["updated_at"] = data.updated_at.empty() ? now_iso_utc() : data.updated_at;
    j["updated_ts"] = static_cast<long long>(std::time(nullptr));
    j["source"] = data.source;
    j["current_price"] = data.current_price;
    j["trailing_pe"] = data.trailing_pe;
    j["price_to_book"] = data.price_to_book;
    j["avg_roe_5y"] = data.avg_roe_5y;
    j["trailing_eps"] = data.trailing_eps;
    j["book_value_per_share"] = data.book_value_per_share;
    j["peg_ratio"] = data.peg_ratio;

    const auto path = quote_metrics_cache_path(symbol);
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (ofs.is_open()) {
        ofs << j.dump(2);
    }
}

BalanceSheetSnapshot load_balance_sheet_snapshot_from_sample(const std::string &symbol) {
    BalanceSheetSnapshot result;
    const auto path = std::filesystem::current_path().parent_path().parent_path() / "uploads" / "valuecell_samples" /
                      ("value_" + symbol + ".json");
    std::ifstream f(path);
    if (!f.is_open()) {
        result.error = "sample_balance_sheet_not_found";
        return result;
    }

    try {
        nlohmann::json j;
        f >> j;
        result.total_assets = j.value("total_assets", -1.0);
        result.total_liabilities = j.value("total_liabilities", -1.0);
        result.source = "sample_cache";
        result.updated_at = j.value("net_profit_updated_at", now_iso_utc());
        if (result.total_assets <= 0.0 || result.total_liabilities < 0.0) {
            result.error = "sample_balance_sheet_incomplete";
        }
    } catch (const std::exception &) {
        result.error = "sample_balance_sheet_parse_error";
    }

    return result;
}

FinnhubQuoteMetricsResult load_quote_metrics_from_sample(const std::string &symbol) {
    FinnhubQuoteMetricsResult result;
    const auto path = std::filesystem::current_path().parent_path().parent_path() / "uploads" / "valuecell_samples" /
                      ("value_" + symbol + ".json");
    std::ifstream f(path);
    if (!f.is_open()) {
        result.error = "sample_quote_not_found";
        return result;
    }

    try {
        nlohmann::json j;
        f >> j;
        result.current_price = j.value("price", -1.0);
        result.trailing_pe = j.value("current_pe", -1.0);
        result.price_to_book = j.value("price_to_book", -1.0);
        result.book_value_per_share = j.value("book_value_per_share", -1.0);
        result.trailing_eps = j.value("eps", -1.0);
        result.avg_roe_5y = normalize_ratio_metric(j.value("avg_roe_5y", -1.0));
        result.source = "sample_cache";
        result.updated_at = j.value("net_profit_updated_at", now_iso_utc());
        if (result.current_price <= 0.0 && result.trailing_pe <= 0.0 &&
            result.price_to_book <= 0.0 && result.avg_roe_5y < 0.0) {
            result.error = "sample_quote_incomplete";
        }
    } catch (const std::exception &) {
        result.error = "sample_quote_parse_error";
    }

    return result;
}

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
            result.book_value_per_share = extract_double(stat, "bookValue");
        }
        // summaryDetail: trailingPE (主要来源)
        if (entry.contains("summaryDetail")) {
            const auto &sd = entry.at("summaryDetail");
            result.trailing_pe = extract_double(sd, "trailingPE");
            // fallback: 如果 defaultKeyStatistics 没有 forwardPE
            if (result.forward_pe < 0.0)
                result.forward_pe = extract_double(sd, "forwardPE");
        }
        // financialData: earningsGrowth, currentPrice, returnOnEquity
        if (entry.contains("financialData")) {
            const auto &fin = entry.at("financialData");
            result.earnings_growth = extract_double(fin, "earningsGrowth", 0.0);
            result.current_price   = extract_double(fin, "currentPrice", 0.0);
            result.avg_roe_5y      = normalize_ratio_metric(
                extract_double(fin, "returnOnEquity"));
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

NetIncomeHistoryResult fetch_net_income_8y(const std::string &symbol,
                                           int timeout_ms,
                                           const ProxyConfig &proxy,
                                           const YahooCrumb &crumb) {
    NetIncomeHistoryResult result;

    const long long now = static_cast<long long>(std::time(nullptr));
    const long long period2 = now;
    const long long period1 = now - 365LL * 24LL * 3600LL * 10LL;

    const std::string url =
        "https://query1.finance.yahoo.com/ws/fundamentals-timeseries/v1/finance/timeseries/" +
        symbol +
        "?type=annualNetIncome&period1=" + std::to_string(period1) +
        "&period2=" + std::to_string(period2);

    const auto [status, body] = yahoo_get(url, timeout_ms / 1000, proxy, crumb);
    if (status != 200 || body.empty()) {
        result.error = "yahoo_net_income_http_error";
        return result;
    }

    try {
        auto j = nlohmann::json::parse(body);
        if (!j.contains("timeseries") || !j.at("timeseries").contains("result")) {
            result.error = "yahoo_net_income_no_result";
            return result;
        }

        std::vector<std::pair<std::string, double>> rows;
        for (const auto &item : j.at("timeseries").at("result")) {
            if (!item.contains("annualNetIncome") || !item.at("annualNetIncome").is_array()) {
                continue;
            }
            for (const auto &entry : item.at("annualNetIncome")) {
                if (!entry.contains("asOfDate") || !entry.contains("reportedValue")) {
                    continue;
                }
                const std::string as_of = entry.at("asOfDate").get<std::string>();
                const auto &rv = entry.at("reportedValue");
                double v = -1.0;
                if (rv.is_object() && rv.contains("raw") && rv.at("raw").is_number()) {
                    v = rv.at("raw").get<double>();
                }
                if (v > -1e50 && v < 1e50) {
                    rows.push_back({as_of, v});
                }
            }
        }

        if (rows.empty()) {
            result.error = "yahoo_net_income_empty";
            return result;
        }

        std::sort(rows.begin(), rows.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });

        for (const auto &r : rows) {
            FinancialMetrics::NetProfitPoint p;
            p.year = 0;
            if (r.first.size() >= 4) {
                try { p.year = std::stoi(r.first.substr(0, 4)); } catch (...) { p.year = 0; }
            }
            p.net_income = r.second;
            p.source = "yahoo_finance";
            result.annual_points.push_back(p);
            result.annual_net_income.push_back(r.second);
        }

        if (result.annual_net_income.size() > 8) {
            result.annual_net_income.erase(
                result.annual_net_income.begin(),
                result.annual_net_income.begin() +
                    (static_cast<int>(result.annual_net_income.size()) - 8));
            result.annual_points.erase(
                result.annual_points.begin(),
                result.annual_points.begin() +
                    (static_cast<int>(result.annual_points.size()) - 8));
        }

        result.source = "yahoo_finance";
        result.updated_at = now_iso_utc();

    } catch (const std::exception &e) {
        result.error = std::string("yahoo_net_income_parse_error: ") + e.what();
    }

    return result;
}

NetIncomeHistoryResult fetch_net_income_8y_alpha(const std::string &symbol,
                                                 const ValuationConfig &config,
                                                 const ProxyConfig &proxy) {
    NetIncomeHistoryResult result;
    if (alpha_rate_limited_active()) {
        result.error = "alpha_vantage_rate_limited";
        return result;
    }
    if (config.alpha_vantage_api_key.empty()) {
        result.error = "alpha_vantage_api_key_not_configured";
        return result;
    }

    const std::string url =
        "https://www.alphavantage.co/query?function=INCOME_STATEMENT&symbol=" + symbol +
        "&apikey=" + config.alpha_vantage_api_key;

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) {
            const int delay_sec = 12 * attempt; // 12s, 24s
            std::this_thread::sleep_for(std::chrono::seconds(delay_sec));
        }

        const auto [status, body] = curl_get(url, config.yahoo_timeout_ms / 1000, proxy);

        if (status != 200 || body.empty()) {
            if (attempt == 2) {
                result.error = "alpha_income_statement_http_error";
                return result;
            }
            continue;
        }

        try {
            auto j = nlohmann::json::parse(body);

            if (j.contains("Error Message") && j.at("Error Message").is_string()) {
                result.error = "alpha_income_statement_error_message";
                return result;
            }

            if (j.contains("Note") && j.at("Note").is_string()) {
                mark_alpha_rate_limited();
                result.error = "alpha_vantage_rate_limited";
                return result;
            }

            if (j.contains("Information") && j.at("Information").is_string()) {
                mark_alpha_rate_limited();
                result.error = "alpha_vantage_rate_limited";
                return result;
            }

            auto parse_row_points = [](const nlohmann::json &arr,
                                       const std::string &source_name)
                -> std::vector<FinancialMetrics::NetProfitPoint> {
                std::vector<FinancialMetrics::NetProfitPoint> rows;
                if (!arr.is_array()) {
                    return rows;
                }

                for (const auto &row : arr) {
                    const std::string date = row.value("fiscalDateEnding", "");
                    const std::string net_income_str = row.value("netIncome", "");
                    if (date.size() < 4 || net_income_str.empty() || net_income_str == "None") {
                        continue;
                    }

                    FinancialMetrics::NetProfitPoint p;
                    try { p.year = std::stoi(date.substr(0, 4)); } catch (...) { p.year = 0; }
                    try { p.net_income = std::stod(net_income_str); } catch (...) { p.net_income = 0.0; }
                    p.source = source_name;

                    if (p.year > 0 && std::abs(p.net_income) > 0.0) {
                        rows.push_back(p);
                    }
                }
                return rows;
            };

            std::vector<FinancialMetrics::NetProfitPoint> rows;

            if (j.contains("annualReports") && j.at("annualReports").is_array()) {
                rows = parse_row_points(j.at("annualReports"), "alpha_vantage");
            }

            if (rows.empty() && j.contains("quarterlyReports") && j.at("quarterlyReports").is_array()) {
                const auto q_rows = parse_row_points(j.at("quarterlyReports"), "alpha_vantage_quarterly");
                std::unordered_map<int, std::pair<double, int>> year_agg;
                for (const auto &q : q_rows) {
                    auto &slot = year_agg[q.year];
                    slot.first += q.net_income;
                    slot.second += 1;
                }

                for (const auto &it : year_agg) {
                    FinancialMetrics::NetProfitPoint p;
                    p.year = it.first;
                    p.net_income = it.second.first;
                    p.source = "alpha_vantage_quarterly";
                    rows.push_back(p);
                }
            }

            if (rows.empty()) {
                if (attempt == 2) {
                    result.error = "alpha_income_statement_empty";
                    return result;
                }
                continue;
            }

            std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
                return a.year < b.year;
            });
            if (rows.size() > 8) {
                rows.erase(rows.begin(), rows.begin() + (static_cast<int>(rows.size()) - 8));
            }

            result.annual_points = rows;
            for (const auto &p : rows) {
                result.annual_net_income.push_back(p.net_income);
            }
            result.source = "alpha_vantage";
            result.updated_at = now_iso_utc();
            return result;
        } catch (const std::exception &) {
            if (attempt == 2) {
                result.error = "alpha_income_statement_parse_error";
                return result;
            }
            continue;
        }
    }

    result.error = "alpha_income_statement_retry_exhausted";

    return result;
}

NetIncomeHistoryResult fetch_net_income_8y_finnhub(const std::string &symbol,
                                                   const ValuationConfig &config,
                                                   const ProxyConfig &proxy) {
    NetIncomeHistoryResult result;
    if (config.finnhub_api_key.empty()) {
        result.error = "finnhub_api_key_not_configured";
        return result;
    }

    const std::string url =
        "https://finnhub.io/api/v1/stock/financials-reported?symbol=" + symbol +
        "&freq=annual&token=" + config.finnhub_api_key;

    const auto [status, body] = curl_get(url, config.yahoo_timeout_ms / 1000, proxy);
    if (status == 429) {
        result.error = "finnhub_rate_limited";
        return result;
    }
    if (status != 200 || body.empty()) {
        result.error = "finnhub_financials_http_error";
        return result;
    }

    try {
        auto j = nlohmann::json::parse(body);
        if (j.contains("error") && j.at("error").is_string() &&
            !j.at("error").get<std::string>().empty()) {
            result.error = "finnhub_financials_error";
            return result;
        }

        if (!j.contains("data") || !j.at("data").is_array()) {
            result.error = "finnhub_financials_no_data";
            return result;
        }

        auto parse_number = [](const nlohmann::json &v) -> double {
            if (v.is_number()) return v.get<double>();
            if (v.is_string()) {
                const auto s = v.get<std::string>();
                if (s.empty() || s == "None" || s == "null" || s == "-") return 0.0;
                try { return std::stod(s); } catch (...) { return 0.0; }
            }
            return 0.0;
        };

        auto extract_net_income = [&](const nlohmann::json &ic) -> double {
            static const std::array<const char *, 4> kCandidates = {
                "NetIncomeLoss",
                "ProfitLoss",
                "NetIncomeLossAvailableToCommonStockholdersBasic",
                "NetIncomeLossAttributableToParent"
            };
            for (const auto *key : kCandidates) {
                if (ic.contains(key)) {
                    const double v = parse_number(ic.at(key));
                    if (std::abs(v) > 0.0) return v;
                }
            }
            return 0.0;
        };

        std::unordered_map<int, std::pair<std::string, double>> by_year;
        for (const auto &item : j.at("data")) {
            int year = 0;
            if (item.contains("year") && item.at("year").is_number_integer()) {
                year = item.at("year").get<int>();
            }
            if (year <= 0 && item.contains("endDate") && item.at("endDate").is_string()) {
                const auto end_date = item.at("endDate").get<std::string>();
                if (end_date.size() >= 4) {
                    try { year = std::stoi(end_date.substr(0, 4)); } catch (...) { year = 0; }
                }
            }
            if (year <= 0) continue;

            if (!item.contains("report") || !item.at("report").is_object()) continue;
            const auto &report = item.at("report");
            if (!report.contains("ic") || !report.at("ic").is_object()) continue;

            const double net_income = extract_net_income(report.at("ic"));
            if (std::abs(net_income) <= 0.0) continue;

            std::string end_date = "";
            if (item.contains("endDate") && item.at("endDate").is_string()) {
                end_date = item.at("endDate").get<std::string>();
            }

            auto it = by_year.find(year);
            if (it == by_year.end() || end_date > it->second.first) {
                by_year[year] = {end_date, net_income};
            }
        }

        std::vector<FinancialMetrics::NetProfitPoint> rows;
        rows.reserve(by_year.size());
        for (const auto &[year, data] : by_year) {
            FinancialMetrics::NetProfitPoint p;
            p.year = year;
            p.net_income = data.second;
            p.source = "finnhub";
            rows.push_back(p);
        }

        if (rows.empty()) {
            result.error = "finnhub_financials_empty";
            return result;
        }

        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
            return a.year < b.year;
        });
        if (rows.size() > 8) {
            rows.erase(rows.begin(), rows.begin() + (static_cast<int>(rows.size()) - 8));
        }

        result.annual_points = rows;
        for (const auto &p : rows) {
            result.annual_net_income.push_back(p.net_income);
        }
        result.source = "finnhub";
        result.updated_at = now_iso_utc();
    } catch (const std::exception &) {
        result.error = "finnhub_financials_parse_error";
    }

    return result;
}

FinnhubQuoteMetricsResult fetch_quote_metrics_finnhub(const std::string &symbol,
                                                      const ValuationConfig &config,
                                                      const ProxyConfig &proxy) {
    FinnhubQuoteMetricsResult result;
    if (config.finnhub_api_key.empty()) {
        result.error = "finnhub_api_key_not_configured";
        return result;
    }

    const auto cached_metric = load_quote_metrics_cache(symbol, config.net_income_cache_ttl_hours);
    const bool has_metric_cache =
        cached_metric.error.empty() &&
        (cached_metric.trailing_pe > 0.0 || cached_metric.price_to_book > 0.0 ||
         cached_metric.avg_roe_5y >= 0.0 || cached_metric.trailing_eps > 0.0 ||
         cached_metric.book_value_per_share > 0.0);

    const std::string quote_url =
        "https://finnhub.io/api/v1/quote?symbol=" + symbol +
        "&token=" + config.finnhub_api_key;
    const std::string metric_url =
        "https://finnhub.io/api/v1/stock/metric?symbol=" + symbol +
        "&metric=all&token=" + config.finnhub_api_key;

    const int quote_timeout_sec =
        (std::max)(3, config.yahoo_timeout_ms / 1000);
    const int metric_timeout_sec =
        (std::max)(15, config.yahoo_timeout_ms / 1000);

    const auto [quote_status, quote_body] =
        curl_get(quote_url, quote_timeout_sec, proxy);
    if (quote_status == 429) {
        result.error = "finnhub_rate_limited";
        return result;
    }
    if (quote_status != 200 || quote_body.empty()) {
        result.error = "finnhub_quote_http_error";
        return result;
    }

    try {
        auto quote_json = nlohmann::json::parse(quote_body);
        const auto parse_quote_double = [&](const std::string &key) -> double {
            if (!quote_json.contains(key)) return -1.0;
            const auto &v = quote_json.at(key);
            if (v.is_number()) return v.get<double>();
            if (v.is_string()) {
                try { return std::stod(v.get<std::string>()); } catch (...) { return -1.0; }
            }
            return -1.0;
        };

        result.current_price = parse_quote_double("c");
        if (result.current_price <= 0.0) {
            result.current_price = parse_quote_double("pc");
        }
    } catch (const std::exception &) {
        result.error = "finnhub_quote_parse_error";
        return result;
    }

    if (has_metric_cache) {
        result.trailing_pe = cached_metric.trailing_pe;
        result.price_to_book = cached_metric.price_to_book;
        result.avg_roe_5y = cached_metric.avg_roe_5y;
        result.trailing_eps = cached_metric.trailing_eps;
        result.book_value_per_share = cached_metric.book_value_per_share;
        result.peg_ratio = cached_metric.peg_ratio;
        result.source = "finnhub";
        result.updated_at = now_iso_utc();
        result.error.clear();
        return result;
    }

    const auto [metric_status, metric_body] =
        curl_get(metric_url, metric_timeout_sec, proxy);

    try {
        nlohmann::json metric_json;
        bool metric_available = false;
        if (metric_status == 429) {
            result.error = "finnhub_rate_limited";
        } else if (metric_status == 200 && !metric_body.empty()) {
            metric_json = nlohmann::json::parse(metric_body);
            if (metric_json.contains("error") && metric_json.at("error").is_string() &&
                !metric_json.at("error").get<std::string>().empty()) {
                result.error = "finnhub_metric_error";
            } else {
                metric_available = true;
            }
        } else {
            result.error = "finnhub_metric_http_error";
        }

        const auto parse_metric_double = [&](const std::string &key) -> double {
            if (!metric_available) return -1.0;
            if (!metric_json.contains("metric") || !metric_json.at("metric").is_object()) {
                return -1.0;
            }
            const auto &metric = metric_json.at("metric");
            if (!metric.contains(key)) return -1.0;
            const auto &v = metric.at(key);
            if (v.is_number()) return v.get<double>();
            if (v.is_string()) {
                const auto s = v.get<std::string>();
                if (s.empty() || s == "None" || s == "null" || s == "-") return -1.0;
                try { return std::stod(s); } catch (...) { return -1.0; }
            }
            return -1.0;
        };

        result.trailing_pe = parse_metric_double("peTTM");
        if (result.trailing_pe <= 0.0) {
            result.trailing_pe = parse_metric_double("peBasicExclExtraTTM");
        }
        result.price_to_book = parse_metric_double("pb");
        if (result.price_to_book <= 0.0) {
            result.price_to_book = parse_metric_double("pbAnnual");
        }
        result.avg_roe_5y = normalize_ratio_metric(parse_metric_double("roe5Y"));
        if (result.avg_roe_5y < 0.0) {
            result.avg_roe_5y = normalize_ratio_metric(parse_metric_double("roeTTM"));
        }
        result.trailing_eps = parse_metric_double("epsTTM");
        if (result.trailing_eps <= 0.0) {
            result.trailing_eps = parse_metric_double("epsBasicExclExtraItemsTTM");
        }
        result.book_value_per_share = parse_metric_double("bookValuePerShareAnnual");
        if (result.book_value_per_share <= 0.0) {
            result.book_value_per_share = parse_metric_double("bookValuePerShareQuarterly");
        }
        result.peg_ratio = parse_metric_double("pegTTM");
        result.source = "finnhub";
        result.updated_at = now_iso_utc();

        if (result.current_price <= 0.0 && result.trailing_pe <= 0.0 &&
            result.price_to_book <= 0.0 && result.avg_roe_5y < 0.0) {
            result.error = "finnhub_metric_empty";
        } else if (result.current_price > 0.0 && !metric_available) {
            result.error = "finnhub_metric_http_error";
        } else {
            save_quote_metrics_cache(symbol, result);
        }
    } catch (const std::exception &) {
        if (has_metric_cache) {
            result.trailing_pe = cached_metric.trailing_pe;
            result.price_to_book = cached_metric.price_to_book;
            result.avg_roe_5y = cached_metric.avg_roe_5y;
            result.trailing_eps = cached_metric.trailing_eps;
            result.book_value_per_share = cached_metric.book_value_per_share;
            result.peg_ratio = cached_metric.peg_ratio;
            result.source = "finnhub";
            result.updated_at = now_iso_utc();
            result.error = "finnhub_metric_cache_fallback";
        } else if (result.current_price > 0.0) {
            result.error = "finnhub_metric_parse_error";
        } else {
            result.error = "finnhub_quote_and_metric_parse_error";
        }
    }

    return result;
}

EquityHistoryResult fetch_equity_history_yahoo(const std::string &symbol,
                                               int timeout_ms,
                                               const ProxyConfig &proxy,
                                               const YahooCrumb &crumb) {
    EquityHistoryResult result;

    const long long now = static_cast<long long>(std::time(nullptr));
    const long long period2 = now;
    const long long period1 = now - 365LL * 24LL * 3600LL * 10LL;

    const std::string url =
        "https://query1.finance.yahoo.com/ws/fundamentals-timeseries/v1/finance/timeseries/" +
        symbol +
        "?type=annualStockholdersEquity,annualCommonStockEquity,annualTotalEquityGrossMinorityInterest"
        "&period1=" + std::to_string(period1) +
        "&period2=" + std::to_string(period2);

    const auto [status, body] = yahoo_get(url, timeout_ms / 1000, proxy, crumb);
    if (status != 200 || body.empty()) {
        result.error = "yahoo_equity_history_http_error";
        return result;
    }

    try {
        auto j = nlohmann::json::parse(body);
        if (!j.contains("timeseries") || !j.at("timeseries").contains("result")) {
            result.error = "yahoo_equity_history_no_result";
            return result;
        }

        result.annual_points = extract_timeseries_points<FinancialMetrics::EquityPoint>(
            j.at("timeseries").at("result"),
            {
                "annualStockholdersEquity",
                "annualCommonStockEquity",
                "annualTotalEquityGrossMinorityInterest",
            },
            "yahoo_finance");

        if (result.annual_points.empty()) {
            result.error = "yahoo_equity_history_empty";
            return result;
        }

        if (result.annual_points.size() > 8) {
            result.annual_points.erase(
                result.annual_points.begin(),
                result.annual_points.begin() +
                    (static_cast<int>(result.annual_points.size()) - 8));
        }

        result.source = "yahoo_finance";
        result.updated_at = now_iso_utc();
    } catch (const std::exception &) {
        result.error = "yahoo_equity_history_parse_error";
    }

    return result;
}

EquityHistoryResult fetch_equity_history_alpha(const std::string &symbol,
                                               const ValuationConfig &config,
                                               const ProxyConfig &proxy) {
    EquityHistoryResult result;
    if (config.alpha_vantage_api_key.empty()) {
        result.error = "alpha_equity_history_api_key_not_configured";
        return result;
    }

    const std::string url =
        "https://www.alphavantage.co/query?function=BALANCE_SHEET&symbol=" + symbol +
        "&apikey=" + config.alpha_vantage_api_key;

    const auto [status, body] = curl_get(url, config.yahoo_timeout_ms / 1000, proxy);
    if (status != 200 || body.empty()) {
        result.error = "alpha_equity_history_http_error";
        return result;
    }

    try {
        auto j = nlohmann::json::parse(body);
        if (j.contains("Error Message") || j.contains("Information")) {
            result.error = "alpha_equity_history_error";
            return result;
        }
        if (!j.contains("annualReports") || !j.at("annualReports").is_array() ||
            j.at("annualReports").empty()) {
            result.error = "alpha_equity_history_empty";
            return result;
        }

        for (const auto &row : j.at("annualReports")) {
            const std::string date = row.value("fiscalDateEnding", "");
            if (date.size() < 4) {
                continue;
            }

            int year = 0;
            try {
                year = std::stoi(date.substr(0, 4));
            } catch (...) {
                year = 0;
            }
            if (year <= 0) {
                continue;
            }

            auto parse_row_double = [&](const std::string &key) -> double {
                if (!row.contains(key)) return -1.0;
                const auto &v = row.at(key);
                if (v.is_number()) return v.get<double>();
                if (v.is_string()) {
                    const auto s = v.get<std::string>();
                    if (s.empty() || s == "None" || s == "-") return -1.0;
                    try { return std::stod(s); } catch (...) { return -1.0; }
                }
                return -1.0;
            };

            double equity = parse_row_double("totalShareholderEquity");
            if (equity <= 0.0) {
                equity = parse_row_double("totalStockholderEquity");
            }
            if (equity <= 0.0) {
                const double assets = parse_row_double("totalAssets");
                const double liabilities = parse_row_double("totalLiabilities");
                if (assets > 0.0 && liabilities >= 0.0 && assets > liabilities) {
                    equity = assets - liabilities;
                }
            }
            if (equity <= 0.0) {
                continue;
            }

            FinancialMetrics::EquityPoint point;
            point.year = year;
            point.equity = equity;
            point.source = "alpha_vantage";
            result.annual_points.push_back(point);
        }

        if (result.annual_points.empty()) {
            result.error = "alpha_equity_history_incomplete";
            return result;
        }

        std::sort(result.annual_points.begin(), result.annual_points.end(),
                  [](const auto &a, const auto &b) { return a.year < b.year; });
        if (result.annual_points.size() > 8) {
            result.annual_points.erase(
                result.annual_points.begin(),
                result.annual_points.begin() +
                    (static_cast<int>(result.annual_points.size()) - 8));
        }

        result.source = "alpha_vantage";
        result.updated_at = now_iso_utc();
    } catch (const std::exception &) {
        result.error = "alpha_equity_history_parse_error";
    }

    return result;
}

BalanceSheetSnapshot fetch_balance_sheet_snapshot_yahoo(
    const std::string &symbol,
    int timeout_ms,
    const ProxyConfig &proxy,
    const YahooCrumb &crumb) {
    BalanceSheetSnapshot result;

    const long long now = static_cast<long long>(std::time(nullptr));
    const long long period2 = now;
    const long long period1 = now - 365LL * 24LL * 3600LL * 10LL;

    const std::string url =
        "https://query1.finance.yahoo.com/ws/fundamentals-timeseries/v1/finance/timeseries/" +
        symbol +
        "?type=annualTotalAssets,annualTotalLiabilitiesNetMinorityInterest,annualTotalLiab" +
        "&period1=" + std::to_string(period1) +
        "&period2=" + std::to_string(period2);

    const auto [status, body] = yahoo_get(url, timeout_ms / 1000, proxy, crumb);
    if (status != 200 || body.empty()) {
        result.error = "yahoo_balance_sheet_http_error";
        return result;
    }

    try {
        auto j = nlohmann::json::parse(body);
        if (!j.contains("timeseries") || !j.at("timeseries").contains("result")) {
            result.error = "yahoo_balance_sheet_no_result";
            return result;
        }

        const auto &result_arr = j.at("timeseries").at("result");
        const auto latest_assets = extract_latest_timeseries_value(
            result_arr, {"annualTotalAssets"});
        const auto latest_liabilities = extract_latest_timeseries_value(
            result_arr,
            {"annualTotalLiabilitiesNetMinorityInterest", "annualTotalLiab"});

        result.total_assets = latest_assets.second;
        result.total_liabilities = latest_liabilities.second;
        result.source = "yahoo_finance";
        result.updated_at =
            !latest_assets.first.empty() ? latest_assets.first : now_iso_utc();

        if (result.total_assets <= 0.0 || result.total_liabilities < 0.0) {
            result.error = "yahoo_balance_sheet_incomplete";
        }
    } catch (const std::exception &) {
        result.error = "yahoo_balance_sheet_parse_error";
    }

    return result;
}

BalanceSheetSnapshot fetch_balance_sheet_snapshot_alpha(
    const std::string &symbol,
    const ValuationConfig &config,
    const ProxyConfig &proxy) {
    BalanceSheetSnapshot result;
    if (alpha_rate_limited_active()) {
        result.error = "alpha_vantage_rate_limited";
        return result;
    }
    if (config.alpha_vantage_api_key.empty()) {
        result.error = "alpha_balance_sheet_api_key_not_configured";
        return result;
    }

    const std::string url =
        "https://www.alphavantage.co/query?function=BALANCE_SHEET&symbol=" + symbol +
        "&apikey=" + config.alpha_vantage_api_key;

    const auto [status, body] = curl_get(url, config.yahoo_timeout_ms / 1000, proxy);
    if (status != 200 || body.empty()) {
        result.error = "alpha_balance_sheet_http_error";
        return result;
    }

    try {
        auto j = nlohmann::json::parse(body);
        if (j.contains("Error Message") || j.contains("Information")) {
            if (j.contains("Information")) {
                mark_alpha_rate_limited();
            }
            result.error = "alpha_balance_sheet_error";
            return result;
        }
        if (!j.contains("annualReports") || !j.at("annualReports").is_array() ||
            j.at("annualReports").empty()) {
            result.error = "alpha_balance_sheet_empty";
            return result;
        }

        const auto &latest = j.at("annualReports").at(0);
        auto parse_av_double = [&](const std::string &key) -> double {
            if (!latest.contains(key)) return -1.0;
            const auto &v = latest.at(key);
            if (v.is_number()) return v.get<double>();
            if (v.is_string()) {
                const auto s = v.get<std::string>();
                if (s.empty() || s == "None" || s == "-") return -1.0;
                try { return std::stod(s); } catch (...) { return -1.0; }
            }
            return -1.0;
        };

        result.total_assets = parse_av_double("totalAssets");
        result.total_liabilities = parse_av_double("totalLiabilities");
        result.source = "alpha_vantage";
        result.updated_at = latest.value("fiscalDateEnding", now_iso_utc());

        if (result.total_assets <= 0.0 || result.total_liabilities < 0.0) {
            result.error = "alpha_balance_sheet_incomplete";
        }
    } catch (const std::exception &) {
        result.error = "alpha_balance_sheet_parse_error";
    }

    return result;
}

BalanceSheetSnapshot fetch_balance_sheet_snapshot_finnhub(
    const std::string &symbol,
    const ValuationConfig &config,
    const ProxyConfig &proxy) {
    BalanceSheetSnapshot result;
    if (config.finnhub_api_key.empty()) {
        result.error = "finnhub_api_key_not_configured";
        return result;
    }

    const std::string url =
        "https://finnhub.io/api/v1/stock/financials-reported?symbol=" + symbol +
        "&freq=annual&token=" + config.finnhub_api_key;

    const auto [status, body] = curl_get(url, config.yahoo_timeout_ms / 1000, proxy);
    if (status == 429) {
        result.error = "finnhub_rate_limited";
        return result;
    }
    if (status != 200 || body.empty()) {
        result.error = "finnhub_balance_sheet_http_error";
        return result;
    }

    try {
        auto j = nlohmann::json::parse(body);
        if (j.contains("error") && j.at("error").is_string() &&
            !j.at("error").get<std::string>().empty()) {
            result.error = "finnhub_balance_sheet_error";
            return result;
        }
        if (!j.contains("data") || !j.at("data").is_array() || j.at("data").empty()) {
            result.error = "finnhub_balance_sheet_empty";
            return result;
        }

        const auto parse_number = [](const nlohmann::json &v) -> double {
            if (v.is_number()) return v.get<double>();
            if (v.is_string()) {
                const auto s = v.get<std::string>();
                if (s.empty() || s == "None" || s == "null" || s == "-") return -1.0;
                try { return std::stod(s); } catch (...) { return -1.0; }
            }
            return -1.0;
        };

        const auto extract_bs_value = [&](const nlohmann::json &bs,
                                          const std::vector<std::string> &concepts) -> double {
            if (!bs.is_array()) return -1.0;
            for (const auto &item : bs) {
                if (!item.is_object()) continue;
                const std::string concept = item.value("concept", "");
                for (const auto &candidate : concepts) {
                    if (concept == candidate) {
                        if (item.contains("value")) {
                            return parse_number(item.at("value"));
                        }
                    }
                }
            }
            return -1.0;
        };

        const auto &latest = j.at("data").at(0);
        if (!latest.contains("report") || !latest.at("report").is_object() ||
            !latest.at("report").contains("bs")) {
            result.error = "finnhub_balance_sheet_empty";
            return result;
        }

        const auto &bs = latest.at("report").at("bs");
        result.total_assets = extract_bs_value(bs, {
            "us-gaap_Assets"
        });
        result.total_liabilities = extract_bs_value(bs, {
            "us-gaap_Liabilities"
        });
        result.source = "finnhub";
        result.updated_at = latest.value("endDate", now_iso_utc());

        if (result.total_assets <= 0.0 || result.total_liabilities < 0.0 ||
            result.total_liabilities > result.total_assets) {
            result.error = "finnhub_balance_sheet_incomplete";
        }
    } catch (const std::exception &) {
        result.error = "finnhub_balance_sheet_parse_error";
    }

    return result;
}

} // namespace (anonymous)

void recompute_financial_metrics(FinancialMetrics &metrics) {
    metrics.eps = metrics.trailing_eps;
    metrics.price = metrics.price > 0.0 ? metrics.price : -1.0;
    metrics.avg_roe_5y = normalize_ratio_metric(metrics.avg_roe_5y);
    metrics.valuation_label = "unknown";

    auto apply_peg_label = [&]() {
        if (metrics.peg > 0.0 && metrics.peg < 1.0) {
            metrics.valuation_label = "undervalued";
        } else if (metrics.peg >= 1.0 && metrics.peg <= 2.0) {
            metrics.valuation_label = "fair";
        } else if (metrics.peg > 2.0) {
            metrics.valuation_label = "overvalued";
        }
    };

    metrics.pe = -1.0;
    if (metrics.trailing_pe > 0.0) {
        metrics.pe = metrics.trailing_pe;
    } else if (metrics.eps <= 0.0) {
        push_warning_once(metrics, "invalid_eps");
    } else if (metrics.price > 0.0) {
        metrics.pe = metrics.price / metrics.eps;
    }

    if (metrics.total_assets > 0.0 && metrics.total_liabilities >= 0.0) {
        metrics.debt_ratio = metrics.total_liabilities / metrics.total_assets;
        if (metrics.debt_ratio > 0.50) {
            push_warning_once(metrics, "high_debt_ratio");
        }
    } else {
        metrics.debt_ratio = -1.0;
        push_warning_once(metrics, "missing_debt_ratio");
    }

    if (metrics.avg_roe_5y < 0.0) {
        push_warning_once(metrics, "missing_roe_history");
    }

    const auto &np = metrics.net_profit_8y;
    const int n = static_cast<int>(np.size());

    if (n < 8) {
        push_warning_once(metrics, "insufficient_net_profit_history");

        if (n >= 2) {
            std::vector<double> yoy_growth;
            yoy_growth.reserve(static_cast<std::size_t>(n - 1));

            for (int i = 1; i < n; ++i) {
                const double prev = np[i - 1];
                const double curr = np[i];
                if (prev > 0.0) {
                    yoy_growth.push_back((curr - prev) / prev);
                }
            }

            if (!yoy_growth.empty()) {
                const double yoy_sum = std::accumulate(yoy_growth.begin(), yoy_growth.end(), 0.0);
                metrics.growth_5y_cagr = yoy_sum / static_cast<double>(yoy_growth.size());
                push_warning_once(metrics, "growth_fallback_avg_yoy_used");

                if (metrics.growth_5y_cagr <= 0.0) {
                    push_warning_once(metrics, "non_positive_growth");
                }
                if (metrics.growth_5y_cagr < 0.10) {
                    push_warning_once(metrics, "low_growth_5y_cagr");
                }

                if (metrics.growth_5y_cagr > 0.0 && metrics.pe > 0.0) {
                    metrics.peg = metrics.pe / (metrics.growth_5y_cagr * 100.0);
                    apply_peg_label();
                }
            } else {
                push_warning_once(metrics, "invalid_growth_base");
            }
        }

        return;
    }

    metrics.np_recent3_avg = (np[n - 3] + np[n - 2] + np[n - 1]) / 3.0;
    metrics.np_past3_avg = (np[n - 8] + np[n - 7] + np[n - 6]) / 3.0;

    if (metrics.np_past3_avg <= 0.0) {
        push_warning_once(metrics, "invalid_growth_base");
        return;
    }

    metrics.growth_5y_cagr =
        std::pow(metrics.np_recent3_avg / metrics.np_past3_avg, 1.0 / 5.0) - 1.0;

    if (metrics.growth_5y_cagr <= 0.0) {
        push_warning_once(metrics, "non_positive_growth");
    }
    if (metrics.growth_5y_cagr < 0.10) {
        push_warning_once(metrics, "low_growth_5y_cagr");
    }

    if (metrics.growth_5y_cagr <= 0.0) {
        return;
    }

    if (metrics.pe > 0.0) {
        metrics.peg = metrics.pe / (metrics.growth_5y_cagr * 100.0);
    }
    apply_peg_label();
}

namespace {

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
    if (alpha_rate_limited_active()) {
        result.error = "alpha_vantage_rate_limited";
        return result;
    }

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
        if (j.contains("Information") || j.contains("Note")) {
            mark_alpha_rate_limited();
            result.error = "alpha_vantage_rate_limited";
            return result;
        }
        if (j.contains("Error Message")) {
            result.error = "alpha_vantage_error";
            return result;
        }

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
        for (const auto &roe_key : {"ReturnOnEquity5YearAvg", "ReturnOnEquity5YearAverage",
                                    "ReturnOnEquity5Y", "ReturnOnEquityTTM"}) {
            const double roe = normalize_ratio_metric(parse_av_double(roe_key));
            if (roe >= 0.0) {
                result.avg_roe_5y = roe;
                break;
            }
        }
        result.trailing_eps    = parse_av_double("EPS");
        result.book_value_per_share = parse_av_double("BookValue");
        result.earnings_growth = parse_av_double("RevenueGrowthTTMYOY");
        result.has_sufficient_data = (result.trailing_pe > 0.0) && (result.peg_ratio > 0.0);
        result.data_source = "alpha_vantage";
        result.warnings.push_back("alpha_vantage_fallback_used");

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
    metrics.symbol = normalize_symbol_alias(symbol);
    const std::string effective_symbol = metrics.symbol;
    if (effective_symbol != symbol) {
        spdlog::info("[ValuationClient] Normalize symbol {} -> {}", symbol, effective_symbol);
    }

    const ProxyConfig proxy = parse_proxy(config.http_proxy);
    if (proxy.enabled) {
        spdlog::info("[ValuationClient] Using proxy {}:{} for {}", proxy.host, proxy.port, effective_symbol);
    }

    // Step 0: 获取 Yahoo crumb & cookie（带静态缓存，25 分钟有效）
    const auto crumb = get_or_refresh_crumb(config.yahoo_timeout_ms / 1000, proxy);
    const bool yahoo_available = crumb.valid;
    if (!crumb.valid) {
        spdlog::warn("[ValuationClient] Yahoo crumb failed, will try anyway");
        push_warning_once(metrics, "yahoo_crumb_unavailable");
    }

    // Step 1: 估值主链（按可配置优先级）
    bool quote_ok = false;
    auto merge_quote_fields = [&](const std::string &source_name,
                                  double price,
                                  double trailing_pe,
                                  double peg_ratio,
                                  double price_to_book,
                                  double trailing_eps,
                                  double avg_roe_5y,
                                  double book_value_per_share) {
        bool filled_any = false;
        if (metrics.price <= 0.0 && price > 0.0) {
            metrics.price = price;
            filled_any = true;
        }
        if (metrics.trailing_pe <= 0.0 && trailing_pe > 0.0) {
            metrics.trailing_pe = trailing_pe;
            filled_any = true;
        }
        if (metrics.peg_ratio <= 0.0 && peg_ratio > 0.0) {
            metrics.peg_ratio = peg_ratio;
            filled_any = true;
        }
        if (metrics.price_to_book <= 0.0 && price_to_book > 0.0) {
            metrics.price_to_book = price_to_book;
            filled_any = true;
        }
        if (metrics.trailing_eps <= 0.0 && trailing_eps > 0.0) {
            metrics.trailing_eps = trailing_eps;
            filled_any = true;
        }
        if (metrics.avg_roe_5y < 0.0 && avg_roe_5y >= 0.0) {
            metrics.avg_roe_5y = avg_roe_5y;
            filled_any = true;
        }
        if (metrics.book_value_per_share <= 0.0 && book_value_per_share > 0.0) {
            metrics.book_value_per_share = book_value_per_share;
            filled_any = true;
        }
        if (filled_any && metrics.data_source.empty()) {
            metrics.data_source = source_name;
        }
        if (filled_any && metrics.data_source != source_name) {
            push_warning_once(metrics, "quote_source_" + source_name);
        }
    };

    auto has_main_quote_fields = [&]() {
        return metrics.trailing_pe > 0.0 &&
               metrics.price_to_book > 0.0 &&
               metrics.avg_roe_5y >= 0.0 &&
               metrics.price > 0.0;
    };

    for (const auto &source : config.us_stocks_data_source_priority) {
        if (source == "yahoo_finance") {
            if (!yahoo_available) {
                continue;
            }
            const auto qs = fetch_quote_summary(effective_symbol, config.yahoo_timeout_ms, proxy, crumb);
            if (!qs.error.empty()) {
                metrics.warnings.push_back(qs.error);
                continue;
            }

            metrics.earnings_growth = qs.earnings_growth;
            metrics.forward_pe      = qs.forward_pe;
            merge_quote_fields("yahoo_finance", qs.current_price, qs.trailing_pe,
                               qs.peg_ratio, qs.price_to_book, qs.trailing_eps,
                               qs.avg_roe_5y, qs.book_value_per_share);
            quote_ok = quote_ok || has_main_quote_fields();
            if (has_main_quote_fields()) {
                break;
            }
            continue;
        }

        if (source == "alpha_vantage") {
            const auto av = fetch_from_alpha_vantage(effective_symbol, config, proxy);
            if (!av.error.empty()) {
                metrics.warnings.push_back(av.error);
                continue;
            }
            metrics.forward_pe = av.forward_pe;
            metrics.earnings_growth = av.earnings_growth;
                // AlphaVantage OVERVIEW may not include a current price; attempt to fetch GLOBAL_QUOTE
                double alpha_price = av.price;
                if (alpha_price <= 0.0) {
                    try {
                        const auto [qstatus, qbody] = curl_get(
                            "https://www.alphavantage.co/query?function=GLOBAL_QUOTE&symbol=" + effective_symbol + "&apikey=" + config.alpha_vantage_api_key,
                            config.yahoo_timeout_ms / 1000, proxy);
                        if (qstatus == 200 && !qbody.empty()) {
                            auto jq = nlohmann::json::parse(qbody);
                            if (jq.contains("Global Quote") && jq.at("Global Quote").contains("05. price")) {
                                const auto sprice = jq.at("Global Quote").at("05. price").get<std::string>();
                                try { alpha_price = std::stod(sprice); }
                                catch (...) { /* ignore parse errors */ }
                            }
                        }
                    } catch (...) {
                        // ignore failures; keep metrics.price as-is
                    }
                }
            merge_quote_fields("alpha_vantage", alpha_price, av.trailing_pe,
                               av.peg_ratio, av.price_to_book, av.trailing_eps,
                               av.avg_roe_5y, av.book_value_per_share);
            quote_ok = quote_ok || has_main_quote_fields();
            if (has_main_quote_fields()) {
                break;
            }
            continue;
        }

        if (source == "simfin") {
            metrics.warnings.push_back("simfin_source_not_implemented");
            continue;
        }

        if (source == "finnhub") {
            const auto fh = fetch_quote_metrics_finnhub(effective_symbol, config, proxy);
            if (!fh.error.empty() && fh.current_price <= 0.0 && fh.trailing_pe <= 0.0 &&
                fh.price_to_book <= 0.0 && fh.avg_roe_5y < 0.0) {
                metrics.warnings.push_back(fh.error);
                continue;
            }
            if (!fh.error.empty()) {
                push_warning_once(metrics, fh.error);
            }
            merge_quote_fields("finnhub", fh.current_price, fh.trailing_pe,
                               fh.peg_ratio, fh.price_to_book, fh.trailing_eps,
                               fh.avg_roe_5y, fh.book_value_per_share);
            quote_ok = quote_ok || has_main_quote_fields();
            if (has_main_quote_fields()) {
                break;
            }
            continue;
        }
    }

    if (!has_main_quote_fields()) {
        const auto sample_quote = load_quote_metrics_from_sample(effective_symbol);
        if (sample_quote.error.empty()) {
            merge_quote_fields("sample_cache", sample_quote.current_price,
                               sample_quote.trailing_pe, sample_quote.peg_ratio,
                               sample_quote.price_to_book, sample_quote.trailing_eps,
                               sample_quote.avg_roe_5y, sample_quote.book_value_per_share);
            push_warning_once(metrics, "quote_from_sample_cache");
        }
    }

    quote_ok = has_main_quote_fields();
    if (!quote_ok) {
        metrics.error = "all_quote_sources_failed";
    }

    // Step 1.5: 资产负债表快照（总负债 / 总资产）
    bool balance_sheet_ok = false;
    if (!yahoo_available && alpha_rate_limited_active()) {
        const auto cached_bs = load_balance_sheet_snapshot_from_sample(effective_symbol);
        if (cached_bs.error.empty()) {
            metrics.total_assets = cached_bs.total_assets;
            metrics.total_liabilities = cached_bs.total_liabilities;
            balance_sheet_ok = true;
            push_warning_once(metrics, "debt_ratio_from_sample_cache");
        }
    }
    for (const auto &source : config.us_stocks_data_source_priority) {
        if (balance_sheet_ok) {
            break;
        }
        BalanceSheetSnapshot bs;

        if (source == "yahoo_finance") {
            if (!yahoo_available) {
                continue;
            }
            bs = fetch_balance_sheet_snapshot_yahoo(
                effective_symbol, config.yahoo_timeout_ms, proxy, crumb);
        } else if (source == "alpha_vantage") {
            bs = fetch_balance_sheet_snapshot_alpha(effective_symbol, config, proxy);
        } else if (source == "finnhub") {
            bs = fetch_balance_sheet_snapshot_finnhub(effective_symbol, config, proxy);
        } else {
            continue;
        }

        if (!bs.error.empty()) {
            push_warning_once(metrics, bs.error);
            continue;
        }

        metrics.total_assets = bs.total_assets;
        metrics.total_liabilities = bs.total_liabilities;
        balance_sheet_ok = true;

        if (bs.source != metrics.data_source && !bs.source.empty()) {
            push_warning_once(metrics, "debt_ratio_source_" + bs.source);
        }
        break;
    }
    if (!balance_sheet_ok) {
        push_warning_once(metrics, "missing_debt_ratio");
    }

    // Step 2: 净利润历史（先缓存，再按优先级外部源）
    constexpr std::size_t kRequiredYears = 8;
    bool ni_ok = false;
    bool has_partial_best = false;
    NetIncomeHistoryResult best_partial;

    auto adopt_net_income = [&](const NetIncomeHistoryResult &ni) {
        metrics.net_profit_8y = ni.annual_net_income;
        metrics.net_profit_history = ni.annual_points;
        metrics.net_profit_source = ni.source;
        metrics.net_profit_updated_at = ni.updated_at;
    };

    auto consider_partial = [&](const NetIncomeHistoryResult &ni) {
        if (ni.annual_points.empty()) return;
        if (!has_partial_best || ni.annual_points.size() > best_partial.annual_points.size()) {
            best_partial = ni;
            has_partial_best = true;
        }
    };

    const auto cached_ni = load_net_income_cache(effective_symbol, config.net_income_cache_ttl_hours);
    if (cached_ni.error.empty()) {
        if (cached_ni.annual_points.size() >= kRequiredYears) {
            adopt_net_income(cached_ni);
            ni_ok = true;
        } else {
            metrics.warnings.push_back("cache_net_income_insufficient_years");
            consider_partial(cached_ni);
        }
    }

    if (!ni_ok) {
        for (const auto &source : config.us_stocks_data_source_priority) {
            NetIncomeHistoryResult ni;

            if (source == "alpha_vantage") {
                ni = fetch_net_income_8y_alpha(effective_symbol, config, proxy);
            } else if (source == "yahoo_finance") {
                if (!yahoo_available) {
                    continue;
                }
                ni = fetch_net_income_8y(effective_symbol, config.yahoo_timeout_ms, proxy, crumb);
            } else if (source == "finnhub") {
                ni = fetch_net_income_8y_finnhub(effective_symbol, config, proxy);
            } else if (source == "simfin") {
                metrics.warnings.push_back("simfin_source_not_implemented");
                continue;
            } else {
                continue;
            }

            if (!ni.error.empty()) {
                metrics.warnings.push_back(ni.error);
                continue;
            }

            if (ni.annual_points.size() >= kRequiredYears) {
                adopt_net_income(ni);
                ni_ok = true;
                save_net_income_cache(effective_symbol, ni);
                break;
            }

            metrics.warnings.push_back(
                source + "_net_income_insufficient_years_" +
                std::to_string(ni.annual_points.size()));
            consider_partial(ni);
        }
    }

    if (!ni_ok && has_partial_best) {
        adopt_net_income(best_partial);
        if (best_partial.source != "cache") {
            save_net_income_cache(effective_symbol, best_partial);
        }
    }

    // Step 3: 获取价格历史，计算历史 PE 均值
    if (metrics.trailing_eps > 0.01 && yahoo_available) {
        const auto ph = fetch_price_history(effective_symbol, config.yahoo_timeout_ms,
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
            spdlog::warn("[ValuationClient] Yahoo chart failed for {}: {}", effective_symbol, ph.error);
        }
    } else {
        spdlog::warn("[ValuationClient] trailingEps={:.4f} for {} — skipping PE history",
                     metrics.trailing_eps, effective_symbol);
    }

    metrics.has_sufficient_data =
        (metrics.trailing_pe > 0.0) &&
        (metrics.peg_ratio > 0.0) &&
        (metrics.historical_pe_mean > 0.0);

    recompute_financial_metrics(metrics);

    spdlog::info(
        "[ValuationClient] {} — PE(new)={:.2f}, PEG(new)={:.2f}, growth5y={:.4f}, avg_roe_5y={:.4f}, debt_ratio={:.4f}, PB={:.2f}, source={}, sufficient={}",
        symbol, metrics.pe, metrics.peg,
        metrics.growth_5y_cagr, metrics.avg_roe_5y, metrics.debt_ratio, metrics.price_to_book, metrics.data_source,
        metrics.has_sufficient_data);

    return metrics;
}

} // namespace finguard::valuation
