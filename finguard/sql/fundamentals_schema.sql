PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS companies (
    symbol TEXT PRIMARY KEY,
    normalized_symbol TEXT NOT NULL,
    company_name TEXT,
    market TEXT,
    currency TEXT,
    sector TEXT,
    industry TEXT,
    is_active INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS ingestion_runs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    run_type TEXT NOT NULL,
    symbol_scope TEXT,
    status TEXT NOT NULL,
    started_at TEXT NOT NULL,
    finished_at TEXT,
    error_summary TEXT
);

CREATE TABLE IF NOT EXISTS annual_fundamentals (
    symbol TEXT NOT NULL,
    fiscal_year INTEGER NOT NULL,
    revenue REAL,
    operating_income REAL,
    net_income REAL,
    eps_diluted REAL,
    shares_diluted REAL,
    total_assets REAL,
    total_liabilities REAL,
    total_equity REAL,
    operating_cash_flow REAL,
    capex REAL,
    free_cash_flow REAL,
    book_value_per_share REAL,
    roe REAL,
    debt_ratio REAL,
    source TEXT,
    source_updated_at TEXT,
    quality_flag TEXT,
    ingest_run_id INTEGER,
    PRIMARY KEY (symbol, fiscal_year),
    FOREIGN KEY (symbol) REFERENCES companies(symbol) ON DELETE CASCADE,
    FOREIGN KEY (ingest_run_id) REFERENCES ingestion_runs(id) ON DELETE SET NULL
);

CREATE TABLE IF NOT EXISTS latest_quote_metrics (
    symbol TEXT PRIMARY KEY,
    price REAL,
    trailing_pe REAL,
    forward_pe REAL,
    price_to_book REAL,
    peg_ratio REAL,
    trailing_eps REAL,
    market_cap REAL,
    source TEXT,
    quote_time TEXT,
    updated_at TEXT,
    FOREIGN KEY (symbol) REFERENCES companies(symbol) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS ingestion_issues (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ingest_run_id INTEGER,
    symbol TEXT,
    stage TEXT,
    issue_code TEXT NOT NULL,
    issue_message TEXT,
    created_at TEXT NOT NULL,
    FOREIGN KEY (ingest_run_id) REFERENCES ingestion_runs(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_companies_normalized_symbol
    ON companies(normalized_symbol);

CREATE INDEX IF NOT EXISTS idx_annual_symbol_year
    ON annual_fundamentals(symbol, fiscal_year);

CREATE INDEX IF NOT EXISTS idx_ingestion_runs_status
    ON ingestion_runs(status);

CREATE INDEX IF NOT EXISTS idx_ingestion_issues_run
    ON ingestion_issues(ingest_run_id);
