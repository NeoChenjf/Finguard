#include "data/fundamentals_db.h"

#include "sqlite3.h"

#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <ctime>

namespace finguard::data {

namespace {

constexpr const char *kEnvOverrideLabel = "环境变量覆盖";
constexpr const char *kEnvOverrideDescription = "开发/调试覆盖，优先于已保存的数据库 profile。";

struct DbHandle {
    sqlite3 *db = nullptr;
    ~DbHandle() {
        if (db != nullptr) {
            sqlite3_close(db);
        }
    }
};

struct StmtHandle {
    sqlite3_stmt *stmt = nullptr;
    ~StmtHandle() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
    }
};

double column_double_or(sqlite3_stmt *stmt, int col, double fallback = -1.0) {
    return sqlite3_column_type(stmt, col) == SQLITE_NULL ? fallback
                                                          : sqlite3_column_double(stmt, col);
}

std::string column_text_or_empty(sqlite3_stmt *stmt, int col) {
    const auto *text = sqlite3_column_text(stmt, col);
    return text == nullptr ? std::string{} : std::string(reinterpret_cast<const char *>(text));
}

std::filesystem::path detect_finguard_root() {
    std::error_code ec;
    auto cur = std::filesystem::current_path(ec);
    if (ec) {
        return {};
    }

    for (auto p = cur; !p.empty(); p = p.parent_path()) {
        // Production environment: check for data/ and config/ directories
        if (std::filesystem::exists(p / "data") &&
            std::filesystem::exists(p / "config")) {
            return p;
        }
        // Development environment: check for CMakeLists.txt
        if (std::filesystem::exists(p / "CMakeLists.txt") &&
            std::filesystem::exists(p / "src") &&
            std::filesystem::exists(p / "config")) {
            return p;
        }
        if (std::filesystem::exists(p / "finguard" / "CMakeLists.txt") &&
            std::filesystem::exists(p / "finguard" / "src") &&
            std::filesystem::exists(p / "finguard" / "config")) {
            return p / "finguard";
        }
        if (p == p.root_path()) {
            break;
        }
    }
    return {};
}

std::vector<FundamentalsDbProfileInfo> build_builtin_profiles() {
    const auto root = detect_finguard_root();
    const auto base = !root.empty() ? root : std::filesystem::current_path();

    std::vector<FundamentalsDbProfileInfo> profiles;
    profiles.push_back({"main",
                        "主库",
                        "ValueCell 默认正式数据库。",
                        base / "data" / "fundamentals.db",
                        false,
                        false,
                        false});
    profiles.push_back({"tcom_demo",
                        "TCOM demo",
                        "前端数据库验证示例：Trip.com 单例库。",
                        base / "data" / "examples" / "tcom_fundamentals_example.db",
                        true,
                        false,
                        false});
    profiles.push_back({"brk_b_demo",
                        "BRK-B demo",
                        "前端数据库验证示例：Berkshire Hathaway Class B 单例库。",
                        base / "data" / "examples" / "brk-b_fundamentals_example.db",
                        true,
                        false,
                        false});
    profiles.push_back({"pdd_demo",
                        "PDD demo",
                        "前端数据库验证示例：PDD Holdings 单例库。",
                        base / "data" / "examples" / "pdd_fundamentals_example.db",
                        true,
                        false,
                        false});
    for (auto &profile : profiles) {
        profile.exists = std::filesystem::exists(profile.path);
    }
    return profiles;
}

bool read_file(const std::filesystem::path &path, std::string &out) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    out = oss.str();
    return true;
}

bool prepare_statement(sqlite3 *db, const char *sql, StmtHandle &out, std::string &error) {
    if (sqlite3_prepare_v2(db, sql, -1, &out.stmt, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db);
        return false;
    }
    return true;
}

std::string current_iso_utc() {
    const auto now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char buffer[32] = {0};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buffer);
}

