/**
 * @file allocation_types.h
 * @brief 资产配置功能的共享数据类型
 *
 * 定义所有规则引擎共用的数据结构，包括输入参数、输出结果等。
 */

#pragma once

#include <map>
#include <string>
#include <vector>

namespace finguard {
namespace core {

/// 投资人画像参数
struct AllocationParams {
    int age;                                 // 年龄
    std::string investor_type;               // 投资人类型 (novice/experienced/professional)
    std::string experience_years;            // 经验年限 (0-5/5-10/10+)
    std::string annualized_return;           // 年化收益 (0-10/10-20/20+%)
    std::string beat_sp500_10y;              // 10年是否跑赢标普500 (yes/no)
    double individual_stock_percent;         // 个股占比 (0.0-1.0)

    // 当前持仓 (可选)
    struct Holding {
        std::string symbol;                  // 资产代码
        double weight;                       // 权重
    };
    std::vector<Holding> current_portfolio;
};

/// 资产配置结果
struct AllocationResult {
    std::string rule_name;                   // 规则名称 (shouzhe/bridgewater/permanent/swensen)
    std::map<std::string, double> allocation;// 建议配置 {资产 -> 权重}
    std::string rationale;                   // 配置理由（白话解释）

    // 风险报告
    struct RiskReport {
        std::string status;                  // 状态 (PASS/WARN/ERROR)
        std::vector<std::string> triggered_rules;  // 触发的规则及告警
    } risk_report;

    // 调仓建议
    struct RebalancingAction {
        std::string symbol;                  // 资产代码
        double current_weight;               // 当前权重
        double target_weight;                // 目标权重
        std::string action;                  // 操作描述 (增持/减持)
    };
    std::vector<RebalancingAction> rebalancing_actions;
};

/// ETF 资产定义（所有规则通用）
struct ETFDefinition {
    std::string symbol;                      // 代码
    std::string name_zh;                     // 中文名称
    std::string name_en;                     // 英文名称
    std::string category;                    // 分类 (stock/bond/commodity/cash)
};

} // namespace core
} // namespace finguard
