from __future__ import annotations

from seed_fundamentals_db import normalize_symbol, seed_symbols
from pathlib import Path


def main() -> None:
    symbols = ["TCOM", "BRK-B", "PDD"]
    base = Path(__file__).resolve().parents[1] / "data" / "examples"
    base.mkdir(parents=True, exist_ok=True)

    for raw_symbol in symbols:
        symbol = normalize_symbol(raw_symbol)
        output = base / f"{symbol.lower()}_fundamentals_example.db"
        seed_symbols([symbol], output_db=output, reset=True)

    print("Prepared all ValueCell demo DBs: TCOM / BRK-B / PDD")


if __name__ == "__main__":
    main()
