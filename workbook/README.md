# workbook - 总领文档（agent 请先阅读此文件）

本 README 是 `workbook` 目录的权威入口。后续 agent 在执行与参照本目录内规则时，应当先阅读本文件，然后按指示打开相应 md 文档并遵循其中条目。

目录说明：

- 自动化操作约定：[workbook/automation.md](workbook/automation.md) —— 包含启动后端的标准命令、失败诊断与日志要求。
- 角色与原则：[workbook/role_and_principles.md](workbook/role_and_principles.md) —— 定义 agent 身份、行为守则、授权与权限管理。
- 经验规则与最佳实践：[workbook/experience_rules.md](workbook/experience_rules.md) —— 列出避免常见错误的经验条目（后端/测试分离、API Key 处理、日志规范等）。
- 工作日志与跟踪：[workbook/work_log_and_tracking.md](workbook/work_log_and_tracking.md) —— 说明工作日志格式、阶段文档与交接规范、`manage_todo_list` 使用说明。
 - 文档与教学规范：[workbook/docs_guidelines.md](workbook/docs_guidelines.md) —— 编写/合并教学与规则文档的格式、元数据、变更日志与发布流程。
 - 后端运行与轮询验收规则：[workbook/backend_runtime_rules.md](workbook/backend_runtime_rules.md) —— 自动化 agent 启动后端、轮询 `/health` 并采集验收证据的操作规则（2026-01-28 初版）。

使用说明（对 agent）：

1. 每次需要遵守 `workbook` 中规则时，先打开并阅读本 README。随后打开相关文档逐条遵守。
2. 文档内容为可执行规范，非仅指示；agent 在执行时应直接参照文档条目，并在操作后写入相应日志或 `next_plan.md`/`phase{N}.md`。
3. 若文档需要更新或补充，提交修改并在 `next_plan.md` 记录变更原因与验证步骤。

加入规则指南（当用户要求自动加入新规则时使用）

当用户说：“按照 README 当中的规则将我们刚刚解决问题的过程抽象成规则，加入 workbook 当中”时，agent 应按下列流程无误地将规则加入 `workbook`：

步骤概览：
- 1) 读取本文件：先读取 `workbook/README.md`，获得类目映射（哪些文件对应哪些主题）。
- 2) 抽取候选规则：从“问题解决过程”文本中提取要点，生成一条或多条候选规则（包含标题、要点、示例命令/代码、风险说明与 tags）。
- 3) 归一化与元数据：为每条候选规则生成文件级元数据（YAML）：
	```yaml
	---
	title: 自动化后端启动失败诊断
	tags: [automation, backend]
	last_updated: 2026-01-27
	source: 会话摘要或问题 id
	status: draft
	---
	```
- 4) 去重与相似度判断：对比目标类目已有内容，采用语义相似度判断（embedding+余弦相似度优先）：
	- 相似度 >= 0.85：视为已存在（跳过新增），但在原文件末尾追加一条变更记录（说明已被识别为重复）。
	- 0.70 <= 相似度 < 0.85：标记为“待人工复核”，在目标文件追加草稿条目并在摘要中注明需复核。
	- 相似度 < 0.70：视为新规则，直接加入对应类目文件或新建类目文件（见下）。

- 5) 插入位置与文件选择：
	- 若 README 中存在与候选规则匹配的类目（基于 tags 或类目关键词），将规则加入该类目对应的 `workbook/*.md` 文件。
	- 若无匹配类目，则新建文件（文件名使用 `yyyy-mm-dd-slug.md` 或 `slug.md`），并在 `workbook/README.md` 的目录说明中注册该新文件（一行概述 + 链接）。

- 6) 变更记录要求：
	- 每次自动或手动加入/修改规则，必须在目标 md 文件末尾追加一条变更日志，格式：`[YYYY-MM-DD] (自动加入|自动匹配|需复核) 来源摘要`。
	- 若涉及高风险/权限类规则，务必设置 `requires_manual_review: true` 并不要自动执行相应操作。

- 7) 文件头与格式要求：
	- 每个 `workbook/*.md` 文件顶部保留 YAML 元数据块（见上）并包含 `tags` 与 `last_updated` 字段。
	- 文档主体须包含：规则描述、适用场景、示例（命令或代码）、风险/注意事项、变更日志。

- 8) 灰度区（人工复核）：当候选规则与现有内容相似但不完全相同时，agent 应将条目以草稿形式追加到目标文件并在变更日志中标注“需复核”，同时向用户报告并等待人工确认后再标记为正式条目。

示例：
- 用户对话摘要："我们在本次调试中发现：若 uvicorn 启动时报错 'app 未定义'，通常是因为模块路径或工作目录不正确。最佳启动流程是先 cd 到 backend 并激活虚拟环境。"
- agent 抽取结果（新增至 `workbook/automation.md`）：
	- 标题：后端启动常见错误与修复（app 未定义）
	- 描述：说明原因、解决步骤
	- 示例命令：PowerShell 启动片段
	- 变更日志：`[2026-01-27] (自动加入) 来源：会话 2026-01-27 调试记录`

提交与记录：
- agent 在完成添加/标注后，应在会话中回复简短变更摘要，并写入 `next_plan.md` 或生成 commit message 草稿供用户确认（示例 commit message："docs(workbook): add automation rule - backend start diagnostics").

审计与回滚：
- 所有自动变更应保留变更日志，用户可要求回滚；agent 应提供回滚命令或说明如何手动撤销该变更。

自动/半自动判断的阈值与建议：
- 语义相似度（embedding+余弦）：新增阈值 0.85（重复）、0.70（复核）
- 文本相似备用方法：Jaccard 或 fuzzy ratio，阈值可设为 0.80（备用）

说明：遵循以上流程，用户以后只需下达一句话（如题），agent 即可按 README 规则自动抽取并将新规则合入 `workbook`，或在需要人工判断时报告并等待确认。
