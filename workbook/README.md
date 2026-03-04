---
title: Workbook 员工手册 — 总入口
tags: [index, governance]
last_updated: 2026-02-25
status: published
---

# Workbook — agent 员工手册

本目录是 agent 的行为规范与操作手册。每次新会话开始工作前，先阅读本 README，再按需打开对应文档。

## 文档目录

| 文件 | Skill 定位 | 一句话说明 |
|------|-----------|-----------|
| [role_and_principles.md](role_and_principles.md) | 身份·行为·沟通 | agent 是谁、怎么做事、怎么和用户沟通 |
| [automation.md](automation.md) | 后端运维自动化 | finguard C++ 后端的启动、检测、验收与故障排查 |
| [work_log_and_tracking.md](work_log_and_tracking.md) | 阶段文档·工作追踪 | phase 文档四章结构、工作日志、交接、Spec 流程 |
| [docs_guidelines.md](docs_guidelines.md) | 文档编写·教学·格式 | 文档结构、编码、教学规范、编辑防重复规则 |
| [experience_rules.md](experience_rules.md) | 开发经验速查 | 项目特有的开发陷阱与最佳实践 |

## 使用方式

1. 阅读本 README，了解有哪些 Skill 文档。
2. 按当前任务打开相关文档，逐条遵守。
3. 操作完成后，将结果写入 `phase{N}.md` 或 `next_plan.md`。

## 新增规则流程

当用户要求"将解决过程抽象成规则加入 workbook"时：

1. **归类**：根据规则主题，选择目标文件（参照上方目录表）。若无匹配类目，新建 `slug.md` 并在本 README 注册。
2. **去重**：检查目标文件是否已有语义相同的条目。若已存在则跳过；若相似但不完全相同，标注"需复核"。
3. **写入**：在目标文件的合适位置追加新条目，包含：规则描述、适用场景、示例、风险/注意事项。
4. **变更日志**：在目标文件末尾追加 `[YYYY-MM-DD] (自动加入|需复核) 来源摘要`。
5. **回报**：向用户回复变更摘要。

---

变更日志
- [2026-02-25] 全面整理重构：消除跨文档重复、删除过时内容、合并 backend_runtime_rules 入 automation、精简 README。
