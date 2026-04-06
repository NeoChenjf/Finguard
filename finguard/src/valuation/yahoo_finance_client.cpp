#include "valuation/yahoo_finance_client.h"
#include "valuation/proxy_config.h"
#include "valuation/yahoo_finance_client_internal.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

namespace finguard::valuation {

using namespace internal;

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

namespace internal {

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
        const auto &indicators = chart_result.at("indicators");

        std::vector<double> closes;
        if (indicators.contains("adjclose") && !indicators.at("adjclose").empty() &&
            indicators.at("adjclose").at(0).contains("adjclose")) {
            for (const auto &v : indicators.at("adjclose").at(0).at("adjclose")) {
                if (v.is_number()) closes.push_back(v.get<double>());
            }
        } else if (indicators.contains("quote") && !indicators.at("quote").empty() &&
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
            closes.erase(closes.begin(),
                         closes.begin() + (static_cast<int>(closes.size()) - quarters));
        }
        result.quarterly_closes = std::move(closes);
    } catch (const std::exception &e) {
        result.error = std::string("yahoo_chart_parse_error: ") + e.what();
    }
    return result;
}

namespace {

double fetch_alpha_global_quote_price(const std::string &symbol,
                                      const ValuationConfig &config,
                                      const ProxyConfig &proxy) {
    if (config.alpha_vantage_api_key.empty()) {
        return -1.0;
    }

    try {
        const auto [status, body] = curl_get(
            "https://www.alphavantage.co/query?function=GLOBAL_QUOTE&symbol=" + symbol +
                "&apikey=" + config.alpha_vantage_api_key,
            config.yahoo_timeout_ms / 1000, proxy);
        if (status != 200 || body.empty()) {
            return -1.0;
        }

        auto jq = nlohmann::json::parse(body);
        if (jq.contains("Global Quote") && jq.at("Global Quote").contains("05. price")) {
            const auto sprice = jq.at("Global Quote").at("05. price").get<std::string>();
            try {
                return std::stod(sprice);
            } catch (...) {
                return -1.0;
            }
        }
    } catch (...) {
        return -1.0;
    }

    return -1.0;
}

void adopt_net_income(FinancialMetrics &metrics, const NetIncomeHistoryResult &ni) {
    metrics.net_profit_8y = ni.annual_net_income;
    metrics.net_profit_history = ni.annual_points;
    metrics.net_profit_source = ni.source;
    metrics.net_profit_updated_at = ni.updated_at;
}

void maybe_load_sample_quote(FinancialMetrics &metrics, const std::string &symbol) {
    if (has_main_quote_fields(metrics)) {
        return;
    }
    const auto sample_quote = load_quote_metrics_from_sample(symbol);
    if (sample_quote.error.empty()) {
        QuoteFieldBundle bundle;
        bundle.price = sample_quote.current_price;
        bundle.trailing_pe = sample_quote.trailing_pe;
        bundle.peg_ratio = sample_quote.peg_ratio;
        bundle.price_to_book = sample_quote.price_to_book;
        bundle.trailing_eps = sample_quote.trailing_eps;
        bundle.avg_roe_5y = sample_quote.avg_roe_5y;
        bundle.book_value_per_share = sample_quote.book_value_per_share;
        merge_quote_fields(metrics, "sample_cache", bundle);
        push_warning_once(metrics, "quote_from_sample_cache");
    }
}

void fetch_quote_pipeline(FinancialMetrics &metrics, const std::string &symbol,
                          const ValuationConfig &config, const ProxyConfig &proxy,
                          const YahooCrumb &crumb, bool yahoo_available) {
    for (const auto &source : config.us_stocks_data_source_priority) {
        if (source == "yahoo_finance") {
            if (!yahoo_available) {
                continue;
            }
            const auto qs = fetch_quote_summary(symbol, config.yahoo_timeout_ms, proxy, crumb);
            if (!qs.error.empty()) {
                metrics.warnings.push_back(qs.error);
                continue;
            }

            metrics.earnings_growth = qs.earnings_growth;
            metrics.forward_pe = qs.forward_pe;
            QuoteFieldBundle bundle;
            bundle.price = qs.current_price;
            bundle.trailing_pe = qs.trailing_pe;
            bundle.peg_ratio = qs.peg_ratio;
            bundle.price_to_book = qs.price_to_book;
            bundle.trailing_eps = qs.trailing_eps;
            bundle.avg_roe_5y = qs.avg_roe_5y;
            bundle.book_value_per_share = qs.book_value_per_share;
            merge_quote_fields(metrics, "yahoo_finance", bundle);
            if (has_main_quote_fields(metrics)) {
                break;
            }
            continue;
        }

        if (source == "alpha_vantage") {
            const auto av = fetch_from_alpha_vantage(symbol, config, proxy);
            if (!av.error.empty()) {
                metrics.warnings.push_back(av.error);
                continue;
            }
            metrics.forward_pe = av.forward_pe;
            metrics.earnings_growth = av.earnings_growth;
            double alpha_price = av.price;
            if (alpha_price <= 0.0) {
                alpha_price = fetch_alpha_global_quote_price(symbol, config, proxy);
            }
            QuoteFieldBundle bundle;
            bundle.price = alpha_price;
            bundle.trailing_pe = av.trailing_pe;
            bundle.peg_ratio = av.peg_ratio;
            bundle.price_to_book = av.price_to_book;
            bundle.trailing_eps = av.trailing_eps;
            bundle.avg_roe_5y = av.avg_roe_5y;
            bundle.book_value_per_share = av.book_value_per_share;
            merge_quote_fields(metrics, "alpha_vantage", bundle);
            if (has_main_quote_fields(metrics)) {
                break;
            }
            continue;
        }

        if (source == "simfin") {
            metrics.warnings.push_back("simfin_source_not_implemented");
            continue;
        }

        if (source == "finnhub") {
            const auto fh = fetch_quote_metrics_finnhub(symbol, config, proxy);
            if (!fh.error.empty() && fh.current_price <= 0.0 && fh.trailing_pe <= 0.0 &&
                fh.price_to_book <= 0.0 && fh.avg_roe_5y < 0.0) {
                metrics.warnings.push_back(fh.error);
                continue;
            }
            if (!fh.error.empty()) {
                push_warning_once(metrics, fh.error);
            }
            QuoteFieldBundle bundle;
            bundle.price = fh.current_price;
            bundle.trailing_pe = fh.trailing_pe;
            bundle.peg_ratio = fh.peg_ratio;
            bundle.price_to_book = fh.price_to_book;
            bundle.trailing_eps = fh.trailing_eps;
            bundle.avg_roe_5y = fh.avg_roe_5y;
            bundle.book_value_per_share = fh.book_value_per_share;
            merge_quote_fields(metrics, "finnhub", bundle);
            if (has_main_quote_fields(metrics)) {
                break;
            }
        }
    }

    maybe_load_sample_quote(metrics, symbol);
    if (!has_main_quote_fields(metrics)) {
        metrics.error = "all_quote_sources_failed";
    }
}

void fetch_balance_sheet_pipeline(FinancialMetrics &metrics, const std::string &symbol,
                                  const ValuationConfig &config, const ProxyConfig &proxy,
                                  const YahooCrumb &crumb, bool yahoo_available) {
    bool balance_sheet_ok = false;
    if (!yahoo_available && alpha_rate_limited_active()) {
        const auto cached_bs = load_balance_sheet_snapshot_from_sample(symbol);
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
            bs = fetch_balance_sheet_snapshot_yahoo(symbol, config.yahoo_timeout_ms, proxy, crumb);
        } else if (source == "alpha_vantage") {
            bs = fetch_balance_sheet_snapshot_alpha(symbol, config, proxy);
        } else if (source == "finnhub") {
            bs = fetch_balance_sheet_snapshot_finnhub(symbol, config, proxy);
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
    }

    if (!balance_sheet_ok) {
        push_warning_once(metrics, "missing_debt_ratio");
    }
}

void fetch_net_income_pipeline(FinancialMetrics &metrics, const std::string &symbol,
                               const ValuationConfig &config, const ProxyConfig &proxy,
                               const YahooCrumb &crumb, bool yahoo_available) {
    constexpr std::size_t kRequiredYears = 8;
    bool ni_ok = false;
    bool has_partial_best = false;
    NetIncomeHistoryResult best_partial;

    auto consider_partial = [&](const NetIncomeHistoryResult &ni) {
        if (ni.annual_points.empty()) return;
        if (!has_partial_best || ni.annual_points.size() > best_partial.annual_points.size()) {
            best_partial = ni;
            has_partial_best = true;
        }
    };

    const auto cached_ni = load_net_income_cache(symbol, config.net_income_cache_ttl_hours);
    if (cached_ni.error.empty()) {
        if (cached_ni.annual_points.size() >= kRequiredYears) {
            adopt_net_income(metrics, cached_ni);
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
                ni = fetch_net_income_8y_alpha(symbol, config, proxy);
            } else if (source == "yahoo_finance") {
                if (!yahoo_available) {
                    continue;
                }
                ni = fetch_net_income_8y(symbol, config.yahoo_timeout_ms, proxy, crumb);
            } else if (source == "finnhub") {
                ni = fetch_net_income_8y_finnhub(symbol, config, proxy);
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
                adopt_net_income(metrics, ni);
                ni_ok = true;
                save_net_income_cache(symbol, ni);
                break;
            }

            metrics.warnings.push_back(source + "_net_income_insufficient_years_" +
                                       std::to_string(ni.annual_points.size()));
            consider_partial(ni);
        }
    }

