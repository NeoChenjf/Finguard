from __future__ import annotations

import argparse
import json
import sqlite3
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = ROOT.parent
DB_PATH = ROOT / "data" / "fundamentals.db"
SCHEMA_PATH = ROOT / "sql" / "fundamentals_schema.sql"

COMPANY_METADATA: dict[str, dict[str, Any]] = {
    "TCOM": {
        "company_name": "Trip.com Group Limited",
        "market": "US",
        "currency": "USD",
        "sector": "Consumer Cyclical",
        "industry": "Travel Services",
    },
    "PDD": {
        "company_name": "PDD Holdings Inc.",
        "market": "US",
        "currency": "USD",
        "sector": "Consumer Cyclical",
        "industry": "Internet Retail",
    },
    "BRK-B": {
        "company_name": "Berkshire Hathaway Inc. Class B",
        "market": "US",
        "currency": "USD",
        "sector": "Financial Services",
        "industry": "Insurance - Diversified",
    },
    "AAPL": {
        "company_name": "Apple Inc.",
        "market": "US",
        "currency": "USD",
        "sector": "Technology",
        "industry": "Consumer Electronics",
    },
}


def now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def normalize_symbol(symbol: str) -> str:
    s = symbol.strip().upper().replace(".", "-").replace("/", "-")
    if s in {"BRK", "BRKB", "BRK-B"}:
        return "BRK-B"
    return s


def read_json_if_exists(path: Path | None) -> dict[str, Any] | None:
    if path is None or not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def first_existing(paths: list[Path]) -> Path | None:
    for path in paths:
        if path.exists():
            return path
    return None


@dataclass
class SymbolPayload:
    symbol: str
    analysis_json: dict[str, Any] | None
    net_income_cache_json: dict[str, Any] | None
    quote_cache_json: dict[str, Any] | None


def load_symbol_payload(symbol: str) -> SymbolPayload:
    sym = normalize_symbol(symbol)
    analysis_path = first_existing(
        [
            REPO_ROOT / "uploads" / "valuecell_samples" / f"value_{sym}.json",
            REPO_ROOT / "uploads" / "tmp" / f"value_{sym}.json",
        ]
    )
    net_income_path = first_existing(
        [
            ROOT / "build" / "Release" / "uploads" / "tmp" / f"valuation_net_income_{sym}.json",
            ROOT / "build" / "uploads" / "tmp" / f"valuation_net_income_{sym}.json",
            ROOT / "uploads" / "tmp" / f"valuation_net_income_{sym}.json",
        ]
    )
    quote_path = first_existing(
        [
            ROOT / "build" / "uploads" / "tmp" / f"valuation_quote_metrics_{sym}.json",
            ROOT / "build" / "Release" / "uploads" / "tmp" / f"valuation_quote_metrics_{sym}.json",
            ROOT / "uploads" / "tmp" / f"valuation_quote_metrics_{sym}.json",
        ]
    )

    return SymbolPayload(
        symbol=sym,
        analysis_json=read_json_if_exists(analysis_path),
        net_income_cache_json=read_json_if_exists(net_income_path),
        quote_cache_json=read_json_if_exists(quote_path),
    )


def ensure_schema(conn: sqlite3.Connection) -> None:
    conn.executescript(SCHEMA_PATH.read_text(encoding="utf-8"))


def start_run(conn: sqlite3.Connection, symbols: list[str]) -> int:
    cur = conn.execute(
        """
        INSERT INTO ingestion_runs(run_type, symbol_scope, status, started_at)
        VALUES (?, ?, ?, ?)
        """,
        ("bootstrap", ",".join(symbols), "running", now_iso()),
    )
    return int(cur.lastrowid)


def finish_run(conn: sqlite3.Connection, run_id: int, status: str, error_summary: str | None) -> None:
    conn.execute(
        """
        UPDATE ingestion_runs
        SET status = ?, finished_at = ?, error_summary = ?
        WHERE id = ?
        """,
        (status, now_iso(), error_summary, run_id),
    )


def record_issue(conn: sqlite3.Connection, run_id: int, symbol: str, stage: str, code: str, message: str) -> None:
    conn.execute(
        """
        INSERT INTO ingestion_issues(ingest_run_id, symbol, stage, issue_code, issue_message, created_at)
        VALUES (?, ?, ?, ?, ?, ?)
        """,
        (run_id, symbol, stage, code, message, now_iso()),
    )


def upsert_company(conn: sqlite3.Connection, symbol: str) -> None:
    ts = now_iso()
    meta = COMPANY_METADATA.get(symbol, {})
    conn.execute(
        """
        INSERT INTO companies(symbol, normalized_symbol, company_name, market, currency, sector, industry, is_active, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(symbol) DO UPDATE SET
            normalized_symbol = excluded.normalized_symbol,
            company_name = excluded.company_name,
            market = excluded.market,
            currency = excluded.currency,
            sector = excluded.sector,
            industry = excluded.industry,
            is_active = excluded.is_active,
            updated_at = excluded.updated_at
        """,
        (
            symbol,
            symbol,
            meta.get("company_name", symbol),
            meta.get("market", "US"),
            meta.get("currency", "USD"),
            meta.get("sector"),
            meta.get("industry"),
            1,
            ts,
            ts,
        ),
    )


