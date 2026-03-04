# 责任边界：入口限流 vs 内部等待

目标：搞清楚为什么入口要"直接拒绝"而内部调用要"限时等待"，以及这样设计的原因，能在项目代码里找到对应的实现。

---

## 学习路线（从易到难）

1. 先理解"入口"和"内部"分别指什么（概念分层）。
2. 再理解两种策略的区别：直接拒绝 vs 限时等待。
3. 最后对照项目代码看这两种策略在哪里被实现。

---

## 核心概念解释（生活类比）

### 什么是"入口层"？

> 想象一家火锅店。**入口层**就是店门口的服务员。

外面来了 100 个顾客，今天只有 10 张桌子。服务员应该怎么做？

- **策略 A（直接拒绝）**：今天满座，直接告诉顾客"不好意思今天满了，请换天再来"。  
  顾客不进门、不占用任何桌椅、不让厨房多备料。

- **策略 B（让顾客等在门口）**：先把 100 个人都叫进来在门口站着。  
  结果：大厅越堆越多，厨房乱、服务员乱、正在吃饭的顾客体验也变差。

**结论**：入口就应该用策略 A——直接拒绝，保护整个系统不堆积。

---

### 什么是"内部调用"？

> 同一家火锅店里，传菜员要从厨房取菜。

有时候厨师手头忙，差 10 秒才能出菜。传菜员应该怎么做？

- **策略 A（直接走掉）**：厨师说"还没好"，传菜员立刻走人，顾客就没菜吃。  
  结果：明明 10 秒能拿到的菜，白白丢失了。

- **策略 B（等一会儿再来拿）**：传菜员在旁边等最多 30 秒，如果出菜就端走，超时才放弃。  
  结果：大多数情况下厨师能在时限内出菜，成功率更高。

**结论**：内部调用应该用策略 B——限时等待，平滑下游的短暂抖动。

---

## 为什么要区分这两种策略？

| 位置 | 来自哪里 | 结果要求 | 推荐策略 |
|------|----------|----------|----------|
| 入口层 | 外部用户 / 客户端 | 快速响应，不能让用户一直挂着 | 直接拒绝（返回 429） |
| 内部调用 | 服务内部 → LLM/下游 | 下游偶发抖动是正常的，值得多等一下 | 限时等待（设 timeout） |

**关键原因拆解：**

1. **入口保护系统整体不堆积**  
   如果入口也等待，外部大量请求同时挂起等待，服务器内存/线程会被耗尽。哪怕下游最终恢复，期间系统已经雪崩了。直接拒绝能让客户端立刻知道"请稍后再试"，不占资源。

2. **内部等待平滑下游抖动**  
   下游（比如 LLM API）偶尔会有几十毫秒的延迟波动，这叫"抖动"（jitter）。如果内部也立刻放弃，会白白浪费大量请求。设一个合理的超时（比如 20 秒），大多数真正慢的请求都能在超时前完成。

---

## 关键流程（一句话版本）

```
外部请求 → 入口限流检测 → 没令牌？立刻 429 ✕
                         ↓ 有令牌
              内部调用 LLM → 设置 timeout_ms → 等待，超时才失败
```

---

## 真实代码示例（来自项目）

### 1. 入口层：直接拒绝（routes.cpp）

以下代码位于 HTTP 路由处理器里，这是外部请求第一个到达的地方：

```cpp
// routes.cpp — /api/v1/plan 路由入口层逻辑

// 检查当前并发数是否超限
if (!g_concurrency_limiter.try_acquire()) {
    // try_acquire() 失败 = 超并发 → 立刻构造 429 响应，直接返回
    util::global_metrics().record_rate_limit_reject(); // 记录被拒绝的指标
    Json::Value body = rate_limit_error_body();        // 标准 429 错误体
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(k429TooManyRequests);          // HTTP 状态码 429
    resp->addHeader("X-Trace-Id", trace_id);           // 携带追踪 ID，方便排查
    cb(resp);                                          // 直接回调，请求结束
    return;                                            // 不继续往下走
}

// 检查入口令牌桶（基于 user_id+route）
if (!g_entry_bucket.allow(make_entry_key(req), rate_cfg.entry.rate_rps, rate_cfg.entry.capacity)) {
    // allow() 失败 = 没有令牌 → 同样立刻 429，不等待
    util::global_metrics().record_rate_limit_reject();
    Json::Value body = rate_limit_error_body();
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(k429TooManyRequests);
    resp->addHeader("X-Trace-Id", trace_id);
    cb(resp);
    g_concurrency_limiter.release();                   // 释放并发槽
    return;
}
// 只有两步都通过，才继续处理请求
```

