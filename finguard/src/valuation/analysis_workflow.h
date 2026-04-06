#pragma once

#include <string>

#include "valuation/valuation_config.h"
#include "valuation/yahoo_finance_client.h"

namespace finguard::valuation {

struct AnalysisContext {
    std::string requested_symbol;
    std::string normalized_symbol;
    FinancialMetrics metrics;
    bool has_structured_data = false;
    bool loaded_from_db = false;
    bool fetched_from_network = false;
    bool persisted_to_db = false;
    bool used_llm_knowledge_fallback = false;
    std::string limitation_note;
};

std::string extract_symbol_candidate(const std::string &text);

AnalysisContext load_analysis_context(const std::string &requested_symbol,
                                      const ValuationConfig &config,
                                      bool allow_db_write = true);

std::string build_llm_analysis_prompt(const std::string &user_request,
                                      const AnalysisContext &context,
                                      bool valuecell_mode = false);

} // namespace finguard::valuation
