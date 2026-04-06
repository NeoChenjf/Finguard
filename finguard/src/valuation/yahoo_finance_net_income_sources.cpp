#include "valuation/yahoo_finance_client_internal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <unordered_map>

namespace finguard::valuation::internal {

NetIncomeHistoryResult fetch_net_income_8y(const std::string &symbol, int timeout_ms,
                                           const ProxyConfig &proxy,
                                           const YahooCrumb &crumb) {
    NetIncomeHistoryResult result;

    const long long now = static_cast<long long>(std::time(nullptr));
    const long long period2 = now;
    const long long period1 = now - 365LL * 24LL * 3600LL * 10LL;

    const std::string url =
        "https://query1.finance.yahoo.com/ws/fundamentals-timeseries/v1/finance/timeseries/" +
        symbol + "?type=annualNetIncome&period1=" + std::to_string(period1) +
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

        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
            return a.first < b.first;
        });

        for (const auto &r : rows) {
            FinancialMetrics::NetProfitPoint p;
            p.year = 0;
            if (r.first.size() >= 4) {
                try {
                    p.year = std::stoi(r.first.substr(0, 4));
                } catch (...) {
                    p.year = 0;
                }
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
            const int delay_sec = 12 * attempt;
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
                    try {
                        p.year = std::stoi(date.substr(0, 4));
                    } catch (...) {
                        p.year = 0;
                    }
                    try {
                        p.net_income = std::stod(net_income_str);
                    } catch (...) {
                        p.net_income = 0.0;
                    }
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
                try {
                    return std::stod(s);
                } catch (...) {
                    return 0.0;
                }
            }
            return 0.0;
        };

        auto extract_net_income = [&](const nlohmann::json &ic) -> double {
            static const std::array<const char *, 4> kCandidates = {
                "NetIncomeLoss",
                "ProfitLoss",
                "NetIncomeLossAvailableToCommonStockholdersBasic",
                "NetIncomeLossAttributableToParent",
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
                    try {
                        year = std::stoi(end_date.substr(0, 4));
                    } catch (...) {
                        year = 0;
                    }
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

} // namespace finguard::valuation::internal
