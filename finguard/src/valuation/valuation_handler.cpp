#include "valuation/valuation_handler.h"

#include "valuation/safety_margin.h"
#include "valuation/search_wrapper.h"
#include "valuation/valuation_config.h"
#include "valuation/yahoo_finance_client.h"
#include "llm/llm_client.h"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>

namespace finguard::valuation {

namespace {

// ── 辅助：将 double 格式化为指定小数位字符串 ──
std::string fmt_double(double v, int prec = 2) {
    if (v < 0.0) return "N/A";
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(prec) << v;
    return oss.str();
}

// ── 构建 LLM 定性评分 prompt ──
std::string build_qualitative_prompt(const std::string &symbol,
                                      const SearchResult &search,
                                      const FinancialMetrics &metrics,
                                      const std::string &lang) {
    std::ostringstream prompt;

    if (lang == "zh") {
        prompt << "你是一位专业的价值投资分析师。请基于以下关于 " << symbol
               << " 的搜索资料，给出定性分析。\n\n";
        if (!search.combined_text.empty()) {
            prompt << "参考资料：\n" << search.combined_text << "\n";
        } else {
            prompt << "（未能获取搜索资料，请基于你的知识进行分析）\n\n";
        }
        prompt << "财务背景：当前 PE=" << fmt_double(metrics.trailing_pe)
               << "，历史 PE 均值=" << fmt_double(metrics.historical_pe_mean)
               << "，PEG=" << fmt_double(metrics.peg_ratio)
               << "，安全边际="
               << (metrics.has_sufficient_data &&
                       metrics.trailing_pe < metrics.historical_pe_mean &&
                       metrics.peg_ratio > 0.0 && metrics.peg_ratio < 1.0
                       ? "是"
                       : "否")
               << "。\n\n";
        prompt
            << "请以以下 JSON 格式回复（不要包含任何其他内容）：\n"
               R"({
  "qualitative_score": <0.0~10.0的浮点数，10=极强竞争壁垒>,
  "moat": "<护城河评价，1~2句>",
  "management": "<管理层评价，1~2句>",
  "business_model": "<商业模式描述，1~2句>",
  "conclusion": "<核心投资结论，2~3句，综合定性与定量>"
})";
    } else {
        prompt << "You are a professional value investing analyst. Based on the search "
                  "results below for " << symbol << ", provide a qualitative analysis.\n\n";
        if (!search.combined_text.empty()) {
            prompt << "References:\n" << search.combined_text << "\n";
        } else {
            prompt << "(No search data available; use your knowledge.)\n\n";
        }
        prompt << "Financial context: Trailing PE=" << fmt_double(metrics.trailing_pe)
               << ", Historical PE mean=" << fmt_double(metrics.historical_pe_mean)
               << ", PEG=" << fmt_double(metrics.peg_ratio) << ".\n\n";
        prompt
            << "Reply ONLY in this JSON format:\n"
               R"({
  "qualitative_score": <float 0.0~10.0>,
  "moat": "<moat analysis, 1-2 sentences>",
  "management": "<management assessment, 1-2 sentences>",
  "business_model": "<business model description, 1-2 sentences>",
  "conclusion": "<core investment conclusion, 2-3 sentences>"
})";
    }

    return prompt.str();
}

// ── 从 LLM full_text 中解析 JSON ──
// 兼容 LLM 在 JSON 外包了 markdown 代码块的情况
struct QualitativeAnalysis {
    double qualitative_score = -1.0;
    std::string moat;
    std::string management;
    std::string business_model;
    std::string conclusion;
    bool parse_ok = false;
};

QualitativeAnalysis parse_qualitative_json(const std::string &llm_output) {
    QualitativeAnalysis qa;

    // 尝试提取第一个 { ... } 块
    const auto brace_open  = llm_output.find('{');
    const auto brace_close = llm_output.rfind('}');
    if (brace_open == std::string::npos || brace_close == std::string::npos ||
        brace_close <= brace_open) {
        return qa;
    }

    const std::string json_str = llm_output.substr(brace_open,
                                                    brace_close - brace_open + 1);
    try {
        auto j = nlohmann::json::parse(json_str);
        qa.qualitative_score = j.value("qualitative_score", -1.0);
        qa.moat              = j.value("moat", "");
        qa.management        = j.value("management", "");
        qa.business_model    = j.value("business_model", "");
        qa.conclusion        = j.value("conclusion", "");
        qa.parse_ok          = true;
    } catch (...) {
        // JSON 解析失败：返回 parse_ok = false
    }
    return qa;
}

// ── 生成 Markdown 分析报告 ──
std::string build_markdown_report(const std::string &symbol,
                                   const FinancialMetrics &metrics,
                                   const SafetyMarginResult &sm,
                                   const QualitativeAnalysis &qa) {
    std::ostringstream md;
    md << "## Value Cell 分析报告 — " << symbol << "\n\n";
    md << "| 指标 | 值 |\n";
    md << "|------|----|\n";
    md << "| 定性评分 | "
       << (qa.qualitative_score >= 0 ? fmt_double(qa.qualitative_score, 1) + " / 10" : "N/A")
       << " |\n";
    md << "| 当前 PE | " << fmt_double(metrics.trailing_pe) << " |\n";
    md << "| 历史 PE 均值 | " << fmt_double(metrics.historical_pe_mean) << " |\n";
    md << "| 当前 PEG | " << fmt_double(metrics.peg_ratio) << " |\n";
    md << "| 市净率 (PB) | " << fmt_double(metrics.price_to_book) << " |\n";

    if (sm.insufficient_data) {
        md << "| 安全边际判定 | ⚠️ 数据不足 |\n";
    } else {
        md << "| 安全边际判定 | "
           << (sm.in_safety_margin ? "✅ Yes" : "❌ No") << " |\n";
    }
    md << "| 数据来源 | " << metrics.data_source << " |\n";

    md << "\n### 安全边际分析\n" << sm.reason << "\n";

    md << "\n### 核心投资结论\n"
       << (qa.conclusion.empty() ? "（LLM 分析暂不可用）" : qa.conclusion)
       << "\n";

    md << "\n### 定性分析\n";
    md << "- **护城河**：" << (qa.moat.empty() ? "N/A" : qa.moat) << "\n";
    md << "- **管理层**：" << (qa.management.empty() ? "N/A" : qa.management) << "\n";
    md << "- **商业模式**：" << (qa.business_model.empty() ? "N/A" : qa.business_model) << "\n";

    return md.str();
}

} // namespace (anonymous)

