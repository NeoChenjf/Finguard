/**
 * @file allocation_rule.cpp
 * @brief 资产配置规则基类的实现
 */

#include "allocation_rule.h"

#include <cmath>

namespace finguard {
namespace core {

bool AllocationRule::validate_weights(
    const std::map<std::string, double>& allocation) const {
    // 检查单个资产权重非负
    for (const auto& [symbol, weight] : allocation) {
        if (weight < 0) {
            return false;
        }
    }

    // 检查权重和接近 1.0（容差 FLOAT_EPSILON）
    double sum = 0.0;
    for (const auto& [symbol, weight] : allocation) {
        sum += weight;
    }

    return std::fabs(sum - 1.0) < FLOAT_EPSILON;
}

} // namespace core
} // namespace finguard
