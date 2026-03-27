/**
 * @file rule_factory.h
 * @brief 资产配置规则工厂类
 *
 * 负责根据规则名称创建对应的规则实例。
 * 采用工厂模式，隔离规则的创建逻辑。
 */

#pragma once

#include "core/allocation_rule.h"

#include <memory>
#include <string>
#include <vector>

namespace finguard {
namespace core {

/// 规则工厂类
class RuleFactory {
public:
    /**
     * 根据规则名称创建规则实例
     * @param rule_name 规则名称 ("shouzhe", "bridgewater", "permanent", "swensen")
     * @return 规则实例（unique_ptr）
     * @throws std::invalid_argument 如果规则名不存在
     */
    static std::unique_ptr<AllocationRule> create_rule(
        const std::string& rule_name);

    /// 列出所有可用規則
    static std::vector<std::string> available_rules();

    /// 获取规则的简短描述
    static std::string get_description(const std::string& rule_name);
};

} // namespace core
} // namespace finguard
