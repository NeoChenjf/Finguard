/**
 * @file fixed_rules.h
 * @brief 固定配置规则组（桥水、永久组合、斯文森）
 *
 * 这三个规则的配置是固定的，不随年龄或投资人类型变化。
 */

#pragma once

#include "core/allocation_rule.h"

namespace finguard {
namespace core {
namespace rules {

/// 桥水全天候策略 (Ray Dalio)
/// 固定配置：股票30% | 债券40% | TIPS15% | 大宗商品/黄金15%
class BridgewaterAllWeatherRule : public AllocationRule {
public:
    AllocationResult calculate(const AllocationParams& params) override;

    std::string rule_name() const override { return "bridgewater"; }

    std::string short_description() const override {
        return "桥水全天候策略 - 风险平衡，固定配置";
    }
};

/// 永久投资组合 (Harry Browne)
/// 固定配置：股票25% | 债券25% | 黄金25% | 现金25%（完美平衡）
class PermanentPortfolioRule : public AllocationRule {
public:
    AllocationResult calculate(const AllocationParams& params) override;

    std::string rule_name() const override { return "permanent"; }

    std::string short_description() const override {
        return "永久投资组合 - 懒人配置，完美平衡";
    }
};

/// 斯文森耶鲁配置规则 (David Swensen)
/// 固定配置：美股30% | 国际发达市场15% | 新兴市场10% | REITs20% | 债券15% |
/// 黄金10%
class SwensenYaleRule : public AllocationRule {
public:
    AllocationResult calculate(const AllocationParams& params) override;

    std::string rule_name() const override { return "swensen"; }

    std::string short_description() const override {
        return "斯文森耶鲁配置 - 高度多元化，学术背书";
    }
};

} // namespace rules
} // namespace core
} // namespace finguard
