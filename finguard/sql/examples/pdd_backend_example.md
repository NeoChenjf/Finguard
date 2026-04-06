# PDD 后台数据库完整示例

## 生成示例库
```powershell
python .\finguard\scripts\build_symbol_example_db.py PDD
```

生成结果：

```text
finguard/data/examples/pdd_fundamentals_example.db
```

## 示例库预期内容
- `companies.symbol = PDD`
- `company_name = PDD Holdings Inc.`
- `annual_fundamentals` 包含近 8 年净利润
- 最新年包含：
  - `total_assets = 505034316000`
  - `total_liabilities = 191721192000`
  - `book_value_per_share = 40.65`
  - `debt_ratio ≈ 0.3796`
- `latest_quote_metrics` 包含：
  - `price = 101.0`
  - `trailing_pe ≈ 9.4636`
  - `price_to_book ≈ 2.4727`

## 后端验收
```powershell
$env:FINGUARD_FUNDAMENTALS_DB_PATH = 'D:\AI_Investment\finguard\data\examples\pdd_fundamentals_example.db'
.\finguard\build\Release\finguard.exe
```

请求：

```http
POST /api/v1/valuecell
{"symbol":"PDD"}
```

预期：
- `symbol = PDD`
- `pe ≈ 9.4636`
- `debt_ratio ≈ 0.3796`
- `growth_5y_cagr = -1`（当前样本下为 invalid growth base）
- `warnings` 至少包含：
  - `invalid_growth_base`
