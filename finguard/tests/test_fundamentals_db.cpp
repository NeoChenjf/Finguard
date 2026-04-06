#include <gtest/gtest.h>

#include "data/fundamentals_db.h"
#include "sqlite3.h"
#include "valuation/fundamentals_db_client.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

struct EnvVarGuard {
    explicit EnvVarGuard(const char *name) : name_(name) {
        const char *current = std::getenv(name_);
        had_value_ = current != nullptr && current[0] != '\0';
        if (had_value_) {
            original_value_ = current;
        }
    }

    ~EnvVarGuard() {
#ifdef _WIN32
        _putenv_s(name_, had_value_ ? original_value_.c_str() : "");
#else
        if (had_value_) {
            setenv(name_, original_value_.c_str(), 1);
        } else {
            unsetenv(name_);
        }
#endif
    }

    void clear() const {
#ifdef _WIN32
        _putenv_s(name_, "");
#else
        unsetenv(name_);
#endif
    }

private:
    const char *name_;
    bool had_value_ = false;
    std::string original_value_;
};

struct DbHandle {
    sqlite3 *db = nullptr;
    ~DbHandle() {
        if (db != nullptr) {
            sqlite3_close(db);
        }
    }
};

void exec_or_fail(sqlite3 *db, const char *sql) {
    char *errmsg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        const std::string msg = errmsg != nullptr ? errmsg : "sqlite_exec_failed";
        if (errmsg != nullptr) {
            sqlite3_free(errmsg);
        }
        FAIL() << msg;
    }
}

void reset_db(sqlite3 *db) {
    exec_or_fail(db, "DELETE FROM ingestion_issues;");
    exec_or_fail(db, "DELETE FROM latest_quote_metrics;");
    exec_or_fail(db, "DELETE FROM annual_fundamentals;");
    exec_or_fail(db, "DELETE FROM ingestion_runs;");
    exec_or_fail(db, "DELETE FROM companies;");
}

std::filesystem::path use_test_db_path() {
    const auto path = std::filesystem::current_path() / "test_fundamentals.db";
#ifdef _WIN32
    _putenv_s("FINGUARD_FUNDAMENTALS_DB_PATH", path.string().c_str());
#else
    setenv("FINGUARD_FUNDAMENTALS_DB_PATH", path.string().c_str(), 1);
#endif
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path;
}

} // namespace

TEST(FundamentalsDb, InitializeCreatesSchema) {
    const auto db_path = use_test_db_path();
    std::string error;
    EXPECT_TRUE(finguard::data::initialize_fundamentals_db("main", &error)) << error;
    EXPECT_EQ(finguard::data::resolve_fundamentals_db_path(), db_path);
    EXPECT_TRUE(std::filesystem::exists(db_path));
    EXPECT_TRUE(std::filesystem::exists(finguard::data::resolve_fundamentals_schema_path()));
}

TEST(FundamentalsDb, BuiltinProfilesResolveExpectedKeys) {
    EnvVarGuard env_guard("FINGUARD_FUNDAMENTALS_DB_PATH");
    env_guard.clear();

    const auto profiles = finguard::data::available_fundamentals_db_profiles();
    EXPECT_GE(profiles.size(), 4u);

    const auto tcom_profile = finguard::data::resolve_fundamentals_db_profile("tcom_demo");
    EXPECT_EQ(tcom_profile.key, "tcom_demo");
    EXPECT_TRUE(tcom_profile.is_demo);
    EXPECT_NE(tcom_profile.path.string().find("tcom_fundamentals_example.db"), std::string::npos);

    const auto fallback_profile = finguard::data::resolve_fundamentals_db_profile("unknown_profile");
    EXPECT_EQ(fallback_profile.key, "main");
}

