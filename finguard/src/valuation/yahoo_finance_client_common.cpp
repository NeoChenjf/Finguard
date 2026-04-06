#include "valuation/yahoo_finance_client_internal.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace finguard::valuation::internal {

namespace {
std::atomic<long long> s_alpha_rate_limited_until{0};
std::mutex s_crumb_mutex;
YahooCrumb s_cached_crumb;
std::chrono::steady_clock::time_point s_crumb_time{};
} // namespace

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

// 判断 Alpha Vantage 是否被限流
bool alpha_rate_limited_active() {
    const auto now = static_cast<long long>(std::time(nullptr));
    return now < s_alpha_rate_limited_until.load();
}

void mark_alpha_rate_limited(int cooldown_sec) {
    const auto now = static_cast<long long>(std::time(nullptr));
    s_alpha_rate_limited_until.store(now + cooldown_sec);
}


// 通过 curl 配置文件实现参数隔离，避免 C++ shell 调用时的转义问题
std::pair<int, std::string> curl_get(const std::string &url, int timeout_sec,
                                     const ProxyConfig &proxy,
                                     const std::string &extra_header,
                                     const std::string &cookie_jar,
                                     const std::string &cookie_file) {
    // 用于生成唯一的临时配置文件名
    static std::atomic<unsigned long long> s_cfg_counter{0};
    const auto cfg_id = ++s_cfg_counter;
    // 生成临时 curl 配置文件名
    const std::string cfg_path = "valuation_curl_" + std::to_string(cfg_id) + ".cfg";
    {
        // 打开配置文件，写入 curl 参数
        std::ofstream ofs(cfg_path, std::ios::binary);
        // 写入目标 URL
        ofs << "url = \"" << url << "\"\n";
        // 静默模式
        ofs << "silent\n";
        // 超时时间
        ofs << "max-time = " << timeout_sec << "\n";
        // 输出 HTTP 状态码到最后一行
        ofs << "write-out = \"\\n%{http_code}\"\n";
        // 写入常用 header
        ofs << "header = \"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36\"\n";
        ofs << "header = \"Accept: */*\"\n";
        // 追加额外 header（如 token）
        if (!extra_header.empty()) {
            ofs << "header = \"" << extra_header << "\"\n";
        }
        // 代理设置
        if (proxy.enabled) {
            ofs << "proxy = " << (proxy.use_ssl ? "https://" : "http://")
                << proxy.host << ":" << proxy.port << "\n";
        }
        // cookie-jar 设置
        if (!cookie_jar.empty()) {
            ofs << "cookie-jar = \"" << cookie_jar << "\"\n";
        }
        // cookie 文件
        if (!cookie_file.empty()) {
            ofs << "cookie = \"" << cookie_file << "\"\n";
        }
    }

    // 构造 curl 命令，-K 参数指定配置文件，2>NUL 屏蔽错误输出
    const std::string cmd = "curl.exe -K \"" + cfg_path + "\" 2>NUL";
    spdlog::debug("[ValuationHTTP] exec config-based: url={}", url);

    std::string output;
    {
        // 执行 curl 命令，读取输出
        std::array<char, 8192> buf{};
        std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(cmd.c_str(), "r"), _pclose);
        if (!pipe) {
            spdlog::error("[ValuationHTTP] popen 失败: {}", url);
            std::error_code ec;
            std::filesystem::remove(cfg_path, ec);
            return {-1, ""};
        }
        // 逐块读取 curl 输出
        while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe.get()) != nullptr) {
            output += buf.data();
        }
    }
    {
        // 请求结束后删除临时配置文件
        std::error_code ec;
        std::filesystem::remove(cfg_path, ec);
    }

    while (!output.empty() &&
           (output.back() == '\r' || output.back() == '\n' || output.back() == ' ')) {
        output.pop_back();
    }

    const auto last_nl = output.rfind('\n');
    if (last_nl == std::string::npos) {
        std::string trimmed;
        for (char c : output) {
            if (c >= '0' && c <= '9') trimmed += c;
        }
        if (!trimmed.empty()) {
            try {
                return {std::stoi(trimmed), ""};
            } catch (...) {
            }
        }
        spdlog::warn("[ValuationHTTP] curl returned empty for {}", url);
        return {-1, ""};
    }

    const std::string code_str = output.substr(last_nl + 1);
    std::string trimmed_code;
    for (char c : code_str) {
        if (c >= '0' && c <= '9') trimmed_code += c;
    }

    int status = -1;
    try {
        status = std::stoi(trimmed_code);
    } catch (...) {
    }

    const std::string body = output.substr(0, last_nl);
    spdlog::debug("[ValuationHTTP] {} -> status={}, body_len={}", url, status, body.size());
    return {status, body};
}

