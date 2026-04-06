#include "valuation/analysis_workflow.h"

#include "data/fundamentals_db.h"
#include "valuation/fundamentals_db_client.h"
#include "valuation/yahoo_finance_client_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace finguard::valuation {

namespace {

bool contains_warning(const FinancialMetrics &metrics, const std::string &warning) {
    return std::find(metrics.warnings.begin(), metrics.warnings.end(), warning) !=
           metrics.warnings.end();
}

void add_warning_once(FinancialMetrics &metrics, const std::string &warning) {
    if (!contains_warning(metrics, warning)) {
        metrics.warnings.push_back(warning);
    }
}

void erase_warning(FinancialMetrics &metrics, const std::string &warning) {
    metrics.warnings.erase(
        std::remove(metrics.warnings.begin(), metrics.warnings.end(), warning),
        metrics.warnings.end());
}

bool has_useful_structured_data(const FinancialMetrics &metrics) {
    return metrics.price > 0.0 || metrics.trailing_pe > 0.0 || metrics.price_to_book > 0.0 ||
           metrics.book_value_per_share > 0.0 || !metrics.net_profit_history.empty() ||
           (metrics.total_assets > 0.0 && metrics.total_liabilities >= 0.0);
}

bool profile_allows_autowrite(const std::string &configured_profile) {
    const auto profile = data::resolve_fundamentals_db_profile(configured_profile);
    return !profile.forced_by_env && !profile.is_demo && profile.key == "main";
}

std::string fmt_number(double value, int digits = 2, const std::string &fallback = "N/A") {
    if (!std::isfinite(value) || value < 0.0) {
        return fallback;
    }
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(digits);
    oss << value;
    return oss.str();
}

std::string fmt_percent(double value, int digits = 2) {
    if (!std::isfinite(value) || value < 0.0) {
        return "N/A";
    }
    return fmt_number(value * 100.0, digits) + "%";
}

std::string fmt_signed_number(double value, int digits = 2, const std::string &fallback = "N/A") {
    if (!std::isfinite(value)) {
        return fallback;
    }
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(digits);
    oss << value;
    return oss.str();
}

bool persist_metrics_snapshot(const std::string &requested_symbol,
                              const FinancialMetrics &metrics,
                              const std::string &configured_profile,
                              std::string *error) {
    if (!has_useful_structured_data(metrics)) {
        if (error != nullptr) {
            *error = "no_structured_financial_data";
        }
        return false;
    }

    const auto normalized_symbol = internal::normalize_symbol_alias(
        metrics.symbol.empty() ? requested_symbol : metrics.symbol);
    const auto now = internal::now_iso_utc();

    data::CompanyRecord company;
    company.symbol = normalized_symbol;
    company.normalized_symbol = normalized_symbol;
    company.company_name = normalized_symbol;
    company.market = "US";
    company.currency = "USD";
    company.is_active = true;
    company.created_at = now;
    company.updated_at = now;

    std::string local_error;
    if (!data::upsert_company_record(company, configured_profile, &local_error)) {
        if (error != nullptr) {
            *error = local_error;
        }
        return false;
    }

    std::vector<data::AnnualFundamentalsRecord> annual_rows;
    annual_rows.reserve(metrics.net_profit_history.size());
    for (const auto &point : metrics.net_profit_history) {
        if (point.year <= 0) {
            continue;
        }
        data::AnnualFundamentalsRecord row;
        row.fiscal_year = point.year;
        row.net_income = point.net_income;
        row.source = point.source.empty() ? metrics.data_source : point.source;
        row.source_updated_at =
            metrics.net_profit_updated_at.empty() ? now : metrics.net_profit_updated_at;
        row.quality_flag = "auto_bootstrap";
        annual_rows.push_back(row);
    }

    if (!annual_rows.empty()) {
        auto &latest = annual_rows.back();
        latest.book_value_per_share = metrics.book_value_per_share;
        latest.total_assets = metrics.total_assets;
        latest.total_liabilities = metrics.total_liabilities;
        latest.total_equity = (metrics.total_assets > 0.0 && metrics.total_liabilities >= 0.0)
                                  ? (metrics.total_assets - metrics.total_liabilities)
                                  : -1.0;
        latest.debt_ratio = metrics.debt_ratio;
        latest.eps_diluted = metrics.eps;
    }

    if (!data::upsert_annual_fundamentals_records(
            normalized_symbol, annual_rows, configured_profile, &local_error)) {
        if (error != nullptr) {
            *error = local_error;
        }
        return false;
    }

    data::LatestQuoteMetricsRecord quote;
    quote.price = metrics.price;
    quote.trailing_pe = metrics.trailing_pe;
    quote.forward_pe = metrics.forward_pe;
    quote.price_to_book = metrics.price_to_book;
    quote.peg_ratio = metrics.peg_ratio > 0.0 ? metrics.peg_ratio : metrics.peg;
    quote.trailing_eps = metrics.trailing_eps > 0.0 ? metrics.trailing_eps : metrics.eps;
    quote.source = metrics.data_source.empty() ? "auto_bootstrap" : metrics.data_source;
    quote.quote_time = now;
    quote.updated_at = now;

    if (quote.price > 0.0 || quote.trailing_pe > 0.0 || quote.price_to_book > 0.0 ||
        quote.trailing_eps > 0.0) {
        if (!data::upsert_latest_quote_metrics_record(
                normalized_symbol, quote, configured_profile, &local_error)) {
            if (error != nullptr) {
                *error = local_error;
            }
            return false;
        }
    }

    return true;
}

std::vector<std::string> tokenize_candidate_symbols(const std::string &text) {
    std::vector<std::string> tokens;
    std::string current;

    auto flush = [&]() {
        if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    };

    for (char ch : text) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '.' || ch == '-' || ch == '/') {
            current.push_back(ch);
        } else {
            flush();
        }
    }
    flush();
    return tokens;
}

} // namespace

