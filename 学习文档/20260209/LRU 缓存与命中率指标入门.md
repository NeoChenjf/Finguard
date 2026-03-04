---
title: LRU 缓存与命中率指标入门
tags: [cache, lru, metrics]
last_updated: 2026-02-09
source: workbook
status: draft
---
# LRU 缓存与命中率指标入门

## 标题与目标
用最少概念理解 LRU 缓存策略与“命中率”如何衡量缓存价值。

## 学习路线 / 分层
1. 先理解“缓存”是把常用结果放近一点。
2. 再理解 LRU 是“最近用过的留下”。
3. 最后理解命中率如何反映效果与成本。

## 核心概念解释（含生活类比）
- 缓存：把常用数据放到“伸手就能拿到的抽屉”。
- LRU：抽屉里只放“最近使用过的物品”，很久没用就挪出去。
- 命中率：你伸手就拿到所需物品的比例。

## 关键流程 / 步骤
1. 读缓存：若命中，直接返回（记录命中）。
2. 未命中：去源头取数据（记录未命中）。这里的“源头”是缓存之外的真实来源，比如数据库、文件、远程服务或计算结果；类比为“主柜子/仓库”，不是另一个抽屉。
3. 写缓存：把新数据放进缓存；若满了，淘汰最久未用的条目。
4. 计算命中率：命中次数 / 总访问次数。

## 真实代码片段（C++ 简化示例）
```cpp
// 仅演示思路：list 维护使用顺序，map 做快速索引
struct LruCache { // 定义一个最简 LRU 缓存结构体
  size_t cap; // 缓存容量上限
  std::list<std::pair<std::string, std::string>> items; // 按“最近使用”排序的条目
  std::unordered_map<std::string, decltype(items.begin())> idx; // key 到 list 迭代器的索引
  size_t hit = 0, miss = 0; // 命中与未命中计数

  bool get(const std::string& k, std::string& v) { // 读取缓存
    auto it = idx.find(k); // 查索引
    if (it == idx.end()) { miss++; return false; } // 未命中：记录并返回失败
    items.splice(items.begin(), items, it->second); // 命中：移到最前
    v = it->second->second; // 取值返回
    hit++; return true; // 记录命中并返回成功
  } // get 结束

  void put(const std::string& k, const std::string& v) { // 写入缓存
    auto it = idx.find(k); // 查索引是否已存在
    if (it != idx.end()) { // 若存在，更新并提升为最近使用
      it->second->second = v; // 更新值
      items.splice(items.begin(), items, it->second); // 移到最前
      return; // 完成
    } // 已存在分支结束
    if (items.size() == cap) { // 如果满了就淘汰最久未用
      idx.erase(items.back().first); // 移除索引
      items.pop_back(); // 移除末尾条目
    } // 淘汰结束
    items.emplace_front(k, v); // 新条目放到最前
    idx[k] = items.begin(); // 建立索引
  } // put 结束

  double hit_rate() const { // 计算命中率
    auto total = hit + miss; // 访问总次数
    return total == 0 ? 0.0 : static_cast<double>(hit) / total; // 保护除零
  } // hit_rate 结束
}; // LruCache 结束
```

## 小结 + 自测问题
小结：LRU 适合“近期热点”明显的场景；命中率越高越省时省钱。

自测：
1. 为什么 LRU 要记录“最近使用顺序”？
2. 命中率 80% 代表什么含义？

自测题答案：
1. 为了淘汰“最久未用”的条目，必须知道最近使用顺序。
2. 表示 100 次访问中约 80 次命中缓存、20 次未命中。

## 下一步建议
- 学习缓存穿透/击穿/雪崩的基础概念。
- 结合业务评估缓存容量与 TTL 策略。

变更日志
- [2026-02-09] (手动新增) 新建 LRU 与命中率教学文档
