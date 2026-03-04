---
title: Phase 4 可靠性与可观测基线规范
tags: [phase4, reliability, observability, rate_limit, circuit_breaker]
last_updated: 2026-02-10
source: workbook
status: draft
---
# Phase 4 可靠性与可观测基线规范

## 标题与目标
用“可先落地、再调参”的方式，确定 Phase 4 的可靠性与可观测基线规范。

## 学习路线 / 分层
1. 先理解：为什么要先有基线（可执行、可验收）。
2. 再理解：限流、熔断、超时/重试、指标与日志的基本作用。
3. 最后能把基线变成可配置、可验证的实现方案。

## 核心概念解释（含生活类比）
- 基线：开店的“最低服务标准”，先能营业，再逐步优化。
- 限流：门口的“放行闸”，控制单位时间进入的人数。
- 熔断：电路的“保险丝”，出问题时先断开，避免全盘拖垮。
- 观测：厨房的“监控面板”，看见哪里慢、哪里堵。

## 关键流程 / 步骤
1. 确定限流维度与 key（user + route 为主，模型单独桶）。
2. 明确无令牌时策略（入口层直接拒绝，内部调用限时等待）。
3. 设定熔断阈值与半开规则（如 50% 错误率 / 30s 窗口 / 半开 1~3 次）。
4. 定义最小指标集（请求量、延迟分位数、拒绝数、熔断次数、外部调用耗时）。
5. 统一日志字段（trace_id、route、status、latency_ms）。
6. 把参数下沉为配置（便于后续调参，不改代码）。

## 真实代码片段（C++ 伪代码，逐行注释）
```cpp
// 构造限流 key（以 user + route 为主，模型单独桶）
std::string make_bucket_key(const Request& req) {
  // 取用户标识（如 user_id 或 api_key）
  std::string user = req.user_id;
  // 取接口路径（如 /api/v1/chat/stream）
  std::string route = req.path;
  // 组合成主桶 key
  return "bucket:user:" + user + ":route:" + route;
}

// 入口层限流：无令牌直接拒绝
Response handle_request(Request& req) {
  // 获取主桶 key
  auto key = make_bucket_key(req);
  // 尝试拿令牌
  if (!token_bucket.allow(key)) {
    // 入口层直接拒绝（429）
    return Response::reject(429, "rate limited");
  }
  // 通过后继续处理
  return do_work(req);
}
```

## 小结 + 自测问题
小结：先立“可执行基线”，再通过配置与数据逐步优化参数。

自测：
1. 为什么入口层选择直接拒绝，而不是排队？
2. 为什么“模型桶”要单独设置？
3. 为什么日志必须带 trace_id？

自测题答案：
1. 入口层直接拒绝能保护系统整体，避免堆积导致雪崩；排队更适合内部调用层。
2. 外部模型成本高且易受限，单独桶可控制成本与稳定性。
3. trace_id 能把一次请求跨组件的日志串起来，便于排障。

## 下一步建议
- 把限流、熔断、超时/重试的参数写入配置文件并支持热更新。
- 制定压测计划与验收指标（P95、吞吐、拒绝率）。
- 结合 OpenTelemetry 引入 span 级别的链路追踪。

变更日志
- [2026-02-10] (手动新增) 建立 Phase 4 基线规范教学文档。