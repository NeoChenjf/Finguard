# Phase 13 - PlanPage.tsx 第一波治理

## Chapter 1: Spec pre-plan (frozen)

### 1) 本轮治理目标
- Decision / recommendation: 优先治理 `frontend/src/pages/PlanPage.tsx`，将其从页面布局 + 表单状态 + 提交编排 + 结果渲染四合一页面收敛为 page container + plan hook + 输入区组件 + 结果区组件。
- Why: 当前 `PlanPage.tsx` 是前端最明显热点，后续规则扩展、交互改版、结果对比增强都会继续堆在这里。
- Details: 本轮只做结构治理，不改接口、不改页面行为、不改规则计算口径。

### 2) 外部兼容性冻结
- Decision / recommendation: 保持页面 URL、文案语义、交互流程、提交时机、按钮行为、错误展示、结果展示、规则对比 shape 不变。
- Why: 本轮目标是拆热点，不是前端产品改版。
- Details: `/api/v1/plan` 请求 body、`PlanRequest` / `PlanResponse`、多规则并发请求与结果合并逻辑保持兼容。

### 3) 结构边界冻结
- Decision / recommendation: `PlanPage.tsx` 只保留页面容器职责，plan 私有常量/算法/类型、状态与提交逻辑、输入区展示、结果区展示分别收敛。
- Why: 避免继续把页面结构、业务逻辑、局部算法和 JSX 大段混在一个文件里。
- Details:
  - hook 统一承载状态、派生数据、交互动作
  - 输入区组件负责规则选择、画像、守拙预览、持仓、提交按钮
  - 结果区组件负责错误态、空态、对比表、规则结果卡片
  - 本轮不做更细粒度卡片碎拆

## Chapter 2: Phase 13 plan
Steps:
1. 读取 `PlanPage.tsx`、`api/client.ts` 与现有前端目录结构。
2. 新建 plan 私有模块，收敛 types/constants/utils。
3. 新建 `usePlanPage` hook，迁移状态、派生逻辑与提交编排。
4. 拆出输入区与结果区组件，保留当前页面行为与渲染结构。
5. 将 `PlanPage.tsx` 收缩为薄容器。
6. 执行前端 build，并回归后端 Release build / tests。
7. 更新 phase 文档与 `next_plan.md`。

Acceptance items:
1. `PlanPage.tsx` 明显缩成薄容器。
2. 输入区、结果区、hook、plan 私有算法能在 10 分钟内定位。
3. 前端 build 通过。
4. 后端构建与测试保持通过。
5. 页面行为与接口兼容保持不变。

## Chapter 3: Execution details

### Step execution log
- 新增 plan 私有模块：
  - `frontend/src/features/plan/types.ts`
  - `frontend/src/features/plan/constants.ts`
  - `frontend/src/features/plan/utils.ts`
  - `frontend/src/features/plan/usePlanPage.ts`
- 新增左右两大区块组件：
  - `frontend/src/features/plan/components/PlanInputPanel.tsx`
  - `frontend/src/features/plan/components/PlanResultsPanel.tsx`
- `frontend/src/pages/PlanPage.tsx` 已缩成薄容器，仅负责页面级布局与左右区块装配。
- 保留现有：
  - 规则多选 / 全选 / 仅守拙
  - 画像与资格校验
  - 守拙预览条形图
  - 持仓增删改
  - 多规则并发请求与结果合并
  - 空态 / 错态 / 结果态切换

### Problems and fixes
- 问题: `PlanInputPanel.tsx` 中 preview rows 初版由 `PREVIEW_META` 推断出过窄字面量联合类型，导致追加“个股仓位 / bg-orange-500”时报 TS 类型错误。
- 修复: 为 preview rows 显式引入 `PreviewRow` 结构，放宽为普通 `string + number` 组合。
- 问题: `npm run build` 在 PowerShell 中默认命中 `npm.ps1`，受本机 execution policy 限制；同时 Vite/esbuild 在沙箱内 spawn worker 时被拦截。
- 修复: 改用 `npm.cmd run build`，并在最终验收时申请一次提权构建以完成 Vite/esbuild worker 校验。
- 说明: 本轮未改动任何前后端 wire shape、文案语义与规则口径，只做职责拆分与结构收敛。

## Chapter 4: Acceptance details

### Acceptance checklist
- [x] `PlanPage.tsx` 瘦身完成（收缩到约 24 行）
- [x] plan hook 抽离完成
- [x] 输入区组件拆分完成
- [x] 结果区组件拆分完成
- [x] 前端 build 通过
- [x] 后端构建与测试保持通过

### Artifact list
- `frontend/src/pages/PlanPage.tsx`
- `frontend/src/features/plan/types.ts`
- `frontend/src/features/plan/constants.ts`
- `frontend/src/features/plan/utils.ts`
- `frontend/src/features/plan/usePlanPage.ts`
- `frontend/src/features/plan/components/PlanInputPanel.tsx`
- `frontend/src/features/plan/components/PlanResultsPanel.tsx`

### Change log
- 2026-03-29: 完成 Phase 13 第一波 PlanPage 前端结构治理，将页面收敛为薄容器 + hook + 输入区/结果区组件。
