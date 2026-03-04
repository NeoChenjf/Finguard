#pragma once

#include <string>

namespace finguard::valuation {

// valuation.json 对应的配置结构体
struct ValuationConfig {
    std::string tavily_api_key;
    std::string alpha_vantage_api_key;
    int yahoo_timeout_ms = 5000;
    int search_max_results = 5;
    int pe_history_quarters = 6;   // 历史 PE 均值使用的季度数（2~8）
    std::string qualitative_prompt_language = "zh";
    std::string http_proxy;        // 格式: "http://host:port"，空表示不用代理
};

// 从 cwd/config/valuation.json 加载配置；文件不存在时返回默认值（empty keys）
ValuationConfig load_valuation_config();

} // namespace finguard::valuation
