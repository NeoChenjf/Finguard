# Engineering quick reference

## General implementation rules

- Before changing a module, read the related code and understand the interface, call chain, error handling, and current abstractions.
- Keep each change as small as possible; prefer additive changes over invasive rewrites; preserve backward compatibility.
- Follow single responsibility: do not mix HTTP routing, business logic, and utility code in one file.

## Backend service and testing

- Separate service startup from API testing so tests do not depend on repeatedly starting and stopping the full service.
- Prefer test clients or focused harnesses over launching the full server for every test.

## API key, auth, and logging

- After changing keys, restart the service or use the supported dynamic update path.
- For 401 or 502 errors, first check key validity, quota, and whether configuration actually reloaded.
- Prefer structured logging with trace id, time, and severity; mask sensitive fields.

## Config, build, and benchmarking

- YAML must be UTF-8 without BOM.
- After changing `config/`, verify `build/config/` is updated.
- Before benchmarking, relax rate limits so 429 responses do not distort results.
- Validate each optimization independently and run a regression benchmark so the change stays reversible.

## Frontend (React + Vite + TypeScript)

- If an interactive CLI blocks automation, feed input through the shell to skip the prompt.
- When frameworks change versions, check the official migration path instead of assuming old config still applies.
- For CSS and frontend syntax failures, inspect bracket and semicolon balance line by line.
- In development, prefer Vite `proxy` for same-origin requests; when cross-origin is real, handle OPTIONS preflight correctly.
- Send optional headers such as `X-API-Key` only when a value exists.
- Sync global frontend state immediately after successful writes so the UI matches real configuration.
- Use tolerances for floating-point threshold checks.
- Prefer explicit allowlists over fuzzy matching for auto-classification.
- Collapse secondary information by default and reveal it progressively in dense forms.

## Tauri package acceptance

- A passing `tauri dev` flow does not prove the packaged app is correct.
- In Tauri v2, detect `__TAURI_INTERNALS__`, not `__TAURI__`.
- Packaged apps hitting `http://127.0.0.1:8080` trigger real cross-origin behavior and real OPTIONS preflight.
- In Drogon, handle OPTIONS preflight in `registerPreRoutingAdvice`, not `registerPreHandlingAdvice`.
- After rebuilding an installer, check at least: sidecar process, `/health`, OPTIONS 204, core page submission flow, and clean shutdown without leftover processes.

## External APIs (Yahoo and search)

- Yahoo and similar services often require a full browser User-Agent.
- If the response format is uncertain, default `Accept` to `*/*` and only request JSON when the endpoint is known to return JSON.
- Some Yahoo fields live in different modules, so missing values may require fallback calculation.
- If the network exits through mainland China, direct Yahoo access may be blocked; do not misdiagnose that as an application bug.

## Code organization for FinGuard

- Keep clear boundaries such as `core/`, `server/`, and `util/`.
- Separate rule interfaces, concrete rule implementations, factory logic, HTTP routes, and utility functions into focused files.
- Before finishing a change, check:
  - Did I understand the existing design first?
  - Is the change minimal?
  - Does each file have one clear responsibility?
  - Is backward compatibility preserved?
  - Did I add the necessary tests?
  - Did I avoid unnecessary duplication?
