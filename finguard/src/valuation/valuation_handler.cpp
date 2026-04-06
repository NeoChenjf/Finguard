#include "valuation/valuation_handler.h"

#include "llm/llm_client.h"
#include "valuation/analysis_workflow.h"
#include "valuation/safety_margin.h"
#include "valuation/valuation_config.h"

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>

namespace finguard::valuation {

namespace {

std::string fmt_double(double v, int prec = 2) {
    if (v < 0.0) return "N/A";
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(prec) << v;
    return oss.str();
}

std::string build_markdown_fallback_report(const std::string &symbol,
                                           const FinancialMetrics &metrics,
                                           const SafetyMarginResult &sm) {
    std::ostringstream md;
    md << "## 核心结论\n";
    md << "当前未能获得可用的 LLM 正式分析，以下为基于结构化财务数据的回退版摘要。\n\n";

    md << "## 定量解读\n";
    md << "- 股价: " << fmt_double(metrics.price) << "\n";
    md << "- EPS: " << fmt_double(metrics.eps) << "\n";
    md << "- PE: " << fmt_double(metrics.pe) << "\n";
    md << "- PB: " << fmt_double(metrics.price_to_book) << "\n";
    md << "- PEG: " << fmt_double(metrics.peg) << "\n";
    md << "- 5年平均 ROE: "
       << (metrics.avg_roe_5y >= 0.0 ? fmt_double(metrics.avg_roe_5y * 100.0) + "%" : "N/A")
       << "\n";
    md << "- 负债率: "
       << (metrics.debt_ratio >= 0.0 ? fmt_double(metrics.debt_ratio * 100.0) + "%" : "N/A")
       << "\n";
    md << "- 5年 CAGR: "
       << (metrics.growth_5y_cagr >= 0.0 ? fmt_double(metrics.growth_5y_cagr * 100.0) + "%" : "N/A")
       << "\n";
    md << "- 每股净资产: " << fmt_double(metrics.book_value_per_share) << "\n";
    md << "- 安全边际: " << (sm.insufficient_data ? "数据不足" : (sm.in_safety_margin ? "是" : "否"))
       << "\n";
    md << "- 安全边际解释: " << sm.reason << "\n\n";

    md << "## 商业模式与竞争格局\n";
    md << "- 当前回退报告不包含 LLM 的定性展开，请在 API Key 可用时重试。\n\n";

    md << "## 关键风险\n";
    if (metrics.warnings.empty()) {
        md << "- 当前未发现额外结构化数据告警。\n\n";
    } else {
        for (const auto &warning : metrics.warnings) {
            md << "- " << warning << "\n";
        }
        md << "\n";
    }

    md << "## 数据来源与局限\n";
    md << "- symbol: " << symbol << "\n";
    md << "- 财务数据来源: " << (metrics.data_source.empty() ? "unknown" : metrics.data_source) << "\n";
    md << "- 本文为无 LLM 正式输出时的后备结果。\n";

    return md.str();
}

bool should_use_llm_output(const llm::StreamResult &result) {
    return !result.degraded && !result.full_text.empty() &&
           result.full_text.find("FinGuard mock stream response") != 0 &&
           result.full_text.find("FinGuard fallback response") != 0;
}

} // namespace

void handle_valuecell(const drogon::HttpRequestPtr &req,
                      std::function<void(const drogon::HttpResponsePtr &)> &&cb) {
    using namespace drogon;
    const auto start = std::chrono::steady_clock::now();

    auto json_ptr = req ? req->getJsonObject() : nullptr;
    if (!json_ptr || !json_ptr->isMember("symbol")) {
        Json::Value err;
        err["error"] = "missing_symbol";
        err["message"] = "请求体必须包含 symbol 字段，如 {\"symbol\": \"AAPL\"}";
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        cb(resp);
        return;
    }

    const std::string requested_symbol = (*json_ptr)["symbol"].asString();
    if (requested_symbol.empty() || requested_symbol.size() > 20) {
        Json::Value err;
        err["error"] = "invalid_symbol";
        err["message"] = "symbol 格式不合法";
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        cb(resp);
        return;
    }
    spdlog::info("[ValuationHandler] Start analysis for {}", requested_symbol);

    const auto val_config = load_valuation_config();
    const auto analysis_context = load_analysis_context(requested_symbol, val_config, true);
    const auto &metrics = analysis_context.metrics;

    const auto sm = calculate_safety_margin(metrics);

    llm::StreamResult llm_result;
    std::string markdown;
    const auto prompt = build_llm_analysis_prompt(
        "请基于给定结构化财务数据完成一份 ValueCell 基本面分析，重点解释估值、成长、盈利质量、资本结构、商业模式与关键风险。",
        analysis_context,
        true);

    llm::LlmClient client;
    llm_result = client.stream_chat(prompt);
    if (should_use_llm_output(llm_result)) {
        markdown = llm_result.full_text;
    } else {
        markdown = build_markdown_fallback_report(metrics.symbol.empty() ? requested_symbol : metrics.symbol,
                                                 metrics, sm);
    }

    const auto elapsed_ms = static_cast<int>(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
            .count());

    Json::Value resp_body;
    resp_body["symbol"] = metrics.symbol;
    if (metrics.symbol != requested_symbol) {
        resp_body["requested_symbol"] = requested_symbol;
    }
    if (!metrics.db_profile.empty()) {
        resp_body["db_profile"] = metrics.db_profile;
    }
    if (!metrics.db_profile_label.empty()) {
        resp_body["db_profile_label"] = metrics.db_profile_label;
    }

    resp_body["book_value_per_share"] =
        (metrics.book_value_per_share > 0) ? metrics.book_value_per_share : -1.0;
    resp_body["eps"] = (metrics.eps > 0) ? metrics.eps : -1.0;
    resp_body["price"] = (metrics.price > 0) ? metrics.price : -1.0;
    resp_body["total_assets"] = (metrics.total_assets > 0) ? metrics.total_assets : -1.0;
    resp_body["total_liabilities"] =
        (metrics.total_liabilities >= 0) ? metrics.total_liabilities : -1.0;
    resp_body["debt_ratio"] = (metrics.debt_ratio >= 0) ? metrics.debt_ratio : -1.0;
    resp_body["np_recent3_avg"] = metrics.np_recent3_avg;
    resp_body["np_past3_avg"] = metrics.np_past3_avg;
    resp_body["growth_5y_cagr"] = metrics.growth_5y_cagr;
    resp_body["avg_roe_5y"] = (metrics.avg_roe_5y >= 0) ? metrics.avg_roe_5y : -1.0;
    resp_body["pe"] = (metrics.pe > 0) ? metrics.pe : -1.0;
    resp_body["peg"] = (metrics.peg > 0) ? metrics.peg : -1.0;
    resp_body["valuation_label"] = metrics.valuation_label;

    Json::Value net_profit_arr(Json::arrayValue);
    for (const auto v : metrics.net_profit_8y) {
        net_profit_arr.append(v);
    }
    resp_body["net_profit_8y"] = net_profit_arr;

    Json::Value net_profit_history_arr(Json::arrayValue);
    for (const auto &row : metrics.net_profit_history) {
        Json::Value one;
        one["year"] = row.year;
        one["net_income"] = row.net_income;
        one["source"] = row.source;
        net_profit_history_arr.append(one);
    }
    resp_body["net_profit_history"] = net_profit_history_arr;
    resp_body["net_profit_source"] = metrics.net_profit_source;
    resp_body["net_profit_updated_at"] = metrics.net_profit_updated_at;

    Json::Value warnings_arr(Json::arrayValue);
    for (const auto &w : metrics.warnings) {
        warnings_arr.append(w);
    }
    resp_body["warnings"] = warnings_arr;

    resp_body["current_pe"] = (metrics.trailing_pe > 0) ? metrics.trailing_pe : -1.0;
    resp_body["historical_pe_mean"] =
        (metrics.historical_pe_mean > 0) ? metrics.historical_pe_mean : -1.0;
    resp_body["current_peg"] = (metrics.peg_ratio > 0) ? metrics.peg_ratio : -1.0;
    resp_body["price_to_book"] = (metrics.price_to_book > 0) ? metrics.price_to_book : -1.0;
    resp_body["earnings_growth"] = metrics.earnings_growth;
    resp_body["pe_history_quarters_used"] = metrics.pe_history_quarters_used;

    resp_body["safety_margin"] = sm.insufficient_data ? false : sm.in_safety_margin;
    resp_body["safety_margin_reason"] = sm.reason;

    resp_body["qualitative_score"] = -1.0;
    Json::Value qa_detail;
    qa_detail["moat"] = "";
    qa_detail["management"] = "";
    qa_detail["business_model"] = "";
    resp_body["qualitative_analysis"] = qa_detail;

    resp_body["investment_conclusion"] = markdown;
    resp_body["data_source"] = metrics.data_source.empty() ? "unknown" : metrics.data_source;
    resp_body["analysis_time_ms"] = elapsed_ms;

    if (!metrics.error.empty()) {
        resp_body["data_warning"] = metrics.error;
    }
    if (!llm_result.error.empty()) {
        resp_body["llm_warning"] = llm_result.error;
    }

    spdlog::info("[ValuationHandler] {} done in {}ms — safety_margin={}, llm_degraded={}",
                 metrics.symbol, elapsed_ms, sm.in_safety_margin, llm_result.degraded);

    auto http_resp = HttpResponse::newHttpJsonResponse(resp_body);
    http_resp->setStatusCode(k200OK);
    cb(http_resp);
}

} // namespace finguard::valuation
