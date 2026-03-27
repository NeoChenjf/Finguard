/**
 * @file shouzhe_rule.h
 * @brief 守拙价值多元化基金理念规则
 *
 * 核心特性：
 * - 年龄驱动：债券占比 = age_decade / 100
 * - 黄金固定 10%
 * - 指数配比固定：港股:A股:美股 = 1:3:16
 * - 支持投资人分层（novice/experienced/professional）个股权重上限
 */

#pragma once

#include "core/allocation_rule.h"

namespace finguard {
namespace core {
namespace rules {

class ShouZheRule : public AllocationRule {
public:
    AllocationResult calculate(const AllocationParams& params) override;

    std::string rule_name() const override { return "shouzhe"; }

    std::string short_description() const override {
        return "守拙价值多元化基金理念 - 年龄驱动，个股可选";
    }

private:
    /// 根据年龄计算债券占比
    /// 公式：债券占比 = (age / 10) * 10 / 100
    /// 如：30岁 -> 30%, 35岁 -> 30%, 40岁 -> 40%
    static double calculate_bond_by_age(int age);

    /// 根据投资人类型和参数确定个股权重上限
    static double get_individual_stock_limit(const AllocationParams& params);

    /// 计算指数部分配置（港:A:美 = 1:3:16）
    static std::map<std::string, double> calculate_index_allocation(double index_equity);

    /// 检查并生成风控告警
    static AllocationResult::RiskReport generate_risk_report(
        const AllocationParams& params,
        const std::map<std::string, double>& allocation);
};

} // namespace rules
} // namespace core
} // namespace finguard