TEST(FundamentalsDb, LoadSnapshotReturnsAnnualsAndQuote) {
    use_test_db_path();
    std::string error;
    ASSERT_TRUE(finguard::data::initialize_fundamentals_db("main", &error)) << error;

    DbHandle handle;
    ASSERT_EQ(sqlite3_open(finguard::data::resolve_fundamentals_db_path().string().c_str(), &handle.db),
              SQLITE_OK);
    reset_db(handle.db);

    exec_or_fail(handle.db,
                 "INSERT INTO companies(symbol, normalized_symbol, company_name, market, currency, is_active, created_at, updated_at)"
                 " VALUES ('TCOM', 'TCOM', 'Trip.com', 'US', 'USD', 1, '2026-03-30T00:00:00Z', '2026-03-30T00:00:00Z');");
    exec_or_fail(handle.db,
                 "INSERT INTO annual_fundamentals(symbol, fiscal_year, net_income, roe, total_assets, total_liabilities, book_value_per_share, debt_ratio, source, source_updated_at, quality_flag)"
                 " VALUES "
                 "('TCOM', 2018, 1112000000, 0.09, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('TCOM', 2019, 7011000000, 0.10, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('TCOM', 2020, -3247000000, 0.01, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('TCOM', 2021, -550000000, 0.02, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('TCOM', 2022, 1403000000, 0.04, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('TCOM', 2023, 9918000000, 0.07, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('TCOM', 2024, 17067000000, 0.08, 242581000000, 99099000000, 37.7, 0.4085, 'seed', '2026-03-30T00:00:00Z', 'test');");
    exec_or_fail(handle.db,
                 "INSERT INTO latest_quote_metrics(symbol, price, trailing_pe, price_to_book, peg_ratio, trailing_eps, source, quote_time, updated_at)"
                 " VALUES ('TCOM', 49.02, 6.8121, 1.3267, 0.0816, 47.7859, 'seed', '2026-03-30T00:00:00Z', '2026-03-30T00:00:00Z');");

    const auto snapshot = finguard::data::load_fundamentals_snapshot("TCOM");
    EXPECT_TRUE(snapshot.db_available);
    EXPECT_TRUE(snapshot.symbol_found);
    ASSERT_TRUE(snapshot.company.has_value());
    EXPECT_EQ(snapshot.company->company_name, "Trip.com");
    EXPECT_EQ(snapshot.annual_rows.size(), 7u);
    ASSERT_TRUE(snapshot.latest_quote.has_value());
    EXPECT_NEAR(snapshot.latest_quote->trailing_pe, 6.8121, 1e-9);
}

TEST(FundamentalsDb, UpsertHelpersPersistAndReloadRecords) {
    use_test_db_path();
    std::string error;

    finguard::data::CompanyRecord company;
    company.symbol = "AAPL";
    company.normalized_symbol = "AAPL";
    company.company_name = "Apple Inc.";
    company.market = "US";
    company.currency = "USD";
    ASSERT_TRUE(finguard::data::upsert_company_record(company, "main", &error)) << error;

    finguard::data::AnnualFundamentalsRecord annual_2023;
    annual_2023.fiscal_year = 2023;
    annual_2023.net_income = 96995000000.0;
    annual_2023.source = "unit_test";
    annual_2023.source_updated_at = "2026-03-30T00:00:00Z";

    finguard::data::AnnualFundamentalsRecord annual_2024;
    annual_2024.fiscal_year = 2024;
    annual_2024.net_income = 100000000000.0;
    annual_2024.total_assets = 352000000000.0;
    annual_2024.total_liabilities = 290000000000.0;
    annual_2024.book_value_per_share = 4.25;
    annual_2024.debt_ratio = 0.8239;
    annual_2024.source = "unit_test";
    annual_2024.source_updated_at = "2026-03-30T00:00:00Z";

    ASSERT_TRUE(finguard::data::upsert_annual_fundamentals_records(
        "AAPL", {annual_2023, annual_2024}, "main", &error)) << error;

    finguard::data::LatestQuoteMetricsRecord quote;
    quote.price = 220.0;
    quote.trailing_pe = 28.0;
    quote.price_to_book = 51.0;
    quote.trailing_eps = 7.85;
    quote.source = "unit_test";
    quote.updated_at = "2026-03-30T00:00:00Z";
    ASSERT_TRUE(
        finguard::data::upsert_latest_quote_metrics_record("AAPL", quote, "main", &error))
        << error;

    const auto snapshot = finguard::data::load_fundamentals_snapshot("AAPL");
    EXPECT_TRUE(snapshot.symbol_found);
    ASSERT_EQ(snapshot.annual_rows.size(), 2u);
    ASSERT_TRUE(snapshot.latest_quote.has_value());
    EXPECT_NEAR(snapshot.latest_quote->price, 220.0, 1e-9);
    EXPECT_NEAR(snapshot.annual_rows.back().book_value_per_share, 4.25, 1e-9);
}