bool bind_nullable_double(sqlite3_stmt *stmt, int index, double value) {
    if (!std::isfinite(value) || value < 0.0) {
        return sqlite3_bind_null(stmt, index) == SQLITE_OK;
    }
    return sqlite3_bind_double(stmt, index, value) == SQLITE_OK;
}

bool bind_text_or_null(sqlite3_stmt *stmt, int index, const std::string &value) {
    if (value.empty()) {
        return sqlite3_bind_null(stmt, index) == SQLITE_OK;
    }
    return sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

bool exec_simple(sqlite3 *db, const char *sql, std::string &error) {
    char *errmsg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        error = errmsg != nullptr ? errmsg : "sqlite_exec_failed";
        if (errmsg != nullptr) {
            sqlite3_free(errmsg);
        }
        return false;
    }
    if (errmsg != nullptr) {
        sqlite3_free(errmsg);
    }
    return true;
}

} // namespace

std::filesystem::path resolve_finguard_root_path() {
    return detect_finguard_root();
}

std::vector<FundamentalsDbProfileInfo> available_fundamentals_db_profiles() {
    return build_builtin_profiles();
}

FundamentalsDbProfileInfo resolve_fundamentals_db_profile(
    const std::string &configured_profile) {
    if (const char *custom = std::getenv("FINGUARD_FUNDAMENTALS_DB_PATH");
        custom != nullptr && custom[0] != '\0') {
        FundamentalsDbProfileInfo info;
        info.key = "env_override";
        info.label = kEnvOverrideLabel;
        info.description = kEnvOverrideDescription;
        info.path = std::filesystem::path(custom);
        info.forced_by_env = true;
        info.exists = std::filesystem::exists(info.path);
        return info;
    }

    const auto selected_key = configured_profile.empty() ? "main" : configured_profile;
    auto profiles = build_builtin_profiles();
    for (auto &profile : profiles) {
        if (profile.key == selected_key) {
            return profile;
        }
    }
    for (auto &profile : profiles) {
        if (profile.key == "main") {
            return profile;
        }
    }

    FundamentalsDbProfileInfo fallback;
    fallback.key = "main";
    fallback.label = "主库";
    fallback.description = "ValueCell 默认正式数据库。";
    fallback.path = std::filesystem::current_path() / "data" / "fundamentals.db";
    fallback.exists = std::filesystem::exists(fallback.path);
    return fallback;
}

std::filesystem::path resolve_fundamentals_db_path(const std::string &configured_profile) {
    return resolve_fundamentals_db_profile(configured_profile).path;
}

std::filesystem::path resolve_fundamentals_schema_path() {
    const auto root = resolve_finguard_root_path();
    if (!root.empty()) {
        return root / "sql" / "fundamentals_schema.sql";
    }
    return std::filesystem::current_path() / "sql" / "fundamentals_schema.sql";
}