YahooCrumb fetch_yahoo_crumb(int timeout_sec, const ProxyConfig &proxy) {
    YahooCrumb yc;
    yc.cookie_file = "yahoo_cookies.txt";
    const int effective_timeout_sec =
        timeout_sec < 2 ? 2 : (timeout_sec > 3 ? 3 : timeout_sec);

    const auto [s1, b1] = curl_get("https://fc.yahoo.com/", effective_timeout_sec, proxy,
                                   "", yc.cookie_file, "");
    (void)b1;
    spdlog::debug("[YahooCrumb] Step1 cookie fetch: status={}", s1);
    if (s1 != 200) {
        spdlog::warn("[YahooCrumb] Cookie fetch unavailable (status={}), skip Yahoo crumb", s1);
        return yc;
    }

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) {
            const int delay_ms = attempt * 2000;
            spdlog::info("[YahooCrumb] Retry {} after {}ms...", attempt, delay_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
        const auto [s2, crumb] = curl_get(
            "https://query2.finance.yahoo.com/v1/test/getcrumb",
            effective_timeout_sec, proxy, "", "", yc.cookie_file);

        if (s2 == 200 && !crumb.empty()) {
            yc.crumb = crumb;
            while (!yc.crumb.empty() &&
                   (yc.crumb.back() == '\r' || yc.crumb.back() == '\n' || yc.crumb.back() == ' ')) {
                yc.crumb.pop_back();
            }
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

std::string url_encode(const std::string &s) {
    std::ostringstream oss;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << c;
        } else {
            oss << '%' << std::uppercase << std::hex << ((c >> 4) & 0x0F) << (c & 0x0F);
        }
    }
    return oss.str();
}

std::pair<int, std::string> yahoo_get(const std::string &url, int timeout_sec,
                                      const ProxyConfig &proxy,
                                      const YahooCrumb &crumb) {
    std::string full_url = url;
    if (crumb.valid) {
        full_url += (url.find('?') != std::string::npos ? "&" : "?");
        full_url += "crumb=" + url_encode(crumb.crumb);
    }
    return curl_get(full_url, timeout_sec, proxy, "", "", crumb.cookie_file);
}

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

void save_net_income_cache(const std::string &symbol, const NetIncomeHistoryResult &data) {
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
            {"source", p.source.empty() ? data.source : p.source},
        });
    }

    const auto path = net_income_cache_path(symbol);
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (ofs.is_open()) {
        ofs << j.dump(2);
    }
}

FinnhubQuoteMetricsResult load_quote_metrics_cache(const std::string &symbol, int ttl_hours) {
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
    const auto path = std::filesystem::current_path().parent_path().parent_path() /
                      "uploads" / "valuecell_samples" / ("value_" + symbol + ".json");
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
    const auto path = std::filesystem::current_path().parent_path().parent_path() /
                      "uploads" / "valuecell_samples" / ("value_" + symbol + ".json");
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

bool merge_quote_fields(FinancialMetrics &metrics, const std::string &source_name,
                        const QuoteFieldBundle &bundle) {
    bool filled_any = false;
    if (metrics.price <= 0.0 && bundle.price > 0.0) {
        metrics.price = bundle.price;
        filled_any = true;
    }
    if (metrics.trailing_pe <= 0.0 && bundle.trailing_pe > 0.0) {
        metrics.trailing_pe = bundle.trailing_pe;
        filled_any = true;
    }
    if (metrics.peg_ratio <= 0.0 && bundle.peg_ratio > 0.0) {
        metrics.peg_ratio = bundle.peg_ratio;
        filled_any = true;
    }
    if (metrics.price_to_book <= 0.0 && bundle.price_to_book > 0.0) {
        metrics.price_to_book = bundle.price_to_book;
        filled_any = true;
    }
    if (metrics.trailing_eps <= 0.0 && bundle.trailing_eps > 0.0) {
        metrics.trailing_eps = bundle.trailing_eps;
        filled_any = true;
    }
    if (metrics.avg_roe_5y < 0.0 && bundle.avg_roe_5y >= 0.0) {
        metrics.avg_roe_5y = bundle.avg_roe_5y;
        filled_any = true;
    }
    if (metrics.book_value_per_share <= 0.0 && bundle.book_value_per_share > 0.0) {
        metrics.book_value_per_share = bundle.book_value_per_share;
        filled_any = true;
    }
    if (filled_any && metrics.data_source.empty()) {
        metrics.data_source = source_name;
    }
    if (filled_any && metrics.data_source != source_name) {
        push_warning_once(metrics, "quote_source_" + source_name);
    }
    return filled_any;
}

bool has_main_quote_fields(const FinancialMetrics &metrics) {
    return metrics.trailing_pe > 0.0 && metrics.price_to_book > 0.0 &&
           metrics.avg_roe_5y >= 0.0 && metrics.price > 0.0;
}

} // namespace finguard::valuation::internal
