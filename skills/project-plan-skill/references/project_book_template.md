# 项目书模板（产品上架版）

> 用于生成 `项目书.md`。语气面向真实产品上架，避免面试/简历语气。

## 1. 产品定位
- 一句话定位（产品/服务 + 目标用户 + 核心价值）。

## 2. 产品目标
- 目标 1
- 目标 2
- 目标 3

## 3. 目标用户与场景
- 目标用户：
- 核心场景：
  - 场景 1
  - 场景 2

## 4. 核心架构（HTTP + 流式）
- 客户端：
- C++ API 服务（核心）：
- Python/脚本（可选）：
- 存储：

**架构职责划分**：
- C++ 服务职责
- 数据/前端职责

## 5. 核心模块
### 5.1 请求流水线（Pipeline）
请求 → 画像解析 → 数据检索 → 候选方案生成 → 风控闸门 → 报告流式输出

### 5.2 风控闸门（Risk Gate，C++ 必须实现）
- 规则 1
- 规则 2

**标准输出**：
- PASS / WARN / FAIL
- 被触发的规则列表
- 自动修正后的组合（可选）

### 5.3 可靠性（限流/熔断/重试）
- 限流策略
- 熔断策略
- 超时重试与降级

### 5.4 可观测与压测
- Metrics：
- 压测工具与指标：

## 6. HTTP 接口设计
### 6.1 配置建议（非流式）
**POST** `/api/v1/plan`

输入示例：
```json
{
  "profile": {
    "risk_level": "medium",
    "horizon_months": 36,
    "cashflow_monthly": 3000,
    "emergency_fund_months": 6
  },
  "portfolio": [
    {"symbol":"SPY","weight":0.30},
    {"symbol":"BND","weight":0.40},
    {"symbol":"GLD","weight":0.10},
    {"symbol":"CASH","weight":0.20}
  ],
  "constraints": {
    "max_single_asset": 0.35,
    "max_drawdown": 0.20
  }
}
```

输出字段：
- `proposed_portfolio`
- `risk_report`
- `rationale`
- `rebalancing_actions`

### 6.2 投研问答（流式 SSE）
**POST** `/api/v1/chat/stream`

`text/event-stream`，每个 event：
- `type`: token / cite / metric / warning / done
- `payload`: 内容

### 6.3 数据更新（可选）
**POST** `/api/v1/data/refresh`

## 7. 技术栈
- C++ Web：
- JSON：
- 日志：
- 指标：
- 测试：
- 构建：

## 8. 目录结构（建议）
```
<项目目录结构>
```

## 9. 交付与上线要求
- 启动方式：
- README 必须包含：
  - 架构图
  - 接口示例（curl）
  - 压测结果（P95、吞吐）
  - 风控规则示例（rules.yaml）
  - 关键设计说明（线程模型、限流熔断、缓存策略）

## 10. 阶段路线
**第一阶段（...）**：

**第二阶段（...）**：

**第三阶段（...）**：

## 11. 风险与对策
- 风险 1：对策
- 风险 2：对策