bool initialize_fundamentals_db(const std::string &configured_profile, std::string *error) {
    const auto db_path = resolve_fundamentals_db_path(configured_profile);
    const auto schema_path = resolve_fundamentals_schema_path();

    std::error_code ec;
    std::filesystem::create_directories(db_path.parent_path(), ec);

    std::string schema_sql;
    if (!read_file(schema_path, schema_sql)) {
        if (error != nullptr) {
            *error = "fundamentals_schema_not_found";
        }
        return false;
    }

    DbHandle handle;
    if (sqlite3_open(db_path.string().c_str(), &handle.db) != SQLITE_OK) {
        if (error != nullptr) {
            *error = handle.db != nullptr ? sqlite3_errmsg(handle.db) : "sqlite_open_failed";
        }
        return false;
    }

    char *errmsg = nullptr;
    if (sqlite3_exec(handle.db, schema_sql.c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK) {
        if (error != nullptr) {
            *error = errmsg != nullptr ? errmsg : "sqlite_schema_exec_failed";
        }
        if (errmsg != nullptr) {
            sqlite3_free(errmsg);
        }
        return false;
    }
    if (errmsg != nullptr) {
        sqlite3_free(errmsg);
    }
    return true;
}

bool upsert_company_record(const CompanyRecord &record,
                           const std::string &configured_profile,
                           std::string *error) {
    std::string local_error;
    if (!initialize_fundamentals_db(configured_profile, &local_error)) {
        if (error != nullptr) {
            *error = local_error;
        }
        return false;
    }

    DbHandle handle;
    const auto db_path = resolve_fundamentals_db_path(configured_profile);
    if (sqlite3_open(db_path.string().c_str(), &handle.db) != SQLITE_OK) {
        if (error != nullptr) {
            *error = handle.db != nullptr ? sqlite3_errmsg(handle.db) : "sqlite_open_failed";
        }
        return false;
    }

    const auto created_at = record.created_at.empty() ? current_iso_utc() : record.created_at;
    const auto updated_at = record.updated_at.empty() ? current_iso_utc() : record.updated_at;

    StmtHandle stmt;
    if (!prepare_statement(
            handle.db,
            "INSERT INTO companies("
            "symbol, normalized_symbol, company_name, market, currency, sector, industry, "
            "is_active, created_at, updated_at"
            ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10) "
            "ON CONFLICT(symbol) DO UPDATE SET "
            "normalized_symbol=excluded.normalized_symbol, "
            "company_name=excluded.company_name, "
            "market=excluded.market, "
            "currency=excluded.currency, "
            "sector=excluded.sector, "
            "industry=excluded.industry, "
            "is_active=excluded.is_active, "
            "updated_at=excluded.updated_at",
            stmt,
            local_error)) {
        if (error != nullptr) {
            *error = local_error;
        }
        return false;
    }

    sqlite3_bind_text(stmt.stmt, 1, record.symbol.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.stmt, 2, record.normalized_symbol.c_str(), -1, SQLITE_TRANSIENT);
    bind_text_or_null(stmt.stmt, 3, record.company_name);
    bind_text_or_null(stmt.stmt, 4, record.market);
    bind_text_or_null(stmt.stmt, 5, record.currency);
    bind_text_or_null(stmt.stmt, 6, record.sector);
    bind_text_or_null(stmt.stmt, 7, record.industry);
    sqlite3_bind_int(stmt.stmt, 8, record.is_active ? 1 : 0);
    sqlite3_bind_text(stmt.stmt, 9, created_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.stmt, 10, updated_at.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        if (error != nullptr) {
            *error = sqlite3_errmsg(handle.db);
        }
        return false;
    }
    return true;
}

