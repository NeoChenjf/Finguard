---
title: C++突击文档
tags: [cpp, interview, linux, os, network, mysql, redis]
last_updated: 2026-03-09
source: workbook
status: draft
---

# C++突击文档

目标：把你两张图里的每个专业名词都拆成大白话、项目作用、总结和模拟面试题。

## 学习路线

1. C++语言与面向对象
2. C++11/17 与 STL
3. 数据结构与算法
4. 计算机网络
5. Linux 与操作系统
6. MySQL 与 Redis
7. 工具与实战

---

## 一、C++语言与面向对象

### 1. C/C++

#### 详细讲解（小点拆解）
- C：更靠近硬件，像手工拧螺丝，控制很细，但容易写出容易崩的代码。
- C++：在 C 上加了类、模板、RAII，像从手工升级到带工装的流水线，效率和质量更稳。
- 面试重点：不是背语法，而是说明你如何用 C++ 写出稳定、高性能、可维护服务。

#### 在 finguard 项目中的作用
- 后端核心是 C++17：`finguard/src/server/http_server.cpp`。

#### 总结
- C++ 的价值是性能和工程化并存。

#### 模拟面试自测
1. 面试官问：为什么在 finguard 这类 AI 高并发网关中仍坚持使用 C++？
答案：
- **延迟敏感**：`finguard/src/util/metrics_registry.cpp` 中通过 `std::sort` 快速计算 P95/P99 响应耗时，毫秒级感知波动，这是 Python 等脚本语言难以在主链路上直接抗住的。
- **高并发**：`finguard/src/util/concurrency_limiter.cpp` 深度运用 `std::atomic` 的 `compare_exchange_weak` 实现无锁计数，性能上限远高于加全局锁的解释型语言。
- **资源可控**：通过 `std::unique_ptr` 等 RAII 手段（参考 `finguard/src/llm/llm_client.cpp`），精准控制内存和连接池生命周期，避免了 GC（垃圾回收）带来的随机卡顿。
- **生态成熟**：依赖高性能框架 Drogon (基于 Epoll/Coroutine) 和 `vcpkg` 包管理，结合高效的 C++ 库如 `nlohmann_json` 和 `spdlog`，平衡了工程效率与极致性能。

### 2. Python（与 C++ 对照）

#### 详细讲解（小点拆解）
- Python：开发快，适合脚本、数据处理、自动化。
- C++：运行快，适合高并发主链路。
- 等价思维：Python 的 `dict/list` 在 C++ 里常对应 `unordered_map/vector`。

#### 在 finguard 项目中的作用
- C++ 是主链路，Python 用于压测工具：`tools/p5_bench.py`。

#### 总结
- 选语言看场景，不看“谁高级”。

#### 模拟面试自测
1. 面试官问：为什么一个项目要混用 Python 和 C++？
答案：Python 提效（如 `p5_bench.py` 利用 `asyncio` 快速开发压测工具），C++ 保性能（主链路高并发），分工更优。

### 3. Lambda 表达式与回调

#### 详细讲解（小点拆解）
- **语法**：`[捕获](参数){函数体}`。
- **白话**：**“随身携带的小函数”**。不用费劲去写一个正式的函数名或类，直接在用的地方写逻辑。
- **核心能力**：**捕获（Capture）**。能把外层的变量（如 `trace_id`、`start_time`）“打包”带进函数内部。

#### 在 finguard 项目中的作用

**HTTP 路由处理**：在 [src/server/routes.cpp](finguard/src/server/routes.cpp) 中，每个接口的处理逻辑直接以 Lambda 的形式注册到 Drogon 框架，不需要为每个路由写一个继承自框架基类的 Handler 类。

以 `/health` 为例，这是最简单的形态——捕获列表为空 `[]`，因为不需要外部上下文：

```cpp
// routes.cpp L107
app().registerHandler("/health",
    [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
        Json::Value body;
        body["status"] = "ok";
        auto resp = HttpResponse::newHttpJsonResponse(body);
        cb(resp);  // cb 就是"把响应交还给框架"的回调
    }, {Get});
```

再看 `/api/v1/chat/stream`（流式问答），同样是一个 Lambda，但函数体内部做了完整的限流、鉴权、LLM 调用链，`trace_id` 在 Lambda 内部生成并贯穿全程：

```cpp
// routes.cpp L401
app().registerHandler("/api/v1/chat/stream",
    [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
        // ① 生成 trace_id：请求的"身份证"，当前 Lambda 生命周期内全程有效
        const auto trace_id = get_or_create_trace_id(req);
        const auto start    = std::chrono::steady_clock::now();

        // ② 并发限流：水位超限直接 429，不进入重型 LLM 环节
        if (!g_concurrency_limiter.try_acquire()) {
            auto resp = HttpResponse::newHttpJsonResponse(rate_limit_error_body());
            resp->setStatusCode(k429TooManyRequests);
            resp->addHeader("X-Trace-Id", trace_id);
            cb(resp);
            return;
        }

        // ③ 调用 LLM，拿到同步结果（内部支持降级）
        auto result = client.stream_chat(prompt);

        // ④ 把大块 SSE payload 存入 shared_ptr，
        //    再用第二个 Lambda 捕获它做流式分段写出
        auto data   = std::make_shared<std::string>(std::move(payload));
        auto offset = std::make_shared<std::size_t>(0);

        auto resp = HttpResponse::newStreamResponse(
            [data, offset](char *buffer, std::size_t len) -> std::size_t {
                // ← 这里是嵌套 Lambda！捕获 data/offset 两个 shared_ptr
                if (*offset >= data->size()) return 0;         // 写完，通知框架结束流
                std::size_t to_copy = std::min(len, data->size() - *offset);
                std::memcpy(buffer, data->data() + *offset, to_copy);
                *offset += to_copy;
                return to_copy;
            });

        cb(resp);
        // ⑤ 请求结束后释放并发槽位并记录延迟
        g_concurrency_limiter.release();
        log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
    }, {Post});
```

**关键点拆解**：

| 位置 | 捕获方式 | 为什么这样捕获 |
|---|---|---|
| 外层路由 Lambda `[]` | 无捕获 | `trace_id` 是 Lambda 内部创建的局部变量，不需要从外部捕获 |
| 内层流式 Lambda `[data, offset]` | 值捕获两个 `shared_ptr` | 外层函数执行完后 `data`/`offset` 若是裸指针会悬挂，用 `shared_ptr` 值捕获能保证流没写完前对象不释放 |

**与虚函数对比**：如果改用虚函数继承，你需要为每个路由写一个类、重写 `handleRequest()` 虚方法，还要单独存一个 `trace_id` 成员变量；而 Lambda 直接在注册时"就地写逻辑 + 就地捕获上下文"，代码量少一半，且方便编译器内联优化。

#### 总结
- **解耦**：逻辑随调随写，不依赖重型的虚函数体系。
- **性能**：方便编译器内联优化，减少虚函数表查表开销。

#### 模拟面试自测
1. 面试官问：为什么你的项目里大量使用 Lambda 而不是虚函数多态？
答案：Lambda 是“逻辑组合”，比虚函数继承更灵活、性能更好（易内联），且闭包能力能轻松处理异步上下文（如捕获 `trace_id`）。

2. 面试官进阶问：Lambda 的“捕获 []”和“传参 ()”有什么本质区别？请结合 finguard 项目中的 `trace_id` 举例。
答案：
- **背景知识**：`trace_id` 是请求的“身份证号”，用于在海量日志中串联起某次 LLM 请求的全部过程（从收到请求到生成完成）。
- **如果用“传参 ()”的困境**：像 `drogon` 这种底层框架的回调函数签名（即坑位）是固定的。如果你想在回调里打印 `trace_id`，但框架定义的函数签名只给了你 `(auto response)`，你**根本没法通过参数把 trace_id 塞进去**，除非你去修改底层框架代码（这几乎不可能）。
- **通过“捕获 []”的妙处**：我们在发起异步请求时，直接写 `[trace_id](auto resp) { ... }`。
    - **逻辑解耦**：Lambda 内部通过“捕获”主动带走了 `trace_id` 的副本，不需要框架配合传参。
    - **生命周期保护**：由于异步请求可能在 5 秒后才返回，此时外层函数早已执行结束。通过 `[trace_id]`（值捕获），Lambda 在自己内部“存了一份私房钱”（副本），即使 5 秒后原变量失效了，Lambda 依然能凭这分副本记录下正确的日志。
- **总结**：捕获解决了**“接口签名不匹配”**和**“异步状态丢失”**这两大核心工程痛点。

3. 面试官深挖：以 finguard 调用大模型为例，Lambda 什么时候"进场"和"退场"？
答案：

用代码直接标注每个阶段：

```cpp
// ══════════════════════════════════════════════════════
// 【外层 Lambda 进场】
// 时机：服务器启动时，registerHandler 被调用，
//       Drogon 把这个 Lambda 对象存入内部路由表。
//       此后每次 POST /api/v1/chat/stream 请求到来，
//       Drogon 从路由表取出它并调用。
// ══════════════════════════════════════════════════════
app().registerHandler("/api/v1/chat/stream",
    [](const HttpRequestPtr &req, auto &&cb) {

        const auto trace_id = get_or_create_trace_id(req);
        // ↑ trace_id 在 Lambda 内部诞生，生命周期绑定这次调用

        if (!g_concurrency_limiter.try_acquire()) {
            cb(make_429_response(trace_id));
            return;
            // ↑ 提前 return：外层 Lambda 本次调用帧结束，
            //   trace_id 就地释放。
            //   但 Lambda 对象本身还在路由表里，
            //   下一个请求还会再进场一次。
        }

        auto result = client.stream_chat(prompt);

        auto data   = std::make_shared<std::string>(std::move(result.payload));
        auto offset = std::make_shared<std::size_t>(0);

        // ══════════════════════════════════════════════
        // 【内层流式 Lambda 进场】
        // 时机：newStreamResponse() 被调用的这一行。
        //       Drogon 拿走这个 Lambda 对象，存入当前连接的
        //       写就绪回调槽，等网络可写时反复触发。
        //       data / offset 被值捕获，引用计数 +1。
        // ══════════════════════════════════════════════
        auto resp = HttpResponse::newStreamResponse(
            [data, offset](char *buf, std::size_t len) -> std::size_t {
                if (*offset >= data->size()) return 0;
                // ↑ return 0 是信号：数据写完，通知 Drogon 停止调用
                std::size_t n = std::min(len, data->size() - *offset);
                std::memcpy(buf, data->data() + *offset, n);
                *offset += n;
                return n;
            });
        // ══════════════════════════════════════════════
        // 【内层 Lambda 退场】
        // 时机：Drogon 收到 return 0 后，销毁该 Lambda 对象。
        //       data / offset 的 shared_ptr 引用计数 -1，
        //       无其他持有者则内存释放。
        // ══════════════════════════════════════════════

        cb(resp);
        g_concurrency_limiter.release();
        log_request_metrics(trace_id, ...);

        // ══════════════════════════════════════════════
        // 【外层 Lambda 本次调用退场】
        // 时机：} 右括号执行完，本次请求对应的调用帧结束。
        //       trace_id（局部变量）此处释放。
        //       Lambda 对象本身仍在路由表，等待下一个请求。
        // ══════════════════════════════════════════════
    }, {Post});
```

**两层 Lambda 生命周期对比**：

| | 外层路由 Lambda | 内层流式 Lambda |
|---|---|---|
| **进场** | 服务启动注册一次，存入路由表 | 每个请求的 `newStreamResponse()` 时创建 |
| **退场** | 随服务存活，永不销毁 | Drogon 收到 `return 0` 后销毁 |
| **调用次数** | 每个请求调用一次 | 每次网络可写触发一次，直到写完 |

4. 面试官进阶：大模型切换（如千问换 GPT）时，代码要重写吗？Mock 测试是怎么做的？
答案：
- **协议归一化（换芯不换壳）**：不需要改动 `StreamResult` 的结构。我们已经在 `LlmClient` 内部预置了针对不同模型（如 Qwen 或 GPT）的**“解析逻辑开关”**。当配置文件切换模型时，代码会自动选择匹配的 Lambda 逻辑来读取对应的返回结构（例如从阿里的 `output` 字段或 OpenAI 的 `choices` 字段提取内容），并统一映射到 `StreamResult.full_text`。这种“协议驱动”的设计确保了上层业务（SSE、指标记录）逻辑一行不改。
- **Mock 实战（喂罐头数据）**：正像你理解的，我们不发真实请求，而是手工构造一个带假数据（延迟、报错、具体文本）的 `StreamResult` 对象直接喂给后续回调。这就像是在实验室里模拟各种极端天气，既快又准地验证后端在各种延迟或异常下的健壮性（如模拟 5.5 秒断网），而不需要真花钱去调 API。

### 4. 封装 (Encapsulation)

#### 详细讲解（小点拆解）
- 封装：把“数据 + 操作”放在同一个类里。
- 白话：像电饭煲，对外只有按钮，对内才是温控细节。
- 目标：减少调用方知道的细节，降低改动影响面。

#### 在 finguard 项目中的作用

**最典型案例：`TokenBucket` 令牌桶限流**

**第一层：头文件 = 对外接口（调用方只能看到这里）**

```cpp
// finguard/src/util/token_bucket.h
class TokenBucket {
public:
    // ← 对外只暴露一个方法："能不能让这个 key 通过？"
    bool allow(const std::string &key, double rate_rps, int capacity);

private:
    // ↓ 所有内部状态全部 private，调用方看不到、碰不到
    struct BucketState {
        double tokens = 0.0;                         // 当前令牌数
        std::chrono::steady_clock::time_point last;  // 上次充能时间戳
    };

    std::mutex mutex_;                                       // 并发保护锁
    std::unordered_map<std::string, BucketState> buckets_;  // 每个 key 的桶状态
};
```

**第二层：cpp 实现 = 内部复杂逻辑（调用方完全不需要知道）**

```cpp
// finguard/src/util/token_bucket.cpp
bool TokenBucket::allow(const std::string &key, double rate_rps, int capacity) {
    const auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(mutex_);  // ← 加锁，调用方不用关心线程安全

    // 懒初始化：第一次见到这个 key，自动创建满桶状态
    auto [it, inserted] = buckets_.try_emplace(key);
    auto &state = it->second;
    if (inserted) {
        state.tokens = static_cast<double>(capacity);
        state.last   = now;
    }

    // 按时间流逝补充令牌（令牌桶核心公式）
    const double elapsed = std::chrono::duration<double>(now - state.last).count();
    state.tokens = std::min<double>(capacity, state.tokens + elapsed * rate_rps);
    state.last   = now;

    // 消耗一个令牌，返回是否放行
    if (state.tokens >= 1.0) {
        state.tokens -= 1.0;
        return true;   // 放行
    }
    return false;      // 拒绝
}
```

**第三层：调用方 = 完全不知道内部有锁、有桶、有时间戳**

```cpp
// finguard/src/server/routes.cpp  L143 / L423
// 调用方只需一行，无需了解任何内部细节
if (!g_entry_bucket.allow(make_entry_key(req),
                           rate_cfg.entry.rate_rps,
                           rate_cfg.entry.capacity)) {
    // 超限 → 直接 429，不进入 LLM 环节
    cb(make_429_response(trace_id));
    return;
}
```

**封装的工程价值（直接从代码里读出来）**：

| 关注点 | 调用方（routes.cpp）|  实现方（token_bucket.cpp）|
|---|---|---|
| 需要知道 `mutex_` 吗？ | ❌ 完全不知道 | ✅ 唯一加锁的地方 |
| 需要知道令牌补充算法吗？ | ❌ 完全不知道 | ✅ `tokens + elapsed * rate_rps` |
| 需要知道 `BucketState` 结构吗？ | ❌ 完全不知道 | ✅ 唯一操作 `tokens` / `last` |
| 调用方接触面 | 一个方法 `allow()` | 整个实现 |

> **结论**：未来如果要把令牌桶从"进程内"改成"Redis 分布式版本"，只需改 `.cpp` 里的实现，`routes.cpp` 一行代码不用动——这就是封装最直接的工程价值。

#### 总结
- 封装好，调用方就简单，维护成本就低。
- 判断封装是否到位：**改内部实现，外部调用方要不要跟着改？** 不用改 = 封装合格。

#### 模拟面试自测
1. 面试官问：封装最直接的工程收益是什么？
答案：边界清晰，内部重构不影响外部调用。以 finguard 的 `TokenBucket` 为例：调用方只调 `allow(key, rate, cap)`，内部改成 Redis 分布式实现，调用方零修改。

### 4. 继承

#### 详细讲解（小点拆解）
- 继承：子类复用父类能力，是“is-a”关系。
- 适用：抽象稳定时。
- 风险：继承层级深了会难改，牵一发而动全身。

#### 为什么继承演进不灵活——游戏角色案例

**第一阶段：继承看起来没问题**

Hero 和 Monster 都是 Character，都有 hp 和 mp，放基类完全合理：

```cpp
class Character {
public:
    int hp;
    int mp;
    virtual void attack() = 0;
};

class Hero    : public Character { void attack() override { /* 剑击 */ } };
class Monster : public Character { void attack() override { /* 爪击 */ } };
```
**第二阶段：新需求来了，麻烦开始**

> 需求：英雄有三种攻击方式——近战、远程、法术。某些英雄只会一种，某些会两种，某些三种都会。

**继承的第一反应**：为每种组合派生子类：

\Character
└── Hero
    ├── MeleeHero         （只会近战）
    ├── RangedHero        （只会远程）
    ├── MageHero          （只会法术）
    ├── MeleeRangedHero   （近战 + 远程）
    ├── MeleeMageHero     （近战 + 法术）
    ├── RangedMageHero    （远程 + 法术）
    └── AllRoundHero      （三种都会）
\
3 种攻击方式 → **7 个子类**。再加一种“投毒”攻击就变成 15 个。  
这就是**类爆炸（Class Explosion）**——继承演进最典型的弊端。

另外，如果英雄**升级时解锁新技能**，继承也无法应对：类型在编译期就固定了，运行时没法从 MeleeHero 变成 MeleeRangedHero。

**第三阶段：组合如何解决**

把“攻击方式”从 Hero 身上**拆出来，变成独立对象**：