**关注点**：`try_acquire()` 和 `allow()` 都是**非阻塞**的——拿不到就立刻 `return`，不死等。

---

### 2. 内部调用：限时等待（llm_client.cpp）

以下代码是内部调用 LLM 时的超时策略：

```cpp
// llm_client.cpp — 内部调用 LLM 的超时配置

// 从配置文件读取默认超时
int timeout_ms = timeout_cfg.external_call_timeout_ms;  // 全局默认超时（如 15000ms = 15秒）
int max_retries = timeout_cfg.external_call_max_retries; // 最大重试次数

// 按路由覆盖：chat_stream 有自己的专属超时
const auto route_it = timeout_cfg.route_overrides.find("chat_stream");
if (route_it != timeout_cfg.route_overrides.end()) {
    timeout_ms = route_it->second.timeout_ms;    // 路由级超时（如 20000ms = 20秒）
    max_retries = route_it->second.max_retries;
}

// 按模型覆盖：特定模型还可以有更细粒度的超时
const auto model_timeout_it = timeout_cfg.model_overrides.find(family);
if (model_timeout_it != timeout_cfg.model_overrides.end()) {
    timeout_ms = model_timeout_it->second.timeout_ms;  // 模型级超时
    max_retries = model_timeout_it->second.max_retries;
}

// 将最终确定的超时时间写入调用配置
cfg.timeout_ms = timeout_ms;
// 后续 HTTP 请求会带着这个 timeout_ms 去等待下游，超时才失败
```

**关注点**：这里不是立刻放弃，而是**设好超时时间之后再发请求**，等待到期才算失败。

---

## 对应的配置文件

```yaml
# config/timeout.yaml（部分示意）

external_call_timeout_ms: 15000  # 内部调用默认最多等 15 秒
external_call_max_retries: 1     # 超时后最多重试 1 次

route_overrides:
  chat_stream:
    timeout_ms: 20000            # /chat/stream 路由可以等更久（LLM 生成慢）
    max_retries: 1

# config/rate_limit.yaml（部分示意）

entry:
  rate_rps: 10                   # 入口每秒放行 10 个请求
  capacity: 20                   # 令牌桶最多存 20 个（允许短暂突发）
  # 没令牌 → 立刻 429，不等待
```

---

## 小结

- **责任边界**：入口 vs 内部，是两种完全不同的保护场景。
- **入口层**：直接拒绝（non-blocking）。目的是快速保护系统不被外部请求压垮，客户端得到明确的 429 可以自行退避。
- **内部调用**：限时等待（blocking with timeout）。目的是容忍下游短暂抖动，不因偶发慢响应就白白丢弃请求。
- 两者结合，既不让系统堆积崩溃，又不因为下游一点波动就频繁失败。

---

## 自测题（含答案）

**1) 入口层为什么不能"等待"外部请求？**
> 答案：入口若等待，大量并发请求会同时挂住，占用线程和内存，请求队列越堆越深，最终导致系统整体雪崩。直接拒绝能让资源立刻释放。

**2) 内部调用为什么要设 timeout 而不是立刻放弃？**
> 答案：下游（如 LLM）有抖动是正常的，短暂延迟后能恢复。设超时等待，大多数情况能成功；立刻放弃会白白丢失本可成功的请求，降低系统可用性。

**3) 项目里入口限流检测失败后，下一步是什么？**
> 答案：立刻构造 429 错误体，回调 `cb(resp)` 返回给客户端，并 `return`，不再继续执行后续逻辑。

**4) `timeout_ms` 可以按什么维度覆盖？**
> 答案：可以按路由（`route_overrides`）和模型（`model_overrides`）分别覆盖，精细控制不同链路的等待上限。

---

## 下一步建议

1. 打开 [finguard/src/server/routes.cpp](../../finguard/src/server/routes.cpp) 找到 `g_entry_bucket.allow()` 那行，观察整个入口判断流程。
2. 打开 [finguard/src/llm/llm_client.cpp](../../finguard/src/llm/llm_client.cpp) 找到 `cfg.timeout_ms = timeout_ms` 那行，观察超时覆盖的三级优先级（全局 → 路由 → 模型）。
3. 尝试把 `config/rate_limit.yaml` 里的 `rate_rps` 设为 1，用压测工具发 5 个并发请求，观察是否有 429 返回。

---

变更日志
- [2026-02-23] (自动加入) 来源：用户请求讲解 phase4.md 第5条"责任边界（入口限流 vs 内部等待）"