bool upsert_annual_fundamentals_records(
    const std::string &symbol,
    const std::vector<AnnualFundamentalsRecord> &records,
    const std::string &configured_profile,
    std::string *error) {
    if (records.empty()) {
        return true;
    }

    std::string local_error;
    if (!initialize_fundamentals_db(configured_profile, &local_error)) {
        if (error != nullptr) {
            *error = local_error;
        }
        return false;
    }

    DbHandle handle;
    const auto db_path = resolve_fundamentals_db_path(configured_profile);
    if (sqlite3_open(db_path.string().c_str(), &handle.db) != SQLITE_OK) {
        if (error != nullptr) {
            *error = handle.db != nullptr ? sqlite3_errmsg(handle.db) : "sqlite_open_failed";
        }
        return false;
    }

    if (!exec_simple(handle.db, "BEGIN IMMEDIATE TRANSACTION;", local_error)) {
        if (error != nullptr) {
            *error = local_error;
        }
        return false;
    }

    StmtHandle stmt;
    if (!prepare_statement(
            handle.db,
            "INSERT INTO annual_fundamentals("
            "symbol, fiscal_year, revenue, operating_income, net_income, eps_diluted, "
            "shares_diluted, total_assets, total_liabilities, total_equity, operating_cash_flow, "
            "capex, free_cash_flow, book_value_per_share, roe, debt_ratio, source, "
            "source_updated_at, quality_flag"
            ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, "
            "?17, ?18, ?19) "
            "ON CONFLICT(symbol, fiscal_year) DO UPDATE SET "
            "revenue=excluded.revenue, "
            "operating_income=excluded.operating_income, "
            "net_income=excluded.net_income, "
            "eps_diluted=excluded.eps_diluted, "
            "shares_diluted=excluded.shares_diluted, "
            "total_assets=excluded.total_assets, "
            "total_liabilities=excluded.total_liabilities, "
            "total_equity=excluded.total_equity, "
            "operating_cash_flow=excluded.operating_cash_flow, "
            "capex=excluded.capex, "
            "free_cash_flow=excluded.free_cash_flow, "
            "book_value_per_share=excluded.book_value_per_share, "
            "roe=excluded.roe, "
            "debt_ratio=excluded.debt_ratio, "
            "source=excluded.source, "
            "source_updated_at=excluded.source_updated_at, "
            "quality_flag=excluded.quality_flag",
            stmt,
            local_error)) {
        if (error != nullptr) {
            *error = local_error;
        }
        return false;
    }

    for (const auto &record : records) {
        sqlite3_reset(stmt.stmt);
        sqlite3_clear_bindings(stmt.stmt);

        sqlite3_bind_text(stmt.stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt.stmt, 2, record.fiscal_year);
        bind_nullable_double(stmt.stmt, 3, record.revenue);
        bind_nullable_double(stmt.stmt, 4, record.operating_income);
        bind_nullable_double(stmt.stmt, 5, record.net_income);
        bind_nullable_double(stmt.stmt, 6, record.eps_diluted);
        bind_nullable_double(stmt.stmt, 7, record.shares_diluted);
        bind_nullable_double(stmt.stmt, 8, record.total_assets);
        bind_nullable_double(stmt.stmt, 9, record.total_liabilities);
        bind_nullable_double(stmt.stmt, 10, record.total_equity);
        bind_nullable_double(stmt.stmt, 11, record.operating_cash_flow);
        bind_nullable_double(stmt.stmt, 12, record.capex);
        bind_nullable_double(stmt.stmt, 13, record.free_cash_flow);
        bind_nullable_double(stmt.stmt, 14, record.book_value_per_share);
        bind_nullable_double(stmt.stmt, 15, record.roe);
        bind_nullable_double(stmt.stmt, 16, record.debt_ratio);
        bind_text_or_null(stmt.stmt, 17, record.source);
        bind_text_or_null(stmt.stmt, 18, record.source_updated_at);
        bind_text_or_null(stmt.stmt, 19, record.quality_flag);

        if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
            local_error = sqlite3_errmsg(handle.db);
            exec_simple(handle.db, "ROLLBACK;", local_error);
            if (error != nullptr) {
                *error = local_error;
            }
            return false;
        }
    }

    if (!exec_simple(handle.db, "COMMIT;", local_error)) {
        if (error != nullptr) {
            *error = local_error;
        }
        return false;
    }
    return true;
}