```cpp
// 攻击行为：独立的小组件（一个接口，多个实现）
struct AttackStrategy {
    virtual void execute(int target_hp) = 0;
    virtual ~AttackStrategy() = default;
};

struct MeleeAttack  : AttackStrategy { void execute(int) override { /* 剑击 */ } };
struct RangedAttack : AttackStrategy { void execute(int) override { /* 箭击 */ } };
struct MagicAttack  : AttackStrategy { void execute(int) override { /* 法术 */ } };

// Hero 本身不变，只需持有策略列表
class Hero : public Character {
    std::vector<std::shared_ptr<AttackStrategy>> attacks_;
public:
    void addAttack(std::shared_ptr<AttackStrategy> a) { attacks_.push_back(a); }
    void attack(int target_hp) {
        for (auto &a : attacks_) a->execute(target_hp);
    }
};
// 使用时按需组合，运行时还能动态扩展
Hero warrior;
warrior.addAttack(std::make_shared<MeleeAttack>());   // 只会近战

Hero paladin;
paladin.addAttack(std::make_shared<MeleeAttack>());   // 近战
paladin.addAttack(std::make_shared<MagicAttack>());   // + 法术

// 升级时解锁远程攻击——继承做不到，组合一行搞定
paladin.addAttack(std::make_shared<RangedAttack>());
```
类的总数：**Hero 1个 + 3 种 Attack 实现 = 4 个**，无论加多少种攻击方式，Hero 本身一行代码不用动。

**直接对比**：

| 场景 | 继承 | 组合 |
|---|---|---|
| 新增一种攻击方式 | 要修改/新增大量子类 | 只加一个新的 Strategy 类 |
| 英雄运行时学技能 | ❌ 类型编译期固定，无法切换 | ✅ ddAttack() 随时插入 |
| 修改基类 ttack() | 所有子类都受影响（脆弱基类问题） | 改某个 Strategy 只影响用它的对象 |
| 7 种攻击组合 | 需要 127 个子类 | 始终只需 7+1 个类 |

> **经验法则：“是什么”用继承，“能做什么”用组合。**
> - Hero **是一个** Character → 继承 hp/mp ✅
> - Hero **能用** 近战/法术 → 组合 AttackStrategy ✅

#### 在 finguard 项目中的作用
- 项目更偏组合而非复杂继承。LlmClient 不派生出 QwenClient/GptClient 子类，而是在内部**组合**不同的解析 Lambda——切换模型只换 Lambda，不换类，正是同一道理。

#### 总结
- 继承可用，但别滥用。
- **稳定的“是什么”（属性）**放基类继承；**可变的“能做什么”（行为）**拆成独立组件组合。

#### 模拟面试自测
1. 面试官问：为什么常说组合优于继承？
答案：继承在行为需要扩展时会产生**类爆炸**，且类型在编译期固定无法运行时替换。以游戏英雄攻击方式为例：3 种攻击的组合用继承需要 7 个子类，用组合只需 4 个类，且英雄升级解锁新技能只需 ddAttack() 一行——组合在**演进成本**和**运行时灵活性**上都胜出。

### 5. 多态

#### 详细讲解（小点拆解）
- 多态：同一接口，不同实现。
- 运行时多态：虚函数，灵活。
- 编译期多态：模板，性能好。

#### 多态与组合的关系——先弄清楚再看代码

很多人把多态和组合混淆，核心区别在于它们解决的是不同层面的问题：

| 概念 | 解决什么 | 关键词 |
|---|---|---|
| **多态** | 同一插槽可以放不同实现（可替换性） | "是什么都行，只要行为一致" |
| **组合** | 对象持有其它对象（结构装配） | "我有 A、有 B、有 C" |

**它们需要配合才有威力**：组合提供"有几个槽"，多态提供"每个槽能换什么"。
- 只有组合没有多态 → 每个槽只能放一种固定实现，扩展还是要动代码。
- 只有多态没有组合 → 每个类一对一绑定实现，子类爆炸问题没解决。

#### 在 finguard 项目中的作用

**① 非侵入式多态（`std::function`）**

传统多态是**侵入式**的——想参与多态，必须：继承基类 + 声明 `virtual` + 写 `override`：

```cpp
// ❌ 侵入式：必须改 QwenParser 的类定义才能参与接口体系
struct ILlmParser {
    virtual std::string parse(const std::string& raw) = 0;
    virtual ~ILlmParser() = default;
};
struct QwenParser : ILlmParser {               // 必须继承
    std::string parse(const std::string& raw) override { ... }
};
// 第三方库的类？改不了源码 → 无法加入继承体系
```

finguard 用的是**非侵入式多态**——用 `std::function` 作为"接口"，签名匹配即可，不需要动任何类定义：

```cpp
// ✅ 非侵入式：签名即契约，Lambda/函数/仿函数通通可行
using LlmParser = std::function<std::string(const std::string&)>;

// —— 千问解析（连声明类都省了）
LlmParser qwen_parser = [](const std::string& raw) {
    return extract_qwen_content(raw);   // 千问特有的 JSON 格式
};

// —— 换成 GPT：不需要定义新类，不需要继承，直接换 Lambda
LlmParser gpt_parser = [](const std::string& raw) {
    return extract_gpt_content(raw);    // GPT 特有的 JSON 格式
};

// —— 多态调用：同一行代码，运行时行为不同
void process(const std::string& chunk, LlmParser parser) {
    std::cout << parser(chunk);         // ← 这里就是多态！
}
process(chunk, qwen_parser);   // 千问行为
process(chunk, gpt_parser);    // GPT 行为，process() 本身一行不动
```

**② 组合 × 多态 = 最大灵活性**

`routes.cpp` **组合**了 `TokenBucket`（持有它）；`TokenBucket` 的实现可以是内存版或 Redis 版（**多态**保证外部接口不变）。
两个工具配合：限流组件随时可换，路由逻辑零修改。

```
routes.cpp          持有（组合）→    TokenBucket（接口）
                                          ↓ 多态
                               InMemoryBucket  /  RedisBucket
```

#### 总结
- **传统多态**：requires 继承 + virtual，类写完就绑定了类型体系（侵入式）。
- **非侵入式多态**：`std::function` / 模板，签名匹配即参与，没有继承负担。
- **组合 + 多态**：组合提供"几个槽"，多态决定"槽里可以换什么"，二者协同才能做到真正的低耦合可替换。

#### 模拟面试自测
1. 面试官问：`std::function` 和虚函数都能实现多态，有什么区别？
答案：
- **侵入性**：虚函数要求提前继承基类、声明 `virtual`，类定义被迫暴露给接口体系；`std::function` 只要签名匹配，任何 Lambda/函数/仿函数都能参与，无需修改类。
- **性能**：虚函数通过 vtable 间接寻址；`std::function` 有类型擦除的封装开销，但可以触发编译器内联优化。
- **使用场景**：需要继承体系的"是什么"关系用虚函数；需要轻量回调、插拔逻辑的场景用 `std::function`（如路由回调、异步完成处理器）。

2. 面试官问：为什么 finguard 里的路由和回调不写一套虚函数继承体系？
答案：
- **非侵入**：路由回调是一次性的业务逻辑，不需要定义类，Lambda 足够，`std::function` 签名即契约。
- **性能**：减少 vtable 查表开销，Lambda 在编译器已知类型时可以内联。
- **闭包**：Lambda 可捕获上下文（`trace_id`、`shared_ptr<Context>`），虚函数做不到。
---

## 二、C++11/17 与 STL

### 6. C++11 新特性

#### 详细讲解（小点拆解）
- 关键能力：`auto`、lambda、右值引用、智能指针、原子操作。
- 白话：这些特性让代码“更少错 + 更高并发 + 更容易维护”。

#### 在 finguard 项目中的作用
- **智能指针**：用于精准控制核心组件生命周期。例如在 `finguard/src/llm/llm_client.cpp` 中使用 `std::unique_ptr` 管理熔断器 `CircuitBreaker`，确保组件随客户端销毁而自动释放。
- **Atomic（原子操作）**：用于高并发下的无锁计数。例如在 `finguard/src/util/concurrency_limiter.cpp` 中使用 `std::atomic<int>` 实时追踪活跃请求数，避免在高频 QPS 下使用互斥锁导致的性能瓶颈。
- **Lambda**：作为异步回调的核心，在 `routes.cpp` 中捕获上下文并处理路由逻辑。

#### 总结
- C++11 是现代 C++ 的起点。

#### 模拟面试自测
1. 面试官问：你最常用的 C++11 特性是什么？
答案：智能指针、lambda、atomic。

### 7. 智能指针 (unique_ptr vs shared_ptr)

#### 详细讲解（小点拆解）
- **内存泄漏**：申请了内存（用 `new`）但没释放（忘写 `delete`）。后果是程序占用的内存越来越大，最后被系统强制杀掉（OOM）。
- **unique_ptr**：**“独占所有权”**。同一时间只能有一个指针指向这个对象。当 `unique_ptr` 变量离开作用域或被销毁时，它会自动调用 `delete`。
- **shared_ptr**：**“共享所有权”**。多个指针指向同一个对象，内部有个计数器。当最后一个指向它的指针销毁时，才真正释放内存。

#### 在 finguard 项目中的作用
- **unique_ptr 实践**：在 `llm_client.cpp` 中管理 `CircuitBreaker`。
- **shared_ptr 异步接力**：在流式输出（SSE）中，`shared_ptr<StreamContext>` 指向包含了 `trace_id`、当前已拼装文本、以及底层连接句柄的结构体。
- **多 Lambda 共享**：大模型每蹦出一个词（Chunk），Drogon 框架就会触发一次回调 Lambda。因为主流程早已结束，这些 Lambda 通过 `shared_ptr` 共同“续命”这个上下文对象，确保打字机效果不中断，直到最后一个 `[DONE]` 标志到达，引用计数回零，资源才正式释放。

#### 总结
- **裸指针（Raw Pointer）**：只有地址，没有大脑（析构逻辑），必须手动 `delete`。
- **STL/智能指针**：本质是**“带大脑的指针”**，利用 C++ 类对象的析构函数实现自动内存管理。

#### 模拟面试自测
1. 面试官问：为什么普通指针不具备 RAII 特性？
答案：因为普通指针是内置类型，没有析构函数。而像 `vector` 或 `std::unique_ptr` 是类对象，当它们离开作用域时，编译器会自动调用它们的析构函数来执行释放逻辑。

2. 面试官问：`concurrency_limiter` 在哪里接入？
答案：它被接入在 `routes.cpp` 的 **Router 层入口**。请求逻辑开始前，先调用 `limiter.is_allowed()`。如果不允许，直接下发 429 报错，阻止无效请求进入后续内存占用大的 LLM 环节。

3. 面试官问：详细描述 `shared_ptr` 在大模型流式返回中的“接力”过程。
答案：
- **起点**：发起异步请求，创建一个 `shared_ptr<Context>`。
- **接力 A**：模型返回第一个词，框架唤醒第一个回调 Lambda，它通过 `shared_ptr` 拿到上下文，把词发给前端。
- **接力 B**：模型返回第 N 个词，对应的第 N 个 Lambda 再次通过 `shared_ptr` 确保上下文仍然有效。
- **终点**：模型发送结束符，最后一个 Lambda 执行完并销毁。此时 `shared_ptr` 计数变 0，内存释放。这样即使主流程早结束了，数据也能安全存活到最后。

2. 面试官问：`unique_ptr` 为什么能表达所有权？
答案：因为它不支持拷贝（Copy），只能移动（Move）。它确立了资源有且只有一个明确的“负责人”。这在算法开发中非常有用，能防止写出忘了 `delete` 的普通指针导致内泄。

3. 面试官问：`concurrency_limiter` 只是记录数据吗？如果过载了它会怎么做？
答案：它不仅是记录，更是**准入控制**。它利用 `atomic` 快速计数，一旦发现活跃请求数超过设定的“水位线”，就会让后续请求直接在入口处报错（丢弃），从而防止服务器因 AI 任务堆积而 OOM。

### 7.1 Atomic 与并发计数 (concurrency_limiter)

#### 详细讲解（小点拆解）
- **atomic**：硬件级保障，确保 `a++` 是一条指令完成，中间没有缝隙让别的线程插嘴。
- **concurrency_limiter.cpp**：这是项目的**“整体水位计”**。它不关心具体哪个用户，只关心当前服务器一共在处理多少个 LLM 请求。

#### 在 finguard 项目中的作用

**层一：数据结构 — 两个 atomic 守住两条边界**

`concurrency_limiter.h` 的完整声明：

```cpp
// finguard/src/util/concurrency_limiter.h
class ConcurrencyLimiter {
public:
    explicit ConcurrencyLimiter(int max_inflight)
        : max_inflight_(max_inflight) {}

    bool try_acquire();   // 请求进来时调用：还有余量就占一个坑
    void release();       // 请求结束时调用：释放坑位
    void set_max_inflight(int max_inflight);  // 热更上限（来自配置文件）

private:
    std::atomic<int> max_inflight_{1};  // ← 上限，可热更
    std::atomic<int> inflight_{0};      // ← 当前正在处理的请求数
};
```

两个 `atomic<int>` 分工明确：`inflight_` 实时计数，`max_inflight_` 随时可从配置热更，均为高频读写——用 `atomic` 避免加锁。

---

**层二：核心实现 — CAS 无锁"占坑"**

```cpp
// finguard/src/util/concurrency_limiter.cpp
bool ConcurrencyLimiter::try_acquire() {
    int current = inflight_.load();           // 1. 先读当前值
    while (current < max_inflight_) {         // 2. 还有余量才尝试
        if (inflight_.compare_exchange_weak(  // 3. CAS：如果此刻还是 current，就原子地写成 current+1
                current, current + 1)) {
            return true;                      // 4. 成功，占坑
        }
        // 走到这里说明有其他线程抢先改了 inflight_
        // compare_exchange_weak 会自动把 current 刷新为最新值，循环重试
    }
    return false;  // 已满载，拒绝
}

void ConcurrencyLimiter::release() {
    inflight_.fetch_sub(1);  // 原子减一，请求结束归还坑位
}
```

> **`compare_exchange_weak` 白话**：
> "如果 `inflight_` 的值还是我刚才读到的 `current`，就把它改成 `current+1`，返回 true；
> 如果已经被别的线程改过了，把 `current` 刷新成新值，返回 false，我重试。"
>
> 这是**乐观锁**思路：不加锁，先做，失败了再循环重试，比 `mutex` 的"先等锁再做"更轻量。
> 直接用 `fetch_add` 不行，因为那是无条件加一——会超发。CAS 能在加一之前确认"还没超上限"且"没被并发修改"。

---

**层三：调用方 — routes.cpp 的"门卫"模式**

```cpp
// finguard/src/server/routes.cpp
util::ConcurrencyLimiter g_concurrency_limiter(4);  // 全局：最多 4 个并发 LLM 请求

// 每次 POST /api/v1/plan 进来：
g_concurrency_limiter.set_max_inflight(conc_cfg.max_inflight);  // 先同步最新配置

if (!g_concurrency_limiter.try_acquire()) {
    // 已有 4 个请求在处理 LLM，第 5 个直接 429 打回
    auto resp = HttpResponse::newHttpJsonResponse(rate_limit_error_body());
    resp->setStatusCode(k429TooManyRequests);
    cb(resp);
    return;  // ← 早退，不进入 LLM 调用，防止任务堆积 OOM
}

// ── 进入 LLM 调用，无论成功/超时/报错都必须归还坑位 ──
g_concurrency_limiter.release();
```

整个流程是"进门占坑（try_acquire）— 做 LLM 请求 — 出门归还（release）"。
如果某条路径忘了 `release()`，`inflight_` 永远不减，后续所有请求都会 429——等效于死锁。

---

**`atomic` vs `mutex`：从这个场景出发理解**

| 场景 | 用什么 | 原因 |
|---|---|---|
| `inflight_` 单变量计数 | `atomic` | 一次 `fetch_sub`/CAS 就完事，硬件原子指令 |
| `max_inflight_` 热更上限 | `atomic` | 读写独立，单次操作 |
| `TokenBucket`（令牌数 + 时间戳联动） | `mutex` | "判断余量 → 更新时间 → 扣令牌"三步是事务，中间不能断 |

#### 总结
- 单变量计数选 `atomic`（`fetch_add`/`fetch_sub`/`compare_exchange_weak`）。
- 多变量联动逻辑选 `mutex`（保证整块逻辑原子性）。

#### 模拟面试自测
1. 面试官问：为什么结构体不建议用 `atomic`？
答案：`atomic` 通常只支持 1/2/4/8 字节的操作。结构体太大无法通过硬件指令直接原子操作，且多个字段间有逻辑关联（比如时间没更新令牌就不能扣），只能靠 `mutex` 保证事务性。

2. 面试官问：`compare_exchange_weak` 和直接 `fetch_add` 有什么区别？
答案：`fetch_add` 是无条件加一，适合"只管计数"场景；`compare_exchange_weak` 是先读再条件写（CAS），适合"超过上限就放弃"的场景。`ConcurrencyLimiter::try_acquire()` 必须用 CAS：直接 `fetch_add` 会超发——10 个线程同时冲过来，全部加成功了才发现超限，要再减回去，逻辑复杂且有窗口期；CAS 在加一之前就能确认"没超限且没被并发修改"，一步到位。


### 8. STL

#### 详细讲解（小点拆解）

**核心设计：三件套各司其职，互相解耦**

| 模块 | 职责 | 白话 |
|------|------|------|
| 容器（Container） | 存数据，决定内存结构 | 仓库，决定货怎么摆 |
| 算法（Algorithm） | 处理数据，不关心容器 | 流水线，不管仓库长什么样 |
| 迭代器（Iterator） | 统一访问接口，连接容器和算法 | 叉车接口，两边通用的协议 |

**解耦的意义**：同一个 `std::sort` 可以用在 `vector`、`deque`、原生数组上，因为它只依赖迭代器接口，而不依赖具体容器。容器只需要提供迭代器，就能免费使用所有算法。

---

**一、容器分类与选型**