TEST(FundamentalsDbClient, MissingQuoteAndAliasWarningsArePreserved) {
    use_test_db_path();
    std::string error;
    ASSERT_TRUE(finguard::data::initialize_fundamentals_db("main", &error)) << error;

    DbHandle handle;
    ASSERT_EQ(sqlite3_open(finguard::data::resolve_fundamentals_db_path().string().c_str(), &handle.db),
              SQLITE_OK);
    reset_db(handle.db);

    exec_or_fail(handle.db,
                 "INSERT INTO companies(symbol, normalized_symbol, company_name, market, currency, is_active, created_at, updated_at)"
                 " VALUES ('BRK-B', 'BRK-B', 'Berkshire Hathaway', 'US', 'USD', 1, '2026-03-30T00:00:00Z', '2026-03-30T00:00:00Z');");
    exec_or_fail(handle.db,
                 "INSERT INTO annual_fundamentals(symbol, fiscal_year, net_income, roe, total_assets, total_liabilities, book_value_per_share, debt_ratio, source, source_updated_at, quality_flag)"
                 " VALUES "
                 "('BRK-B', 2018, 4021000000, 0.11, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('BRK-B', 2019, 81417000000, 0.10, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('BRK-B', 2020, 42521000000, 0.09, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('BRK-B', 2021, 89795000000, 0.10, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('BRK-B', 2022, -22819000000, 0.12, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('BRK-B', 2023, 96223000000, 0.10, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('BRK-B', 2024, 88995000000, 0.11, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('BRK-B', 2025, 66968000000, 0.12, 1222176000000, 502473000000, 498660, 0.4111, 'seed', '2026-03-30T00:00:00Z', 'test');");

    const auto metrics = finguard::valuation::fetch_financial_metrics_from_db("BRK");
    EXPECT_EQ(metrics.symbol, "BRK-B");
    EXPECT_LT(metrics.pe, 0.0);
    EXPECT_TRUE(std::find(metrics.warnings.begin(), metrics.warnings.end(),
                          "db_missing_latest_quote") != metrics.warnings.end());
}

TEST(FundamentalsDbClient, StaleQuoteWarningIsReported) {
    use_test_db_path();
    std::string error;
    ASSERT_TRUE(finguard::data::initialize_fundamentals_db("main", &error)) << error;

    DbHandle handle;
    ASSERT_EQ(sqlite3_open(finguard::data::resolve_fundamentals_db_path().string().c_str(), &handle.db),
              SQLITE_OK);
    reset_db(handle.db);

    exec_or_fail(handle.db,
                 "INSERT INTO companies(symbol, normalized_symbol, company_name, market, currency, is_active, created_at, updated_at)"
                 " VALUES ('PDD', 'PDD', 'PDD Holdings', 'US', 'USD', 1, '2026-03-30T00:00:00Z', '2026-03-30T00:00:00Z');");
    exec_or_fail(handle.db,
                 "INSERT INTO annual_fundamentals(symbol, fiscal_year, net_income, roe, total_assets, total_liabilities, book_value_per_share, debt_ratio, source, source_updated_at, quality_flag)"
                 " VALUES "
                 "('PDD', 2017, -525115000, 0.02, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('PDD', 2018, -10217125000, 0.01, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('PDD', 2019, -6967603000, 0.03, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('PDD', 2020, -7179742000, 0.04, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('PDD', 2021, 7768670000, 0.12, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('PDD', 2022, 31538062000, 0.16, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('PDD', 2023, 60026544000, 0.21, NULL, NULL, NULL, NULL, 'seed', '2026-03-30T00:00:00Z', 'test'),"
                 "('PDD', 2024, 112434512000, 0.25, 505034316000, 191721192000, 40.65, 0.3796, 'seed', '2026-03-30T00:00:00Z', 'test');");
    exec_or_fail(handle.db,
                 "INSERT INTO latest_quote_metrics(symbol, price, trailing_pe, price_to_book, peg_ratio, trailing_eps, source, quote_time, updated_at)"
                 " VALUES ('PDD', 101.0, 9.4636, 2.4727, -1.0, 17.2311, 'seed', '2026-03-01T00:00:00Z', '2026-03-01T00:00:00Z');");

    const auto metrics = finguard::valuation::fetch_financial_metrics_from_db("PDD");
    EXPECT_TRUE(std::find(metrics.warnings.begin(), metrics.warnings.end(),
                          "db_stale_quote_metrics") != metrics.warnings.end());
    EXPECT_NEAR(metrics.trailing_pe, 9.4636, 1e-9);
}

TEST(FundamentalsDb, EnvOverrideTakesPrecedenceOverConfiguredProfile) {
    const auto custom_path = std::filesystem::current_path() / "env_override_demo.db";
#ifdef _WIN32
    _putenv_s("FINGUARD_FUNDAMENTALS_DB_PATH", custom_path.string().c_str());
#else
    setenv("FINGUARD_FUNDAMENTALS_DB_PATH", custom_path.string().c_str(), 1);
#endif
    const auto profile = finguard::data::resolve_fundamentals_db_profile("pdd_demo");
    EXPECT_EQ(profile.key, "env_override");
    EXPECT_TRUE(profile.forced_by_env);
    EXPECT_EQ(profile.path, custom_path);
#ifdef _WIN32
    _putenv_s("FINGUARD_FUNDAMENTALS_DB_PATH", "");
#else
    unsetenv("FINGUARD_FUNDAMENTALS_DB_PATH");
#endif
}