std::string extract_symbol_candidate(const std::string &text) {
    static const std::unordered_set<std::string> kStopWords = {
        "PE",   "PB",   "PEG", "ROE",  "CAGR", "LLM", "API",  "JSON", "HTTP",
        "EPS",  "ETF",  "USD", "HKD",  "RMB",  "CEO", "CFO",  "GDP",  "IPO",
        "N/A",  "AI",   "USA", "THE",  "AND",  "FOR", "WITH", "WHAT", "HOW",
        "WHY",  "CAN",  "YOU", "PLEASE"};

    const auto tokens = tokenize_candidate_symbols(text);
    for (const auto &raw_token : tokens) {
        if (raw_token.empty() || raw_token.size() > 10) {
            continue;
        }

        bool has_lowercase = false;
        std::string token;
        token.reserve(raw_token.size());
        for (char ch : raw_token) {
            if (std::islower(static_cast<unsigned char>(ch))) {
                has_lowercase = true;
            }
            token.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        }
        if (has_lowercase || kStopWords.count(token) > 0) {
            continue;
        }

        bool valid = true;
        bool has_alpha = false;
        for (char ch : token) {
            if (std::isalpha(static_cast<unsigned char>(ch))) {
                has_alpha = true;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-' ||
                ch == '/') {
                continue;
            }
            valid = false;
            break;
        }
        if (!valid || !has_alpha) {
            continue;
        }

        const auto normalized = internal::normalize_symbol_alias(token);
        if (normalized.size() >= 1 && normalized.size() <= 10) {
            return normalized;
        }
    }
    return {};
}

AnalysisContext load_analysis_context(const std::string &requested_symbol,
                                      const ValuationConfig &config,
                                      bool allow_db_write) {
    AnalysisContext context;
    context.requested_symbol = requested_symbol;
    context.normalized_symbol = internal::normalize_symbol_alias(requested_symbol);

    const auto profile = data::resolve_fundamentals_db_profile(config.valuecell_db_profile);
    auto db_metrics =
        fetch_financial_metrics_from_db(requested_symbol, config.valuecell_db_profile);
    db_metrics.db_profile = profile.key;
    db_metrics.db_profile_label = profile.label;

    const bool db_has_data = has_useful_structured_data(db_metrics);
    const bool db_missing_symbol =
        db_metrics.error == "db_symbol_not_found" || contains_warning(db_metrics, "db_symbol_not_found");
    const bool db_incomplete =
        contains_warning(db_metrics, "db_missing_latest_quote") ||
        contains_warning(db_metrics, "db_insufficient_annual_history") || !db_has_data;

    if (db_has_data && !db_incomplete) {
        context.metrics = db_metrics;
        context.has_structured_data = true;
        context.loaded_from_db = true;
        return context;
    }

    auto network_metrics = fetch_financial_metrics(requested_symbol, config);
    network_metrics.db_profile = profile.key;
    network_metrics.db_profile_label = profile.label;

    if (has_useful_structured_data(network_metrics)) {
        erase_warning(network_metrics, "db_symbol_not_found");
        erase_warning(network_metrics, "db_missing_latest_quote");
        erase_warning(network_metrics, "db_insufficient_annual_history");
        add_warning_once(network_metrics, "db_network_bootstrap_used");

        context.metrics = network_metrics;
        context.has_structured_data = true;
        context.fetched_from_network = true;
        context.loaded_from_db = db_has_data;

        if (allow_db_write && profile_allows_autowrite(config.valuecell_db_profile)) {
            std::string persist_error;
            if (persist_metrics_snapshot(requested_symbol, network_metrics,
                                         config.valuecell_db_profile, &persist_error)) {
                context.persisted_to_db = true;
                add_warning_once(context.metrics, "db_network_bootstrap_persisted");
            } else if (!persist_error.empty()) {
                add_warning_once(context.metrics, "db_network_bootstrap_persist_failed");
                context.limitation_note = persist_error;
            }
        } else if (allow_db_write && profile.is_demo) {
            add_warning_once(context.metrics, "db_demo_profile_autowrite_skipped");
        }

        return context;
    }

    if (db_has_data) {
        context.metrics = db_metrics;
        context.has_structured_data = true;
        context.loaded_from_db = true;
        context.limitation_note = "network_refresh_unavailable";
        return context;
    }

    context.metrics = network_metrics;
    context.metrics.symbol = context.normalized_symbol;
    context.metrics.db_profile = profile.key;
    context.metrics.db_profile_label = profile.label;
    add_warning_once(context.metrics, "llm_general_knowledge_fallback");
    if (db_missing_symbol) {
        erase_warning(context.metrics, "db_symbol_not_found");
    }
    context.used_llm_knowledge_fallback = true;
    context.limitation_note = "structured_financial_data_unavailable";
    return context;
}