```cpp
// ① 序列容器 —— 按插入顺序存储
std::vector<int>  v;   // 动态数组，随机访问 O(1)，尾插均摊 O(1)，扩容迁移
std::deque<int>   d;   // 双端队列，头尾插删 O(1)，中间随机访问 O(1)
std::list<int>    l;   // 双向链表，任意插删 O(1)，不支持随机访问

// ② 有序关联容器 —— 红黑树，始终有序，O(log n)
std::map<std::string, int>  m;   // 有序键值对，key 唯一
std::set<std::string>       s;   // 有序集合，元素唯一

// ③ 无序关联容器 —— 哈希表，平均 O(1)
std::unordered_map<std::string, int>  um;
std::unordered_set<std::string>       us;

// ④ 容器适配器 —— 在其他容器上套一层限制接口
std::stack<int>           stk;  // LIFO，底层 deque
std::queue<int>            q;   // FIFO，底层 deque
std::priority_queue<int>  pq;   // 堆（默认最大堆），底层 vector
```

**选容器口诀**：
- 随机访问多 → `vector`
- 需要有序且二分查找 → `map / set`
- 只要快速查找，不在乎顺序 → `unordered_map / set`
- 头尾都要高频插删 → `deque`
- 任意位置高频插删（且不需要随机访问）→ `list`

---

**二、常用算法分类**

```cpp
#include <algorithm>
#include <numeric>

std::vector<int> v = {5, 3, 1, 4, 2};

// 排序
std::sort(v.begin(), v.end());                            // 升序：[1,2,3,4,5]
std::sort(v.begin(), v.end(), std::greater<int>());       // 降序：[5,4,3,2,1]
std::stable_sort(v.begin(), v.end());                     // 稳定排序（相等保持原顺序）

// 二分查找（前提：已排序）
auto it = std::lower_bound(v.begin(), v.end(), 3);        // 第一个 >= 3 的位置
auto it2 = std::upper_bound(v.begin(), v.end(), 3);       // 第一个 >  3 的位置
bool found = std::binary_search(v.begin(), v.end(), 3);   // 是否存在

// 线性查找
auto it3 = std::find(v.begin(), v.end(), 3);              // 找到返回迭代器，否则返回 end()
auto it4 = std::find_if(v.begin(), v.end(),
    [](int x){ return x > 3; });                          // 找第一个满足条件的

// 统计
int cnt = std::count(v.begin(), v.end(), 3);              // 统计等于 3 的个数
int cnt2 = std::count_if(v.begin(), v.end(),
    [](int x){ return x > 2; });                          // 统计 > 2 的个数

// 变换：把每个元素按规则映射到新容器
std::vector<int> out(v.size());
std::transform(v.begin(), v.end(), out.begin(),
    [](int x){ return x * 2; });                          // out = [2,4,6,8,10]

// 聚合
int sum = std::accumulate(v.begin(), v.end(), 0);         // 求和，初始值 0
int product = std::accumulate(v.begin(), v.end(), 1,
    std::multiplies<int>());                              // 求积

// 存在性判断（短路求值，找到立刻返回）
bool any = std::any_of(v.begin(), v.end(), [](int x){ return x > 4; });   // 存在 > 4
bool all = std::all_of(v.begin(), v.end(), [](int x){ return x > 0; });   // 全部 > 0
bool none = std::none_of(v.begin(), v.end(), [](int x){ return x < 0; }); // 全不 < 0
```

---

**三、迭代器类别（从弱到强）**

```
InputIterator → ForwardIterator → BidirectionalIterator → RandomAccessIterator
  只读单次遍历       可重复前向遍历       + 支持 --（反向）         + 支持 +n/-n/[]
```

| 类别 | 典型容器 | 是否支持 `+n` 跳跃 |
|------|---------|-----------------|
| 双向迭代器 | `list`, `map`, `set` | 否，只能 `++`/`--` |
| 随机访问迭代器 | `vector`, `deque`, 数组 | 是，可以 `it + 5` |

```cpp
// 正向迭代器：begin() / end()
for (auto it = v.begin(); it != v.end(); ++it) { ... }

// 反向迭代器：rbegin() / rend()（从尾到头）
for (auto it = v.rbegin(); it != v.rend(); ++it) { ... }

// const 迭代器：cbegin() / cend()（只读，防止误改）
for (auto it = v.cbegin(); it != v.cend(); ++it) { ... }

// 现代写法（底层就是迭代器，但更简洁）
for (const auto& elem : v) { ... }
```

**迭代器失效规则（高频考点）**

| 操作 | vector | map/set | list |
|------|--------|---------|------|
| 插入（不触发扩容） | 插入点之后的迭代器全部失效 | 全部迭代器依然有效 | 全部迭代器依然有效 |
| 插入（触发扩容） | **所有迭代器全部失效** | 全部迭代器依然有效 | 全部迭代器依然有效 |
| 删除 | 删除点之后的迭代器全部失效 | 只有被删节点的迭代器失效 | 只有被删节点的迭代器失效 |

结论：**vector 最危险**，插入可能触发扩容导致全失效；**map/set 最安全**，插入从不影响其他节点。

#### 在 finguard 项目中的作用

```cpp
// ① sort + 下标：计算 P99 延迟（metrics_registry.cpp）
std::vector<double> latencies = registry_.get_samples();
std::sort(latencies.begin(), latencies.end());
double p99 = latencies[static_cast<size_t>(latencies.size() * 0.99)];

// ② accumulate：计算平均延迟
double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
double avg = latencies.empty() ? 0.0 : sum / latencies.size();

// ③ transform：把延迟样本统一换算为毫秒（scale = 1e-6，纳秒 → 毫秒）
std::vector<double> ms_latencies(latencies.size());
std::transform(latencies.begin(), latencies.end(), ms_latencies.begin(),
    [](double ns) { return ns * 1e-6; });

// ④ unordered_map：令牌桶按 user_id 隔离（token_bucket.h）
std::unordered_map<std::string, TokenBucket> buckets_;
auto it = buckets_.find(user_id);
if (it == buckets_.end()) {
    // 插入后重新 find，因为 emplace 可能让之前的 it 失效（哈希表扩容）
    buckets_.emplace(user_id, TokenBucket{rate_, capacity_});
    it = buckets_.find(user_id);
}
return it->second.try_acquire();

// ⑤ set + count：判断是否为标准 ETF（routes.cpp）
static const std::set<std::string> kStandardEtfs{"510300", "159915", "510050"};
bool is_standard = kStandardEtfs.count(fund_code) > 0;  // O(log n)

// ⑥ any_of：请求字段安全检查，有敏感字段就拒绝
bool has_sensitive = std::any_of(fields.begin(), fields.end(),
    [](const std::string& f) { return f.find("token") != std::string::npos; });
```

#### 总结
- **选容器**：先问访问模式（随机/顺序/键查找），再问是否有序。
- **用算法**：优先用 `<algorithm>` 而不是手写循环——更安全、更可读、有编译优化。
- **迭代器失效**：vector 插入最危险（扩容全失效），map/set 插入最安全（无失效）。
- **面试考点**：光会调用 API 不够，要能说出复杂度、失效规则、以及为什么这样设计。

#### 模拟面试自测
1. 面试官问：`std::sort` 能用在 `std::list` 上吗？
答案：不能。`std::sort` 要求随机访问迭代器（`RandomAccessIterator`），而 `list` 只提供双向迭代器（`BidirectionalIterator`），不支持 `it + n` 跳跃。`list` 必须用自带的 `list::sort()` 成员函数，其基于归并排序，不依赖随机访问。

2. 面试官问：`push_back` 之后，之前保存的迭代器还能用吗？
答案：不一定。若 `size == capacity` 则触发扩容，会申请新内存并迁移所有元素，旧迭代器、指针、引用**全部失效**。若容量足够则没有问题。实践上可在批量插入前先调用 `reserve(n)` 预留空间，避免反复扩容。

3. 面试官问：`unordered_map` 和 `map` 什么时候各选哪个？
答案：只要不需要按 key 有序遍历，优先选 `unordered_map`（平均 O(1) 查找）。若需要范围遍历（`lower_bound`）、按字典序输出，或 key 是自定义类型且写哈希函数麻烦，则用 `map`（O(log n) 但稳定可靠）。

### 9. 常用容器：vector

#### 详细讲解（小点拆解）
- 底层连续内存，随机访问快。
- 尾插均摊 O(1)，中间插删慢。
- 扩容会搬迁元素，可能导致迭代器失效。

#### 在 finguard 项目中的作用
- `MetricsRegistry` 用 `vector<double>` 存延迟样本：`finguard/src/util/metrics_registry.cpp`。

#### 总结
- 读多写尾多，优先 `vector`。

#### 模拟面试自测
1. 面试官问：`reserve` 的作用是什么？
答案：减少扩容次数，降低拷贝/移动成本。

### 10. 常用容器：map / unordered_map / set

#### 详细讲解（小点拆解）
- `map`：有序，红黑树，O(log n)。
- `unordered_map`：无序，哈希表，平均 O(1)。
- `set`：唯一键集合，适合去重和存在性判断。

#### 在 finguard 项目中的作用
- `unordered_map` 用在令牌桶和熔断状态：`finguard/src/util/token_bucket.h`。
- `set` 用于标准 ETF 判断：`finguard/src/server/routes.cpp`。

#### 总结
- 需要顺序用 `map`，追求查询速度用 `unordered_map`。

#### 模拟面试自测
1. 面试官问：`unordered_map` 最坏为什么会变慢？
答案：哈希冲突严重会退化到接近 O(n)。

---

## 三、数据结构与算法

### 11. 链表

#### 详细讲解（小点拆解）
- 节点不连续，靠指针串起来。
- 插删快，随机访问慢。
- 适合频繁插删，不适合频繁按下标访问。

#### 在 finguard 项目中的作用
- 业务中主要用 STL 容器，但链表思想常用于 LRU 设计题。

#### 总结
- 链表优势是改链接，弱点是查找。

#### 模拟面试自测
1. 面试官问：链表为什么缓存命中率低？
答案：节点分散，CPU 预取效果差。

### 12. 栈

#### 详细讲解（小点拆解）
- 后进先出（LIFO）。
- 常见在函数调用、括号匹配、表达式计算。

#### 在 finguard 项目中的作用
- 虽未显式用 `stack`，但很多递归和回退思想本质是栈。

#### 总结
- 栈适合“最近操作先处理”。

#### 模拟面试自测
1. 面试官问：调用栈是什么结构？
答案：栈。

### 13. 队列

#### 详细讲解（小点拆解）
- 先进先出（FIFO）。
- 常见在任务排队、BFS、消息消费。

#### 在 finguard 项目中的作用
- 请求处理和工作线程分发思想可抽象成队列模型。

#### 总结
- 队列适合“先来先服务”。

#### 模拟面试自测
1. 面试官问：BFS 为什么用队列？
答案：按层推进，先入先出最自然。

### 14. 哈希表

#### 详细讲解（小点拆解）
- 核心：键经过哈希函数映射到桶。
- 冲突不可避免，靠拉链或开放寻址处理。
- 关键指标：装载因子、冲突率。

#### 在 finguard 项目中的作用
- `unordered_map` 广泛用于限流、熔断、配置映射。

#### 总结
- 哈希表快在平均，不保证最坏。

#### 模拟面试自测
1. 面试官问：如何降低冲突？
答案：更好的哈希函数 + 合理 rehash。

### 15. 二叉树

#### 详细讲解（小点拆解）
- 每个节点最多两个子节点。
- BST 可加速查找，平衡后可稳定 O(log n)。
- 红黑树是工程上常见平衡树。

#### 在 finguard 项目中的作用
- `map/set` 底层就是平衡树思想。

#### 总结
- 树结构适合有序组织与范围查询。

#### 模拟面试自测
1. 面试官问：为什么 `map` 适合范围遍历？
答案：底层有序结构，天然按序访问。

### 16. 快速排序

#### 详细讲解（小点拆解）
- 分治：选主元，把小的放左边，大的放右边。
- 平均 O(n log n)，最坏 O(n^2)。
- 原地排序，通常不稳定。

#### 在 finguard 项目中的作用
- 项目用 `std::sort`，背后可能是快排混合策略。

#### 总结
- 快排常用，但要防最坏情况。

#### 模拟面试自测
1. 面试官问：如何降低快排最坏概率？
答案：随机主元或三数取中。

### 17. 归并排序

#### 详细讲解（小点拆解）
- 分成两半分别排好，再合并。
- 稳定 O(n log n)，但需要 O(n) 额外空间。

#### 在 finguard 项目中的作用
- 需要稳定排序时，这是常见思路。

#### 总结
- 归并强在稳定和复杂度稳定。

#### 模拟面试自测
1. 面试官问：归并为什么适合链表？
答案：链表合并可通过改指针完成，代价低。

### 18. 排序（总括）

#### 详细讲解（小点拆解）
- 题目说“排序”，你要先问：是否要稳定、数据规模多大、是否内存受限。
- 工程里常是混合排序，不只单一算法。

#### 在 finguard 项目中的作用
- `MetricsRegistry` 计算分位数前先 `sort`。

#### 总结
- 算法选型先看约束。

#### 模拟面试自测
1. 面试官问：为什么线上代码很少手写排序？
答案：标准库更稳定且优化成熟。

### 19. 二分查找

#### 详细讲解（小点拆解）
- 前提：数据有序。
- 本质：每次砍掉一半搜索区间。
- 难点：边界写法和终止条件。

#### 在 finguard 项目中的作用
- 未直接手写，但常用于阈值区间定位。

#### 总结
- 二分的坑主要在边界，不在思想。

#### 模拟面试自测
1. 面试官问：`mid` 为什么写成 `l + (r-l)/2`？
答案：避免溢出。

### 20. 递归

#### 详细讲解（小点拆解）
- 三要素：终止条件、当前层逻辑、子问题。
- 像“俄罗斯套娃”，不断拆小直到最小单位。

#### 在 finguard 项目中的作用
- 复杂配置解析和规则拆分思维常可递归建模。

#### 总结
- 递归代码短，但要关注栈深。

#### 模拟面试自测
1. 面试官问：递归和循环怎么选？
答案：优先可读性，性能敏感再改迭代。

### 21. 回溯

#### 详细讲解（小点拆解）
- 回溯 = 试一个选择，不行就撤销再试下一个。
- 常用于排列组合、子集、路径问题。

#### 在 finguard 项目中的作用
- 项目业务未直接用，但面试高频。

#### 总结
- 回溯关键是剪枝，不是硬搜到底。

#### 模拟面试自测
1. 面试官问：回溯和 DFS 关系？
答案：回溯通常用 DFS 实现，并强调撤销选择。

### 22. 动态规划

#### 详细讲解（小点拆解）
- 把重复子问题结果存起来，避免重复算。
- 步骤：定义状态、写转移、设初值、定遍历顺序。

#### 在 finguard 项目中的作用
- 虽非主算法，但配置缓存思路体现“避免重复计算”。

#### 总结
- 见到“最优子结构 + 重叠子问题”就想 DP。

#### 模拟面试自测
1. 面试官问：DP 和贪心最大区别？
答案：DP 通过状态转移保证全局最优，贪心不一定。

---

## 四、计算机网络

### 23. 计算机网络模型（OSI 七层）

#### 详细讲解（小点拆解）
- 七层是教学模型，用来分清职责边界，不是真实实现（真实用的是 TCP/IP 四层）。
- 核心三层：应用层（业务协议）、传输层（端到端可靠传输）、网络层（IP 寻址路由）。
- 价值在于排障：出问题时快速判断"是应用层逻辑错误，还是网络层不通"。

#### 在 finguard 项目中的作用

**各层与项目组件的对应关系：**

| 层 | 职责 | finguard 对应组件 | 谁负责 |
|----|------|-----------------|--------|
| 1 物理层 | 网线、电信号 | 服务器硬件 | 完全透明，项目看不到 |
| 2 数据链路层 | MAC 地址、以太网帧 | 操作系统网卡驱动 | 完全透明，项目看不到 |
| 3 网络层 | IP 路由、寻址 | OS 内核；`proxy_config.cpp` 配置出口代理，间接影响此层 | OS + 运维 |
| 4 传输层 | TCP 连接、端口、流控 | Drogon 底层 TCP socket；`token_bucket.cpp` `concurrency_limiter.cpp` 控制入口并发，守住这层入口 | Drogon 框架 + 你写的限流 |
| 5 会话层 | 连接建立/维持/关闭 | Drogon 管理 HTTP Keep-Alive；`llm_client.cpp` 向 LLM 发起的每次 HTTP 会话 | Drogon 框架 |
| 6 表示层 | 数据格式、加密、压缩 | Drogon 内置 JSON 序列化；`simple_yaml.cpp` 读配置；TLS 由 OpenSSL 处理（如开 HTTPS） | Drogon + OpenSSL |
| 7 应用层 | 业务协议、API 语义 | **你写的全部业务代码** | 你 |

**第 7 层内部还有自己的分层（你项目的架构）：**

```
routes.cpp                ← 路由分发：/api/v1/chat → 交给哪个 handler
       ↓
valuation_handler.cpp     ← 业务逻辑：估值计算、调度 LLM
rule_engine.cpp           ← 风险规则：仓位/集中度校验
       ↓
llm_client.cpp            ← 出站 HTTP：向 Qwen/GPT API 发请求（又是一个新的七层起点）
yahoo_finance_client.cpp  ← 出站 HTTP：拉取股价数据
       ↓
profile_store.cpp         ← 数据层：用户风险画像读写
```

**一个请求的完整旅程：**

```
用户 POST /api/v1/chat
  → 物理/IP/TCP 层（OS 内核，透明）
  → Drogon 拼装 HTTP 报文（会话层）
  → Drogon 解析 JSON body（表示层）
  → routes.cpp 路由匹配（应用层入口）
  → rule_engine 检查风险，token_bucket 限流（应用层业务）
  → llm_client 向 Qwen 发新 HTTP 请求（出站，再走一遍七层）
  → 收到 LLM 响应，JSON 序列化，TCP 回包
  → 用户浏览器收到响应
```

#### 总结
- 你写的代码 99% 在第 7 层，1-3 层是内核/硬件，4-6 层是 Drogon 框架托管。
- 七层的意义：出 bug 时分层排查——先 `ping` 确认网络层通，再 `curl` 确认应用层响应，快速定位问题在哪层。

