// Phase Fundamental-Analysis-Optimization:
// 新口径派生指标与边界 warnings 单元测试
#include <gtest/gtest.h>

#include "valuation/yahoo_finance_client.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

bool contains_warning(const std::vector<std::string> &warnings,
                      const std::string &target) {
    return std::find(warnings.begin(), warnings.end(), target) != warnings.end();
}

} // namespace

TEST(ValuationMetrics, RecomputeDerivedMetricsWithFullHistory) {
    finguard::valuation::FinancialMetrics metrics;
    metrics.trailing_eps = 5.0;
    metrics.price = 100.0;
    metrics.avg_roe_5y = 0.235;
    metrics.total_assets = 1000.0;
    metrics.total_liabilities = 400.0;
    metrics.net_profit_8y = {100.0, 110.0, 120.0, 130.0, 150.0, 180.0, 210.0, 240.0};

    finguard::valuation::recompute_financial_metrics(metrics);

    EXPECT_NEAR(metrics.np_recent3_avg, 210.0, 1e-9);
    EXPECT_NEAR(metrics.np_past3_avg, 110.0, 1e-9);
    EXPECT_NEAR(metrics.growth_5y_cagr, std::pow(210.0 / 110.0, 1.0 / 5.0) - 1.0, 1e-9);
    EXPECT_NEAR(metrics.pe, 20.0, 1e-9);
    EXPECT_NEAR(metrics.peg, 20.0 / (metrics.growth_5y_cagr * 100.0), 1e-9);
    EXPECT_NEAR(metrics.avg_roe_5y, 0.235, 1e-9);
    EXPECT_NEAR(metrics.debt_ratio, 0.4, 1e-9);
    EXPECT_EQ(metrics.valuation_label, "fair");
    EXPECT_FALSE(contains_warning(metrics.warnings, "high_debt_ratio"));
    EXPECT_FALSE(contains_warning(metrics.warnings, "low_growth_5y_cagr"));
    EXPECT_FALSE(contains_warning(metrics.warnings, "missing_roe_history"));
}

TEST(ValuationMetrics, TrailingPeTakesPriorityForMainPeAndPeg) {
    finguard::valuation::FinancialMetrics metrics;
    metrics.trailing_pe = 7.42;
    metrics.trailing_eps = -1.0;
    metrics.price = -1.0;
    metrics.total_assets = 1000.0;
    metrics.total_liabilities = 400.0;
    metrics.net_profit_8y = {100.0, 110.0, 120.0, 130.0, 150.0, 180.0, 210.0, 240.0};

    finguard::valuation::recompute_financial_metrics(metrics);

    EXPECT_NEAR(metrics.pe, 7.42, 1e-9);
    EXPECT_NEAR(metrics.peg, 7.42 / (metrics.growth_5y_cagr * 100.0), 1e-9);
    EXPECT_FALSE(contains_warning(metrics.warnings, "invalid_eps"));
}

TEST(ValuationMetrics, HighDebtRatioAndLowGrowthEmitWarnings) {
    finguard::valuation::FinancialMetrics metrics;
    metrics.trailing_eps = 5.0;
    metrics.price = 100.0;
    metrics.total_assets = 100.0;
    metrics.total_liabilities = 60.0;
    metrics.net_profit_8y = {100.0, 100.0, 100.0, 100.0, 100.0, 120.0, 130.0, 140.0};

    finguard::valuation::recompute_financial_metrics(metrics);

    const double expected_recent3 = (120.0 + 130.0 + 140.0) / 3.0;
    const double expected_past3 = (100.0 + 100.0 + 100.0) / 3.0;
    const double expected_growth = std::pow(expected_recent3 / expected_past3, 1.0 / 5.0) - 1.0;

    EXPECT_NEAR(metrics.debt_ratio, 0.6, 1e-9);
    EXPECT_NEAR(metrics.growth_5y_cagr, expected_growth, 1e-9);
    EXPECT_LT(metrics.growth_5y_cagr, 0.10);
    EXPECT_TRUE(contains_warning(metrics.warnings, "high_debt_ratio"));
    EXPECT_TRUE(contains_warning(metrics.warnings, "low_growth_5y_cagr"));
}

