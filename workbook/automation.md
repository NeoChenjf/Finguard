---
title: 后端运维自动化
tags: [automation, backend, finguard]
last_updated: 2026-02-25
source: workbook
status: published
---

# 后端运维自动化

本文件规定 finguard C++ 后端的启动、健康检查、验收与故障排查的标准操作流程。

---

## 1. 启动 finguard 后端

### 1.1 Debug 构建启动

```powershell
# 确保已在项目根目录
$p = Start-Process `
  -FilePath 'd:\AI_Investment\finguard\build\Debug\finguard.exe' `
  -WorkingDirectory 'd:\AI_Investment\finguard\build' `
  -RedirectStandardOutput 'd:\AI_Investment\tools\temp\finguard_out.log' `
  -RedirectStandardError 'd:\AI_Investment\tools\temp\finguard_err.log' `
  -PassThru
Write-Output "PID:$($p.Id)"
```

### 1.2 Release 构建启动

```powershell
$p = Start-Process `
  -FilePath 'd:\AI_Investment\finguard\build\Release\finguard.exe' `
  -WorkingDirectory 'd:\AI_Investment\finguard\build' `
  -RedirectStandardOutput 'd:\AI_Investment\tools\temp\finguard_out.log' `
  -RedirectStandardError 'd:\AI_Investment\tools\temp\finguard_err.log' `
  -PassThru
Write-Output "PID:$($p.Id)"
```

### 1.3 前置准备

- 确保 `tools/temp/` 目录存在：`New-Item -ItemType Directory -Path d:\AI_Investment\tools\temp -Force`
- 确保 `build/config/` 目录下配置文件为无 BOM 的 UTF-8（YAML 解析器不兼容 BOM）。

## 2. 健康检查与轮询

服务启动后，轮询 `/health` 确认就绪：

```powershell
$ready = $false
for ($i = 0; $i -lt 20; $i++) {
    try {
        $r = Invoke-RestMethod -Uri 'http://127.0.0.1:8080/health' -Method Get -TimeoutSec 1
        if ($r.status -eq 'ok') { $ready = $true; break }
    } catch {}
    Start-Sleep -Milliseconds 500
}
if (-not $ready) { Stop-Process -Id $p.Id -Force; throw 'SERVICE_NOT_READY' }
```

## 3. 验收操作

### 3.1 接口验收（保存响应为存证）

```powershell
# Health
Invoke-RestMethod -Uri 'http://127.0.0.1:8080/health' -Method Get |
  ConvertTo-Json -Compress | Out-File d:\AI_Investment\tools\temp\health.json -Encoding utf8

# Plan
$planBody = '{"profile":{"age":35,"risk":"medium","capital":500000}}'
Invoke-RestMethod -Uri 'http://127.0.0.1:8080/api/v1/plan' `
  -Method Post -ContentType 'application/json' -Body $planBody |
  ConvertTo-Json -Compress | Out-File d:\AI_Investment\tools\temp\plan.json -Encoding utf8
```

### 3.2 最小验收判定

- `/health` 返回 `{ "status": "ok" }`。
- `/api/v1/plan` 返回有效 JSON（mock 或真实）。
- stdout/stderr 日志文件存在（可为空，但须存在）。

## 4. 停止服务

```powershell
Stop-Process -Id $p.Id -Force
```

停止后收集日志：

```powershell
Get-Content d:\AI_Investment\tools\temp\finguard_out.log -Raw
Get-Content d:\AI_Investment\tools\temp\finguard_err.log -Raw
```

## 5. 故障排查

| 现象 | 可能原因 | 排查步骤 |
|------|---------|---------|
| 服务未就绪（轮询超时） | 端口占用、可执行文件路径错误、依赖缺失 | 检查 8080 端口占用 `netstat -ano \| findstr 8080`；确认 exe 路径；查看 stderr 日志 |
| 接口超时/错误 | LLM 配置缺失、网络不通、API Key 无效 | 查看 `finguard_err.log`；确认 `config/llm.json` 配置正确 |
| YAML 解析失败 | 配置文件含 UTF-8 BOM | 用 `[System.IO.File]::WriteAllText(path, content, [System.Text.UTF8Encoding]::new($false))` 写无 BOM 文件 |
| LNK1168 链接失败 | finguard.exe 被占用 | 停止运行中的进程后重新构建 |

## 6. 自动化约束

- 所有自动化操作必须写入日志（命令、时间戳、结果）。
- 日志中敏感信息（API Key 等）必须脱敏。
- 自动化启动失败时，将诊断信息写入 `tools/temp/` 并暂停等待人工复核。

---

变更日志
- [2026-02-25] 全面整理：合并原 automation.md 与 backend_runtime_rules.md；移除过时 Python/uvicorn 内容；更新为 finguard C++ 后端。