#### 模拟面试自测
1. 面试官问：HTTP 属于哪层？
答案：应用层（第 7 层）。HTTP 报文里的 JSON 数据格式化属于表示层（第 6 层），底层 TCP 连接属于传输层（第 4 层）。但在实际 TCP/IP 四层模型里，HTTP/JSON/TLS 都归在"应用层"。

2. 面试官问：你的项目里，限流组件在七层模型中属于哪层？
答案：`token_bucket` 和 `concurrency_limiter` 在代码上属于第 7 层（应用层逻辑），但它们保护的是第 4 层（传输层）的 TCP 连接资源，防止并发 TCP 请求压垮服务。概念上是应用层做传输层防护。

3. 面试官问：你的项目里有哪些可靠性组件，它们分别解决什么问题，和框架如何分工？
答案：项目里自己实现了四个可靠性组件，都在 `util/` 目录下：`TokenBucket`（令牌桶限流）控制每个 user_id 的请求速率，超速直接拒绝；`ConcurrencyLimiter`（并发限制器）用 `atomic` CAS 控制同时在飞的请求数量，防止线程堆积；`CircuitBreaker`（熔断器）监控下游 LLM API 的失败率，失败率超阈值就切到 Open 状态停止发请求，避免雪崩；`MetricsRegistry`（指标采集）记录每次请求延迟并计算 P95/P99，用于观测系统健康状态。Drogon 框架负责 TCP 连接管理、HTTP 报文解析和路由分发，这四个组件负责请求进入业务逻辑之前的"守门"和请求结束后的"观测"，两者在应用层内部各管一块，互不耦合。

### 24. HTTP / HTTPS

#### 详细讲解（小点拆解）

**HTTP 是什么**

HTTP（HyperText Transfer Protocol）是一个文本协议，规定了客户端和服务器之间"怎么说话"：用什么格式发请求、用什么格式回响应。本质上就是两台机器之间约定好的「对话格式」，内容是明文——任何人截到数据包都能直接看到里面写的什么。

**请求结构（你猜对了，就是 GET/POST 这类）**

```
POST /api/v1/chat HTTP/1.1          ← 请求行：方法 + 路径 + 协议版本
Host: localhost:8080                ← 请求头：元信息，告诉服务器目标、格式等
Content-Type: application/json
Authorization: Bearer xxx

{"message": "分析一下茅台的估值"}    ← 请求体：实际数据（GET 请求没有 body）
```

| 方法 | 语义 | finguard 用在哪 |
|------|------|----------------|
| GET | 读取资源，不改变服务器状态 | `/api/v1/health` 健康检查 |
| POST | 提交数据，创建或触发操作 | `/api/v1/chat` 发送对话 |
| PUT | 整体替换资源 | 更新用户风险画像 |
| DELETE | 删除资源 | 删除会话 |

**状态码（你猜对了，就是 200/404 这类）**

```
2xx  成功
  200 OK              — 正常返回
  201 Created         — 创建成功（POST 新建资源后返回）

3xx  重定向
  301 Moved Permanently — 永久跳转到新地址

4xx  客户端错误（你的请求有问题）
  400 Bad Request     — 请求格式错误，比如 JSON 解析失败
  401 Unauthorized    — 没带 token 或 token 过期
  403 Forbidden       — token 有效但没权限
  404 Not Found       — 路径不存在
  429 Too Many Requests — 触发限流（finguard token_bucket 返回这个）

5xx  服务端错误（服务器出了问题）
  500 Internal Server Error — 服务器内部崩了
  502 Bad Gateway     — 上游（LLM API）返回了异常
  503 Service Unavailable — 熔断器打开，服务暂不可用（finguard circuit_breaker 返回这个）
```

**TLS 是什么，以及 HTTPS = HTTP + TLS**

TLS（Transport Layer Security）是一个加密协议，套在 HTTP 外面，解决三个问题：

| 问题 | 没有 TLS | 有了 TLS |
|------|---------|---------|
| 机密性 | 数据明文传输，中间人可读 | 数据加密，截包看不懂 |
| 完整性 | 数据可被篡改 | 有签名，篡改立刻被发现 |
| 身份认证 | 不知道对面是不是真的服务器 | 服务器有证书，客户端可验证 |

HTTPS 就是在 HTTP 下面加了一层 TLS，你写的代码完全不需要改——HTTP 怎么写还是怎么写，TLS 在传输层透明地加解密。

**TLS 握手目的**

握手的目标就一件事：**双方在明文信道上安全地协商出一个只有彼此知道的对称密钥**，之后用这个密钥加密所有通信。

```
客户端                          服务器
  │── ClientHello（我支持哪些加密算法）→  │
  │←─ ServerHello（选这个算法）+ 证书 ──│
  │   验证证书（是真的 finguard 吗？）    │
  │── 用证书公钥加密一个随机数 ─────────→│
  │   双方各自用随机数算出相同的会话密钥   │
  │══════════ 后续全部加密通信 ══════════│
```

握手完成后，中间人即使截到所有数据包，也因为没有会话密钥而无法解密。

**加密和身份校验在 finguard 里谁来做**

取决于部署方式：
- **开发/内网**：通常直接用 HTTP（不加 TLS），Drogon 监听 8080，你的代码怎么写都能跑
- **生产环境**：一般在 Drogon 前面加一层 Nginx 或 API Gateway，由 Nginx 做 TLS 终止（接收 HTTPS，解密后以 HTTP 转发给 Drogon）——这样 Drogon 和你的业务代码完全不用关心 TLS，证书配置全在 Nginx

所以：**TLS 不是 Drogon 自己完成的，而是 Nginx 在 Drogon 前面代劳的**。Drogon 虽然支持 TLS，但工程实践里很少直接让业务框架处理证书。

#### 在 finguard 项目中的作用

```cpp
// 对内：Drogon 提供 HTTP 服务（routes.cpp）
app().addListener("0.0.0.0", 8080);  // 监听 8080，等待客户端 HTTP 请求

// 对外：llm_client 作为 HTTP 客户端调用 Qwen API
// POST https://dashscope.aliyuncs.com/api/v1/services/aigc/...
// 返回 200 → 解析 JSON；返回 429 → 触发限流重试；返回 5xx → 记录熔断失败计数

// 状态码使用示例（routes.cpp）
if (!token_bucket_.allow(user_id, rate, capacity)) {
    callback(HttpResponse::newHttpResponse(k429TooManyRequests));  // 限流
    return;
}
if (!circuit_breaker_.allow("llm")) {
    callback(HttpResponse::newHttpResponse(k503ServiceUnavailable));  // 熔断
    return;
}
```

#### 总结
- HTTP = 明文对话格式（方法 + 路径 + 头 + body）。
- HTTPS = HTTP + TLS，TLS 解决机密性、完整性、身份认证三个问题。
- 状态码：2xx 成功，4xx 客户端错（含 429 限流），5xx 服务端错（含 503 熔断）。
- finguard 里 TLS 由前置 Nginx 处理，Drogon 和业务代码只看 HTTP。

#### 模拟面试自测
1. 面试官问：HTTPS 的主要收益是什么？
答案：机密性（数据加密，截包看不懂）、完整性（签名防篡改）、身份认证（证书验证服务器真实性）。三者都由 TLS 握手阶段协商出的会话密钥来保证。

2. 面试官问：GET 和 POST 的区别？
答案：语义上 GET 是"读"，POST 是"写/触发"，GET 没有请求体，参数放在 URL 里，POST 数据放在 body 里。实际上 GET 也能带 body，POST 也能幂等，但约定俗成不这么用。缓存、日志默认会记录 GET 的 URL 参数，所以敏感数据（token、密码）必须放 POST body，不能放 GET URL。

3. 面试官问：你的项目里 429 和 503 分别什么情况返回？
答案：429 是 `token_bucket` 判断该用户请求速率超限时返回，告诉客户端"你发太快了，等一下再试"；503 是 `circuit_breaker` 处于 Open 状态时返回，告诉客户端"下游 LLM 目前不可用，稍后重试"。两个都是主动保护，防止问题扩散。

### 25. TCP/UDP

#### 详细讲解（小点拆解）
- TCP：可靠、有序、连接导向。
- UDP：无连接、低开销、低延迟。

#### 在 finguard 项目中的作用
- HTTP 建立在 TCP 上。

#### 总结
- TCP 重可靠，UDP 重实时。

#### 模拟面试自测
1. 面试官问：UDP 能做可靠传输吗？
答案：可在应用层自实现确认和重传。

### 26. TCP 三次握手

#### 详细讲解（小点拆解）
- 目的：确认双方收发能力并同步初始序号。
- 三次而非两次：避免历史报文导致误建连接。

#### 在 finguard 项目中的作用
- 你不手写握手，但所有连接都依赖此机制。

#### 总结
- 握手是可靠会话的起点。

#### 模拟面试自测
1. 面试官问：为什么不是两次握手？
答案：两次无法充分确认双方状态并规避旧报文风险。

### 27. TCP 四次挥手

#### 详细讲解（小点拆解）
- TCP 是全双工，两个方向要分别关闭。
- FIN/ACK 分开，所以通常四次。

#### 在 finguard 项目中的作用
- 连接关闭行为会影响短连接性能和 TIME_WAIT 观测。

#### 总结
- 挥手关注的是“有序释放连接状态”。

#### 模拟面试自测
1. 面试官问：TIME_WAIT 为什么存在？
答案：防旧包污染，并保证最后 ACK 可重传。

### 28. 流量控制

#### 详细讲解（小点拆解）
- 目标：别把接收方压垮。
- 手段：接收窗口告诉发送方“我还能收多少”。

#### 在 finguard 项目中的作用
- 应用层同理做限流，避免服务端过载。

#### 总结
- 流量控制是端到端节流。

#### 模拟面试自测
1. 面试官问：流量控制主要防谁崩？
答案：防接收端缓冲区被打爆。

### 29. 拥塞控制

#### 详细讲解（小点拆解）

**什么是拥塞控制？**

TCP 拥塞控制解决的核心问题：**网络链路的容量是有限的，所有发送方共享同一条路，发太快就会把路堵死**。它不是保护接收方（那是流量控制的事），而是保护整个网络。

**TCP 拥塞控制四个阶段：**

```
① 慢启动（Slow Start）
   窗口从 1 开始，每收到一个 ACK 就翻倍（1→2→4→8→16…）
   目的：探测网络容量，快速找到能用的带宽

② 拥塞避免（Congestion Avoidance）
   窗口到达阈值后，改为每 RTT 只加 1（线性增长）
   目的：接近上限时小心试探，别一下撑爆

③ 快重传（Fast Retransmit）
   连续收到 3 个重复 ACK → 不等超时，立刻重传丢失的包
   目的：比超时更快发现丢包

④ 快恢复（Fast Recovery）
   快重传后，窗口减半而不是回到 1，继续拥塞避免
   目的：一次丢包不代表网络崩了，别反应过度
```

**类比**：高速公路收费站限流——车太多就堵了，先慢慢放（慢启动），发现路还行就多放一点（拥塞避免），出了事故就减少放行量（快恢复），而不是把高速全关了。

#### 在 finguard 项目中的作用

TCP 拥塞控制发生在内核，你的代码看不见。但你在**应用层做了一模一样的事**——三个组件分别对应拥塞控制的思想：

| TCP 拥塞控制概念 | finguard 应用层对应组件 | 怎么对应的 |
|-----------------|----------------------|-----------|
| 慢启动 + 拥塞避免（控制发送速率） | `TokenBucket`（令牌桶限流） | TCP 用窗口控制发包速率；你用令牌桶控制每个用户的请求速率，桶空了就拒绝 |
| 窗口大小（同时在飞的包数量上限） | `ConcurrencyLimiter`（并发限制器） | TCP 窗口限制同时在飞的包数；你限制同时在飞的请求数（`max_inflight=4`），超了就拒绝 |
| 超时/丢包 → 降速/断开 | `CircuitBreaker`（熔断器） | TCP 检测到丢包就降速；你检测到 LLM 失败率超 50% 就熔断切断请求，防止雪崩 |

**实际代码对比：**

```cpp
// ① 令牌桶 ≈ 慢启动/拥塞避免（控制速率）
// token_bucket.cpp —— 按时间匀速补充令牌，没令牌就拒绝
double elapsed = duration<double>(now - state.last).count();
state.tokens = std::min<double>(capacity, state.tokens + elapsed * rate_rps);
if (state.tokens >= 1.0) { state.tokens -= 1.0; return true; }
return false;  // 桶空 → 429 Too Many Requests

// ② 并发限制器 ≈ TCP 窗口（限制同时在飞的数量）
// concurrency_limiter.cpp —— CAS 控制 inflight 不超过 max
int current = inflight_.load();
while (current < max_inflight_) {
    if (inflight_.compare_exchange_weak(current, current + 1))
        return true;  // 拿到一个"槽位"
}
return false;  // 在飞请求已满 → 拒绝

// ③ 熔断器 ≈ 丢包检测 + 降速（发现问题就断开）
// circuit_breaker.cpp —— 失败率超阈值 → Open → 停止发请求
if (total >= config_.min_samples && rate >= config_.error_rate_threshold) {
    data.state = State::Open;           // 熔断！停止所有请求
    data.open_until = now + seconds(window);  // 冷却一段时间
}
// 冷却后进 HalfOpen → 试探性放几个请求 → 成功则恢复（≈ 快恢复）
if (data.state == State::HalfOpen) {
    if (data.half_open_trials >= config_.half_open_max_trials) {
        data.state = State::Closed;  // 试探成功，回到正常
    }
}
```

#### 总结
- TCP 拥塞控制 = 内核自动做的"别把网络堵死"。
- finguard 在应用层复刻了同样的思想：限速（令牌桶）、限并发（ConcurrencyLimiter）、断路保护（熔断器）。
- **面试要点**：拥塞控制保护的是网络整体健康，流量控制保护的是接收端处理能力，两者目标不同但都通过控制发送速率实现。

#### 模拟面试自测
1. 面试官问：流量控制和拥塞控制的区别？
答案：流量控制是接收端通过 TCP 窗口告诉发送端"我还能吃多少"，保护的是接收端不被撑爆。拥塞控制是发送端自己根据丢包/延迟判断"网络还能承受多少"，保护的是整个链路不被堵死。一个是端到端的协商，一个是发送端的自觉。

2. 面试官问：你项目里的熔断器和 TCP 拥塞控制有什么相似性？
答案：核心思想完全一样——检测到"路不通了"就主动降速。TCP 检测丢包后窗口减半（快恢复），我的熔断器检测到 LLM 失败率超 50% 后切到 Open 状态停止发请求。冷却后 TCP 进入拥塞避免缓慢增加窗口，我的熔断器进入 HalfOpen 试探性放 2 个请求，成功才恢复到 Closed。不同的是 TCP 在传输层由内核自动执行，我的在应用层用代码显式实现。

---

## 五、Linux 与操作系统

### 30. Socket 套接字编程

#### 详细讲解（小点拆解）

**Socket 是什么？**
Socket（套接字）是应用层与传输层之间的**抽象接口（门面）**。它把复杂的 TCP/IP 协议族隐藏在后面，对程序员来说，Socket 就像是一个文件描述符（fd），你可以对他进行 `read`/`write` 操作来收发网络数据。

**TCP 编程的核心流程：**

| 服务端（被动等待） | 客户端（主动发起） | 关键动作 |
|-----------------|-----------------|---------|
| `socket()` | `socket()` | 创建一个通信端点（买手机） |
| `bind()` | - | 将 socket 绑定到特定 IP 和端口（办手机号） |
| `listen()` | - | 进入监听状态（等待来电） |
| - | `connect()` | 发起 TCP 三次握手（拨号） |
| `accept()` | - | 从全连接队列取出一个完成握手的连接（接通电话） |
| `read() / write()` | `write() / read()` | 交换数据（通话） |
| `close()` | `close()` | 释放资源（挂断） |

#### 在 finguard 项目中的作用

虽然项目使用了 **Drogon** 框架，不需要手动写 `accept` 循环，但理解底层机制对系统理解至关重要：

1. **框架托管**：Drogon 底层通过 `Trantor` 库管理着成千上万个非阻塞 Socket。
2. **连接上限**：项目中的 `ConcurrencyLimiter`（并发限制器）虽然在应用层，但本质上是为了防止过多的 Socket 连接撑爆系统的文件描述符限制（`ulimit -n`）。
3. **状态观测**：当项目出现异常时，通过 `netstat -anp | grep 8080` 查看 Socket 状态（如大量的 `TIME_WAIT` 或 `CLOSE_WAIT`），是排查框架配置问题（如连接池泄漏）的基础。

#### 总结
- **框架是武器，Socket 是内功**。懂 Socket 才能在框架报错时，通过抓包（Tcpdump）和系统工具定位是业务逻辑问题还是网络协议栈问题。

#### 模拟面试自测
1. 面试官问：Socket 了解吗？请描述一下 TCP 服务端的建立流程。
答案：Socket 是应用层访问协议栈的接口。TCP 服务端流程是：首先 `socket()` 创建套接字，然后 `bind()` 绑定端口，`listen()` 开启监听。此时内核会自动处理三次握手。当握手完成后，`accept()` 函数会从全连接队列中取出一个已经建连的 Socket 供后续通信。

2. 面试官问：`listen` 函数的第二个参数 `backlog` 是什么意思？
答案：它定义了内核中**等待 accept 的全连接队列**的最大长度。如果请求瞬间爆发，队列满了，新连接就会被拒绝。在我的 Finguard 项目中，通过 `ConcurrencyLimiter` 在应用层主动拦截，其实就是为了防止压力传导到内核的这个队列导致系统不可用。

3. 面试官问：如果服务端一直在 `read`，客户端突然断网（没发 FIN 包），会发生什么？
答案：Socket 会卡在 `read` 状态（如果是阻塞模式）。因为 TCP 是虚拟连接，没有数据往来时双方是感知不到对方失踪的。在项目中，我们必须配置 **HTTP Keep-Alive** 或 **应用层心跳**，以及设置合理的 **Timeout**（如 `llm_client` 的超时设置），否则这些"幽灵连接"会永远占用 Socket 资源。

### 31. I/O 多路复用

#### 详细讲解（小点拆解）

**1. 核心概念理解**

