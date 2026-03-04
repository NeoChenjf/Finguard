#include "valuation/safety_margin.h"

#include <sstream>

namespace finguard::valuation {

SafetyMarginResult calculate_safety_margin(const FinancialMetrics &metrics) {
    SafetyMarginResult result;

    // ── 数据完整性检查 ──
    if (!metrics.has_sufficient_data ||
        metrics.trailing_pe <= 0.0 ||
        metrics.historical_pe_mean <= 0.0 ||
        metrics.peg_ratio <= -1.0) {
        result.insufficient_data = true;
        result.reason = "数据不足：缺少 trailing PE、历史 PE 均值或 PEG";
        return result;
    }

    // ── 两条判定规则 ──
    result.pe_below_mean = (metrics.trailing_pe < metrics.historical_pe_mean);
    result.peg_below_one = (metrics.peg_ratio > 0.0 && metrics.peg_ratio < 1.0);

    // ── 综合判定 ──
    result.in_safety_margin = result.pe_below_mean && result.peg_below_one;

    // ── 人类可读原因 ──
    std::ostringstream oss;
    oss << "当前 PE=" << metrics.trailing_pe
        << "，历史均值=" << metrics.historical_pe_mean
        << "，PEG=" << metrics.peg_ratio << "。";
    if (result.pe_below_mean) {
        oss << "当前 PE 低于历史均值（✅）";
    } else {
        oss << "当前 PE 高于历史均值（❌）";
    }
    oss << "；";
    if (result.peg_below_one) {
        oss << "PEG < 1.0，增长被低估（✅）";
    } else {
        oss << "PEG ≥ 1.0，增长已被充分定价（❌）";
    }
    if (result.in_safety_margin) {
        oss << "→ 处于安全边际区间。";
    } else {
        oss << "→ 不在安全边际区间。";
    }
    result.reason = oss.str();

    return result;
}

} // namespace finguard::valuation
