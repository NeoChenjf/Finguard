#include "valuation/fundamentals_db_client.h"

#include "data/fundamentals_db.h"
#include "valuation/yahoo_finance_client_internal.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace finguard::valuation {

namespace {

constexpr int kQuoteStaleHours = 24 * 7;

bool parse_utc_time(const std::string &text, std::time_t &out) {
    if (text.size() < 20) {
        return false;
    }

    std::tm tm{};
    std::istringstream iss(text);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (iss.fail()) {
        return false;
    }
#ifdef _WIN32
    out = _mkgmtime(&tm);
#else
    out = timegm(&tm);
#endif
    return out > 0;
}

template <typename T>
std::vector<T> last_n(std::vector<T> values, std::size_t n) {
    if (values.size() <= n) {
        return values;
    }
    return std::vector<T>(values.end() - static_cast<std::ptrdiff_t>(n), values.end());
}

} // namespace

FinancialMetrics fetch_financial_metrics_from_db(const std::string &requested_symbol,
                                                 const std::string &configured_profile) {
    using namespace internal;

    FinancialMetrics metrics;
    const std::string normalized_symbol = normalize_symbol_alias(requested_symbol);
    metrics.symbol = normalized_symbol;
    metrics.data_source = "sqlite";
    metrics.net_profit_source = "sqlite";

    const auto snapshot = data::load_fundamentals_snapshot(normalized_symbol, configured_profile);
    metrics.db_profile = snapshot.db_profile_key;
    metrics.db_profile_label = snapshot.db_profile_label;
    if (!snapshot.db_available) {
        metrics.error = snapshot.error.empty() ? "fundamentals_db_not_found" : snapshot.error;
        push_warning_once(metrics, "db_symbol_not_found");
        return metrics;
    }

    if (!snapshot.symbol_found) {
        metrics.error = snapshot.error.empty() ? "db_symbol_not_found" : snapshot.error;
        push_warning_once(metrics, "db_symbol_not_found");
        return metrics;
    }

    metrics.symbol = snapshot.resolved_symbol.empty() ? normalized_symbol : snapshot.resolved_symbol;
    metrics.error = snapshot.error;

    if (snapshot.latest_quote.has_value()) {
        const auto &quote = snapshot.latest_quote.value();
        metrics.price = quote.price;
        metrics.trailing_pe = quote.trailing_pe;
        metrics.forward_pe = quote.forward_pe;
        metrics.price_to_book = quote.price_to_book;
        metrics.peg_ratio = quote.peg_ratio;
        metrics.trailing_eps = quote.trailing_eps;
        metrics.eps = quote.trailing_eps;

        std::time_t updated_ts = 0;
        if (parse_utc_time(quote.updated_at, updated_ts)) {
            const auto now = std::time(nullptr);
            const auto age_hours = static_cast<int>((now - updated_ts) / 3600);
            if (age_hours > kQuoteStaleHours) {
                push_warning_once(metrics, "db_stale_quote_metrics");
            }
        }
    } else {
        push_warning_once(metrics, "db_missing_latest_quote");
    }

    std::vector<data::AnnualFundamentalsRecord> annual_rows = snapshot.annual_rows;
    if (annual_rows.empty()) {
        push_warning_once(metrics, "db_insufficient_annual_history");
        recompute_financial_metrics(metrics);
        return metrics;
    }

    const auto &latest = annual_rows.back();
    metrics.book_value_per_share = latest.book_value_per_share;
    metrics.total_assets = latest.total_assets;
    metrics.total_liabilities = latest.total_liabilities;
    metrics.debt_ratio = latest.debt_ratio;

    std::vector<double> roe_values;
    for (const auto &row : annual_rows) {
        if (row.roe >= 0.0) {
            roe_values.push_back(row.roe);
        }
    }
    roe_values = last_n(std::move(roe_values), 5);
    if (roe_values.size() >= 5) {
        const double sum = std::accumulate(roe_values.begin(), roe_values.end(), 0.0);
        metrics.avg_roe_5y = sum / static_cast<double>(roe_values.size());
    } else {
        metrics.avg_roe_5y = -1.0;
    }

    for (const auto &row : annual_rows) {
        if (row.fiscal_year <= 0 || row.net_income == -1.0) {
            continue;
        }
        FinancialMetrics::NetProfitPoint point;
        point.year = row.fiscal_year;
        point.net_income = row.net_income;
        point.source = row.source.empty() ? "sqlite" : row.source;
        metrics.net_profit_history.push_back(point);
    }

    if (metrics.net_profit_history.size() < 8) {
        push_warning_once(metrics, "db_insufficient_annual_history");
    }

    const auto recent_points = last_n(metrics.net_profit_history, 8);
    metrics.net_profit_history = last_n(metrics.net_profit_history, 10);
    metrics.net_profit_8y.clear();
    metrics.net_profit_8y.reserve(recent_points.size());
    for (const auto &point : recent_points) {
        metrics.net_profit_8y.push_back(point.net_income);
    }
    metrics.net_profit_updated_at =
        latest.source_updated_at.empty() ? now_iso_utc() : latest.source_updated_at;

    recompute_financial_metrics(metrics);
    metrics.data_source = "sqlite";
    metrics.net_profit_source = "sqlite";
    metrics.warnings.erase(
        std::remove(metrics.warnings.begin(), metrics.warnings.end(), "db_symbol_not_found"),
        metrics.warnings.end());
    return metrics;
}

} // namespace finguard::valuation
