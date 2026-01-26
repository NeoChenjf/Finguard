---
name: project-plan-skill
description: "Generate product-oriented 项目书.md and next_plan.md handoff docs from a user-provided project blueprint; use when the user discusses a product project plan and wants ready-to-publish Chinese project documentation and a next-step handoff."
---

# 项目企划 Skill

## Overview

基于用户给出的项目蓝图，生成“上架产品”语气的中文项目书与交接用 next_plan 文档，确保下一位 agent 读完即可推进当前阶段任务。

## Workflow

1. 读取 `workbook.md`，遵循员工手册规则；如出现新约束，追加规则条目。
2. 解析用户蓝图，必要时仅补问关键缺口（如目标用户、核心模块、接口、交付标准）。
3. 生成 `项目书.md`，使用产品语气与中文输出，按参考模板组织章节。
4. 生成 `next_plan.md`，写清当前状态、下一阶段目标、具体任务、验收标准、未决问题与文件清单。
5. 文件保存为 UTF-8（带 BOM），避免中文乱码。
6. 输出简短变更说明与可选下一步。

## Output Requirements

- **语气**：面向上架产品与真实用户场景，避免面试/简历语气。
- **路线表述**：使用“第一阶段/第二阶段…”，不使用“第几天”。
- **文件名**：严格使用 `项目书.md` 与 `next_plan.md`。
- **内容完整**：项目书需覆盖定位、目标、用户场景、架构、模块、接口、技术栈、目录结构、交付要求、阶段路线、风险对策。
- **交接可执行**：next_plan 要让下一位 agent 读完即可执行任务。

## References

按需加载参考模板：

- `references/project_book_template.md`
- `references/next_plan_template.md`
