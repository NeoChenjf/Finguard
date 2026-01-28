---
title: 后端启动与轮询验收规则
tags: [automation, backend, acceptance]
last_updated: 2026-01-28
source: workbook
status: draft
---

# 后端启动与轮询验收规则

目的
- 提供一条可复现的、适用于本仓库的“后台启动并轮询验证”操作规则，供自动化 agent 与人工验收参考。

适用范围
- 本项目 `finguard` 的 C++ 后端可执行（`build/Debug/finguard.exe`），或其他类似需要后台运行并在外部验证 HTTP 接口的服务。

规则步骤（可自动化执行）
1. 准备临时目录用于日志与响应保存：

```powershell
New-Item -ItemType Directory -Path d:\AI_Investment\tools\temp -Force
```

2. 启动后端服务（后台）并重定向 stdout/stderr 到日志文件：

```powershell
$p = Start-Process -FilePath 'd:\AI_Investment\finguard\build\Debug\finguard.exe' -RedirectStandardOutput 'd:\AI_Investment\tools\temp\finguard_out.log' -RedirectStandardError 'd:\AI_Investment\tools\temp\finguard_err.log' -PassThru
Write-Output "PID:$($p.Id)"
```

3. 轮询健康检查直至服务就绪（超时与重试策略）：

```powershell
$ready = $false
for($i=0; $i -lt 20; $i++){
  try{
    $r = Invoke-RestMethod -Uri 'http://127.0.0.1:8080/health' -Method Get -TimeoutSec 1
    if($r.status -eq 'ok'){ $ready = $true; break }
  } catch {}
  Start-Sleep -Milliseconds 500
}
if(-not $ready){ Stop-Process -Id $p.Id -Force; throw 'SERVICE_NOT_READY' }
```

4. 调用待验收接口并把响应保存为文件（便于存证）：

```powershell
$health = Invoke-RestMethod -Uri 'http://127.0.0.1:8080/health' -Method Get
$plan   = Invoke-RestMethod -Uri 'http://127.0.0.1:8080/api/v1/plan' -Method Post -ContentType 'application/json' -Body '{"profile":{"age":35,"risk":"medium","capital":500000}}'
$health | ConvertTo-Json -Compress | Out-File d:\AI_Investment\tools\temp\health.json -Encoding utf8
$plan   | ConvertTo-Json -Compress | Out-File d:\AI_Investment\tools\temp\plan.json -Encoding utf8
```

5. 停止后台服务并收集日志：

```powershell
Stop-Process -Id $p.Id -Force
Get-Content d:\AI_Investment\tools\temp\health.json -Raw
Get-Content d:\AI_Investment\tools\temp\plan.json -Raw
Get-Content d:\AI_Investment\tools\temp\finguard_out.log -Raw
Get-Content d:\AI_Investment\tools\temp\finguard_err.log -Raw
```

验收判定（最小要求）
- `/health` 返回 `{ "status": "ok" }`。
- `/api/v1/plan` 返回预期 JSON（mock 或真实），并保存到 `tools/temp/plan.json`。
- 后端 stdout/stderr 日志文件存在，便于审计；若日志为空，则记录为空状态。

失败处理与诊断要点
- 若服务未就绪：检查可执行路径、端口占用、依赖是否在环境中（Drogon 等）、以及是否有权限问题（写日志目录权限）。
- 若接口调用超时或返回错误：查看 `finguard_err.log` 和 `finguard_out.log`，并把错误摘要写入阶段日志（`phase1.md`）。
- 自动化尝试失败时：将所有命令输出、错误信息与时间戳写入临时日志文件并暂停，等待人工复核。

日志与合规
- 所有自动化操作必须保存命令、时间戳、响应与日志文件路径，敏感信息（API Key）需遮蔽或不写入日志。

变更记录
- 2026-01-28: 初版，来源：自动化验收执行记录。