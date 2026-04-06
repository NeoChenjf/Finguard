#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace finguard::data {

struct FundamentalsDbProfileInfo {
    std::string key;
    std::string label;
    std::string description;
    std::filesystem::path path;
    bool is_demo = false;
    bool exists = false;
    bool forced_by_env = false;
};

struct CompanyRecord {
    std::string symbol;
    std::string normalized_symbol;
    std::string company_name;
    std::string market;
    std::string currency;
    std::string sector;
    std::string industry;
    bool is_active = true;
    std::string created_at;
    std::string updated_at;
};

struct AnnualFundamentalsRecord {
    int fiscal_year = 0;
    double revenue = -1.0;
    double operating_income = -1.0;
    double net_income = -1.0;
    double eps_diluted = -1.0;
    double shares_diluted = -1.0;
    double total_assets = -1.0;
    double total_liabilities = -1.0;
    double total_equity = -1.0;
    double operating_cash_flow = -1.0;
    double capex = -1.0;
    double free_cash_flow = -1.0;
    double book_value_per_share = -1.0;
    double roe = -1.0;
    double debt_ratio = -1.0;
    std::string source;
    std::string source_updated_at;
    std::string quality_flag;
};

struct LatestQuoteMetricsRecord {
    double price = -1.0;
    double trailing_pe = -1.0;
    double forward_pe = -1.0;
    double price_to_book = -1.0;
    double peg_ratio = -1.0;
    double trailing_eps = -1.0;
    double market_cap = -1.0;
    std::string source;
    std::string quote_time;
    std::string updated_at;
};

struct FundamentalsSnapshot {
    bool db_available = false;
    bool symbol_found = false;
    std::string requested_symbol;
    std::string resolved_symbol;
    std::string db_path;
    std::string db_profile_key;
    std::string db_profile_label;
    std::string error;
    std::optional<CompanyRecord> company;
    std::vector<AnnualFundamentalsRecord> annual_rows;
    std::optional<LatestQuoteMetricsRecord> latest_quote;
};

std::filesystem::path resolve_finguard_root_path();
std::vector<FundamentalsDbProfileInfo> available_fundamentals_db_profiles();
FundamentalsDbProfileInfo resolve_fundamentals_db_profile(
    const std::string &configured_profile = "main");
std::filesystem::path resolve_fundamentals_db_path(
    const std::string &configured_profile = "main");
std::filesystem::path resolve_fundamentals_schema_path();
bool initialize_fundamentals_db(const std::string &configured_profile = "main",
                                std::string *error = nullptr);
bool upsert_company_record(const CompanyRecord &record,
                           const std::string &configured_profile = "main",
                           std::string *error = nullptr);
bool upsert_annual_fundamentals_records(
    const std::string &symbol,
    const std::vector<AnnualFundamentalsRecord> &records,
    const std::string &configured_profile = "main",
    std::string *error = nullptr);
bool upsert_latest_quote_metrics_record(
    const std::string &symbol,
    const LatestQuoteMetricsRecord &record,
    const std::string &configured_profile = "main",
    std::string *error = nullptr);
FundamentalsSnapshot load_fundamentals_snapshot(
    const std::string &normalized_symbol,
    const std::string &configured_profile = "main");

} // namespace finguard::data