    if (!ni_ok && has_partial_best) {
        adopt_net_income(metrics, best_partial);
        if (best_partial.source != "cache") {
            save_net_income_cache(symbol, best_partial);
        }
    }
}

void fetch_historical_pe_pipeline(FinancialMetrics &metrics, const std::string &symbol,
                                  const ValuationConfig &config, const ProxyConfig &proxy,
                                  const YahooCrumb &crumb, bool yahoo_available) {
    if (metrics.trailing_eps > 0.01 && yahoo_available) {
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
}

} // namespace

} // namespace internal

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
        spdlog::info("[ValuationClient] Using proxy {}:{} for {}", proxy.host, proxy.port,
                     effective_symbol);
    }

    const auto crumb = get_or_refresh_crumb(config.yahoo_timeout_ms / 1000, proxy);
    const bool yahoo_available = crumb.valid;
    if (!crumb.valid) {
        spdlog::warn("[ValuationClient] Yahoo crumb failed, will try anyway");
        push_warning_once(metrics, "yahoo_crumb_unavailable");
    }

    fetch_quote_pipeline(metrics, effective_symbol, config, proxy, crumb, yahoo_available);
    fetch_balance_sheet_pipeline(metrics, effective_symbol, config, proxy, crumb, yahoo_available);
    fetch_net_income_pipeline(metrics, effective_symbol, config, proxy, crumb, yahoo_available);
    fetch_historical_pe_pipeline(metrics, effective_symbol, config, proxy, crumb, yahoo_available);

    metrics.has_sufficient_data = (metrics.trailing_pe > 0.0) && (metrics.peg_ratio > 0.0) &&
                                  (metrics.historical_pe_mean > 0.0);

    recompute_financial_metrics(metrics);

    spdlog::info(
        "[ValuationClient] {} — PE(new)={:.2f}, PEG(new)={:.2f}, growth5y={:.4f}, avg_roe_5y={:.4f}, debt_ratio={:.4f}, PB={:.2f}, source={}, sufficient={}",
        symbol, metrics.pe, metrics.peg, metrics.growth_5y_cagr, metrics.avg_roe_5y,
        metrics.debt_ratio, metrics.price_to_book, metrics.data_source,
        metrics.has_sufficient_data);

    return metrics;
}

} // namespace finguard::valuation
