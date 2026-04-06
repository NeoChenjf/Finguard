# FinGuard 下一步交接文档

## 当前状态
- 时间：2026-03-30
- 当前主线：ValueCell SQLite 数据库成品 + Tavily 移除 + 统一 LLM prompt
- 结论：在前一轮“前端可直接验证数据库 profile”的基础上，本轮继续完成了：
  - 移除 Tavily 搜索代码与相关配置
  - 把 **ValueCell 基本面分析** 与 **投研对话** 统一为“结构化财务数据 + LLM”prompt
  - 当 DB 缺 symbol 或数据不完整时，优先自动联网抓取结构化财务数据
  - 在 `main` profile 下可自动回写 SQLite 主库
  - 前端 ValueCell 页面重新显示 LLM 分析内容
  - `SettingsPage` 可切换 `main / TCOM demo / BRK-B demo / PDD demo`
  - `ValueCellPage` 可显示当前 active DB profile，并在 demo 模式下提示数据库验证来源
  - 后端 `/api/v1/settings` 与 `/api/v1/valuecell` 已支持 profile 元信息与持久化切库
  - 自动化构建/测试、demo DB 生成、接口级人工验收已完成

## 本轮已完成
1. **SQLite ValueCell profile 产品化**
   - 新增固定 profile 注册表：
     - `main`
     - `tcom_demo`
     - `brk_b_demo`
     - `pdd_demo`
   - 解析优先级固定为：
     1. `FINGUARD_FUNDAMENTALS_DB_PATH`
     2. `config/valuation.json.valuecell_db_profile`
     3. `main`
2. **Settings API 扩展**
   - `GET /api/v1/settings` 新增：
     - `valuecell_db_profile`
     - `valuecell_db_profiles`
     - `valuecell_db_path_hint`
     - `valuecell_db_profile_active`
     - `valuecell_db_profile_label`
   - `POST /api/v1/settings` 支持保存 `valuecell_db_profile`
   - `valuecell_db_profile` 持久化到 `finguard/config/valuation.json`
3. **ValueCell 响应扩展**
   - `/api/v1/valuecell` 保持原响应主体不变
   - 新增可选元信息：
     - `db_profile`
     - `db_profile_label`
4. **前端成品闭环**
   - `frontend/src/pages/SettingsPage.tsx`
     - 新增 ValueCell 数据库 profile 选择区块
   - `frontend/src/pages/ValueCellPage.tsx`
     - 新增当前 active DB badge
     - 新增 demo 验证提示条
     - demo 模式下快捷样本优先引导对应 symbol
   - `frontend/src/pages/StartupPage.tsx`
     - 启动同步持久化 `valuecell_db_profile`
5. **示例数据库工具收口**
   - 通用脚本：`finguard/scripts/build_symbol_example_db.py`
   - 一键生成：`finguard/scripts/build_all_demo_dbs.py`
6. **文档与主文档回填**
   - 已更新单一主文档：`优化/基本面数据库.md`

## 已完成验收

### 自动化
- `python .\\finguard\\scripts\\build_all_demo_dbs.py` ✅
- `cmake -S .\\finguard -B .\\finguard\\build -DBUILD_TESTS=ON` ✅
- `cmake --build .\\finguard\\build --config Release --target finguard_tests finguard` ✅
- `.\finguard\\build\\Release\\finguard_tests.exe` → **55/55 通过** ✅
- `frontend\\npm.cmd run build` ✅

### 人工/API 闭环验收
1. `TCOM demo`
   - 保存 `valuecell_db_profile=tcom_demo`
   - `POST /api/v1/valuecell {"symbol":"TCOM"}`
   - 返回：
     - `db_profile=tcom_demo`
     - `price≈49.02`
     - `pe≈6.8121`
     - `peg≈0.3023`
2. `BRK-B demo`
   - 保存 `valuecell_db_profile=brk_b_demo`
   - `POST /api/v1/valuecell {"symbol":"BRK"}`
   - 返回：
     - `symbol=BRK-B`
     - `db_profile=brk_b_demo`
     - `price≈473.97`
     - `pe≈15.2923`
     - `peg≈1.0523`
3. `PDD demo`
   - 保存 `valuecell_db_profile=pdd_demo`
   - `POST /api/v1/valuecell {"symbol":"PDD"}`
   - 返回：
     - `db_profile=pdd_demo`
     - `price=101.0`
     - `pe≈9.4636`
4. 切回主库
   - 保存 `valuecell_db_profile=main`
   - 立即生效，无需重启服务、无需环境变量

## 关键文件入口

### 主文档 / 交接
- `优化/基本面数据库.md`
- `next_plan.md`

### 后端
- `finguard/src/data/fundamentals_db.h`
- `finguard/src/data/fundamentals_db.cpp`
- `finguard/src/valuation/fundamentals_db_client.h`
- `finguard/src/valuation/fundamentals_db_client.cpp`
- `finguard/src/valuation/valuation_config.h`
- `finguard/src/valuation/valuation_config.cpp`
- `finguard/src/valuation/valuation_handler.cpp`
- `finguard/src/valuation/yahoo_finance_client.h`
- `finguard/src/server/system_routes.cpp`
- `finguard/config/valuation.json`

### 前端
- `frontend/src/api/client.ts`
- `frontend/src/api/store.ts`
- `frontend/src/store/useAppStore.ts`
- `frontend/src/pages/StartupPage.tsx`
- `frontend/src/pages/SettingsPage.tsx`
- `frontend/src/pages/ValueCellPage.tsx`

### 数据/脚本/测试
- `finguard/sql/fundamentals_schema.sql`
- `finguard/scripts/seed_fundamentals_db.py`
- `finguard/scripts/build_symbol_example_db.py`
- `finguard/scripts/build_all_demo_dbs.py`
- `finguard/tests/test_fundamentals_db.cpp`
- `finguard/data/fundamentals.db`
- `finguard/data/examples/tcom_fundamentals_example.db`
- `finguard/data/examples/brk-b_fundamentals_example.db`
- `finguard/data/examples/pdd_fundamentals_example.db`

## 下一步建议
1. **补足 10 年历史覆盖**
   - 当前 demo / main 本地样本仍以 8 年为主
   - 下一步优先补齐至少一组完整 10 年样本，完成数据库方案最初目标
2. **把 seed 脚本升级为正式离线刷新 ETL**
   - 支持批量 symbol
   - 支持 latest quote 周期刷新
   - 支持 ingestion issue 更细粒度记录
3. **补 Settings / profile API 级测试**
   - 当前核心测试已覆盖 resolver 与 DB 读链路
   - 后续可把 `/api/v1/settings` 新字段回归补成明确的 route 级测试
4. **若继续前端治理**
   - 可把 `ValueCellPage.tsx` 进一步拆成 page container + profile banner + metrics/report panels

## 风险与注意事项
- `FINGUARD_FUNDAMENTALS_DB_PATH` 仍保留开发/调试最高优先级覆盖；若本机设置了它，会覆盖 Settings 中的 profile 选择。
- 本轮 demo 数据是**前端验证样本库**，不是正式生产覆盖；当前不承诺满 10 年。
- 本仓库工作树仍有其他并行未提交改动；后续提交时建议按“ValueCell DB productization”与其他主线分组整理，避免混成大提交。
