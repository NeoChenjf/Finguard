/**
 * @file shouzhe_rule.cpp
 * @brief 守拙价值多元化基金理念规则的实现
 */

#include "shouzhe_rule.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace finguard {
namespace core {
namespace rules {

AllocationResult ShouZheRule::calculate(const AllocationParams& params) {
    AllocationResult result;
    result.rule_name = rule_name();

    // ── 步骤1：计算基础配置 ──
    const double gold = 0.10;  // 黄金固定 10%
    const double bond = calculate_bond_by_age(params.age);
    const double equity = (std::max)(1.0 - gold - bond, 0.0);

    // ── 步骤2：计算个股占比 ──
    const double individual_stock_limit = get_individual_stock_limit(params);
    const double actual_stock_pick =
        (std::min)(params.individual_stock_percent, individual_stock_limit);
    const double index_equity = equity - actual_stock_pick;

    // ── 步骤3：计算指数分配 ──
    auto index_allocation = calculate_index_allocation(index_equity);

    // ── 步骤4：构建最终配置 ──
    result.allocation["GLD"] = gold;
    result.allocation["BND"] = bond;
    result.allocation["VOO"] = index_allocation["VOO"];
    result.allocation["HS300"] = index_allocation["HS300"];
    result.allocation["HSI"] = index_allocation["HSI"];
    result.allocation["individual_stock"] = actual_stock_pick;

    // ── 步骤5：生成理由 ──
    {
        std::ostringstream oss;
        oss << "基于守拙价值多元化基金理念：年龄 " << params.age << " 岁 → ";
        oss << "债券(BND) " << static_cast<int>(bond * 100) << "%, ";
        oss << "黄金(GLD) 10%, 股票 " << static_cast<int>(equity * 100) << "%。";

        if (params.investor_type == "novice") {
            oss << " 小白投资人：全部使用指数基金，不配个股。";
        } else if (params.investor_type == "experienced") {
            int limit = (std::min)(static_cast<int>(equity * 100), 50);
            oss << " 经验丰富投资人：个股占总配置 "
                << static_cast<int>(actual_stock_pick * 100) << "%（上限 " << limit
                << "%）。";
        } else if (params.investor_type == "professional") {
            oss << " 专业投资人：个股占总配置 "
                << static_cast<int>(actual_stock_pick * 100) << "%（上限 "
                << static_cast<int>(equity * 100) << "%）。";
        }

        oss << " 指数配比港股:A股:美股 = 1:3:16。";
        result.rationale = oss.str();
    }

    // ── 步骤6：生成风控报告 ──
    result.risk_report = generate_risk_report(params, result.allocation);

    // ── 步骤7：生成调仓建议 ──
    if (!params.current_portfolio.empty()) {
        // 构建当前持仓映射
        std::map<std::string, double> current;
        double current_stock_pick = 0.0;

        const std::set<std::string> standard_etfs = {
            "VOO", "HS300", "HSI", "BND", "GLD"};

        for (const auto& holding : params.current_portfolio) {
            if (standard_etfs.count(holding.symbol)) {
                current[holding.symbol] = holding.weight;
            } else {
                // 非标准 ETF 归为个股持仓
                current_stock_pick += holding.weight;
            }
        }
        current["individual_stock"] = current_stock_pick;

        // 生成调仓建议（权重变化 > 0.5% 时）
        const double rebalance_threshold = 0.005;
        for (const auto& [symbol, target_weight] : result.allocation) {
            double current_weight = 0.0;
            if (current.count(symbol)) {
                current_weight = current[symbol];
            }

            double diff = target_weight - current_weight;
            if (std::fabs(diff) > rebalance_threshold) {
                AllocationResult::RebalancingAction action;
                action.symbol = symbol;
                action.current_weight = current_weight;
                action.target_weight = target_weight;

                if (diff > 0) {
                    action.action = "增持";
                } else {
                    action.action = "减持";
                }

                result.rebalancing_actions.push_back(action);
            }
        }
    }

    return result;
}

double ShouZheRule::calculate_bond_by_age(int age) {
    int age_decade = (age / 10) * 10;
    double bond_ratio = age_decade / 100.0;
    // 约束：债券比例不超过 80%
    return (std::min)(bond_ratio, 0.80);
}

double ShouZheRule::get_individual_stock_limit(const AllocationParams& params) {
    if (params.investor_type == "professional") {
        // 专业投资人：不限制（但不能超过总股票占比）
        return 1.0;
    } else if (params.investor_type == "experienced") {
        // 经验丰富投资人：上限 50%
        return 0.50;
    } else {
        // novice：不允许个股
        return 0.0;
    }
}

std::map<std::string, double> ShouZheRule::calculate_index_allocation(
    double index_equity) {
    std::map<std::string, double> result;

    // 指数比例：港股:A股:美股 = 1:3:16
    const double total_ratio = 1.0 + 3.0 + 16.0;
    result["HSI"] = index_equity * (1.0 / total_ratio);    // 港股
    result["HS300"] = index_equity * (3.0 / total_ratio);  // A股
    result["VOO"] = index_equity * (16.0 / total_ratio);   // 美股

    return result;
}

AllocationResult::RiskReport ShouZheRule::generate_risk_report(
    const AllocationParams& params,
    const std::map<std::string, double>& allocation) {
    AllocationResult::RiskReport report;
    report.status = "PASS";

    // 检查1：投资人分层资格
    if (params.investor_type == "experienced") {
        if (params.experience_years == "0-5" || params.annualized_return == "0-10") {
            report.triggered_rules.push_back(
                "⚠️  经验丰富投资人要求: >=5年经验 且 年化收益>=10%");
            report.status = "WARN";
        }
    } else if (params.investor_type == "professional") {
        if (params.experience_years != "10+" || params.beat_sp500_10y != "yes") {
            report.triggered_rules.push_back(
                "⚠️  专业投资人要求: >=10年经验 且 十年业绩跑赢标普500");
            report.status = "WARN";
        }
    }

    // 检查2：单一资产下限（2.5%）
    const double min_asset_threshold = 0.025;
    const double tolerance = 0.001;

    auto check_asset = [&](const std::string& symbol) {
        if (allocation.count(symbol)) {
            double weight = allocation.at(symbol);
            if (weight > tolerance && weight < min_asset_threshold - tolerance) {
                std::ostringstream oss;
                oss << "⚠️  " << symbol << " 占比 " << static_cast<int>(weight * 100)
                    << "% 低于 2.5% 下限";
                report.triggered_rules.push_back(oss.str());
                report.status = "WARN";
            }
        }
    };

    check_asset("HSI");
    check_asset("HS300");
    check_asset("VOO");

    return report;
}

} // namespace rules
} // namespace core
} // namespace finguard