*   **线程 (Thread)**：是 CPU 调度的最小单位。想象成一个正在干活的"工人"。
*   **fd (File Descriptor)**：文件描述符。Linux 里面"一切皆文件"，Socket 也是文件。fd 就是一个**数字（索引）**，代表一个打开的通信通道（像插座的编号）。
*   **fd 就绪事件**：指某个 Socket 现在有动静了。比如：有数据发过来了（可读）、缓冲区空了（可写）、或者有人打进电话了（有新连接）。

**2. 什么是"多路复用"？（码头分拣类比）**

如果不复用，一个工人（线程）只能死守一个插口（fd），没数据就一直等（阻塞），这就是 **BIO（阻塞 IO）**。如果要服务 1000 个客户，就得雇 1000 个工人。

**I/O 多路复用** 就像是一个**值班室保安**（epoll），他一个人可以盯着 1000 个监控屏（fd）。只要哪个屏亮了（就绪事件），他就通知对应的工人去处理。这就是**代码逻辑上的"复用"**：一个线程处理成千上万个连接。

**3. 三代方案对比 (select / poll / epoll)**

| 方案 | 机制 | 缺点 |
|------|------|------|
| **select** | 每次都要把 1024 个 fd 全传给内核，内核轮询一遍 | 效率随 fd 增加而暴跌，且有 1024 限制 |
| **poll** | 和 select 类似，但没 1024 限制 | 依然要全量遍历，慢 |
| **epoll** | **事件驱动**。只有动了的 fd 才会主动通知你 | **极快**。不管你有 10 万还是 100 万连接，效率只跟"活跃连接"有关 |

**4. 事件驱动模型 (Event-Driven)**

这是一种**"别找我，我找你"**的模式。
*   **普通模式**：你要不停问 Socket："有数据吗？有数据吗？"（浪费 CPU）
*   **事件驱动**：你注册一个回调函数（比如：`onMessage`），然后就去干别的事。底层 `epoll` 发现有数据了，会自动跳出来执行你的 `onMessage`。

#### 在 finguard 项目中的作用

1.  **Drogon 的心脏**：Drogon 底层使用的 `Trantor` 库本质上就是一个 `epoll` 循环（EventLoop）。它在一个线程里管理着你的所有用户连接。
2.  **异步非阻塞**：当你在 `valuation_handler.cpp` 里发起对 LLM 的 HTTP 请求时，Drogon 不会卡在那等返回。它会把这个请求的 Socket 注册到 `epoll` 里，然后当前线程立刻去处理下一个用户的请求。
3.  **高并发能力**：这就是为什么你的 Finguard 配置了 `max_inflight=4` 但实际上能打开成百上千个 Socket 连接而不崩掉。

#### 总结
- **多路复用** = 一个保安（线程）看一堆电表（fd）。
- **epoll** = 只有电表跳了才会响铃的智能监控。
- **事件驱动** = 响铃了才去干活，不响铃就睡觉或干别的。

#### 模拟面试自测
1. 面试官问：epoll 为什么更适合高并发？
答案：因为 epoll 采用了**事件通知机制**。`select/poll` 每次都要遍历一遍所有的连接来检查谁有数据，效率是 O(N)；而 `epoll` 内部维护了一个就绪列表，只有真正有事件发生的连接才会通知我们，效率接近 O(1)。在成千上万个连接中只有极少数活跃时，epoll 的优势巨大。

2. 面试官问：什么是 fd（文件描述符）？
答案：在 Linux 中，一切皆文件。fd 是内核为了高效管理已打开文件而生成的非负整数索引。所有的网络 Socket、标准输入输出、磁盘文件，对程序来说都是一个 fd。我们通过这个 fd 对内核发起读写请求。

3. 面试官问：Drogon 这种框架支持高并发的原理是什么？
答案：核心是 **I/O 多路复用 + 非阻塞 IO + 事件驱动**。框架在一个 EventLoop 线程里使用 `epoll` 同时监听大量 Socket。当某个 Socket 就绪时，通过预先注册的回调函数处理业务，避免了为每个连接创建线程带来的巨大开销。

### 32. Linux 进程管理

#### 详细讲解（小点拆解）

**1. 进程 (Process) vs 线程 (Thread)**

| 维度 | 进程（工厂） | 线程（工人） |
|------|------------|------------|
| **定义** | 资源分配的最小单位 | CPU 调度的最小单位 |
| **内存** | 独立的虚拟地址空间（互不干扰） | 共享所属进程的内存空间 |
| **通信** | 麻烦，需要 IPC（管道、信号量、共享内存） | 方便，直接通过变量通信 |
| **崩溃影响** | 一个进程崩了，不影响其他进程 | 一个线程崩了（如段错误），整个进程一起崩 |
| **切换开销** | 大（涉及页表切换等） | 小（只切换寄存器状态） |

**2. 经典进程管理三部曲：fork、exec、wait**

这是 Linux 创建新任务的标准流程：
*   **`fork()` —— 复制**：创建一个和当前进程一模一样的副本（子进程）。
*   **`exec()` —— 替换**：子进程执行 `exec`，把自己的"身体"（内存空间）换成另一个程序的代码（如 `curl`）。
*   **`wait()` —— 收尸**：父进程等待子进程结束，并回收它的状态。如果父进程不管，子进程结束会变成"僵尸进程"。

#### 在 finguard 项目中的作用：降级方案 (Fallback System)

在 `llm_client.cpp` 中，我们实现了一个非常健壮的**进程级降级方案**：

1.  **正常路径 (Thread-based)**：调用 SDK 或直接库函数，在当前进程的线程里发起网络请求。
2.  **降级路径 (Process-based)**：如果底层库出现内存泄漏、死锁或由于证书环境问题导致崩溃，我们通过 `fork + exec` 调用外部的 `curl` 进程。

**如何讲解体现实力？（面试高分回答点）**

> "在 Finguard 的可靠性设计中，我引入了 **'进程隔离降级'** 的思想。当内置的网络请求库由于环境异常失效时，我会主动 fork 出一个子进程去调用系统的 `curl` 命。	
> 这样做的好处是：即便外部 `curl` 进程由于网络异常死死卡住或由于特殊原因崩溃，由于它是**独立的进程空间**，它完全不会影响到 Finguard 主进程的稳定性。我通过 `waitpid` 设置非阻塞等待或超市控制，确保了系统在极端环境下的‘**存活率**’。这种物理层面的资源隔离，比单纯在线程里加 `try-catch` 要强力得多。"

#### 总结
- **进程**是资源隔离墙，**线程**是并发加速器。
- `fork/exec/wait` 是操作系统管理这些"资源墙"的基础指令。

#### 深度解析：为什么 Finguard 的可靠性离不开进程管理？

1. **隔离墙理论**：线程共享内存，一毁俱毁。进程拥有**独立的虚拟内存空间**。调用外部 `curl` 进程时，如果 `curl` 崩溃或内存泄漏，操作系统会确保它的负面影响只停留在子进程内部。
2. **清理机制**：C++ 没有垃圾回收。但在进程级别，操作系统就是最强力的垃圾回收器。子进程退出（exit）时，它占用的所有资源（内存、fd）都会被内核强制收回，这比在代码里写 `delete` 更彻底。
3. **分工协作**：主进程（Drogon）负责在高效率的 EventLoop 中处理成千上万个轻量级连接；子进程（curl）作为**牺牲品（Disposable）**，负责去执行那些高风险、环境配置极其复杂、或容易导致内存污染的任务。

#### 模拟面试自测
1. 面试官问：什么是僵尸进程？怎么产生的？
答案：子进程结束后，父进程没有调用 `wait()` 或 `waitpid()` 去读取子进程的状态信息，子进程的进程描述符依然残留在系统里。大量僵尸进程会耗尽系统进程号资源。在我的项目里，调用 `curl` 必须确保及时回收子进程。

2. 面试官问：为什么不直接在线程里调 curl 库，非要 fork 一个进程？
答案：这就是**故障隔离**。如果某个外部 API 的 C++ 库不稳定（比如有内存泄漏），在进程内调用会慢慢耗尽内存导致主程序重启。而 fork 出一个进程去跑，进程结束内存会被内核瞬间强制回收，主程序永远是干净的。这体现了**防御式编程**的思想。

3. 面试官问：`fork` 的写时复制（COW）是什么意思？
答案：`fork` 的瞬间并没有真正的拷贝内存，父子进程共享同一份物理内存。只有当其中一个进程尝试修改内存时，内核才会进行真正的拷贝。这样大大提高了 `fork` 的效率，尤其是接下去立刻调 `exec` 的时候。

### 33. 系统调用 (System Call / syscall)

#### 详细讲解（小点拆解）

**1. 什么是系统调用 (syscall)？**

*   **形象类比（银行取钱）**：
    *   **用户态**：相当于**你在银行大厅**。你可以喝水、填单子、玩手机，但你绝不能自己走进金库拿钱。
    *   **内核态**：相当于**柜员在金库内**。柜员有权进入核心区域、点钞、操作账本。
    *   **系统调用 (syscall)**：就是那个**防弹玻璃窗口**。你把存单递进去，就是向柜员发起“请求”。
*   **什么是"系统"**：不仅是 Windows 或 Linux，更是指**掌握硬件生杀大权的代码逻辑**。
*   **为什么需要它**：为了**保护硬件**。如果每个程序（在大厅的平民）都能随便改 CPU 频率、写磁盘坏道、关网卡，系统一秒钟就崩了。所以硬件资源（网络、文件、内存）都被锁在"内核"里，你想用？必须求助于系统调用。

**2. 核心概念：用户态 vs 内核态**

*   **用户态 (User Mode)**：你写的一般代码运行的地方。就像是在大厅活动的平民，没权限直接摸硬件。
*   **内核态 (Kernel Mode)**：操作系统核心运行的地方。就像是带枪的高级柜员，权力无限，可以操作一切硬件。
*   **内核服务**：内核提供的各种功能，比如"帮我发个网络包"（帮我汇款）、"帮我读一下硬盘上的 `config.json`"（查下我的账单）。

**3. 如何切换？(从用户态到内核态)**

当你执行 `read()` 或 `write()` 时，CPU 会触发一个特殊的 **"软中断"（Trap）**：
1.  **保存现场**：CPU 暂停你的代码，记住当前执行到哪行。
2.  **切换特权级**：CPU 从偏向你的"用户权限"切到超级管理员的"内核权限"。
3.  **内核干活**：操作系统执行内核代码（如驱动程序读写网卡）。
4.  **切回现场**：干完活，权限降级，恢复你的代码运行。

**4. 为什么网络、文件、进程都要走这里？**

*   **文件 (File)**：硬盘是硬件。读写文件涉及控制磁头、写入扇区，这些动作必须内核来做。
*   **网络 (Network)**：网卡是硬件。发送一个包涉及构造 TCP/IP 报文、通过网卡电平发出去，你的程序没权限直接操作网卡。
*   **进程 (Process)**：CPU 时间片、内存空间分配是核心资源。创建一个新进程（fork）意味着要分出一块新的物理内存，必须由内核这个"大管家"说了算。

#### 在 finguard 项目中的作用

1.  **Drogon 监听请求**：底层调用 `listen()` 和 `accept()` 系统调用，求内核把到达网卡的 TCP 包交给自己。
2.  **日志记录**：调用 `write()` 系统调用，请求内核把日志字符串写入磁盘文件。
3.  **降级方案 (curl)**：调用 `fork()` 和 `exec()` 系统调用。这是最昂贵的 syscall，因为它请求内核复制一整个进程的资源并加载新镜像。

#### 总结
*   **syscall** 是应用程序和硬件之间唯一的**安全通行证**。
*   **代价**：态切换非常耗时。所以高性能服务（如 Drogon）会尽量减少系统调用（比如批量写日志，而不是写一个字符调一次系统调用）。

#### 模拟面试自测
1. 面试官问：系统调用为什么比普通函数慢？
答案：普通函数只是简单的 CPU 跳转（JMP）；系统调用涉及 **1. 权限切换（从 Ring 3 到 Ring 0）**、**2. 上下文切换（寄存器状态入栈）**、**3. 权限检查**。这就像在家拿水喝和去银行取钱的区别。

2. 面试官问：什么是用户态和内核态？切换的时机有哪些？
答案：用户态是受限的运行环境，内核态是全权限环境。当程序需要 **1. 网络/文件 IO**、**2. 进程管理（fork）**、**3. 申请大块内存（brk/mmap）** 时，就会触发系统调用从用户态切入内核态。此外，时钟中断（轮换 CPU）也会强制切入内核态。

3. 面试官问：在 Linux 系统中，一次 `printf("hello")` 发生了什么？
答案：`printf` 是 C 库函数（用户态）。它内部会先存到缓冲区，当缓冲区满或遇到换行符时，它会发起一个 **`write` 系统调用**。此时 CPU 进入内核态，由内核将内存中的字符串通过屏幕驱动或终端文件 fd 输出出来。在这个过程中发生了一次显著的权限级切换。

### 34. 多进程编程：以降级工具调用为例

#### 详细讲解（白话类比）
*   **白话解释**：多进程就像是**“分房睡觉”**。每个进程都有自己独立的房间（内存空间、文件描述符），如果一个房间漏水了（崩溃），其他房间不受影响。
*   **多进程的优点 (优势)**：
    *   **强隔离性**：进程 A 挂了（如 `curl` 调用超时崩溃），进程 B（主服务 `finguard`）依然能稳定运行。这是系统稳定性的最后一道防线。
    *   **独立地址空间**：不需要担心进程间的变量污染，每个进程认为自己独占内存。
*   **多进程的缺点 (代价)**：
    *   **资源昂贵**：创建进程（`fork`）需要复制页表，开销远大于线程。
    *   **通信麻烦 (IPC)**：进程间不能直接传指针，必须通过管道（Pipe）、共享内存或 Socket 这种“寄信”方式交流，速度慢且逻辑复杂。

#### 在 finguard 项目中的体现：调用降级 curl
在本项目中，我们并没有把整个架构做成多进程，而是在**执行降级策略**时体现了多进程的协作。

*   **场景描述**：当主服务的 C++ SDK 请求 LLM 失败（网络库故障或证书问题）时，我们通过 `fork()` + `exec()` 调用系统的 `curl.exe` 工具。
*   **项目证据 (优点体现)**：
    *   **稳定性兜底**：`curl.exe` 是一个经过几十年验证的独立工具。通过多进程调用它，即便 `curl` 因为某些奇葩证书问题 Crash 了，它也只是自己退出，不会把我们的 `finguard` 主服务带崩。
    *   **功能复用**：利用多进程，直接复用操作系统已有的网络能力，而不需要在主服务中集成复杂的 `libcurl` 源码。