// ── 路由处理器 ──
void handle_valuecell(const drogon::HttpRequestPtr &req,
                      std::function<void(const drogon::HttpResponsePtr &)> &&cb) {
    using namespace drogon;
    const auto start = std::chrono::steady_clock::now();

    // ── Step 1: 解析请求 ──
    auto json_ptr = req ? req->getJsonObject() : nullptr;
    if (!json_ptr || !json_ptr->isMember("symbol")) {
        Json::Value err;
        err["error"]   = "missing_symbol";
        err["message"] = "请求体必须包含 symbol 字段，如 {\"symbol\": \"AAPL\"}";
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        cb(resp);
        return;
    }
    const std::string symbol = (*json_ptr)["symbol"].asString();
    if (symbol.empty() || symbol.size() > 20) {
        Json::Value err;
        err["error"]   = "invalid_symbol";
        err["message"] = "symbol 格式不合法";
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        cb(resp);
        return;
    }
    spdlog::info("[ValuationHandler] Start analysis for {}", symbol);

    // ── Step 2: 加载配置 ──
    const auto val_config = load_valuation_config();

    // ── Step 3: 获取财务指标（Yahoo Finance → Alpha Vantage 降级）──
    const auto metrics = fetch_financial_metrics(symbol, val_config);

    // ── Step 4: 安全边际计算 ──
    const auto sm = calculate_safety_margin(metrics);

    // ── Step 5: 定性搜索（Tavily）──
    const auto search = fetch_qualitative_info(symbol, val_config);

    // ── Step 6: LLM 定性评分 ──
    const std::string prompt = build_qualitative_prompt(
        symbol, search, metrics, val_config.qualitative_prompt_language);

    llm::LlmClient llm_client;
    const auto llm_result = llm_client.stream_chat(prompt);

    QualitativeAnalysis qa;
    if (!llm_result.degraded && !llm_result.full_text.empty()) {
        qa = parse_qualitative_json(llm_result.full_text);
        if (!qa.parse_ok) {
            spdlog::warn("[ValuationHandler] LLM JSON parse failed for {}: {}",
                         symbol, llm_result.full_text.substr(0, 200));
            // 降级：把 full_text 直接作为 conclusion
            qa.conclusion = llm_result.full_text;
        }
    } else {
        spdlog::warn("[ValuationHandler] LLM degraded for {}: {}", symbol,
                     llm_result.error);
    }

    // ── Step 7: 生成 Markdown 报告 ──
    const std::string markdown = build_markdown_report(symbol, metrics, sm, qa);

    // ── Step 8: 组装响应 JSON ──
    const auto elapsed_ms = static_cast<int>(
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start)
            .count());

    Json::Value resp_body;
    resp_body["symbol"] = symbol;

    // 财务指标
    resp_body["current_pe"]         = (metrics.trailing_pe > 0) ? metrics.trailing_pe : -1.0;
    resp_body["historical_pe_mean"] = (metrics.historical_pe_mean > 0) ? metrics.historical_pe_mean : -1.0;
    resp_body["current_peg"]        = (metrics.peg_ratio > 0) ? metrics.peg_ratio : -1.0;
    resp_body["price_to_book"]      = (metrics.price_to_book > 0) ? metrics.price_to_book : -1.0;
    resp_body["earnings_growth"]    = metrics.earnings_growth;
    resp_body["pe_history_quarters_used"] = metrics.pe_history_quarters_used;

    // 安全边际
    resp_body["safety_margin"]      = sm.insufficient_data ? false : sm.in_safety_margin;
    resp_body["safety_margin_reason"] = sm.reason;

    // 定性分析
    resp_body["qualitative_score"]  = (qa.qualitative_score >= 0) ? qa.qualitative_score : -1.0;
    Json::Value qa_detail;
    qa_detail["moat"]           = qa.moat;
    qa_detail["management"]     = qa.management;
    qa_detail["business_model"] = qa.business_model;
    resp_body["qualitative_analysis"] = qa_detail;

    // 结论与报告
    resp_body["investment_conclusion"] = markdown;
    resp_body["data_source"]         = metrics.data_source.empty() ? "unknown" : metrics.data_source;
    resp_body["analysis_time_ms"]    = elapsed_ms;

    // 数据警告
    if (!metrics.error.empty()) {
        resp_body["data_warning"] = metrics.error;
    }
    if (!search.error.empty()) {
        resp_body["search_warning"] = search.error;
    }
    if (!llm_result.error.empty()) {
        resp_body["llm_warning"] = llm_result.error;
    }

    spdlog::info("[ValuationHandler] {} done in {}ms — safety_margin={}, score={:.1f}",
                 symbol, elapsed_ms, sm.in_safety_margin,
                 (qa.qualitative_score >= 0 ? qa.qualitative_score : -1.0));

    auto http_resp = HttpResponse::newHttpJsonResponse(resp_body);
    http_resp->setStatusCode(k200OK);
    cb(http_resp);
}

} // namespace finguard::valuation
