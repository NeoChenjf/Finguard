#include "valuation/yahoo_finance_client_internal.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <unordered_map>

namespace finguard::valuation::internal {

QuoteSummaryResult fetch_quote_summary(const std::string &symbol, int timeout_ms,
                                       const ProxyConfig &proxy,
                                       const YahooCrumb &crumb) {
    QuoteSummaryResult result;

    int status = -1;
    std::string body;

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) {
            const int delay_ms = attempt * 3000;
            spdlog::info("[YahooQuote] Retry {} for {} after {}ms", attempt, symbol, delay_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }

        std::tie(status, body) = yahoo_get(
            "https://query1.finance.yahoo.com/v10/finance/quoteSummary/" + symbol +
                "?modules=defaultKeyStatistics,financialData,summaryDetail&formatted=false&lang=en-US&region=US",
            timeout_ms / 1000, proxy, crumb);

        if (status == 200 && !body.empty()) break;
        if (status != 429) break;
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

        if (entry.contains("defaultKeyStatistics")) {
            const auto &stat = entry.at("defaultKeyStatistics");
            result.forward_pe = extract_double(stat, "forwardPE");
            result.peg_ratio = extract_double(stat, "pegRatio");
            result.trailing_eps = extract_double(stat, "trailingEps", 0.0);
            result.price_to_book = extract_double(stat, "priceToBook");
            result.book_value_per_share = extract_double(stat, "bookValue");
        }
        if (entry.contains("summaryDetail")) {
            const auto &sd = entry.at("summaryDetail");
            result.trailing_pe = extract_double(sd, "trailingPE");
            if (result.forward_pe < 0.0) {
                result.forward_pe = extract_double(sd, "forwardPE");
            }
        }
        if (entry.contains("financialData")) {
            const auto &fin = entry.at("financialData");
            result.earnings_growth = extract_double(fin, "earningsGrowth", 0.0);
            result.current_price = extract_double(fin, "currentPrice", 0.0);
            result.avg_roe_5y = normalize_ratio_metric(extract_double(fin, "returnOnEquity"));
        }
        if (result.peg_ratio < 0.0 && result.trailing_pe > 0.0 && result.earnings_growth > 0.01) {
            result.peg_ratio = result.trailing_pe / (result.earnings_growth * 100.0);
            spdlog::info("[YahooQuote] PEG auto-calc for {}: {:.2f} = {:.1f} / ({:.1f}%)",
                         symbol, result.peg_ratio, result.trailing_pe,
                         result.earnings_growth * 100.0);
        }
    } catch (const std::exception &e) {
        result.error = std::string("yahoo_quotesummary_parse_error: ") + e.what();
    }
    return result;
}

namespace {

double parse_alpha_double(const nlohmann::json &j, const std::string &key) {
    if (!j.contains(key)) return -1.0;
    const auto &v = j.at(key);
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) {
        const std::string s = v.get<std::string>();
        if (s == "None" || s == "-") return -1.0;
        try {
            return std::stod(s);
        } catch (...) {
            return -1.0;
        }
    }
    return -1.0;
}

} // namespace

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

        result.trailing_pe = parse_alpha_double(j, "TrailingPE");
        result.forward_pe = parse_alpha_double(j, "ForwardPE");
        result.peg_ratio = parse_alpha_double(j, "PEGRatio");
        result.price_to_book = parse_alpha_double(j, "PriceToBookRatio");
        for (const auto &roe_key : {"ReturnOnEquity5YearAvg", "ReturnOnEquity5YearAverage",
                                    "ReturnOnEquity5Y", "ReturnOnEquityTTM"}) {
            const double roe = normalize_ratio_metric(parse_alpha_double(j, roe_key));
            if (roe >= 0.0) {
                result.avg_roe_5y = roe;
                break;
            }
        }
        result.trailing_eps = parse_alpha_double(j, "EPS");
        result.book_value_per_share = parse_alpha_double(j, "BookValue");
        result.earnings_growth = parse_alpha_double(j, "RevenueGrowthTTMYOY");
        result.has_sufficient_data = (result.trailing_pe > 0.0) && (result.peg_ratio > 0.0);
        result.data_source = "alpha_vantage";
        result.warnings.push_back("alpha_vantage_fallback_used");
    } catch (const std::exception &e) {
        result.error = std::string("alpha_vantage_parse_error: ") + e.what();
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

    const int quote_timeout_sec = (std::max)(3, config.yahoo_timeout_ms / 1000);
    const int metric_timeout_sec = (std::max)(15, config.yahoo_timeout_ms / 1000);

    const auto [quote_status, quote_body] = curl_get(quote_url, quote_timeout_sec, proxy);
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
                try {
                    return std::stod(v.get<std::string>());
                } catch (...) {
                    return -1.0;
                }
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

    const auto [metric_status, metric_body] = curl_get(metric_url, metric_timeout_sec, proxy);

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
                try {
                    return std::stod(s);
                } catch (...) {
                    return -1.0;
                }
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

} // namespace finguard::valuation::internal