bool upsert_latest_quote_metrics_record(
    const std::string &symbol,
    const LatestQuoteMetricsRecord &record,
    const std::string &configured_profile,
    std::string *error) {
    std::string local_error;
    if (!initialize_fundamentals_db(configured_profile, &local_error)) {
        if (error != nullptr) {
            *error = local_error;
        }
        return false;
    }

    DbHandle handle;
    const auto db_path = resolve_fundamentals_db_path(configured_profile);
    if (sqlite3_open(db_path.string().c_str(), &handle.db) != SQLITE_OK) {
        if (error != nullptr) {
            *error = handle.db != nullptr ? sqlite3_errmsg(handle.db) : "sqlite_open_failed";
        }
        return false;
    }

    StmtHandle stmt;
    if (!prepare_statement(
            handle.db,
            "INSERT INTO latest_quote_metrics("
            "symbol, price, trailing_pe, forward_pe, price_to_book, peg_ratio, trailing_eps, "
            "market_cap, source, quote_time, updated_at"
            ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11) "
            "ON CONFLICT(symbol) DO UPDATE SET "
            "price=excluded.price, "
            "trailing_pe=excluded.trailing_pe, "
            "forward_pe=excluded.forward_pe, "
            "price_to_book=excluded.price_to_book, "
            "peg_ratio=excluded.peg_ratio, "
            "trailing_eps=excluded.trailing_eps, "
            "market_cap=excluded.market_cap, "
            "source=excluded.source, "
            "quote_time=excluded.quote_time, "
            "updated_at=excluded.updated_at",
            stmt,
            local_error)) {
        if (error != nullptr) {
            *error = local_error;
        }
        return false;
    }

    const auto quote_time = record.quote_time.empty() ? current_iso_utc() : record.quote_time;
    const auto updated_at = record.updated_at.empty() ? current_iso_utc() : record.updated_at;

    sqlite3_bind_text(stmt.stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);
    bind_nullable_double(stmt.stmt, 2, record.price);
    bind_nullable_double(stmt.stmt, 3, record.trailing_pe);
    bind_nullable_double(stmt.stmt, 4, record.forward_pe);
    bind_nullable_double(stmt.stmt, 5, record.price_to_book);
    bind_nullable_double(stmt.stmt, 6, record.peg_ratio);
    bind_nullable_double(stmt.stmt, 7, record.trailing_eps);
    bind_nullable_double(stmt.stmt, 8, record.market_cap);
    bind_text_or_null(stmt.stmt, 9, record.source);
    bind_text_or_null(stmt.stmt, 10, quote_time);
    bind_text_or_null(stmt.stmt, 11, updated_at);

    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        if (error != nullptr) {
            *error = sqlite3_errmsg(handle.db);
        }
        return false;
    }
    return true;
}

