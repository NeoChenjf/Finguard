#include "valuation/yahoo_finance_client_internal.h"

#include <algorithm>
#include <string>

namespace finguard::valuation::internal {

EquityHistoryResult fetch_equity_history_yahoo(const std::string &symbol, int timeout_ms,
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
            {"annualStockholdersEquity", "annualCommonStockEquity",
             "annualTotalEquityGrossMinorityInterest"},
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
                    try {
                        return std::stod(s);
                    } catch (...) {
                        return -1.0;
                    }
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

BalanceSheetSnapshot fetch_balance_sheet_snapshot_yahoo(const std::string &symbol,
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
        const auto latest_assets = extract_latest_timeseries_value(result_arr, {"annualTotalAssets"});
        const auto latest_liabilities = extract_latest_timeseries_value(
            result_arr, {"annualTotalLiabilitiesNetMinorityInterest", "annualTotalLiab"});

        result.total_assets = latest_assets.second;
        result.total_liabilities = latest_liabilities.second;
        result.source = "yahoo_finance";
        result.updated_at = !latest_assets.first.empty() ? latest_assets.first : now_iso_utc();

        if (result.total_assets <= 0.0 || result.total_liabilities < 0.0) {
            result.error = "yahoo_balance_sheet_incomplete";
        }
    } catch (const std::exception &) {
        result.error = "yahoo_balance_sheet_parse_error";
    }

    return result;
}

BalanceSheetSnapshot fetch_balance_sheet_snapshot_alpha(const std::string &symbol,
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
                try {
                    return std::stod(s);
                } catch (...) {
                    return -1.0;
                }
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

BalanceSheetSnapshot fetch_balance_sheet_snapshot_finnhub(const std::string &symbol,
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
                try {
                    return std::stod(s);
                } catch (...) {
                    return -1.0;
                }
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
                    if (concept == candidate && item.contains("value")) {
                        return parse_number(item.at("value"));
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
        result.total_assets = extract_bs_value(bs, {"us-gaap_Assets"});
        result.total_liabilities = extract_bs_value(bs, {"us-gaap_Liabilities"});
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

} // namespace finguard::valuation::internal
