/**
 * @file allocation_rule.h
 * @brief 资产配置规则的基类接口
 *
 * 定义所有配置规则必须实现的接口。每个具体规则（守拙、桥水等）
 * 都继承自这个基类，实现自己的 calculate() 方法。
 */

#pragma once

#include "allocation_types.h"

namespace finguard {
namespace core {

/// 资产配置规则基类
class AllocationRule {
public:
    virtual ~AllocationRule() = default;

    /**
     * 计算资产配置建议
     * @param params 投资人画像与参数
     * @return 配置结果（所有规则输出格式相同）
     * @throws std::invalid_argument 参数不合法
     *
     * 实现说明：
     * - 返回的 allocation 应满足权重和约等于 1.0（浮点容差0.001）
     * - 如果参数违反规则约束，应在 risk_report 中标记告警，status="WARN"
     * - rationale 应提供白话解释，用户可以理解规则逻辑
     */
    virtual AllocationResult calculate(const AllocationParams& params) = 0;

    /// 获取规则名称标识
    virtual std::string rule_name() const = 0;

    /// 获取规则的简短描述（用于UI展示）
    virtual std::string short_description() const = 0;

protected:
    /// 检查权重合法性（权重和应接近1.0，单个资产非负）
    /// @param allocation 权重映射表
    /// @return 是否满足条件
    bool validate_weights(const std::map<std::string, double>& allocation) const;

    /// 浮点比较的容差
    static constexpr double FLOAT_EPSILON = 0.001;
};

} // namespace core
} // namespace finguard
