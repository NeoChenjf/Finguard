---
title: trace_id 与链路追踪入门
tags: [observability, trace_id, logging]
last_updated: 2026-02-09
source: workbook
status: draft
---
# trace_id 与链路追踪入门

## 标题与目标
理解 trace_id 是什么，以及为什么它能把一次请求的日志“串起来”。

## 学习路线 / 分层
1. 先理解“同一请求会经过多个组件”。
2. 再理解用 trace_id 贯穿这些组件。
3. 最后理解如何生成、传递、记录。

## 核心概念解释（含生活类比）
- trace_id：快递单号，包裹经过不同站点都用同一个单号。
- 传播：请求携带 trace_id 继续往下游传递。
- 观测：任何日志只要记录 trace_id，就能回溯整条链路。

## 关键流程 / 步骤
1. 入口生成 trace_id（或从上游继承）。
2. 写入响应头，方便客户端追踪。
3. 下游调用时把 trace_id 继续透传。
4. 日志中固定字段输出 trace_id。

## 真实代码片段（C++ 思路示例）
```cpp
// 中间件伪代码：为每个请求注入 trace_id
std::string get_or_create_trace_id(const HttpRequest& req) {
  auto tid = req.getHeader("X-Trace-Id");
  if (!tid.empty()) return tid;
  return generate_uuid();
}

void handle_request(HttpRequest& req, HttpResponse& resp) {
  auto trace_id = get_or_create_trace_id(req);
  resp.addHeader("X-Trace-Id", trace_id);
  LOG_INFO << "trace_id=" << trace_id << " path=" << req.path();
  // 调用下游时继续带上 X-Trace-Id
}
```

## 小结 + 自测问题
小结：trace_id 让一次请求跨服务的日志可以被快速聚合。

自测：
1. 为什么 trace_id 要从入口开始生成？
2. 如果日志没有 trace_id，会发生什么问题？

自测题答案：
1. 入口生成可保证整条链路共用同一 ID，便于关联各组件日志。
2. 日志无法关联成一条请求链路，排障和回溯变困难。

## 下一步建议
- 学习 span / trace 的分层概念（OpenTelemetry）。
- 定义统一的日志字段规范。

变更日志
- [2026-02-09] (手动新增) 新建 trace_id 教学文档
