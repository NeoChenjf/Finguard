---
title: 2026-02-06 学习总览：profile_store 问卷档案读写
tags: [docs, tutorial, profile, questionnaire, risk]
last_updated: 2026-02-06
source: user request
status: draft
---
# 2026-02-06 学习总览：profile_store 问卷档案读写

## 标题与目标

读完这份文档，你能说清 `profile_store` 的作用、文件存放位置、读写流程与常见错误路径。

## 整体学习路线

1. 入口函数与调用场景（路由层如何使用）。
2. 文件位置与数据结构（profiles.json）。
3. 写入流程（upsert_profile）。
4. 读取流程（load_profile）。
5. 典型错误与告警触发。

## 核心概念解释（带类比）

把 `profile_store` 当作“前台登记本”。
- 写入（upsert）像“登记并更新联系人信息”。
- 读取（load）像“按用户 ID 查找登记信息”。

## 关键流程与步骤

### 1) 代码入口与文件位置

- 代码文件：`finguard/src/risk/profile_store.h`、`finguard/src/risk/profile_store.cpp`。
- 数据文件：`<cwd>/config/profiles.json`，实际路径由 `profile_path()` 计算。
- 调用入口：
  - `/api/v1/profile/upsert` 写入问卷档案。
  - `/api/v1/chat/stream` 读取问卷档案。

### 2) 数据结构（profiles.json）

- 根对象必须是 JSON object。
- 结构示意：

```json
{
  "version": 1,
  "users": {
    "user_123": {
      "questionnaire": {
        "investor_profile": "novice",
        "age": 30,
        "individual_stock_percent": 0.2
      }
    }
  }
}
```

### 3) 写入流程（upsert_profile）

触发点：`/api/v1/profile/upsert`。

步骤：
1. 校验 `user_id` 是否为空。
2. 校验 `questionnaire` 是否为对象。
3. 加载现有 profiles（不存在则初始化空结构）。
4. `users[user_id].questionnaire = questionnaire` 写入。
5. 保存回 `profiles.json`。

### 4) 读取流程（load_profile）

触发点：`/api/v1/chat/stream`。

步骤：
1. 校验 `user_id` 是否为空。
2. 读取 profiles.json 并解析。
3. 校验 `users` 是否为对象，且包含 `user_id`。
4. 提取 `questionnaire` 并返回。

### 5) 典型错误与告警

- `missing_user_id`：请求没带 `X-User-Id`。
- `profiles_open_failed` / `profiles_parse_failed`：文件无法读取或 JSON 解析失败。
- `profile_not_found`：profiles.json 中没有该用户。
- `profile_missing_questionnaire`：用户存在，但缺少 questionnaire 字段。

在 `/api/v1/chat/stream` 中，如果读取失败，会追加 `profile_missing_or_unreadable` 或 `profile_error:...` 告警。

## 真实代码片段（节选）

```cpp
// profile_store.cpp: 写入问卷档案
root["users"][user_id]["questionnaire"] = questionnaire;
```

```cpp
// profile_store.cpp: 读取问卷档案
*questionnaire = entry["questionnaire"];
```

## 小结

- profile_store 负责将问卷档案持久化到本地 JSON 文件。
- 写入发生在 upsert 接口，读取发生在 chat 流接口。
- 读取失败不会阻断聊天，但会追加告警，便于风控记录。

## 自测问题

1. profiles.json 的顶层结构包含哪些字段？
2. 为什么写入前要先 load_profiles？
3. 读取失败时路由层会怎么处理？

## 自测问题答案

1. `version` 与 `users`。
2. 为了读取已有用户并保持结构一致，同时处理缺失文件的初始化。
3. 不阻断主流程，追加 `profile_missing_or_unreadable` 或 `profile_error` 的 warning。

## 下一步建议

- 用 `curl` 或 Postman 调用 `/api/v1/profile/upsert` 写入一条档案。
- 然后调用 `/api/v1/chat/stream`，观察 warning 事件的变化。

变更日志
- [2026-02-06] (手动更新) 新建 profile_store 教学文档。