#pragma once

#include "valuation/valuation_config.h"
#include <string>
#include <vector>

namespace finguard::valuation {

// 来自 Yahoo Finance / Alpha Vantage 的结构化财务指标
struct FinancialMetrics {
    std::string symbol;

    // 估值指标
    double trailing_pe = -1.0;       // 当前 trailing P/E
    double forward_pe = -1.0;        // 预期 P/E
    double price_to_book = -1.0;     // 市净率 P/B
    double peg_ratio = -1.0;         // PEG
    double trailing_eps = 0.0;       // 当前 trailing EPS（12个月）
    double earnings_growth = 0.0;    // 盈利增长率

    // 安全边际所需的历史 PE 数据（由 price history 计算）
    double historical_pe_mean = -1.0;       // 历史 PE 均值
    std::vector<double> historical_pe_series; // 各季度 PE 序列（用于调试）
    int pe_history_quarters_used = 0;       // 实际使用的季度数

    // 元信息
    bool has_sufficient_data = false;   // 是否有足够数据进行安全边际判定
    std::string data_source;            // "yahoo_finance" / "alpha_vantage"
    std::string error;                  // 非空表示获取失败
};

// 获取财务指标。先尝试 Yahoo Finance，失败时降级到 Alpha Vantage。
// 同步阻塞调用，超时由 config.yahoo_timeout_ms 控制。
FinancialMetrics fetch_financial_metrics(const std::string &symbol,
                                         const ValuationConfig &config);

} // namespace finguard::valuation
