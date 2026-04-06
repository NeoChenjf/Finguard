from __future__ import annotations

import argparse
import sqlite3
from pathlib import Path

from seed_fundamentals_db import ROOT, normalize_symbol, seed_symbols


def output_db_path(symbol: str) -> Path:
    return ROOT / "data" / "examples" / f"{symbol.lower()}_fundamentals_example.db"


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a single-symbol fundamentals SQLite example DB.")
    parser.add_argument("symbol", help="Example: TCOM / BRK-B / PDD")
    args = parser.parse_args()

    symbol = normalize_symbol(args.symbol)
    db_path = output_db_path(symbol)
    seed_symbols([symbol], output_db=db_path, reset=True)

    conn = sqlite3.connect(db_path)
    try:
        print("--- companies ---")
        for row in conn.execute(
            "select symbol, company_name, market, currency, sector, industry from companies"
        ):
            print(row)

        print("--- annual_fundamentals ---")
        for row in conn.execute(
            f"select fiscal_year, net_income, total_assets, total_liabilities, book_value_per_share, debt_ratio, source "
            f"from annual_fundamentals where symbol='{symbol}' order by fiscal_year"
        ):
            print(row)

        print("--- latest_quote_metrics ---")
        for row in conn.execute(
            f"select symbol, price, trailing_pe, price_to_book, peg_ratio, trailing_eps, source, updated_at "
            f"from latest_quote_metrics where symbol='{symbol}'"
        ):
            print(row)

        print("--- ingestion_runs ---")
        for row in conn.execute(
            "select id, run_type, symbol_scope, status, started_at, finished_at, error_summary "
            "from ingestion_runs order by id"
        ):
            print(row)

        print("--- ingestion_issues ---")
        for row in conn.execute(
            "select id, symbol, stage, issue_code, issue_message from ingestion_issues order by id"
        ):
            print(row)

        print(f"{symbol} example DB ready: {db_path}")
    finally:
        conn.close()


if __name__ == "__main__":
    main()
