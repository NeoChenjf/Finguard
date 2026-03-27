/**
 * @file fixed_rules.cpp
 * @brief 固定配置规则组的实现
 */

#include "fixed_rules.h"

#include <sstream>

namespace finguard {
namespace core {
namespace rules {

// ════════════════════════════════════════════════════════════════════════════
// 桥水全天候策略
// ════════════════════════════════════════════════════════════════════════════

AllocationResult BridgewaterAllWeatherRule::calculate(
    const AllocationParams& params) {
    AllocationResult result;
    result.rule_name = rule_name();

    // 固定配置
    result.allocation["VOO"] = 0.30;   // 股票
    result.allocation["BND"] = 0.40;   // 长期债券
    result.allocation["TIP"] = 0.15;   // TIPS（通膨保护债券）
    result.allocation["GSG"] = 0.15;   // 大宗商品/黄金

    // 理由
    {
        std::ostringstream oss;
        oss << "桥水全天候策略（Ray Dalio）：";
        oss << "通过4类资产平衡风险敞口，应对经济周期各种情景。";
        oss << "股票30%追求增长，债券40%对冲衰退，TIPS15%应对通膨，";
        oss << "大宗商品15%应对滞胀。";
        oss << "预期年化收益: 8-12% | 最大回撤: -10~-15%";
        result.rationale = oss.str();
    }

    // 风控报告（固定配置通常无风险告警）
    result.risk_report.status = "PASS";
    result.risk_report.triggered_rules.clear();

    // 调仓建议
    if (!params.current_portfolio.empty()) {
        const double rebalance_threshold = 0.005;

        // 构建当前持仓映射
        std::map<std::string, double> current;
        for (const auto& holding : params.current_portfolio) {
            current[holding.symbol] = holding.weight;
        }

        // 生成调仓建议
        for (const auto& [symbol, target_weight] : result.allocation) {
            double current_weight = current.count(symbol) ? current[symbol] : 0.0;
            double diff = target_weight - current_weight;

            if (std::fabs(diff) > rebalance_threshold) {
                AllocationResult::RebalancingAction action;
                action.symbol = symbol;
                action.current_weight = current_weight;
                action.target_weight = target_weight;
                action.action = (diff > 0) ? "增持" : "减持";
                result.rebalancing_actions.push_back(action);
            }
        }
    }

    return result;
}

// ════════════════════════════════════════════════════════════════════════════
// 永久投资组合
// ════════════════════════════════════════════════════════════════════════════

AllocationResult PermanentPortfolioRule::calculate(
    const AllocationParams& params) {
    AllocationResult result;
    result.rule_name = rule_name();

    // 固定配置：完美平衡
    result.allocation["VOO"] = 0.25;   // 股票
    result.allocation["TLT"] = 0.25;   // 长期债券
    result.allocation["GLD"] = 0.25;   // 黄金
    result.allocation["SHV"] = 0.25;   // 现金（短期债券）

    // 理由
    {
        std::ostringstream oss;
        oss << "永久投资组合（Harry Browne）：";
        oss << "通过4类资产等权重平衡，对冲经济4状态：";
        oss << "股票25%应对增长期，长期债券25%应对衰退，";
        oss << "黄金25%应对通膨，现金25%应对通缩。";
        oss << "最简单的配置，无需定期调整。";
        oss << "预期年化收益: 6-9% | 最大回撤: -8~-12%";
        result.rationale = oss.str();
    }

    // 风控报告
    result.risk_report.status = "PASS";
    result.risk_report.triggered_rules.clear();

    // 调仓建议
    if (!params.current_portfolio.empty()) {
        const double rebalance_threshold = 0.005;

        std::map<std::string, double> current;
        for (const auto& holding : params.current_portfolio) {
            current[holding.symbol] = holding.weight;
        }

        for (const auto& [symbol, target_weight] : result.allocation) {
            double current_weight = current.count(symbol) ? current[symbol] : 0.0;
            double diff = target_weight - current_weight;

            if (std::fabs(diff) > rebalance_threshold) {
                AllocationResult::RebalancingAction action;
                action.symbol = symbol;
                action.current_weight = current_weight;
                action.target_weight = target_weight;
                action.action = (diff > 0) ? "增持" : "减持";
                result.rebalancing_actions.push_back(action);
            }
        }
    }

    return result;
}

// ════════════════════════════════════════════════════════════════════════════
// 斯文森耶鲁配置规则
// ════════════════════════════════════════════════════════════════════════════

AllocationResult SwensenYaleRule::calculate(const AllocationParams& params) {
    AllocationResult result;
    result.rule_name = rule_name();

    // 固定配置：多元化
    result.allocation["VOO"] = 0.30;    // 美国股票
    result.allocation["VEA"] = 0.15;    // 国际发达市场
    result.allocation["VWO"] = 0.10;    // 新兴市场
    result.allocation["VNQ"] = 0.20;    // REITs（房产）
    result.allocation["BND"] = 0.15;    // 债券
    result.allocation["GLD"] = 0.10;    // 黄金

    // 理由
    {
        std::ostringstream oss;
        oss << "斯文森耶鲁配置规则（David Swensen）：";
        oss << "美国名校捐赠基金的资产配置框架，长期年复合收益超过15%。";
        oss << "美股30%+国际15%+新兴10%=股票多元化55%；";
        oss << "REITs20%+债券15%+黄金10%=防守与对冲35%。";
        oss << "高度多元化，追求长期α收益。";
        oss << "预期年化收益: 10-15% | 最大回撤: -15~-25%";
        result.rationale = oss.str();
    }

    // 风控报告
    result.risk_report.status = "PASS";
    result.risk_report.triggered_rules.clear();

    // 调仓建议
    if (!params.current_portfolio.empty()) {
        const double rebalance_threshold = 0.005;

        std::map<std::string, double> current;
        for (const auto& holding : params.current_portfolio) {
            current[holding.symbol] = holding.weight;
        }

        for (const auto& [symbol, target_weight] : result.allocation) {
            double current_weight = current.count(symbol) ? current[symbol] : 0.0;
            double diff = target_weight - current_weight;

            if (std::fabs(diff) > rebalance_threshold) {
                AllocationResult::RebalancingAction action;
                action.symbol = symbol;
                action.current_weight = current_weight;
                action.target_weight = target_weight;
                action.action = (diff > 0) ? "增持" : "减持";
                result.rebalancing_actions.push_back(action);
            }
        }
    }

    return result;
}

} // namespace rules
} // namespace core
} // namespace finguard
