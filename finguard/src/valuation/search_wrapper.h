#pragma once

#include "valuation/valuation_config.h"
#include <string>
#include <vector>

namespace finguard::valuation {

// 单条搜索结果摘要
struct SearchSnippet {
    std::string title;
    std::string url;
    std::string content;
};

// Tavily 搜索返回结果
struct SearchResult {
    std::vector<SearchSnippet> snippets;
    std::string combined_text;  // 拼接后的摘要，用于喂给 LLM
    std::string error;          // 非空表示搜索失败
};

// 通过 Tavily API 搜索目标股票的定性信息（护城河、管理层、商业模式）
// api_key 为空时返回 error = "no_tavily_api_key"
SearchResult fetch_qualitative_info(const std::string &symbol,
                                    const ValuationConfig &config);

} // namespace finguard::valuation
