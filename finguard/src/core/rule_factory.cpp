/**
 * @file rule_factory.cpp
 * @brief 资产配置规则工厂类的实现
 */

#include "rule_factory.h"

#include "rules/fixed_rules.h"
#include "rules/shouzhe_rule.h"

#include <stdexcept>

namespace finguard {
namespace core {

std::unique_ptr<AllocationRule> RuleFactory::create_rule(
    const std::string& rule_name) {
    if (rule_name == "shouzhe") {
        return std::make_unique<rules::ShouZheRule>();
    } else if (rule_name == "bridgewater") {
        return std::make_unique<rules::BridgewaterAllWeatherRule>();
    } else if (rule_name == "permanent") {
        return std::make_unique<rules::PermanentPortfolioRule>();
    } else if (rule_name == "swensen") {
        return std::make_unique<rules::SwensenYaleRule>();
    } else {
        throw std::invalid_argument(
            "Unknown allocation rule: " + rule_name +
            ". Available: shouzhe, bridgewater, permanent, swensen");
    }
}

std::vector<std::string> RuleFactory::available_rules() {
    return {
        "shouzhe",      // 守拙价值多元化基金理念
        "bridgewater",  // 桥水全天候策略
        "permanent",    // 永久投资组合
        "swensen"       // 斯文森耶鲁配置
    };
}

std::string RuleFactory::get_description(const std::string& rule_name) {
    auto rule = create_rule(rule_name);
    return rule->short_description();
}

} // namespace core
} // namespace finguard