def upsert_annual_rows(conn: sqlite3.Connection, run_id: int, payload: SymbolPayload) -> int:
    rows: list[dict[str, Any]] = []
    if payload.analysis_json and isinstance(payload.analysis_json.get("net_profit_history"), list):
        rows.extend(payload.analysis_json["net_profit_history"])
    elif payload.net_income_cache_json and isinstance(payload.net_income_cache_json.get("rows"), list):
        rows.extend(payload.net_income_cache_json["rows"])

    if not rows:
        return 0

    analysis = payload.analysis_json or {}
    updated_at = analysis.get("net_profit_updated_at") or (payload.net_income_cache_json or {}).get("updated_at") or now_iso()
    latest_year = max(int(row.get("year", 0)) for row in rows)
    count = 0

    for row in rows:
        year = int(row.get("year", 0))
        if year <= 0:
            continue

        latest_extra = year == latest_year
        conn.execute(
            """
            INSERT INTO annual_fundamentals(
                symbol, fiscal_year, net_income, eps_diluted, total_assets, total_liabilities,
                book_value_per_share, debt_ratio, source, source_updated_at, quality_flag, ingest_run_id
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(symbol, fiscal_year) DO UPDATE SET
                net_income = excluded.net_income,
                eps_diluted = COALESCE(excluded.eps_diluted, annual_fundamentals.eps_diluted),
                total_assets = COALESCE(excluded.total_assets, annual_fundamentals.total_assets),
                total_liabilities = COALESCE(excluded.total_liabilities, annual_fundamentals.total_liabilities),
                book_value_per_share = COALESCE(excluded.book_value_per_share, annual_fundamentals.book_value_per_share),
                debt_ratio = COALESCE(excluded.debt_ratio, annual_fundamentals.debt_ratio),
                source = COALESCE(excluded.source, annual_fundamentals.source),
                source_updated_at = COALESCE(excluded.source_updated_at, annual_fundamentals.source_updated_at),
                quality_flag = excluded.quality_flag,
                ingest_run_id = excluded.ingest_run_id
            """,
            (
                payload.symbol,
                year,
                row.get("net_income"),
                analysis.get("eps") if latest_extra else None,
                analysis.get("total_assets") if latest_extra else None,
                analysis.get("total_liabilities") if latest_extra else None,
                analysis.get("book_value_per_share") if latest_extra else None,
                analysis.get("debt_ratio") if latest_extra else None,
                row.get("source") or "sample_seed",
                updated_at,
                "sample_seed",
                run_id,
            ),
        )
        count += 1
    return count


def upsert_quote_row(conn: sqlite3.Connection, payload: SymbolPayload) -> bool:
    analysis = payload.analysis_json or {}
    quote = payload.quote_cache_json or {}
    if not quote and not any(key in analysis for key in ("price", "current_pe", "current_peg", "price_to_book", "eps")):
        return False

    conn.execute(
        """
        INSERT INTO latest_quote_metrics(
            symbol, price, trailing_pe, forward_pe, price_to_book, peg_ratio,
            trailing_eps, market_cap, source, quote_time, updated_at
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(symbol) DO UPDATE SET
            price = excluded.price,
            trailing_pe = excluded.trailing_pe,
            forward_pe = excluded.forward_pe,
            price_to_book = excluded.price_to_book,
            peg_ratio = excluded.peg_ratio,
            trailing_eps = excluded.trailing_eps,
            market_cap = excluded.market_cap,
            source = excluded.source,
            quote_time = excluded.quote_time,
            updated_at = excluded.updated_at
        """,
        (
            payload.symbol,
            quote.get("current_price", analysis.get("price")),
            quote.get("trailing_pe", analysis.get("current_pe")),
            quote.get("forward_pe"),
            quote.get("price_to_book", analysis.get("price_to_book")),
            quote.get("peg_ratio", analysis.get("current_peg")),
            quote.get("trailing_eps", analysis.get("eps")),
            quote.get("market_cap"),
            quote.get("source", analysis.get("data_source", "sample_seed")),
            quote.get("updated_at", analysis.get("net_profit_updated_at", now_iso())),
            quote.get("updated_at", analysis.get("net_profit_updated_at", now_iso())),
        ),
    )
    return True


def seed_symbols(symbols: list[str], output_db: Path = DB_PATH, reset: bool = False) -> None:
    output_db.parent.mkdir(parents=True, exist_ok=True)
    if reset and output_db.exists():
        output_db.unlink()

    conn = sqlite3.connect(output_db)
    try:
        ensure_schema(conn)
        run_id = start_run(conn, symbols)
        issue_count = 0

        for symbol in symbols:
            payload = load_symbol_payload(symbol)
            upsert_company(conn, payload.symbol)

            if upsert_annual_rows(conn, run_id, payload) == 0:
                record_issue(conn, run_id, payload.symbol, "annual_fundamentals", "missing_annual_source", "No annual source JSON found for symbol.")
                issue_count += 1

            if not upsert_quote_row(conn, payload):
                record_issue(conn, run_id, payload.symbol, "latest_quote_metrics", "missing_quote_source", "No quote source JSON found for symbol.")
                issue_count += 1

        finish_run(conn, run_id, "completed_with_warnings" if issue_count else "completed", None if issue_count == 0 else f"{issue_count} issue(s)")
        conn.commit()
        print(f"Seeded fundamentals DB: {output_db}")
        print(f"Symbols: {', '.join(symbols)}")
        print(f"Issues: {issue_count}")
    finally:
        conn.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Seed ValueCell fundamentals SQLite DB from local sample/cache files.")
    parser.add_argument("--symbols", nargs="*", default=["AAPL", "BRK-B", "PDD", "TCOM"])
    parser.add_argument("--output-db", default=str(DB_PATH))
    parser.add_argument("--reset", action="store_true")
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    seed_symbols(
        [normalize_symbol(item) for item in args.symbols],
        output_db=Path(args.output_db),
        reset=args.reset,
    )
