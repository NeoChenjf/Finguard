# TCOM 后台数据库完整示例

## 目标
- 单独创建一个只包含 `TCOM` 的 SQLite 示例库
- 验证 `companies / annual_fundamentals / latest_quote_metrics / ingestion_runs / ingestion_issues`
- 验证后端可以直接读取这个示例库并返回 `/api/v1/valuecell`

## 生成示例库
在仓库根目录执行：

```powershell
python .\finguard\scripts\build_symbol_example_db.py TCOM
```

生成结果：

```text
finguard/data/examples/tcom_fundamentals_example.db
```

## 示例库内容
### 1. companies
- `symbol = TCOM`
- `company_name = Trip.com Group Limited`
- `market = US`
- `currency = USD`
- `sector = Consumer Cyclical`
- `industry = Travel Services`

### 2. annual_fundamentals
- 当前包含 `2017~2024` 共 8 年年报净利润
- 最新年（2024）额外包含：
  - `total_assets = 242581000000`
  - `total_liabilities = 99099000000`
  - `book_value_per_share = 37.7`
  - `debt_ratio = 0.4085192162617847`

### 3. latest_quote_metrics
- `price = 49.02`
- `trailing_pe = 6.8121`
- `price_to_book = 1.3267`
- `peg_ratio = 0.08161570934424682`
- `trailing_eps = 47.7859`
- `source = finnhub`

### 4. ingestion_runs
- 至少有 1 条 `bootstrap` 记录
- `symbol_scope = TCOM`
- `status = completed`

### 5. ingestion_issues
- 当前 TCOM 示例库应为空

## 用示例库启动后端
在 `finguard/build` 目录执行：

```powershell
$env:FINGUARD_FUNDAMENTALS_DB_PATH = 'D:\AI_Investment\finguard\data\examples\tcom_fundamentals_example.db'
.\Release\finguard.exe
```

然后请求：

```http
POST /api/v1/valuecell
{"symbol":"TCOM"}
```

## 预期结果
- `symbol = TCOM`
- `price ≈ 49.02`
- `pe ≈ 6.8121`
- `peg ≈ 0.3023`
- `debt_ratio ≈ 0.4085`
- `growth_5y_cagr ≈ 0.2253`
- `warnings` 至少包含：
  - `missing_roe_history`

## 说明
- 这是一个**完整后端数据库示例**，但当前样本仍是 8 年，不是完整 10 年。
- 若后续补足 10 年样本，只需扩展 seed 来源，不需要改 schema。
