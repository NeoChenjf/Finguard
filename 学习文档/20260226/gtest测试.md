---
title: gtest 测试与回归守护入门（Phase 6 语境）
tags: [tutorial, phase6, gtest, regression, lru]
last_updated: 2026-02-26
source: phase6.md
status: draft
---

# gtest 测试与回归守护入门（Phase 6 语境）

## 标题与目标

目标：弄懂 `placeholder`、`EXPECT_TRUE(true)` 占位符、为什么 Phase 7-9 前后端对接会频繁变化、什么是“回归/守护”，并重新理解 LRU 缓存。

## 学习路线/分层

1. 先理解“占位测试”是什么。
2. 再理解为什么后续阶段容易改动接口。
3. 再理解“回归”与“测试守护”。
4. 最后把 LRU 缓存和你项目里的缓存场景对上。

## 核心概念解释（带类比）

- `placeholder`：占位符。  
  在测试里，指“先把测试文件和测试套件结构搭起来”，但断言还没测真实业务。

- `EXPECT_TRUE(true)` 占位符有什么用：  
  - 验证 gtest 工程能编译、能被 `ctest` 发现、能跑通。  
  - 先打通“测试管线”，避免一开始就被构建问题卡住。  
  - 但它几乎不验证业务正确性，所以只能短期存在。  
  类比：先装一个“通电灯泡”确认线路通了，但它不代表整套家电都正常。

- 为什么 Phase 7-9 会频繁改动前端对接逻辑：  
  - Phase 7 从“无前端”变为“Vite+React 真页面”，API 调用细节会快速迭代。  
  - 设置页、流式聊天页落地时，字段、错误码、Header、SSE 事件格式常会调整。  
  - Phase 8/9 引入桌面壳与打包后，启动方式、配置路径、异常处理也会影响前后端交互。  
  所以前端对接期是“高变化期”，后端需要测试兜底。

- “防止回归”里的两个词：  
  - `回归`：原来能用的功能，被新改动弄坏了。  
  - `守护`：用自动化测试在每次改动后快速报警。  
  类比：回归像“修厨房时把客厅电路弄断”，守护像“每次施工后自动全屋通电检查”。

- `LRU 缓存`（Least Recently Used）：最近最少使用淘汰。  
  当缓存满了，优先淘汰“最长时间没被访问”的项。  
  典型价值：在固定内存下尽量保留“近期热点数据”。

## 关键流程/步骤（可复刻）

1. **占位阶段**：先写 1 个 `EXPECT_TRUE(true)`，确认测试框架可运行。  
2. **替换阶段**：逐步替换为“有输入、有断言”的实质测试。  
3. **守护阶段**：每次改接口都跑 `ctest --output-on-failure`。  
4. **缓存阶段**：对缓存写命中、淘汰、TTL 过期等关键行为测试。  

## 真实代码片段（逐行注释）

### 1) 占位测试长什么样

```cpp
TEST(Rules, Placeholder) {             // 定义一个名为 Placeholder 的测试用例
    EXPECT_TRUE(true);                 // 断言恒为真：只验证“测试框架可运行”
}                                      // 结束测试
```

说明：它的价值是“打通管线”，不是“验证业务”。

### 2) 实质测试示例（来自你项目现有风格）

```cpp
TEST(RulesEngine, ForbiddenKeywordTriggersWarning) {   // 测试：禁词应触发告警
    finguard::risk::RuleEngine engine;                 // 创建规则引擎实例
    std::string error;                                 // 接收加载错误信息
    engine.load_config(&error);                        // 加载 rules.yaml

    Json::Value questionnaire;                         // 构造问卷输入
    questionnaire["investor_profile"] = "experienced";// 设置投资者画像
    questionnaire["age"] = 35;                         // 设置年龄

    auto result = engine.check_request("我想投资博彩行业", questionnaire); // 输入含禁词 prompt
    ASSERT_FALSE(result.warnings.empty());             // 先断言告警列表非空

    bool found = false;                                // 标记是否命中目标禁词
    for (const auto &w : result.warnings) {            // 遍历告警
        if (w.find("博彩") != std::string::npos) {     // 查找是否含“博彩”
            found = true;                              // 找到则置 true
            break;                                     // 结束循环
        }
    }
    EXPECT_TRUE(found);                                // 最终断言：必须命中禁词告警
}
```

### 3) LRU 最小示意（帮助你理解“淘汰最近最少使用”）

```cpp
// 下面是概念示意，不是你项目现成实现
put("A"); put("B"); put("C");        // 容量=3，当前缓存: A B C
get("A");                            // 访问 A 后，A 变“最近使用”
put("D");                            // 插入 D 时超容量，需要淘汰 1 个
// 被淘汰的是 B（最久未使用），A 因刚访问过被保留
```

按时间线：

1. put("A"), put("B"), put("C")

   - 最近使用顺序（新→旧）通常是：C, B, A

2. get("A")

   - A 被再次访问，变成最新
   - 顺序变成：A, C, B

3. put("D") 

   超容量要淘汰最旧

   - 最旧的是 B，所以淘汰 B

## 小结 + 自测题（含答案）

小结：
- `EXPECT_TRUE(true)` 是“测试管线占位”，不是业务验证。
- Phase 7-9 属于前端/桌面集成高变化期，接口细节容易调整。
- 回归=旧功能被新改动弄坏；守护=用自动化测试持续拦截回归。
- LRU 的核心是“容量有限时，优先留住近期热点”。

自测题：
1. 为什么 `EXPECT_TRUE(true)` 不能长期作为主测试？  
2. Phase 7-9 哪类变化最容易引发后端回归？  
3. “回归守护”在工程里最常见的落地动作是什么？  
4. 容量 3 的 LRU 中，`A B C` 后访问 `A`，再插入 `D`，谁会被淘汰？  

答案：
1. 因为它不检验真实业务输入输出，无法发现逻辑错误。  
2. API 字段、Header、错误码、SSE 事件格式和配置生效路径的变化。  
3. 每次提交/改动后自动跑单元测试并阻止失败版本进入主线。  
4. `B`。  

## 下一步建议

1. 我可以继续给你写一份“你项目 5 类 gtest 用例模板清单（按文件名）”。  
2. 你也可以先读现有用例：`finguard/tests/test_rules.cpp`、`finguard/tests/test_cache.cpp`。  

变更日志
- [2026-02-26] (手动更新) 新建 gtest 教学文档，解释 placeholder / 回归守护 / LRU 与 Phase 7-9 关系。
