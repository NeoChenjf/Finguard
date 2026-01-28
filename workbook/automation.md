---
title: 自动化操作约定
tags: [automation]
last_updated: 2026-01-27
source: workbook
status: draft
---

# 自动化操作约定

目的
- 确保在项目开发与调试过程中，后端服务的启动、检测与故障处理可被 agent 安全、可追溯地自动化。自动化以“可复现、可回滚、可追溯”为首要目标。

启动约定（后端）
- 在检测到后端未运行时，优先在 `backend` 目录下尝试启动；确保当前工作目录为 `backend`，以避免导入路径或相对引用出错。

推荐启动命令（PowerShell）：
```powershell
cd backend
& ..\.venv\Scripts\Activate.ps1
python -m uvicorn main:app --host 127.0.0.1 --port 8000 --reload
```

注：`--reload` 仅用于开发阶段，生产环境应使用稳定部署流程。

---

### 示例规则：后端启动常见错误与修复（app 未定义）
```yaml
---
title: 后端启动常见错误与修复（app 未定义）
tags: [automation, backend, diagnostics]
last_updated: 2026-01-27
source: 会话 2026-01-27 调试记录
status: draft
---
```

规则说明
- 描述：当使用 `uvicorn` 启动后端时报错 `NameError: name 'app' is not defined` 或类似错误时，通常原因是启动命令的工作目录不正确或模块路径不一致，导致 `main:app` 无法被正确解析。
- 适用场景：开发环境本地调试、虚拟环境已创建但未激活、存在相对导入的项目结构。

解决步骤
1. 确保在项目根的 `backend` 目录内执行启动命令：
   ```powershell
   cd backend
   & ..\.venv\Scripts\Activate.ps1
   python -m uvicorn main:app --host 127.0.0.1 --port 8000 --reload
   ```
2. 若仍报 `app 未定义`：
   - 检查 `main.py` 中是否存在 `app = FastAPI()` 或正确导出的变量名。
   - 检查是否存在相对导入或包路径问题，尝试在 `backend` 目录下以模块方式运行（例如 `python -c "from main import app; print(app)"`）来验证导入。
   - 确认虚拟环境已激活且依赖已安装（`pip install -r requirements.txt`）。

风险与注意事项
- 不要在未经确认的情况下修改源码以“修复”导入，若需要改动应记录变更并等待人工确认。

变更日志
- [2026-01-27] (自动加入) 来源：会话 2026-01-27 调试记录

启动失败处理
- 自动化启动失败时，agent 应自动收集启动输出并尝试识别常见错误（示例：`app` 未定义、端口占用、依赖缺失、环境变量缺失）。
- 对于可安全自动修复的问题，按文档给出明确修复建议并记录尝试的操作；对不安全或高权限改动，暂停并将诊断与建议呈报用户。

日志与可追溯性
- 每次自动化操作必须写入日志，日志包含：执行命令、时间戳、摘要输出、是否成功、若失败则附错误摘要与建议步骤。
- 日志必须脱敏敏感信息（API Key、密码等不可明文写入）。

权限与风控
- 自动化操作前需记录用户是否已授权自动化权限；用户可随时撤销。
- 禁止自动执行高风险命令（如远程下载并执行、系统级删除、格式化磁盘等）。

注记
- 若后端代码中存在启动依赖（如相对导入），请在启动前确保虚拟环境与工作目录正确。
