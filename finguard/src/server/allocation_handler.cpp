/**
 * @file allocation_handler.cpp
 * @brief 资产配置请求处理器的实现
 */

#include "allocation_handler.h"

#include "core/rule_factory.h"

#include <sstream>
#include <stdexcept>

namespace finguard {
namespace server {

Json::Value AllocationHandler::handle_plan_request(
    const Json::Value& profile_json,
    const Json::Value& portfolio_json,
    const std::string& rule_name) {
    // ── 步骤1：解析参数 ──
    auto params = parse_profile(profile_json);
    if (!portfolio_json.isNull() && portfolio_json.isArray()) {
        params.current_portfolio = parse_portfolio(portfolio_json);
    }

    // ── 步骤2：创建规则实例 ──
    auto rule = core::RuleFactory::create_rule(rule_name);

    // ── 步骤3：计算配置 ──
    auto result = rule->calculate(params);

    // ── 步骤4：转换为JSON响应 ──
    return result_to_json(result);
}

core::AllocationParams AllocationHandler::parse_profile(
    const Json::Value& profile_json) {
    core::AllocationParams params;

    params.age = profile_json.get("age", 30).asInt();
    params.investor_type = profile_json.get("investor_type", "novice").asString();
    params.experience_years = profile_json.get("experience_years", "0-5").asString();
    params.annualized_return = profile_json.get("annualized_return", "0-10").asString();
    params.beat_sp500_10y = profile_json.get("beat_sp500_10y", "no").asString();
    params.individual_stock_percent =
        profile_json.get("individual_stock_percent", 0.0).asDouble();

    // 年龄边界检查
    if (params.age < 18 || params.age > 100) {
        throw std::invalid_argument("Age must be between 18 and 100");
    }

    // 个股占比边界检查
    if (params.individual_stock_percent < 0.0 || params.individual_stock_percent > 1.0) {
        throw std::invalid_argument("individual_stock_percent must be in [0.0, 1.0]");
    }

    return params;
}

std::vector<core::AllocationParams::Holding> AllocationHandler::parse_portfolio(
    const Json::Value& portfolio_json) {
    std::vector<core::AllocationParams::Holding> portfolio;

    for (const auto& item : portfolio_json) {
        if (!item.isMember("symbol") || !item.isMember("weight")) {
            continue;  // 跳过格式不正确的项
        }

        core::AllocationParams::Holding holding;
        holding.symbol = item["symbol"].asString();
        holding.weight = item["weight"].asDouble();

        portfolio.push_back(holding);
    }

    return portfolio;
}

Json::Value AllocationHandler::result_to_json(const core::AllocationResult& result) {
    Json::Value response;

    // 基本信息
    response["rule_name"] = result.rule_name;

    // 建议配置
    Json::Value allocation_json;
    for (const auto& [symbol, weight] : result.allocation) {
        allocation_json[symbol] = weight;
    }
    response["proposed_portfolio"] = allocation_json;

    // 理由
    response["rationale"] = result.rationale;

    // 风险报告
    {
        Json::Value risk_json;
        risk_json["status"] = result.risk_report.status;

        Json::Value rules_json(Json::arrayValue);
        for (const auto& rule : result.risk_report.triggered_rules) {
            rules_json.append(rule);
        }
        risk_json["triggered_rules"] = rules_json;

        response["risk_report"] = risk_json;
    }

    // 调仓建议
    {
        Json::Value actions_json(Json::arrayValue);
        for (const auto& action : result.rebalancing_actions) {
            Json::Value action_json;
            action_json["symbol"] = action.symbol;
            action_json["current_weight"] = action.current_weight;
            action_json["target_weight"] = action.target_weight;
            action_json["action"] = action.action;
            actions_json.append(action_json);
        }
        response["rebalancing_actions"] = actions_json;
    }

    return response;
}

} // namespace server
} // namespace finguard
