#pragma once

#include "valuation/valuation_config.h"
#include <string>
#include <vector>

namespace finguard::valuation {

// 来自 Yahoo Finance / Alpha Vantage 的结构化财务指标
struct FinancialMetrics {
    struct NetProfitPoint {
        int year = 0;
        double net_income = 0.0;
        std::string source;
    };

    struct EquityPoint {
        int year = 0;
        double equity = -1.0;
        std::string source;
    };

    std::string symbol;

    // 旧版估值指标（保留兼容）
    double trailing_pe = -1.0;       // 当前 trailing P/E
    double forward_pe = -1.0;        // 预期 P/E
    double price_to_book = -1.0;     // 市净率 P/B
    double peg_ratio = -1.0;         // PEG
    double trailing_eps = 0.0;       // 当前 trailing EPS（12个月）
    double earnings_growth = 0.0;    // 盈利增长率

    // 安全边际所需的历史 PE 数据（由 price history 计算）
    double historical_pe_mean = -1.0;       // 历史 PE 均值
    std::vector<double> historical_pe_series; // 各季度 PE 序列（用于调试）
    int pe_history_quarters_used = 0;       // 实际使用的季度数

    // 元信息
    bool has_sufficient_data = false;   // 是否有足够数据进行安全边际判定
    std::string data_source;            // "yahoo_finance" / "alpha_vantage"
    std::string error;                  // 非空表示获取失败

    // 新版最小数据集与计算链路
    double book_value_per_share = -1.0; // 每股净资产
    double eps = -1.0;                  // 每股收益（沿用 trailing EPS）
    double price = -1.0;                // 股价
    double total_assets = -1.0;         // 总资产
    double total_liabilities = -1.0;    // 总负债
    double debt_ratio = -1.0;           // 负债率 = 总负债 / 总资产

    std::vector<double> net_profit_8y;  // 过去8年净利润（按时间升序）
    std::vector<NetProfitPoint> net_profit_history; // 过去8年净利润明细（含来源）
    std::vector<EquityPoint> annual_equity_history; // 最近年度股东权益（内部计算用）
    std::string net_profit_source = "unknown";     // yahoo_finance/alpha_vantage/simfin/cache
    std::string net_profit_updated_at;              // ISO 时间戳
    double np_recent3_avg = -1.0;       // 最近3年净利润均值
    double np_past3_avg = -1.0;         // 倒退5年的3年净利润均值
    double growth_5y_cagr = -1.0;       // 五年年化增长率
    double avg_roe_5y = -1.0;           // 5年平均ROE（优先使用上游直接字段）

    double pe = -1.0;                   // PE = price / eps
    double peg = -1.0;                  // PEG = pe / (growth_5y_cagr * 100)

    std::string valuation_label = "unknown"; // undervalued/fair/overvalued/unknown
    std::vector<std::string> warnings;         // 计算告警
};

// 获取财务指标。先尝试 Yahoo Finance，失败时降级到 Alpha Vantage。
// 同步阻塞调用，超时由 config.yahoo_timeout_ms 控制。
FinancialMetrics fetch_financial_metrics(const std::string &symbol,
                                         const ValuationConfig &config);

// 基于已有原始字段，计算派生字段与边界 warnings。
// 纯函数：无 I/O，便于单测。
void recompute_financial_metrics(FinancialMetrics &metrics);

} // namespace finguard::valuation