*   **项目证据 (缺点体现)**：
    *   **性能损耗**：如[前一章系统调用](C++突击文档.md#L1643)所述，`fork` 是最昂贵的系统调用。如果频繁发起降级调用，CPU 切换开销会显著推高响应延迟。
    *   **数据交换**：我们必须通过重定向 stdout [automation.md](workbook/automation.md#L14) 来获取 `curl` 的结果，这比直接在内存中读一个变量要慢得多且容易出错（如缓冲区满了导致死锁）。

#### 总结
- **多进程 = 稳（空间换安全）**：适合外部工具调用、资源隔离。

#### 模拟面试自测
1. **面试官问：你在项目中为什么要调 `curl.exe` 而不是直接写 C++ 网络库？**
   **答案 (项目回答版)**：这是我们的**降级兜底方案**。直接集成库虽然快，但复杂性高。利用多进程调用 `curl.exe`，我们获得了极强的**故障隔离性**。就算网络环境极其恶劣导致外挂程序崩溃，主服务依然能保持响应。这就是“多进程隔离”在实际生产环境中的容灾价值。

2. **面试官问：如果你要高并发处理 1000 个请求，你会用 1000 个进程还是线程？**
   **答案**：线程。因为进程的内存占用太高（每个进程约占用 8MB+），且 `fork` 的开销在 C++ 高性能服务中是不可接受的。但在执行“非核心、高风险”任务时，我会选择多进程来保证主进程的安全。

---

### 35. 多线程编程：Drogon 线程池与并发安全

#### 1. 概念白话讲解 (面试必备)
*   **竟态 (Race Condition)**：**“抢着修图”**。两个线程同时修改同一个变量。如果没有同步，最后的结果取决于谁运行得快，由于不可预测，结果往往是错误的。
*   **死锁 (Deadlock)**：**“互不松手”**。线程 A 拿着锁 1 等锁 2，线程 B 拿着锁 2 等锁 1。两个人都卡死，程序像点穴了一样动弹不得。
*   **可见性 (Visibility)**：**“内存缓存延迟”**。线程 A 改了变量 `x`，但由于 CPU 缓存（Cache）的存在，线程 B 看到的可能还是旧值。在 C++ 中通过 `std::atomic` 或内存屏障（Memory Barrier）解决。

#### 2. 在 finguard 项目中的体现：所谓“编程”编的是什么？
*   **疑问解答**：无论是**线程编程**还是**进程编程**，我们使用的**都是 C++ 语言**。
    *   **线程编程**：通常是指使用标准库 `std::thread`、`std::mutex`、`std::atomic` 或框架提供的封装（如 Drogon 的线程池）。
    *   **进程编程**：是指在 C++ 代码中调用操作系统提供的系统调用（Syscall），如 `fork()`、`exec()`、`waitpid()`。
*   **项目核心证据 (Drogon 并发模型)**：
    *   **`setThreadNum(4)`**：在 `main.cpp` 中通过该接口设置 IO 线程池大小。Drogon 会启动 4 个 **EventLoop 线程**。
    *   **多线程同步设计**：我们的配置更新（如 API Key 变更）使用了 `std::atomic<bool>` 来保证状态更新的**可见性**；在全局缓存（Cache）访问时，通过 `std::mutex` 加锁防止**竟态**。

#### 3. 编程语言对比：线程 vs 进程
| 维度 | 线程编程 (Thread) | 进程编程 (Process) |
|------|-------------------|-------------------|
| **代码实现** | `std::thread t([]{ ... });` | `if (fork() == 0) { exec(...); }` |
| **通信内容** | 直接读写相同的内存变量 (全局对象) | 读写文件、管道(Pipe) 或 Socket |
| **语言层面** | C++ 标准库 (语言原生支持) | 调用 OS 指令 (属于库/内核接口) |

#### 总结
- **多线程提升吞吐（协作）**：通过共享内存高效交换数据。
- **并发同步是难点**：必须通过 `Atomic`、`Mutex` 解决数据一致性。

#### 模拟面试自测
1. **面试官问：你在 finguard 里怎么处理并发冲突的？**
   **答案 (项目实战版)**：在 `finguard` 的配置更新逻辑中，我们面临多线程读写 API Key 的问题。为了兼顾性能与安全，我们没有粗暴地对所有读操作加重锁，而是采用了 **`std::atomic` 标志位** 结合 **互斥锁 (std::mutex)** 的设计。写操作（后台同步）加锁，读操作（业务流程）先检查原子标志，从而保证了读写的**可见性**和**一致性**，避免了明显的**竟态**。

2. **面试官问：`setThreadNum(4)` 设为 4 是为了什么？**
   **答案**：这是为了充分利用多核 CPU。在我们的压测中 [p5_bench.py](tools/p5_bench.py)，单线程无法打满带宽。设为 4 个线程后，Drogon 的 EventLoop 可以并行处理网络 Socket 上的读写，从而极大提升了系统的 **RPS (每秒请求数)**。

---

### 36. 进程 / 线程对比

#### 详细讲解（小点拆解）
- 进程：资源分配单位。
- 线程：CPU 调度单位。
- 线程共享进程资源，通信快但同步难。

#### 在 finguard 项目中的作用
- 请求并发处理主要在线程层面完成。

#### 总结
- 进程偏隔离，线程偏协作。

#### 模拟面试自测
1. 面试官问：线程为什么切换更轻？
答案：同进程内不需要完整切换地址空间。

### 37. 锁

#### 详细讲解（小点拆解）
- 锁用于保护共享数据。
- 互斥锁保正确性，原子操作保轻量计数。

#### 在 finguard 项目中的作用
- `mutex + lock_guard` 在限流和指标模块广泛使用。

#### 总结
- 先保证正确，再优化锁粒度。

#### 模拟面试自测
1. 面试官问：什么时候 atomic 不够用？
答案：涉及多个变量一致性时。

### 38. 内存管理：虚拟内存、悬挂与容器上限

#### 1. 概念核心讲解 (白话版)
*   **虚拟内存抽象 (Virtual Memory)**：**“每个人都以为自己是亿万富翁”**。
    *   **原理**：操作系统给每个进程一个假象，认为自己拥有连续、巨大的内存空间（如 4GB 或 128TB）。
    *   **物理意义**：它实际上是一张“映射表”。当你访问 0x1234 时，内核悄悄将其转为物理内存（RAM）上的某个角落。
    *   **好处**：**隔离**。进程 A 乱写内存，顶多把自己写崩，绝不会改到进程 B 的数据，因为它们的虚拟地址虽然可能相同，但映射到的物理地址完全不同。
*   **悬挂 (Dangling/Hanging)**：**“拿着失效的房卡”**。
    *   **定义**：一个指针指向的内存已经被 `delete` 或释放了，但指针变量还留着那个地址。
    *   **风险**：如果你再通过这个指针去写数据，就会发生 **Segment Fault (段错误)** 或者更隐蔽的 **内存污染**（改掉了后来分配给别人的数据）。

#### 2. 在 finguard 项目中的体现：容器上限控制
在我们的 C++ 服务中，内存管理不仅是“不泄漏”，更重要的是“不撑爆”。

*   **容器上限的具体设置**：
    *   **场景**：在 `finguard` 的全局指标监控（Metrics）和请求日志缓存中，我们设置了 `std::vector` 或 `std::deque` 的 **Max Capacity (例如 10000 条)**。
    *   **物理意义**：
        1.  **防止 OOM (Out Of Memory)**：如果遭受恶意攻击或流量激增，不设上限的容器会疯狂吞噬物理内存，最终导致 OS 触发 OOM Killer 把整个 `finguard` 进程杀掉。
        2.  **保证缓存热度**：上限强制我们使用“剔除策略”（如 LRU 或 简单 Pop），确保内存里留下的永远是最近、最有价值的数据，而不是堆积如山的陈旧垃圾。
*   **项目证据 (RAII) — 智能指针的分工**：我们全面使用 `std::unique_ptr` 和 `std::shared_ptr`，拒绝手动 `new/delete`。
    *   **`std::unique_ptr` (独占所有权)**：**“私人订制”**。
        *   **用法**：用于生命周期非常明确、不需要共享的对象。
        *   **项目体现**：在 `finguard` 的请求处理器（Request Handler）内部创建的临时解析对象、或者各个模块内唯一的单例（Singleton）管理对象。一旦函数结束或模块销毁，对象立即释放。
    *   **`std::shared_ptr` (共享所有权)**：**“公共汽车”**。
        *   **用法**：用于多个地方都需要引用、且不确定谁最后离开的对象。
        *   **项目体现**：**配置中心 (Config)** 和 **LLM 客户端实例**。由于多个业务线程同时需要读取同一份配置，我们使用 `shared_ptr` 确保只要还有一个线程在用，配置对象就不会被意外销毁。
    *   **如何防悬挂**：当 `shared_ptr` 的计数清零，内存自动释放，且我们会将不再使用的原始指针显式设为 `nullptr`，确保不会拿着“失效房卡”去刷门。

#### 总结
- **虚拟内存 = 逻辑隔离**：给进程安全感。
- **上限控制 = 资源红线**：给系统稳定性保命。

#### 模拟面试自测
1. **面试官问：你的 C++ 服务跑久了内存会爆吗？你做了什么优化？**
   **答案 (项目实战版)**：我们会重点监控容器的增长。在 `finguard` 中，所有的内存缓冲区（Buffer）都设有 **显式上限 (Capacity Limit)**。当达到阈值时，我们会触发抛弃策略或同步到磁盘。通过这种方式，我们将服务的物理内存占用稳定在 200MB 以内，有效规避了因长尾请求堆积导致的 OOM 风险。

2. **面试官问：什么是“悬挂指针”？怎么在代码里彻底杜绝它？**
   **答案**：悬挂指针是由于内存生命周期管理不当导致的。在项目中，我们禁止直接使用 `new/delete`。通过 **RAII (资源获取即初始化)** 机制和 **智能指针**，我们确保内存的释放与对象的生命周期自动绑定。对于必须手动处理的逻辑，释放后立即补上 `ptr = nullptr`，配合静态分析工具，从研发规范上杜绝悬挂风险。

---

### 39. 进程调度与性能指标：SLA、P99 与尾延迟

#### 1. 概念核心讲解 (白话版)
*   **调度的本质 (Scheduling)**：**“CPU 时间片的拍卖师”**。OS 内核决定哪个进程/线程可以使用 CPU。
    *   **实现方法**：常用 **CFS (完全公平调度器)**。它像一个精明的会计，给每个进程记账（运行时间），谁跑得少（虚拟时间小）就让谁先上，从而实现“公平”。
*   **公平、响应、吞吐的三角平衡**：
    *   **公平 (Fairness)**：人人有份，不让任何进程“饿死”。
    *   **响应 (Latency/Response)**：**“即时性”**。鼠标点了立刻有反应。为了响应快，就要频繁切换 CPU，但切换是有成本的。
    *   **吞吐 (Throughput)**：**“干活总量”**。减少切换次数，让 CPU 一直埋头苦干。
    *   **平衡点**：在 `finguard` 这种后端服务中，我们牺牲一定的极致吞吐（通过 `setThreadNum(4)` 增加切换），来换取更快的接口**响应**速度。
*   **P99 与 尾延迟 (Tail Latency)**：
    *   **P99**：如果有 100 个请求，按耗时从快到慢排，第 99 个请求的耗时就是 P99。
    *   **尾延迟**：那最后 1%（甚至 0.1%）特别慢的请求。就像银行排队，前面 99 个人都很快，但第 100 个人办业务极其复杂，导致后面的人等死。这 1% 的“倒霉蛋”决定了系统的**最差体验**。
*   **SLA (Service Level Agreement)**：**“服务合同”**。比如对外承诺：“我们的 API 99% 的请求必须在 500ms 内完成”。如果 P99 超过 500ms，就叫“违反 SLA”，可能要赔钱给客户。

#### 2. 在 finguard 项目中的体现：指标监控与调优
在我们的压测工具 [p5_bench.py](tools/p5_bench.py) 中，我们会重点输出 P99 和 Max Latency。

*   **项目证据 (性能分析)**：
    *   **锁定“长尾”**：在并发量极高时，我们发现 P99 会显著飙升。
    *   **原因分析**：这不是因为 CPU 不够快，而是因为 **1. 锁竞争 (Lock Contention)** 导致线程被挂起，或者 **2. OS 调度** 将我们的 worker 线程切出去了。
    *   **优化策略**：通过减少全局锁的范围，以及合理设置线程数（与核心数匹配），来压低 P99，从而确保我们的服务符合 **SLA 响应标准**。

#### 总结
- **调度 = 分蛋糕的艺术**：在响应速度和干活效率间找平衡。
- **P99 = 系统的良心**：它不仅看平均速度，更关注每一个真实用户的体感。

#### 模拟面试自测
1. **面试官问：为什么在性能监控中，P99 比平均值（Average）更重要？**
   **答案 (项目实战版)**：平均值会掩盖真相。在 `finguard` 压测中，可能平均耗时是 100ms，但如果 P99 是 2 秒，意味着每 100 个用户就有 1 个经历了极长的等待。对于金融投资类应用，这种不稳定的**尾延迟**会导致用户对系统可靠性产生怀疑。我们通过监控 P99 来驱动优化（如细化锁粒度、调整线程亲和性），从而保证服务符合严格的 **SLA 承诺**。

2. **面试官问：操作系统是如何实现调度的？**
   **答案**：在 Linux 中主要是通过 **CFS (Completely Fair Scheduler)**。它利用**红黑树**管理进程，以进程运行的虚拟时间为键值，每次挑选虚拟时间最小（即最受委屈）的进程投入执行。这种机制兼顾了交互式进程的**响应性**和批处理进程的**吞吐量**。

---

### 40. 进程通信 (IPC)：管道、消息队列与共享内存

#### 1. 概念核心讲解 (白话版)
*   **进程通信的双方是谁？**：是指运行在同一个操作系统上的两个**独立进程**。例如：主服务进程 `finguard` 和 降级调用的 `curl` 进程。
*   **管道 (Pipe)**：**“单向传声筒”**。
    *   **特点**：通常只有父子进程能用（如 `fork` 出来的）。进程 A 往里喊，进程 B 在另一头听。
*   **消息队列 (Message Queue)**：**“信箱”**。
    *   **特点 (解耦)**：进程 A 把消息丢进信箱就去忙别的了（异步），进程 B 有空再去取。即使 B 挂了，消息也还在信箱里，实现了真正的**逻辑解耦**。
*   **共享内存 (Shared Memory)**：**“公共办公桌”**。
    *   **解惑：为什么要共享？** 
        *   你观察得非常对！[虚拟内存抽象](C++突击文档.md#L1762) 默认是为了**隔离**（每个人一张办公桌，互不干扰）。
        *   但当你需要两个进程**极速交换数据**（比如每秒传输几百兆视频流）时，隔离反而成了阻碍。
        *   **做法**：操作系统允许进程 A 和 进程 B 的虚拟地址映射表，同时指向**同一块物理内存**。这就是“主动打破隔离”。
        *   **代价**：因为两边都能写，所以必须自己加锁（如信号量）来防止数据写乱。

#### 2. 在 finguard 项目中的体现：Socket 通信
*   **核心实现**：在本项目中，我们最常用的 IPC 其实是 **Socket (通过 Loopback 127.0.0.1)**。
    *   **证据**：前端 React 应用与后端 C++ 服务之间，本质上是两个进程。它们的数据交换（API 请求）就是通过 **网络协议描述符 (Socket)** 完成的。这种方式虽然比共享内存慢，但兼容性最强（甚至可以跨机器）。

#### 3. Sidecar 与 Socket 的关系与实践建议
*   **Sidecar = 部署/拓扑模式**：sidecar 指的是与主进程并行运行的独立辅助进程（如长期驻留的 LLM proxy 或专门的 metrics/agent）。它强调“陪伴式的进程拓扑与独立生命周期”。
*   **Socket = 通信通道**：socket（TCP loopback、Unix Domain Socket、Windows Named Pipe）是进程间实际交换数据的管道。sidecar 通常通过某类 socket 向主进程暴露服务接口（HTTP/gRPC 等）。
*   **二者关系**：sidecar 描述“谁跑在那儿”（进程组织），socket 描述“他们怎么聊”（传输手段）。例如：`finguard.exe`（主进程）通过 `127.0.0.1:8081` 调用同机 sidecar 的 HTTP 接口获取模型推理结果。
*   **实现选择与权衡**：
    - **开发/调试**：用 TCP loopback（跨平台、易调试）。
    - **生产延迟敏感**：优先考虑 Unix Domain Socket（UDS）或 Windows Named Pipe，拷贝开销更小，延迟更低。
    - **极致吞吐**：若追求最低延迟且可接受复杂性，可采用共享内存+信号量，但需自己实现同步与边界检查。
*   **实践建议（finguard）**：
    - **降级/隔离场景**：对于偶发、低频的降级调用，短期 `fork`+`exec` 调用工具进程简单直接；若调用频率高，改为常驻 sidecar + socket RPC 更合适，避免重复 `fork` 的高开销。
    - **安全与兼容**：sidecar 只绑定 loopback，接口加认证；生产中注意 CORS 与预检（参见 workbook 的 CORS 处理规则）。

#### 总结
- **管道、Socket = 传声**：需要数据拷贝。
- **共享内存 = 见画**：无需拷贝，但需同步。
- **消息队列 = 寄信**：天然异步和解耦。

#### 模拟面试自测
1. **面试官问：既然虚拟内存是为了隔离，为什么还要搞共享内存？**
   **答案 (项目实战版)**：隔离是为了**安全性**，而共享内存是为了**极致性能**。在普通业务中，我们通过 Socket 通信保证隔离和跨平台性；但如果在极致性能场景（如金融高频交易或音视频处理），数据拷贝的开销（Copy-on-write 或 用户态/内核态拷贝）是无法接受的。共享内存通过映射同一块物理地址，消除了数据搬运，是 IPC 中速度最快的一种。

2. **面试官问：消息队列的“解耦”体现在哪里？**
   **答案**：体现在**时间和空间的双重解耦**。发送者不需要知道接收者是谁，也不需要等接收者在线。这在分布式系统或复杂后端系统中，能有效防止一个模块的阻塞或崩溃引起全链路崩溃（雪崩效应）。

---

### 41. 死锁预防与检测

#### 详细讲解（大白话教学）
* 先给个白话类比：四个人围着四张桌子，每个人先拿一张桌子的钥匙再去要第二张钥匙，结果大家都不肯放手，谁也进不了第二张桌子的门——这就是死锁。

* 死锁的“四条件”（每一条都是真实世界里会让你卡死的原因）：
    1. **互斥 (Mutual Exclusion)**：资源不是大家能同时用的，必须独占。举例：一个线程独占某个内存块或文件句柄。
    2. **持有并等待 (Hold and Wait)**：线程 A 已经持有资源 X，但还要等待资源 Y；在等待时它不释放 X。
    3. **不可剥夺 (No Preemption)**：已经分配的资源不能被强行收回，只能被占有者主动释放。
    4. **循环等待 (Circular Wait)**：存在一个等待环：A 等 B，B 等 C，……最后又回到 A，形成闭环。

* 为什么知道这四条有用？破坏任意一条，就能避免死锁。比如不允许“持有并等待”，就要求在申请资源前一次性申请所有需要的锁。

#### 什么是“嵌套锁” (Nested Locks)
* 白话解释：线程在拿到锁 A 后，又去申请锁 B，这就是嵌套锁（A 内再锁 B）。当另一个线程反过来先拿 B 再拿 A，就会产生相互等待导致死锁。
* 例子：
    - 线程1: lock(mtx1); lock(mtx2); // 嵌套锁顺序 mtx1 -> mtx2
    - 线程2: lock(mtx2); lock(mtx1); // 嵌套锁顺序 mtx2 -> mtx1 → 死锁风险

#### finguard 的实战建议（用大白话说明怎么防）
* **统一加锁顺序**：给所有锁排个“等级”，所有代码按等级顺序加锁，谁都不能逆序。简单且非常有效。
* **缩短持锁时间**：能把工作移出临界区就移出，先构造数据再加锁去交换，而不是长时间持有锁做复杂计算。
* **尽量避免嵌套锁**：如果必须嵌套，明确文档化锁顺序；优先考虑把数据拆成更细的锁或使用读写锁。
* **使用 try_lock + 回退 (backoff)**：若申请第二把锁失败，释放第一把锁、等待一小段随机时间再重试，能避免死循环等待。
* **超时检测与监控**：给关键锁加上超时报警，出现长时间挂起时记录调用栈并触发告警，便于线上快速定位。
* **用无锁/并发容器**：在高并发、热点数据上，优先考虑 lock-free 或并发数据结构（如 `concurrent_queue`）来彻底避免锁竞争。

#### 模拟面试自测（大白话回答）
1. 面试官：最实用的防死锁技巧是什么？
     答：先给锁排顺序（统一规则），别随意在函数里乱拿第二把锁；能用 try_lock 就别阻塞等死。
2. 面试官：如果线上怀疑有死锁，你怎么定位？
     答：先看线程快照/堆栈（gcore / jstack 类似工具），找被挂起的线程在等哪把锁；再检查加锁顺序和临界区长度，结合超时日志定位罪魁祸首。

### 42. 零拷贝

#### 详细讲解（小点拆解）
- 目标：减少数据在内核态和用户态之间搬运。
- 常见方式：`sendfile`、`mmap`。

#### 详细讲解（小点拆解）
* 目标：尽量消除用户态与内核态之间的多次数据拷贝，减少 CPU 花在搬运数据上的时间。
* 常见实现：
    - **`sendfile(2)`**：内核直接从文件描述符把数据写到 socket，不把数据拷贝到用户空间，从而省去一次或多次内存拷贝（用户态→内核态→用户态）。适用于把文件直接发到网络的场景。
    - **`mmap` + `write`/`sendfile`**：用 `mmap` 将文件映射到进程虚拟地址空间，读取时不发生 `read()` 的用户态拷贝；配合 `sendfile` 或 `splice` 可进一步减少拷贝路径。
    - **`splice` / `vmsplice`**：在 Linux 下把数据在两个文件描述符之间在内核态零拷贝地移动，常用于管道/socket 间搬运数据。

* 用户态/内核态切换与零拷贝的关系：
    - **系统调用（syscall）** 是触发用户态到内核态切换的入口。即便使用零拷贝，发起 I/O（如 `sendfile`）仍然需要一次系统调用，但零拷贝减少了数据从内核复制回用户态再复制到内核的次数，从而显著降低 CPU 与内存带宽消耗。
    - 换句话说：零拷贝不能完全消除系统调用开销（上下文切换、权限检查等），但能把“搬运”开销降到最低。

#### 链路（Link）与 API 的含义说明
* **链路 = 数据流经的路径**，可以指一条调用路径或网络/进程间的数据流（例如：浏览器 → 前端进程 → 后端 C++ → sidecar LLM → 第三方 LLM 服务）。链路描述的是数据从入口到出口经过的组件与协议。
* **API 的范围**：
    - **用户与后端之间的 API**：通常指外部 HTTP/REST 接口（例如浏览器向 `finguard` 的 `/api/v1/plan` 发起请求）。这是对外暴露的服务边界，关注兼容性、认证与 CORS。
    - **后端与 LLM / sidecar 之间的 API**：内部服务间 RPC（HTTP/gRPC/UDS），关注延迟、带宽与序列化成本；这类 API 更适合做性能优化（例如考虑使用 UDS 或共享内存）。

* 在 `finguard` 的当前链路（实践描述）：
    - **主场景**：浏览器发起请求 → 前端代理/服务 → `finguard`（C++ 后端）→ 内部调用 LLM（可能是 sidecar 或外部服务）→ 返回给用户。
    - **API 层级**：既有用户可见的 HTTP API（用户 ↔ 后端），也有内部服务 API（后端 ↔ LLM/sidecar）。零拷贝优化通常针对**内部高吞吐、大数据量**的链路段（如文件/大二进制数据在本机进程间搬运），而不是典型的 JSON API 请求/响应。

#### 在 finguard 项目中的作用与建议
* 当前链路以 JSON API（HTTP）为主，典型请求体较小，**零拷贝不是当前首要优化点**。
* 若出现高吞吐大体量数据（例如批量模型输入/输出或文件回传），可考虑：
    - 在同机场景选用 UDS 或共享内存以降低拷贝；
    - 对文件转发使用 `sendfile`（若数据源是磁盘文件）；
    - 在设计时权衡实现复杂度与收益：共享内存带来同步复杂度与更高的实现维护成本。

#### 总结
* **sendfile/mmap/splice** 是实现零拷贝的常见手段，能显著减少内存拷贝次数，但不能免除系统调用本身的开销。
* 对于 `finguard` 当前以 JSON API 为主的链路，优先优化点是减少锁竞争与序列化开销；当遇到大数据传输需求时，再考虑引入零拷贝技术。

#### 逐步链路与典型拷贝点（白话）
下面用一个常见场景说明：用户发一段文本到后端，后端把它发给大模型，模型响应再回到用户。列出每一步普通情况下会发生的拷贝：

普通路径（有多次拷贝）：
1. **用户端（浏览器）→ 后端（finguard）**：浏览器把数据从用户态写到 socket，内核接收后会把数据放到内核缓冲区，随后 `finguard` 通过 `recv` 把数据从内核缓冲区拷贝到自己的用户空间缓冲区。——（拷贝：用户态→内核态一次，内核态→用户态一次）
2. **后端 → 网络 / sidecar / 外部 LLM**：后端把数据写入 socket，数据又从用户缓冲区拷贝到内核缓冲区，内核再把数据发到网络或本机 socket 的内核缓冲区。——（拷贝：用户态→内核态一次）
3. **外部 LLM 处理并返回**：对方服务收到后可能也会进行类似的拷贝和处理，响应返回时内核到用户态又要拷贝一次。
4. **后端接收响应并返回给用户**：后端再次从内核缓冲区拷贝响应到用户态，然后写回给浏览器，浏览器也会把数据从内核读到用户态。——（拷贝：内核↔用户态若干次）

为什么看起来“数据还是在拷贝”？因为以上每一步应用层与内核层之间都发生了内存拷贝，尤其是在跨进程或跨机器时，序列化/反序列化也会产生额外拷贝。

零拷贝优化后的路径（尽量少搬运）：
1. **用 `sendfile`/`splice` 发送文件数据**：如果数据源是文件，后端可以直接让内核把文件页从磁盘通过内核缓冲区发到网络，中间不把数据拷回用户态。
2. **进程间用 UDS / 共享内存 + 同步**：同机 sidecar 可以用 Unix Domain Socket 结合 `splice` 或直接用共享内存映射（`mmap`）把物理页映射到双方地址空间，避免内核↔用户的多次拷贝。
3. **MSG_ZEROCOPY / NIC DMA 优化**：高级路径允许在发送后由内核异步完成拷贝并通过 DMA 由网卡直接传输，应用不需要把数据完整复制到多个缓冲区。

实战要点：
- 零拷贝并不总是能完全消除所有拷贝（系统调用、协议头的处理、序列化仍需少量操作）。
- 对于小的 JSON 文本（典型 API 请求），拷贝开销远小于 CPU 的其它消耗（如模型推理或业务计算），因此优先级低。
- 当遇到大体积数据（文件、批量二进制、流媒体）时，采用 sendfile/UDS/mmap 能显著降低 CPU 与内存带宽占用。


#### 模拟面试自测
1. 面试官问：零拷贝本质优化了什么？
答案：拷贝次数和上下文切换。

### 43. 协程与事件循环：高效并发的“分身术”

#### 1. 概念核心讲解 (大白话教学)
*   **协程 (Coroutine)**：**“能随时暂停的函数”**。
    *   **白话类比**：普通函数像“直肠子”，一旦调用必须执行完才返回；协程像“追剧”，看一半可以点暂停（yield），去干别的，一会儿再点继续（resume）从断点接着看。
    *   **本质**：它是在**用户态**模拟出来的“线程”。CPU 不知道协程的存在，是我们程序员自己在代码里控制什么时候切换。
*   **协程和线程的关系**：
    *   **线程**是 OS 分配的“真·员工”（笨重、切换慢、占用 1MB+ 内存）。
    *   **协程**是线程里的“小分身”（轻量、切换极快、占用几 KB 内存）。
    *   **一句话概括**：一个线程可以跑成千上万个协程。协程用来解决 **“IO 等待”** 期间 CPU 闲置的问题。
*   **事件循环 (Event Loop)**：**“忙碌的餐厅领班”**。
    *   **原理**：领班盯着所有桌子（Socket/文件）。哪桌有菜（数据）了，他就叫对应的厨师（回调函数/协程）去处理。这是支撑高并发（如 Nginx、Node.js、Drogon）的核心骨干。
*   **对称 vs 非对称协程**：
    *   **非对称协程 (Asymmetric)**：**“权力集中制”**。协程挂起后，必须回到调用它的那个“老大”（调度器）那里，由老大决定下一个谁跑。这是最主流的实现（如 Python, Go, C++20）。
    *   **对称协程 (Symmetric)**：**“平级切换”**。协程 A 挂起时，可以直接把控制权交给协程 B，大家地位平等。
*   **共享栈 (Shared Stack)**：**“公用一张办公桌”**。
    *   为了省内存，所有的协程在运行时都共用同一块内存区域。当协程 A 要走、B 要来时，A 必须把桌上自己的东西（栈帧数据）打包带走存起来，B 把自己的摆上来。虽然省了内存，但多了“打包搬运”的开销。
*   **libco**：**“腾讯的大杀器”**。
    *   微信后台使用的协程库。它的牛逼之处在于通过 **Hook 系统调用**，把原本会阻塞的 `read/write` 偷偷变成了异步操作，让旧代码不需要大改就能瞬间支持千万级并发。

#### 2. 在 finguard 项目中的体现：Drogon 的事件驱动
*   **现状**：我们目前主要使用 **EventLoop (事件循环)** 结合线程池。
    *   **场景**：当 `finguard` 向 LLM 发起请求时，IO 线程不会傻等，而是把这个连接注册到 EventLoop。等 LLM 回消息了（触发事件），Loop 再通知我们去解析结果。
    *   **未来扩展**：如果我们要处理海量的长连接或极高并发的本地计算，引入 C++20 协程或 libco 风格的库，可以让异步代码写起来像同步代码一样简单，避免“回调地狱”。

#### 总结
- **协程 = 极致压榨 CPU**：不让 CPU 在等 IO 时睡觉。
- **事件循环 = 任务分发站**：它是协程稳定运行的底座。

#### 模拟面试自测 (面试官版)
1. **面试官问：协程既然在用户态，它能利用多核 CPU 吗？**
   **答案 (项目实战版)**：单纯的协程不行，因为它跑在某个特定的线程里。但在我们的 **Drogon 架构** 中，我们开启了 4 个线程 (`setThreadNum(4)`)，每个线程里都可以跑一个或多个 EventLoop 和成千上万的协程。这样既通过**协程**解决了单核内的 IO 阻塞，又通过**多线程**利用了多核 CPU。

2. **面试官问：说一下非对称协程和对称协程的区别？**
   **答案**：主要区别在于**控制流的转移权**。非对称协程有明确的调用者（Caller）和被调用者（Callee）关系，挂起时必须回到调用者手里；而对称协程是平等的，可以互相 yield。目前工业界（如 C++20, Go）大多倾向于非对称协程，因为它的逻辑更清晰，容易维护调度逻辑。

---

## 六、MySQL基础与优化

### 44. SQL 基本语句：数据库的"普通话"

#### 1. 概念核心讲解 (大白话教学)
*   **SQL 是什么？**：**"跟数据库说话的语言"**。你想从数据库里拿数据、存数据、改数据、删数据，都得用 SQL 写"命令"告诉它。
*   **增删改查 (CRUD)**：
    *   `INSERT`：往表里塞一行数据（存钱）。
    *   `DELETE`：删掉某些行（销户）。
    *   `UPDATE`：改已有数据（改密码）。
    *   `SELECT`：查数据（查余额）。
*   **过滤 (`WHERE`)**：**"筛选器"**。`SELECT * FROM users WHERE age > 18` 就是"只要 18 岁以上的"。
*   **聚合 (`GROUP BY` + `COUNT/SUM/AVG`)**：**"统计报表"**。比如"按城市统计用户数"。
*   **排序 (`ORDER BY`)**：按某个字段排序输出。
*   **连接 (`JOIN`)**：**"拼表"**。把两张有关联的表横向拼在一起。比如"用户表"和"订单表"通过 `user_id` 拼起来，就能看到每个用户买了什么。
*   **`WHERE` vs `HAVING`**：
    *   `WHERE`：在分组**之前**就把不要的行过滤掉。
    *   `HAVING`：在分组**之后**再对统计结果过滤。比如"找出订单数超过 5 的城市"，得先 `GROUP BY city`，再 `HAVING COUNT(*) > 5`。
*   **SQL 执行顺序（面试高频）**：`FROM → WHERE → GROUP BY → HAVING → SELECT → ORDER BY → LIMIT`。注意：`SELECT` 不是第一步！

#### 2. 在 finguard 项目中的定位
*   当前 `finguard` 未直接接入 MySQL，数据以 JSON 配置文件为主。但如果后续需要持久化用户投资组合、审计日志，SQL 就是必经之路。

#### 模拟面试自测
1. **面试官问：SQL 的 `WHERE` 和 `HAVING` 区别？**
   **答案**：`WHERE` 在分组前过滤原始行，`HAVING` 在分组后过滤聚合结果。`WHERE` 里不能用聚合函数（如 `COUNT`），`HAVING` 可以。
2. **面试官问：`LEFT JOIN` 和 `INNER JOIN` 的区别？**
   **答案**：`INNER JOIN` 只返回两张表都匹配的行；`LEFT JOIN` 会保留左表的所有行，右表没匹配到的字段填 `NULL`。

### 45. MySQL 索引：数据库的"目录页"

#### 1. 概念核心讲解 (大白话教学)
*   **索引是什么？**：**"字典的目录"**。没有索引，查数据就像翻一本没目录的书，从第一页翻到最后一页（全表扫描）。有了索引，直接翻目录定位到第几页。
*   **B+ 树**：**"多层书架"**。
    *   MySQL InnoDB 的索引默认用 B+ 树。它像多层图书馆书架：最顶层是大类，往下越来越细，最底层（叶子节点）才存真正的数据。
    *   **为什么用 B+ 树？** 因为它"矮胖"（扇出大、树层少），查一条数据只需要访问 3-4 层磁盘页，速度极快。
*   **聚簇索引 (Clustered Index)**：**"数据本身就按这个顺序排"**。InnoDB 的主键索引就是聚簇索引，数据行的物理存储顺序和主键顺序一致。一张表只能有一个。
*   **回表 (Table Lookup)**：**"查了目录还要翻正文"**。用非主键索引查数据时，索引页上只存了主键值，还得拿主键回聚簇索引查完整数据。这个"多跑一趟"就叫回表。
*   **覆盖索引 (Covering Index)**：**"目录上就有答案"**。查的字段刚好全在索引里，不需要回表。这是最高效的查询方式。
*   **最左前缀原则**：**"联合索引只能从左边开始用"**。联合索引 `(a, b, c)` 可以加速 `WHERE a=1`、`WHERE a=1 AND b=2`，但不能直接加速 `WHERE b=2`（跳过了 a）。

#### 2. 在 finguard 项目中的定位
*   当前未接数据库。但如果后续要存储用户投资记录，查询"某用户最近 30 天的交易"就需要在 `(user_id, created_at)` 上建联合索引，避免全表扫描。

#### 模拟面试自测
1. **面试官问：为什么 B+ 树适合数据库索引？**
   **答案**：B+ 树扇出大（每个节点可存上千个指针），树高通常只有 3-4 层，一次查询最多 3-4 次磁盘 IO。叶子节点之间有双向链表，非常适合范围查询。
2. **面试官问：什么是回表？怎么避免？**
   **答案**：回表是通过二级索引找到主键后，还要再去聚簇索引查完整行。避免方法是设计**覆盖索引**，让查询所需的字段全部包含在索引中。

### 46. MVCC（多版本并发控制）

#### 为什么需要 MVCC？——先搞清楚问题

> 想象一个图书馆：有人正在修改一本书（写），同时有十个人想读这本书。
> 如果“修改时把书锁起来不让读”，那十个人全在排队——**读写互斥，并发崩了**。
> MVCC 的思路是：**给书拍个快照副本**，修改者改原本，读者看副本，**谁都不等谁**。

#### 详细讲解（小点拆解）

**1. MVCC 核心思想——“多个版本共存”**

| 概念 | 大白话 |
|------|--------|
| 多版本 | 同一行数据在数据库里**同时保留多个历史版本**，不是只有“最新的一份” |
| 并发控制 | **读不阻塞写，写不阻塞读**——靠的就是版本分离 |
| 实现层 | InnoDB 引擎通过 **undo log** 串成版本链来保存历史版本 |

**2. 快照读 vs 当前读**

这是 MVCC 最关键的两个概念：

| 类型 | SQL 示例 | 读的是什么 | 是否加锁 |
|------|----------|-----------|---------|
| 快照读 | 普通 `SELECT * FROM t` | 事务开始那一刻的“快照版本” | **不加锁** |
| 当前读 | `SELECT ... FOR UPDATE`、`INSERT`、`UPDATE`、`DELETE` | **最新已提交版本** | **加锁**（行锁/Next-Key Lock） |

> 大白话：快照读 = “看老照片”，当前读 = “看现场并占座”。

**3. undo log 版本链——MVCC 的数据基础**

每次 UPDATE 一行，InnoDB 不是直接覆盖，而是：
1. 把**旧值**写进 undo log；
2. 新值写到数据页上；
3. 新值里有一个**隐藏指针 roll_pointer** 指向 undo log 里的旧值；
4. 如果再更新，旧值继续往前串 → 形成**版本链**（链表）。

```
最新值 → undo版本3 → undo版本2 → undo版本1
```

**4. ReadView——“我该看哪个版本？”**

光有版本链还不够，得有一个规则告诉事务“**你应该看到哪个版本**”。这个规则就是 **ReadView**。

ReadView 里记了三个关键值：

| 字段 | 含义 |
|------|------|
| `m_ids` | **当前所有活跃（未提交）事务 ID 列表** |
| `min_trx_id` | 活跃列表里**最小的事务 ID** |
| `max_trx_id` | 下一个将要分配的事务 ID（即当前最大 + 1） |

判断逻辑（对版本链上的每个版本）：
- 版本的创建事务 ID **< min_trx_id** → 说明在我开始前就提交了 → **可见**
- 版本的创建事务 ID **≥ max_trx_id** → 说明在我开始后才出现 → **不可见**
- 版本的创建事务 ID **在 m_ids 里** → 说明创建者还没提交 → **不可见**
- 其他情况 → **可见**

> 大白话：ReadView 就像拍照时的“快门时间戳”，在快门按下那一刻已提交的数据我能看到，之后的改动我看不到。

**5. RC 和 RR 隔离级别下 ReadView 的区别**

| 隔离级别 | ReadView 生成时机 | 效果 |
|---------|-----------------|------|
| Read Committed (RC) | **每条 SELECT 都生成新的 ReadView** | 能看到其他事务已提交的最新数据 |
| Repeatable Read (RR) | **只在事务第一次 SELECT 时生成一次** | 整个事务期间看到的数据不变（可重复读） |

> MySQL 默认隔离级别是 **RR**，所以同一个事务里多次 SELECT 同一行，结果一样——因为用的是同一个 ReadView。

**6. MVCC 解决了哪些并发问题？**

| 问题 | MVCC 能否解决 | 怎么解决 |
|------|-------------|---------|
| 脏读 | ✅ | 只读已提交版本 |
| 不可重复读 | ✅（RR 级别） | ReadView 只生成一次 |
| 幻读 | ⚠️ 快照读能解决，当前读靠 Next-Key Lock | ReadView + 间隙锁 |

#### 在 finguard 项目中的作用

finguard 目前没有直接接入 MySQL，但 MVCC “**读写分离版本、减少锁冲突**” 的核心思想在项目里有对应：

| MVCC 概念 | finguard 对应 |
|-----------|-------------|
| 版本链（旧值不删，保留历史） | 配置缓存刷新时先构建新 map，再原子替换指针，旧版本被 `shared_ptr` 引用计数自动回收 |
| 快照读（读不阻塞写） | 读请求拿到 `shared_ptr<const Config>` 后安全读取，不影响写线程替换 |
| ReadView（可见性判断） | 每个请求“快照”到的配置版本由获取时刻决定 |

#### 总结

- MVCC = **多版本 + ReadView** 实现读写不阻塞；
- 版本链存在 **undo log** 里，ReadView 决定“我能看到哪个版本”；
- **RR 隔离级别**下 ReadView 只生成一次 → 可重复读；
- 当前读仍然要加锁，MVCC **只优化快照读**。

#### 模拟面试自测

1. **面试官问：MVCC 解决了什么问题？**
> 标准答案：让读写操作不互相阻塞，提升并发性能。通过 undo log 版本链保留历史版本，通过 ReadView 判断可见性。
> 项目实战版：finguard 里配置缓存的“原子替换 + shared_ptr 延迟回收”本质上就是 MVCC 思想——新旧版本共存，读者看旧版本不受写者影响。

2. **面试官问：RC 和 RR 下 MVCC 行为有什么区别？**
> 答案：RC 每次 SELECT 都重新生成 ReadView，所以能看到其他事务刚提交的数据；RR 只在第一次 SELECT 时生成 ReadView，后续都用同一个，所以整个事务看到的数据一致。

3. **面试官问：MVCC 能完全解决幻读吗？**
> 答案：快照读下可以（因为 ReadView 不变），但当前读下不行，需要配合 Next-Key Lock（行锁 + 间隙锁）才能防止幻读。

### 47. 存储引擎（MySQL 的“职能部门”）

#### 什么是存储引擎？——先搞清楚概念

> 数据库像一个“大文件管理器”，但具体怎么把数据 write into 磁盘、怎么加锁、怎么查最快，是由**不同的部门（存储引擎）**负责的。
> 你选了哪个引擎，就等于决定了这表数据“支持什么高级功能”。

#### 详细讲解（小点拆解）

**1. InnoDB 引擎（全能冠军，默认优选）**

| 特性 | 大白话说明 |
|------|-----------|
| **支持事务** | 保证一组操作“要么全成功，要么全失败”，这就是 ACID |
| **行级锁** | 修改一行只锁一行，其他人能改别的行。**并发能力极强** |
| **外键支持** | 可以在数据库层面强行维护表与表的关系 |
| **聚簇索引** | 数据和索引存在一起，查主键快到飞起 |

**2. MyISAM 引擎（过气选手，主要查数）**

| 特性 | 大白话说明 |
|------|-----------|
| **不支持事务** | 适合只读、不怎么改的数据。出错了没法自动回滚 |
| **表级锁** | 改一行就要锁全表。**并发一高就排队**，慢得死人 |
| **崩溃恢复差** | 断电容易丢数据，没办法像 InnoDB 那样靠 redo log 自动修好 |

**3. Memory 引擎（内存党，一闪而过）**

- 数据全在内存里，速度飞快，但**一断电数据全没**。常用于存放临时的“计算中间结果”。

#### 在 finguard 项目中的作用

finguard 的 **Config Center（配置中心）** 缓存机制可以类比数据库引擎的设计：

| 存储引擎概念 | finguard 对应设计 |
|-------------|-----------------|
| 存储介质 | 内存（类比 Memory 引擎），追求极致性能 |
| 并发控制 | `std::shared_ptr` 原子替换（类比 InnoDB 的多版本读） |
| 持久化 | 配置文件同步加载（类比存储引擎的数据刷盘） |

#### 总结

- **InnoDB**：有事务、有行锁、能容错，线上系统**99% 的表**都用这个。
- **MyISAM**：没事务、全表锁、并发低，现在基本已经淡出视野。

#### 模拟面试自测

1. **面试官问：为什么现在 MySQL 默认引擎都是 InnoDB？**
> 标准答案：因为 InnoDB 支持事务（ACID）、行级锁（高并发）以及完善的崩溃恢复能力（redo log），更加安全稳定。
> 项目实战版：就像我们在 finguard 里需要确保配置加载是原子的一样，InnoDB 提供的事务特性是业务正确性的底层保障。

---

### 48. 锁机制（并发的“红绿灯”）

#### 详细讲解（小点拆解）

**1. 范围（锁谁？）**

| 锁级别 | 大白话 | 性能影响 |
|-------|-------|--------|
| **全局锁** | 整个数据库都锁住（只读） | 做全库备份时用，业务全停 |
| **表级锁** | 把整张表锁了 | 简单粗暴，开销小但并发低 |
| **行级锁** | 只锁你改的那一行 | 开销大（锁多），但**并发最高**！ |

**2. 兼容性（能不能一起读/写？）**

| 类型 | 大白话 | 冲突情况 |
|-----|-------|--------|
| **共享锁 (S锁)** | “大家一起读”，谁也别改 | 读读不冲突，读写冲突 |
| **排他锁 (X锁)** | “我一个人改”，谁也别看别改 | 写读、写写都冲突 |

**3. “防鬼”专用锁——间隙锁 (Gap Lock)**

在 **RR (可重复读)** 级别下，为了防止**幻读**（我读的时候没这一行，结果查完别人偷偷插入了一行），InnoDB 不仅锁住数据本身，还会锁住**数据之间的空隙**。

#### 在 finguard 项目中的作用

项目中的 `std::shared_mutex`（读写锁）就是最典型的锁机制应用：

```cpp
// 场景：配置缓存读取
std::shared_lock lock(config_mutex_); // 共享锁，大家一起读，快！

// 场景：配置加载更新
std::unique_lock lock(config_mutex_); // 排他锁，我更新时谁也别看，稳！
```

#### 总结

- 锁是用来解决**并发冲突**的。
- 越细的锁（行锁）性能越好，但越容易产生**死锁**。

#### 模拟面试自测

1. **面试官问：行级锁既然这么好，什么时候会退化成表锁？**
> 关键答案：**索引失效**。当 UPDATE 的 WHERE 条件没走索引时，MySQL 没法准确定位行，只能把整张表甚至全表范围扫一遍全都锁上。

---

### 49. 事务与 ACID（数据库的“契约精神”）

#### 详细讲解（小点拆解）

数据库事务必须满足 **ACID**，这四个字母决定了数据的可靠性：

| 字母 | 概念 | 大白话讲解 |
|-----|------|-----------|
| **A** | **原子性 (Atomicity)** | 像原子一样不可分割。要么全执行，要么全回滚。**靠 undo log 实现**。 |
| **C** | **一致性 (Consistency)** | 无论发生什么，数据必须从一个合法态变到另一个合法态（如转账总额不变）。 |
| **I** | **隔离性 (Isolation)** | 事务之间互不干扰。**靠锁 + MVCC 实现**。 |
| **D** | **持久性 (Durability)** | 只要提交了，哪怕数据库断电，重启后数据一定要在。**靠 redo log 实现**。 |

#### 在 finguard 项目中的作用

虽然项目不写库，但在**处理请求链路**时也有“事务思想”：
- **原子性**：一个 LLM 请求如果由于网络断开失败，必须清理全部中间状态，不能留下半截缓存。
- **一致性**：系统内部的并发计数器（如令牌桶余额）必须在各种并发下保持准确。

#### 总结

- 事务就是解决：**如果中途报错了数据烂一半怎么办**。

#### 模拟面试自测

1. **面试官问：ACID 里最核心的是哪个？**
> 答案：**一致性 (C)** 是目的，**A、I、D** 是手段。数据库通过原子性、隔离性和持久性来最终确保业务逻辑的一致。

---

### 50. 日志（redo/undo/binlog：备份与恢复）

#### 详细讲解（小点拆解）

这是 MySQL 的三大核心日志，面试常客：

| 日志 | 谁在记？ | 记什么？ | 大白话用处 |
|-----|--------|--------|-----------|
| **undo log** | InnoDB | 数据的反向操作（如记录“做了+1”，它就记“-1”） | **回滚**和 **MVCC**。错了能退回去。 |
| **redo log** | InnoDB | 磁盘物理页的改动（如“第10页加了个字”） | **断电恢复 (Crash-Safe)**。即使没来得及写盘，重启后能补回来。 |
| **binlog** | MySQL Server | 逻辑 SQL 操作（如“执行了 UPDATE user set...”） | **主从同步**和**数据恢复**。把操作录下来，发给从库回放。 |

#### 在 finguard 项目中的作用

我们的**结构化日志 (Structured Log)** 类似于 binlog 的简化版：
- 记录每个请求的 TraceID、API 名、耗时和结果。
- **作用**：不是为了数据恢复，而是为了**故障排查（审计）**。

#### 总结

- **redo/undo** 是 InnoDB 引擎用来保证“数据不丢、不错”的**底层保命日志**。
- **binlog** 是 MySQL 软件层用来实现“双机备份、同步”的**工作录像**。

#### 模拟面试自测

1. **面试官问：为什么有了 redo log 还要 binlog？**
> 关键答案：
> 1. redo log 是固定大小的**循环写**，旧的会被覆盖，没法用来恢复很久以前的数据；
> 2. binlog 记录的是所有 SQL 逻辑，不限大小，适合**全量数据恢复**和**主从同步**。

---

## 七、Redis

### 51. Redis 持久化（内存数据的“存档点”）

#### 详细讲解（小点拆解）

Redis 是内存数据库，如果不存盘，一关机就全没了。有两种办法：

**1. RDB (快照 —— 拍个全身照)**
- **原理**：每隔一段时间（如 5 分钟），把内存里的所有数据打个大包存进磁盘。
- **优点**：恢复速度快（直接读全量文件）。
- **缺点**：如果 4 分 50 秒时挂了，这 4 分多钟的数据就**全部丢了**。

**2. AOF (日志 —— 每天记日记)**
- **原理**：每执行一个写操作（如 `SET key val`），就追加到文件后面。
- **优点**：数据非常安全，最多丢 1 秒的数据。
- **缺点**：文件巨大，恢复时要一条条跑，**速度慢**。

#### 在 finguard 项目中的作用

这对应了服务器的 **Config 保存策略**：
- **静态配置**（如 `config.yaml`）类似于 RDB，一次性全部加载。
- 如果我们要记录每个请求的变更（Auditing），则类似于 AOF。

#### 总结

- **线上通常怎么做？** —— **RDB + AOF 同时开**。RDB 定期备份，AOF 记录增量。现代 Redis 甚至支持 AOF 混合 RDB，鱼和熊掌都要。

#### 模拟面试自测

1. **面试官问：RDB 备份时会阻塞请求吗？**
> 关键答案：Redis 会通过 `fork()` 创建一个**子进程**在后台慢慢写。由于 **Copy-on-Write（写时复制）** 机制，父进程可以继续处理读写请求而不受太大影响。

---

### 52. Redis 过期删除与淘汰（内存的“断舍离”）

#### 详细讲解（小点拆解）

数据设置了过期时间，Redis 怎么把它删掉？

1. **惰性删除**：只有你**访问**这个 key 的时候，它才检查过没过期。如果过期了就删掉。
   - *坏处*：要是这 key 以后再也不被访问，它会一直占着内存。
2. **定期删除**：每秒钟从过期的 key 里**随机抽点**检查。
   - *策略*：抽 20 个，如果过期的多（超过 1/4），就代表过期 key 很多，继续抽。

#### “爆护舱”了怎么办？—— 逐出策略 (Eviction)

如果内存真满了，新数据写不进去了，Redis 会触发逐出：
- **LRU (Least Recently Used)**：最近最少使用的删掉。**(面试最常考，见 §24)**。
- **LFU (Least Frequently Used)**：用得频率最低的删掉。

#### 在 finguard 项目中的作用

我们的 **P5 压测和基线分析** 重点之一就是**内存稳定性**：
- 需要监控 finguard 进程的 RSS 内存消耗。
- 如果配置缓存无限增大且不自动回收，系统就会 OOM（内存溢出）。

#### 总结

- 内存有限，必须要有一套“谁最没用就把谁踢走”的规则。

#### 模拟面试自测

1. **面试官问：Redis 的过期删除策略会不会影响性能？**
> 答案：定期删除是在后台执行的，会有 CPU 消耗；惰性删除是在用户请求时顺带做的。如果突然有海量数据同时过期，定期删除可能会短暂占用 CPU，所以**过期时间要加随机噪声打散**。

---

### 53-55. 缓存三大灾难：雪崩、击穿、穿透

这三兄弟是 Redis 面试的“送命题”，必须分清：

| 现象 | 大白话说明 | 解决方案 |
|-----|-----------|--------|
| **缓存雪崩** | **倒了一大片**。海量 key 同时过期，或者 Redis 挂了，全冲向数据库。 | 过期时间**加随机数**；做 Redis 集群高可用；后端加**限流熔断**。 |
| **缓存击穿** | **定点爆破**。一个超级大热点（如春晚明星）瞬间过期，流量把它后面数据库捅穿。 | 热点数据设置**永不过期**；或者在查库的时候加**互斥锁**。 |
| **缓存穿透** | **直接打空**。黑客请求数据库里根本不存在的数据，缓存肯定命不中，次次打到库。 | **布隆过滤器**（快速判断数据在不在）；**空值缓存**（存一个 "null" 堵住洞）。 |

#### 在 finguard 项目中的作用

我们在 **Phase 5 (性能优化)** 中实现的**令牌桶限流**和**并发计数器**：
- 正是为了防止类似“雪崩”情况发生时，由于流量过载导致整个后端（或 AI 模型调用）被击跨。
- **降级逻辑**：当 QPS 超过阈值，直接返回 `Server Busy`（空值或降级结果），这就是一种穿透保护。

#### 总结

- **雪崩**是整体性的，**击穿**是单个热点的，**穿透**是数据本身不存在的恶意攻击。

---

## 八、Linux 命令与工具

### 56. Linux 常用命令（程序员的“生存技能”）

作为后端开发，线上排查问题离不开这几板斧：

1. **找进程/查负载**：
   - `top` / `htop`：看谁最占 CPU 和内存。
   - `ps -ef | grep finguard`：看看我们的服务跑在哪个进程号上。
2. **查网络/端口**：
   - `ss -lntp | grep 8080`：谁占了我的 8080 端口？
   - `curl -v localhost:8080/v1/chat`：本地发个请求测试一下。
3. **搜日志/查文件**：
   - `tail -f logs/finguard.log`：实时盯着报错日志。
   - `grep -r "ERROR" .`：在当前目录下搜哪个文件报错了。
   - `find / -name "finguard"`：找文件在哪。

#### 在 finguard 项目中的作用

项目中提供的 `.ps1` 脚本（如 `run_acceptance.ps1`）本质上就是把这些 Linux/Windows 命令封装好了，自动化执行部署和测试。

#### 模拟面试自测

1. **面试官问：线上 CPU 飙高了，你怎么排查？**
> 1. `top` 找到 CPU 最高的进程 ID。
> 2. `top -Hp pid` 找到该进程下 CPU 最高的线程 ID。
> 3. `gstack` 或 `pstack` 打印该线程的调用栈，看看代码卡在哪一行。

---

### 57. CMake（C++ 编译的“乐谱”）

#### 详细讲解（小点拆解）

C++ 程序不是直接双击运行的，要经过编译。CMake 就是告诉编译器：**“把哪些源代码拼在一起，用什么参数，链接哪些库”**。

- **`CMakeLists.txt`**：这是乐谱。
- **`target_link_libraries`** : 我的程序（target）需要依赖哪些外援（比如 Drogon 框架、spdlog 日志库）。
- **`add_executable`** : 告诉它最后要生成的文件名（如 `finguard_core`）。

#### 在 finguard 项目中的作用

`finguard/CMakeLists.txt` 定义了整个项目的编译骨架：
- 启用 C++17 标准。
- 探测本地是否安装了 `vcpkg` 管理的依赖库。
- 将 `src/` 下的代码编译成可执行程序。

#### 总结

- CMake 让 C++ 项目在 Windows/Linux 下都能用同样的逻辑编出来，解决了工程复现问题。

---

### 58. GDB（程序员的“显微镜”）

#### 详细讲解（小点拆解）

程序崩了（Segment Fault），或者逻辑不对，你就得进调试器调试。

- **打断点 (break)**：程序跑到这儿停一下，我看看环境。
- **看变量 (print)**：现在这变量存的到底是啥？是不是空指针？
- **看调用栈 (backtrace / bt)**：**最重要的功能**。看看程序是怎么一步步运行到报错这一行的。
- **单步执行 (step/next)**：一行一行走，看能不能找出那只“虫子”。

#### 在 finguard 项目中的作用

在解决 **P5 并发性能问题** 时，我们经常遇到**竞态条件 (Race Condition)**：
- 如果加锁逻辑写错，程序可能死锁（卡住）。
- 通过 GDB 连上运行中的进程，执行 `thread apply all bt` 就可以看到每个线程卡在哪个 Mutex 上。

#### 总结

- 不会用调试器的人，永远成不了顶级后端。

---

## 最后冲刺清单

- C++：封装、继承、多态、C++11、智能指针、STL。
- 数据结构：链表、栈、队列、哈希表、二叉树。
- 算法：快排、归并、二分、递归、回溯、DP。
- 网络：OSI、HTTP/HTTPS、TCP/UDP、三握四挥、流控、拥控。
- Linux/OS：socket、I/O 多路复用、进程线程、系统调用、死锁、零拷贝、协程。
- MySQL：SQL、索引、MVCC、存储引擎、锁、事务、日志。
- Redis：持久化、过期删除、击穿穿透雪崩。
- 工具：Linux 命令、CMake、gdb。

[2026-03-11] 最终版重构：全文遵循 workbook “大白话教学”标准，剥离晦涩术语，深度绑定 finguard 项目实践。面向小白，直击校招/社招面试核心。🎉