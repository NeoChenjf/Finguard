#pragma once

#include "valuation/yahoo_finance_client.h"

namespace finguard::valuation {

// 安全边际判定结果
struct SafetyMarginResult {
    bool in_safety_margin = false;    // (PE < 历史均值) AND (PEG < 1.0)
    bool pe_below_mean = false;       // 当前 PE < 历史均值
    bool peg_below_one = false;       // PEG < 1.0
    std::string reason;               // 人类可读判断原因
    bool insufficient_data = false;   // 数据不足，无法判定
};

// 纯函数：基于财务指标计算安全边际。无 I/O 副作用。
// 规则：safety_margin = (currentPE < historicalPEMean) AND (PEG < 1.0)
SafetyMarginResult calculate_safety_margin(const FinancialMetrics &metrics);

} // namespace finguard::valuation
