# finguard backend automation

## Scope

Use this reference only for tasks that explicitly involve starting, validating, or troubleshooting `D:\AI_Investment\finguard`.

## Preparation

- Ensure `D:\AI_Investment\tools\temp\` exists.
- YAML files under `build/config/` must be UTF-8 **without BOM**, or the parser may fail or silently fall back.

## Start commands

### Debug

```powershell
$p = Start-Process `
  -FilePath 'd:\AI_Investment\finguard\build\Debug\finguard.exe' `
  -WorkingDirectory 'd:\AI_Investment\finguard\build' `
  -RedirectStandardOutput 'd:\AI_Investment\tools\temp\finguard_out.log' `
  -RedirectStandardError 'd:\AI_Investment\tools\temp\finguard_err.log' `
  -PassThru
Write-Output "PID:$($p.Id)"
```

### Release

```powershell
$p = Start-Process `
  -FilePath 'd:\AI_Investment\finguard\build\Release\finguard.exe' `
  -WorkingDirectory 'd:\AI_Investment\finguard\build' `
  -RedirectStandardOutput 'd:\AI_Investment\tools\temp\finguard_out.log' `
  -RedirectStandardError 'd:\AI_Investment\tools\temp\finguard_err.log' `
  -PassThru
Write-Output "PID:$($p.Id)"
```

## Readiness polling

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

## Minimum acceptance

- `GET /health` returns `{ "status": "ok" }`
- `POST /api/v1/plan` returns valid JSON
- stdout and stderr log files exist

## Stop and collect logs

```powershell
Stop-Process -Id $p.Id -Force
Get-Content d:\AI_Investment\tools\temp\finguard_out.log -Raw
Get-Content d:\AI_Investment\tools\temp\finguard_err.log -Raw
```

## Common failure patterns

- Service not ready: check port conflicts, executable path, and stderr logs.
- 401, 502, or timeout: check API key validity, quota, and `config/llm.json`.
- YAML parse failure: check for BOM.
- `LNK1168`: the executable is still running; stop it before rebuilding.

## Automation constraints

- Keep command, time, result, and log location for every automation run.
- Mask sensitive values in logs.
- If automation fails, gather diagnostics first, then stop and report clearly.