std::string build_llm_analysis_prompt(const std::string &user_request,
                                      const AnalysisContext &context,
                                      bool valuecell_mode) {
    const auto &metrics = context.metrics;

    std::ostringstream prompt;
    prompt << "你是 FinGuard 投研分析助手。\n"
           << "要求：\n"
           << "1. 优先使用下面给出的【结构化财务数据】作为事实基础。\n"
           << "2. 如果某个判断超出了结构化数据，请明确说明这是基于你的通用知识或常识推断。\n"
           << "3. 不要声称使用网页抓取或实时搜索结果。\n"
           << "4. 不要给出仓位比例、买卖指令或收益保证。\n"
           << "5. 必须单独写【数据来源与局限】部分，清楚说明数据来自数据库、自动抓取还是模型知识补充。\n\n";

    prompt << "【用户问题】\n";
    if (!user_request.empty()) {
        prompt << user_request << "\n\n";
    } else if (valuecell_mode) {
        prompt << "请基于以下结构化财务数据，完成一份完整的基本面分析，重点解释估值、成长、盈利质量、财务稳健性、商业模式与主要风险。\n\n";
    } else {
        prompt << "请围绕该公司的投资研究问题给出专业回答。\n\n";
    }

    prompt << "【分析对象】\n"
           << "requested_symbol: " << (context.requested_symbol.empty() ? "N/A" : context.requested_symbol)
           << "\n"
           << "normalized_symbol: "
           << (context.normalized_symbol.empty() ? "N/A" : context.normalized_symbol) << "\n"
           << "active_db_profile: "
           << (metrics.db_profile_label.empty() ? metrics.db_profile : metrics.db_profile_label)
           << "\n\n";

    prompt << "【结构化财务数据】\n";
    if (context.has_structured_data) {
        prompt << "data_source: " << (metrics.data_source.empty() ? "unknown" : metrics.data_source)
               << "\n"
               << "price: " << fmt_number(metrics.price) << "\n"
               << "eps: " << fmt_number(metrics.eps) << "\n"
               << "pe: " << fmt_number(metrics.pe) << "\n"
               << "pb: " << fmt_number(metrics.price_to_book) << "\n"
               << "peg: " << fmt_number(metrics.peg) << "\n"
               << "book_value_per_share: " << fmt_number(metrics.book_value_per_share) << "\n"
               << "avg_roe_5y: " << fmt_percent(metrics.avg_roe_5y) << "\n"
               << "debt_ratio: " << fmt_percent(metrics.debt_ratio) << "\n"
               << "growth_5y_cagr: " << fmt_percent(metrics.growth_5y_cagr) << "\n"
               << "np_recent3_avg: " << fmt_number(metrics.np_recent3_avg) << "\n"
               << "np_past3_avg: " << fmt_number(metrics.np_past3_avg) << "\n"
               << "total_assets: " << fmt_number(metrics.total_assets) << "\n"
               << "total_liabilities: " << fmt_number(metrics.total_liabilities) << "\n"
               << "valuation_label: "
               << (metrics.valuation_label.empty() ? "unknown" : metrics.valuation_label) << "\n";

        prompt << "net_profit_history: ";
        if (metrics.net_profit_history.empty()) {
            prompt << "N/A";
        } else {
            bool first = true;
            for (const auto &point : metrics.net_profit_history) {
                if (!first) {
                    prompt << "; ";
                }
                first = false;
                prompt << point.year << "=" << fmt_signed_number(point.net_income);
            }
        }
        prompt << "\n";

        prompt << "warnings: ";
        if (metrics.warnings.empty()) {
            prompt << "none";
        } else {
            for (std::size_t i = 0; i < metrics.warnings.size(); ++i) {
                if (i > 0) {
                    prompt << ", ";
                }
                prompt << metrics.warnings[i];
            }
        }
        prompt << "\n\n";
    } else {
        prompt << "当前没有可用的结构化财务数据。\n";
        if (!context.limitation_note.empty()) {
            prompt << "原因: " << context.limitation_note << "\n";
        }
        prompt << "\n";
    }

    prompt << "【输出格式】\n"
           << "请使用 Markdown，并严格包含以下 5 个标题：\n"
           << "## 核心结论\n"
           << "## 定量解读\n"
           << "## 商业模式与竞争格局\n"
           << "## 关键风险\n"
           << "## 数据来源与局限\n\n"
           << "在【定量解读】中必须显式引用数字；在【数据来源与局限】中必须说明：\n"
           << "- 是否来自本地数据库\n"
           << "- 是否触发了自动抓取补数\n"
           << "- 是否只能基于模型知识进行补充判断\n";

    return prompt.str();
}

} // namespace finguard::valuation
