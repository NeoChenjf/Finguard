

# FinGuard 量化价值分析系统 —— 继任/交接文档

## 继任说明
本项目已完成 Phase 1-10 的主线开发，当前“Value Cell 量化价值分析”功能已上线并持续完善。本文档面向继任 agent 或新协作者，帮助你快速理解现状、定位关键文件、明确遗留问题与下一步行动，实现无缝衔接。

---

## 现有功能与已完成项

- 后端 C++/Drogon 实现美股财务数据抓取、降级、指标计算（PE、PEG、PB、CAGR、负债率等）
- 前端 React/Vite 实现仪表盘、星级评分、定性评分、告警与数据展示
- AlphaVantage/Yahoo 数据源优先级与降级机制
- 完整的单元测试与样本验算（见 `优化/基本面分析.md`）
- 交互式仪表盘：PEG/ROE/负债率/PE*PB/CAGR 五星制，动态颜色与结论
- 主要代码入口：`finguard/src/valuation/yahoo_finance_client.cpp`、`frontend/src/pages/ValueCellPage.tsx`

---

## 可完善/遗留功能点

1. **数据源扩展**
	- 增加更多美股/港股/基金数据源（如 Finnhub、SimFin、东方财富等）
	- 支持多币种与本地化
2. **星级规则微调**
	- 允许用户自定义星级判定阈值
	- 增加更多维度（如现金流、分红率、营收增速等）
3. **定性分析增强**
	- LLM prompt 优化，支持多模型/多 API
	- 搜索引擎摘要与 LLM 结合
4. **UI/交互优化**
	- 响应式布局、移动端适配
	- 星级动画、结论解释 hover
5. **配置与部署**
	- 支持一键导入 API Key、自动检测依赖
	- Tauri 安装包自动升级与热更新
6. **测试与验收**
	- 增加端到端自动化测试
	- 完善验收文档与样本库

---

## 技术栈与主要文件分布

| 层         | 技术/文件路径                        | 说明                |
|------------|--------------------------------------|---------------------|
| 桌面壳     | Tauri                                | v2+                 |
| Rust 侧    | frontend/src-tauri/src/lib.rs         | sidecar/config初始化 |
| 前端       | Vite+React+TypeScript+TailwindCSS v4 | 仪表盘/星级/交互     |
| 状态管理   | Zustand                              | 5.0.11              |
| 后端       | C++ / Drogon                         | 8080 REST API       |
| API客户端  | frontend/src/api/client.ts            | 后端 API 客户端     |
| 主要页面   | frontend/src/pages/ValueCellPage.tsx  | 仪表盘/星级/评分     |
| 配置       | finguard/config/valuation.json        | API Key/优先级等    |
| 单元测试   | finguard/tests/test_valuation_metrics.cpp | C++测试           |
| 样本数据   | uploads/valuecell_samples/            | JSON快照            |
| 经验文档   | workbook/                            | 规范/经验/流程      |

---

## 典型开发/调试命令

### 后端 C++
```powershell
cd finguard
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cd build/Release
./finguard.exe
```

### 前端
```powershell
cd frontend
npm install
npm run dev
```

---

## 交接建议与注意事项

- 继任 agent 建议先通读 `frontend/src/pages/ValueCellPage.tsx` 和 `finguard/src/valuation/yahoo_finance_client.cpp`，理解星级评分与数据流。
- 重要参数（如 API Key、数据源优先级）集中在 `finguard/config/valuation.json`。
- 所有验算样本与验收证据均在 `优化/基本面分析.md`，便于回归。
- 如需扩展星级规则，建议先在前端 mock，后端补充字段。
- 交互/样式建议可参考 workbook/ 经验文档。

---

## 下一步行动清单（建议）

1. 评估并补充更多数据源，提升鲁棒性
2. 优化星级与定性评分算法，支持自定义
3. 增强定性分析与 LLM 结合能力
4. 完善移动端与桌面端适配
5. 持续补充验收样本与自动化测试