TEST(ValuationMetrics, MissingDebtRatioEmitsWarning) {
    finguard::valuation::FinancialMetrics metrics;
    metrics.trailing_eps = 4.0;
    metrics.price = 80.0;
    metrics.total_assets = -1.0;
    metrics.total_liabilities = -1.0;
    metrics.net_profit_8y = {80.0, 85.0, 90.0, 95.0, 110.0, 120.0, 135.0, 150.0};

    finguard::valuation::recompute_financial_metrics(metrics);

    EXPECT_LT(metrics.debt_ratio, 0.0);
    EXPECT_TRUE(contains_warning(metrics.warnings, "missing_debt_ratio"));
}

TEST(ValuationMetrics, MissingRoeHistoryEmitsWarning) {
    finguard::valuation::FinancialMetrics metrics;
    metrics.trailing_eps = 4.0;
    metrics.price = 80.0;
    metrics.total_assets = 200.0;
    metrics.total_liabilities = 40.0;
    metrics.net_profit_8y = {80.0, 85.0, 90.0, 95.0, 110.0, 120.0, 135.0, 150.0};

    finguard::valuation::recompute_financial_metrics(metrics);

    EXPECT_LT(metrics.avg_roe_5y, 0.0);
    EXPECT_TRUE(contains_warning(metrics.warnings, "missing_roe_history"));
}

TEST(ValuationMetrics, LargeRoePercentageNormalizesToRatio) {
    finguard::valuation::FinancialMetrics metrics;
    metrics.trailing_pe = 8.0;
    metrics.avg_roe_5y = 25.1;
    metrics.total_assets = 100.0;
    metrics.total_liabilities = 20.0;
    metrics.net_profit_8y = {100.0, 110.0, 120.0, 130.0, 150.0, 180.0, 210.0, 240.0};

    finguard::valuation::recompute_financial_metrics(metrics);

    EXPECT_NEAR(metrics.avg_roe_5y, 0.251, 1e-9);
    EXPECT_FALSE(contains_warning(metrics.warnings, "missing_roe_history"));
}

TEST(ValuationMetrics, NonPositiveGrowthKeepsGrowthWarnings) {
    finguard::valuation::FinancialMetrics metrics;
    metrics.trailing_eps = 5.0;
    metrics.price = 50.0;
    metrics.total_assets = 120.0;
    metrics.total_liabilities = 30.0;
    metrics.net_profit_8y = {300.0, 280.0, 260.0, 240.0, 220.0, 200.0, 180.0, 160.0};

    finguard::valuation::recompute_financial_metrics(metrics);

    EXPECT_LE(metrics.growth_5y_cagr, 0.0);
    EXPECT_TRUE(contains_warning(metrics.warnings, "non_positive_growth"));
    EXPECT_TRUE(contains_warning(metrics.warnings, "low_growth_5y_cagr"));
}

TEST(ValuationMetrics, InsufficientHistoryFallbackStillChecksGrowthThreshold) {
    finguard::valuation::FinancialMetrics metrics;
    metrics.trailing_eps = 5.0;
    metrics.price = 50.0;
    metrics.total_assets = 200.0;
    metrics.total_liabilities = 40.0;
    metrics.net_profit_8y = {100.0, 105.0, 110.0, 115.0, 120.0};

    finguard::valuation::recompute_financial_metrics(metrics);

    EXPECT_TRUE(contains_warning(metrics.warnings, "insufficient_net_profit_history"));
    EXPECT_TRUE(contains_warning(metrics.warnings, "growth_fallback_avg_yoy_used"));
    EXPECT_TRUE(contains_warning(metrics.warnings, "low_growth_5y_cagr"));
    EXPECT_GT(metrics.growth_5y_cagr, 0.0);
    EXPECT_LT(metrics.growth_5y_cagr, 0.10);
}