FundamentalsSnapshot load_fundamentals_snapshot(const std::string &normalized_symbol,
                                                const std::string &configured_profile) {
    FundamentalsSnapshot out;
    out.requested_symbol = normalized_symbol;
    const auto profile = resolve_fundamentals_db_profile(configured_profile);
    const auto db_path = profile.path;
    out.db_path = db_path.string();
    out.db_profile_key = profile.key;
    out.db_profile_label = profile.label;

    if (!std::filesystem::exists(db_path)) {
        out.error = "fundamentals_db_not_found";
        return out;
    }

    DbHandle handle;
    if (sqlite3_open_v2(db_path.string().c_str(), &handle.db, SQLITE_OPEN_READONLY, nullptr) !=
        SQLITE_OK) {
        out.error = handle.db != nullptr ? sqlite3_errmsg(handle.db) : "sqlite_open_failed";
        return out;
    }
    out.db_available = true;
    sqlite3_busy_timeout(handle.db, 2000);

    std::string error;
    StmtHandle company_stmt;
    if (!prepare_statement(
            handle.db,
            "SELECT symbol, normalized_symbol, company_name, market, currency, sector, "
            "industry, is_active, created_at, updated_at "
            "FROM companies WHERE symbol = ?1 OR normalized_symbol = ?1 LIMIT 1",
            company_stmt,
            error)) {
        out.error = error;
        return out;
    }
    sqlite3_bind_text(company_stmt.stmt, 1, normalized_symbol.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(company_stmt.stmt) == SQLITE_ROW) {
        CompanyRecord row;
        row.symbol = column_text_or_empty(company_stmt.stmt, 0);
        row.normalized_symbol = column_text_or_empty(company_stmt.stmt, 1);
        row.company_name = column_text_or_empty(company_stmt.stmt, 2);
        row.market = column_text_or_empty(company_stmt.stmt, 3);
        row.currency = column_text_or_empty(company_stmt.stmt, 4);
        row.sector = column_text_or_empty(company_stmt.stmt, 5);
        row.industry = column_text_or_empty(company_stmt.stmt, 6);
        row.is_active = sqlite3_column_int(company_stmt.stmt, 7) != 0;
        row.created_at = column_text_or_empty(company_stmt.stmt, 8);
        row.updated_at = column_text_or_empty(company_stmt.stmt, 9);
        out.symbol_found = true;
        out.resolved_symbol = row.symbol;
        out.company = row;
    } else {
        out.resolved_symbol = normalized_symbol;
    }

    StmtHandle annual_stmt;
    if (!prepare_statement(
            handle.db,
            "SELECT fiscal_year, revenue, operating_income, net_income, eps_diluted, "
            "shares_diluted, total_assets, total_liabilities, total_equity, "
            "operating_cash_flow, capex, free_cash_flow, book_value_per_share, roe, "
            "debt_ratio, source, source_updated_at, quality_flag "
            "FROM annual_fundamentals WHERE symbol = ?1 ORDER BY fiscal_year ASC",
            annual_stmt,
            error)) {
        out.error = error;
        return out;
    }
    sqlite3_bind_text(annual_stmt.stmt, 1, out.resolved_symbol.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(annual_stmt.stmt) == SQLITE_ROW) {
        AnnualFundamentalsRecord row;
        row.fiscal_year = sqlite3_column_int(annual_stmt.stmt, 0);
        row.revenue = column_double_or(annual_stmt.stmt, 1);
        row.operating_income = column_double_or(annual_stmt.stmt, 2);
        row.net_income = column_double_or(annual_stmt.stmt, 3);
        row.eps_diluted = column_double_or(annual_stmt.stmt, 4);
        row.shares_diluted = column_double_or(annual_stmt.stmt, 5);
        row.total_assets = column_double_or(annual_stmt.stmt, 6);
        row.total_liabilities = column_double_or(annual_stmt.stmt, 7);
        row.total_equity = column_double_or(annual_stmt.stmt, 8);
        row.operating_cash_flow = column_double_or(annual_stmt.stmt, 9);
        row.capex = column_double_or(annual_stmt.stmt, 10);
        row.free_cash_flow = column_double_or(annual_stmt.stmt, 11);
        row.book_value_per_share = column_double_or(annual_stmt.stmt, 12);
        row.roe = column_double_or(annual_stmt.stmt, 13);
        row.debt_ratio = column_double_or(annual_stmt.stmt, 14);
        row.source = column_text_or_empty(annual_stmt.stmt, 15);
        row.source_updated_at = column_text_or_empty(annual_stmt.stmt, 16);
        row.quality_flag = column_text_or_empty(annual_stmt.stmt, 17);
        out.annual_rows.push_back(row);
    }
    if (!out.annual_rows.empty()) {
        out.symbol_found = true;
    }

    StmtHandle quote_stmt;
    if (!prepare_statement(
            handle.db,
            "SELECT price, trailing_pe, forward_pe, price_to_book, peg_ratio, trailing_eps, "
            "market_cap, source, quote_time, updated_at "
            "FROM latest_quote_metrics WHERE symbol = ?1 LIMIT 1",
            quote_stmt,
            error)) {
        out.error = error;
        return out;
    }
    sqlite3_bind_text(quote_stmt.stmt, 1, out.resolved_symbol.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(quote_stmt.stmt) == SQLITE_ROW) {
        LatestQuoteMetricsRecord row;
        row.price = column_double_or(quote_stmt.stmt, 0);
        row.trailing_pe = column_double_or(quote_stmt.stmt, 1);
        row.forward_pe = column_double_or(quote_stmt.stmt, 2);
        row.price_to_book = column_double_or(quote_stmt.stmt, 3);
        row.peg_ratio = column_double_or(quote_stmt.stmt, 4);
        row.trailing_eps = column_double_or(quote_stmt.stmt, 5);
        row.market_cap = column_double_or(quote_stmt.stmt, 6);
        row.source = column_text_or_empty(quote_stmt.stmt, 7);
        row.quote_time = column_text_or_empty(quote_stmt.stmt, 8);
        row.updated_at = column_text_or_empty(quote_stmt.stmt, 9);
        out.latest_quote = row;
        out.symbol_found = true;
    }

    if (!out.symbol_found && out.error.empty()) {
        out.error = "db_symbol_not_found";
    }
    return out;
}

} // namespace finguard::data
