# BRK-B 后台数据库完整示例

## 生成示例库
```powershell
python .\finguard\scripts\build_symbol_example_db.py BRK-B
```

生成结果：

```text
finguard/data/examples/brk-b_fundamentals_example.db
```

## 示例库预期内容
- `companies.symbol = BRK-B`
- `company_name = Berkshire Hathaway Inc. Class B`
- `annual_fundamentals` 包含近 8 年净利润
- 最新年包含：
  - `total_assets = 1222176000000`
  - `total_liabilities = 502473000000`
  - `book_value_per_share = 498660`
  - `debt_ratio ≈ 0.4111`
- `latest_quote_metrics` 包含：
  - `price ≈ 473.97`
  - `trailing_pe ≈ 15.2923`
  - `price_to_book ≈ 1.4275`

## 后端验收
```powershell
$env:FINGUARD_FUNDAMENTALS_DB_PATH = 'D:\AI_Investment\finguard\data\examples\brk-b_fundamentals_example.db'
.\finguard\build\Release\finguard.exe
```

请求：

```http
POST /api/v1/valuecell
{"symbol":"BRK"}
```

预期：
- `symbol = BRK-B`
- `pe ≈ 15.2923`
- `peg ≈ 1.0523`
- `debt_ratio ≈ 0.4111`
- `growth_5y_cagr ≈ 0.1453`
- `warnings` 至少包含：
  - `missing_roe_history`
