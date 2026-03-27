/**
 * @file allocation_handler.h
 * @brief 资产配置请求处理器
 *
 * 处理 /api/v1/plan 的多规则支持，从HTTP请求解析参数，
 * 调用规则引擎，生成JSON响应。
 * 
 * 这个模块隔离了规则引擎的细节，使routes.cpp保持简洁。
 */

#pragma once

#include "core/allocation_types.h"

#include <json/value.h>
#include <string>

namespace finguard {
namespace server {

/// 资产配置处理器
class AllocationHandler {
public:
    /**
     * 处理资产配置请求
     * @param profile_json 投资人画像 JSON 对象
     * @param portfolio_json 当前持仓 JSON 数组 (可选)
     * @param rule_name 规则名称 (可选，默认 "shouzhe")
     * @return 包含建议配置的 JSON 响应体
     * @throws std::invalid_argument 参数不合法
     *
     * 输出JSON格式：
     * {
     *   "rule_name": "shouzhe",
     *   "proposed_portfolio": { "VOO": 0.38, ... },
     *   "risk_report": { "status": "PASS", "triggered_rules": [...] },
     *   "rationale": "...",
     *   "rebalancing_actions": [
     *     { "symbol": "VOO", "current_weight": 0.3, "target_weight": 0.38, "action": "增持" }
     *   ]
     * }
     */
    static Json::Value handle_plan_request(
        const Json::Value& profile_json,
        const Json::Value& portfolio_json,
        const std::string& rule_name = "shouzhe");

private:
    /// 从 JSON 对象解析投资人参数
    static core::AllocationParams parse_profile(const Json::Value& profile_json);

    /// 从 JSON 数组解析当前持仓
    static std::vector<core::AllocationParams::Holding> parse_portfolio(
        const Json::Value& portfolio_json);

    /// 将规则结果转换为 JSON 响应
    static Json::Value result_to_json(const core::AllocationResult& result);
};

} // namespace server
} // namespace finguard
