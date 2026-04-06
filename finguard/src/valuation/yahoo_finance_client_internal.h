#pragma once

#include "valuation/proxy_config.h"
#include "valuation/valuation_config.h"
#include "valuation/yahoo_finance_client.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace finguard::valuation::internal {

struct YahooCrumb {
    std::string crumb;
    std::string cookie_file;
    bool valid = false;
};

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

struct PriceHistoryResult {
    std::vector<double> quarterly_closes;
    std::string error;
};

struct QuoteFieldBundle {
    double price = -1.0;
    double trailing_pe = -1.0;
    double peg_ratio = -1.0;
    double price_to_book = -1.0;
    double trailing_eps = -1.0;
    double avg_roe_5y = -1.0;
    double book_value_per_share = -1.0;
};

// shared helpers
inline double extract_double(const nlohmann::json &j, const std::string &key,
                             double fallback = -1.0) {
    if (!j.contains(key)) return fallback;
    const auto &v = j.at(key);
    if (v.is_number()) return v.get<double>();
    if (v.is_object() && v.contains("raw") && v.at("raw").is_number()) {
        return v.at("raw").get<double>();
    }
    return fallback;
}

double normalize_ratio_metric(double value);
std::string trim_copy(std::string s);
std::string normalize_symbol_alias(std::string symbol);
void push_warning_once(FinancialMetrics &metrics, const std::string &warning);
bool alpha_rate_limited_active();
void mark_alpha_rate_limited(int cooldown_sec = 1800);
std::pair<int, std::string> curl_get(const std::string &url, int timeout_sec,
                                     const ProxyConfig &proxy,
                                     const std::string &extra_header = "",
                                     const std::string &cookie_jar = "",
                                     const std::string &cookie_file = "");
YahooCrumb fetch_yahoo_crumb(int timeout_sec, const ProxyConfig &proxy);
YahooCrumb get_or_refresh_crumb(int timeout_sec, const ProxyConfig &proxy);
std::string url_encode(const std::string &s);
std::pair<int, std::string> yahoo_get(const std::string &url, int timeout_sec,
                                      const ProxyConfig &proxy,
                                      const YahooCrumb &crumb);
std::string now_iso_utc();
std::pair<std::string, double> extract_latest_timeseries_value(
    const nlohmann::json &result_arr,
    const std::vector<std::string> &candidate_keys);

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

std::filesystem::path net_income_cache_path(const std::string &symbol);
std::filesystem::path quote_metrics_cache_path(const std::string &symbol);
NetIncomeHistoryResult load_net_income_cache(const std::string &symbol, int ttl_hours);
void save_net_income_cache(const std::string &symbol, const NetIncomeHistoryResult &data);
FinnhubQuoteMetricsResult load_quote_metrics_cache(const std::string &symbol, int ttl_hours);
void save_quote_metrics_cache(const std::string &symbol, const FinnhubQuoteMetricsResult &data);
BalanceSheetSnapshot load_balance_sheet_snapshot_from_sample(const std::string &symbol);
FinnhubQuoteMetricsResult load_quote_metrics_from_sample(const std::string &symbol);

// quote sources
QuoteSummaryResult fetch_quote_summary(const std::string &symbol, int timeout_ms,
                                       const ProxyConfig &proxy,
                                       const YahooCrumb &crumb);
FinancialMetrics fetch_from_alpha_vantage(const std::string &symbol,
                                          const ValuationConfig &config,
                                          const ProxyConfig &proxy);
FinnhubQuoteMetricsResult fetch_quote_metrics_finnhub(const std::string &symbol,
                                                      const ValuationConfig &config,
                                                      const ProxyConfig &proxy);

// balance sheet sources
EquityHistoryResult fetch_equity_history_yahoo(const std::string &symbol, int timeout_ms,
                                               const ProxyConfig &proxy,
                                               const YahooCrumb &crumb);
EquityHistoryResult fetch_equity_history_alpha(const std::string &symbol,
                                               const ValuationConfig &config,
                                               const ProxyConfig &proxy);
BalanceSheetSnapshot fetch_balance_sheet_snapshot_yahoo(const std::string &symbol,
                                                        int timeout_ms,
                                                        const ProxyConfig &proxy,
                                                        const YahooCrumb &crumb);
BalanceSheetSnapshot fetch_balance_sheet_snapshot_alpha(const std::string &symbol,
                                                        const ValuationConfig &config,
                                                        const ProxyConfig &proxy);
BalanceSheetSnapshot fetch_balance_sheet_snapshot_finnhub(const std::string &symbol,
                                                          const ValuationConfig &config,
                                                          const ProxyConfig &proxy);

// net income sources
NetIncomeHistoryResult fetch_net_income_8y(const std::string &symbol, int timeout_ms,
                                           const ProxyConfig &proxy,
                                           const YahooCrumb &crumb);
NetIncomeHistoryResult fetch_net_income_8y_alpha(const std::string &symbol,
                                                 const ValuationConfig &config,
                                                 const ProxyConfig &proxy);
NetIncomeHistoryResult fetch_net_income_8y_finnhub(const std::string &symbol,
                                                   const ValuationConfig &config,
                                                   const ProxyConfig &proxy);

// orchestration helpers for tests and pipeline
bool merge_quote_fields(FinancialMetrics &metrics, const std::string &source_name,
                        const QuoteFieldBundle &bundle);
bool has_main_quote_fields(const FinancialMetrics &metrics);
PriceHistoryResult fetch_price_history(const std::string &symbol, int timeout_ms,
                                       int quarters, const ProxyConfig &proxy,
                                       const YahooCrumb &crumb);

} // namespace finguard::valuation::internal
