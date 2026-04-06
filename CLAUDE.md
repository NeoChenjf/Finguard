# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

AI_Investment (FinGuard) is a comprehensive investment research platform combining fundamental analysis, asset allocation, LLM-powered insights, and a Tauri-based desktop frontend. The backend is a C++ HTTP server (Drogon framework) with SQLite-based financial data storage, while the frontend is React + TypeScript.

## Build & Development Commands

### Backend (finguard/)

**Build:**
```bash
# Configure with tests enabled
cmake -S ./finguard -B ./finguard/build -DBUILD_TESTS=ON

# Build main executable and tests
cmake --build ./finguard/build --config Release --target finguard finguard_tests
```

**Run:**
```bash
# Start HTTP server (default port 8080)
./finguard/build/Release/finguard.exe
```

**Test:**
```bash
# Run all unit tests
./finguard/build/Release/finguard_tests.exe

# Run specific test suite
./finguard/build/Release/finguard_tests.exe --gtest_filter="FundamentalsDbTest.*"
```

**Dependencies:** Managed via vcpkg (Drogon, nlohmann_json, spdlog, GTest). The `tools/vcpkg/` directory contains the local vcpkg installation.

### Frontend (frontend/)

**Development:**
```bash
cd frontend
npm install
npm run dev              # Vite dev server
npm run tauri:dev        # Tauri dev mode with hot reload
```

**Build:**
```bash
npm run build            # Build web assets
npm run tauri:build      # Build Tauri desktop app installer
```

**Linting:**
```bash
npm run lint
```

### Database Management

**Initialize/seed fundamentals database:**
```bash
python finguard/scripts/seed_fundamentals_db.py
```

**Generate demo databases:**
```bash
python finguard/scripts/build_all_demo_dbs.py
```

## Architecture

### Backend Structure

**Core Modules:**
- `src/server/` - HTTP server and route handlers (Drogon-based)
  - `http_server.cpp` - Server initialization
  - `routes.cpp` - Route registration
  - `*_routes.cpp` - Modular route handlers (health, chat, valuecell, system, plan, profile, allocation)
- `src/llm/` - LLM client abstraction with curl fallback
  - Supports streaming responses via SSE
  - Config-driven (config/llm.json)
  - Automatic fallback to curl.exe when Drogon HTTP client fails
- `src/valuation/` - Value Cell quantitative analysis
  - `fundamentals_db_client.cpp` - SQLite fundamentals data access
  - `yahoo_finance_client.cpp` - Real-time market data fetching
  - `analysis_workflow.cpp` - Orchestrates valuation analysis
  - `safety_margin.cpp` - Safety margin calculations (PE, PEG)
- `src/data/` - SQLite fundamentals database layer
  - Schema: `sql/fundamentals_schema.sql`
  - Profile system: main + demo databases (tcom_demo, brk_b_demo, pdd_demo)
- `src/core/` - Asset allocation rule engine
  - `allocation_rule.h` - Base class for allocation strategies
  - `rules/shouzhe_rule.cpp` - "守拙" (conservative) allocation strategy
  - `rules/fixed_rules.cpp` - Fixed allocation strategies
  - `rule_factory.cpp` - Factory pattern for rule instantiation
- `src/risk/` - Risk management and profile storage
  - `rule_engine.cpp` - Request/response validation rules
  - `profile_store.cpp` - User profile persistence
- `src/util/` - Reliability utilities
  - `token_bucket.cpp` - Rate limiting
  - `circuit_breaker.cpp` - Circuit breaker pattern
  - `concurrency_limiter.cpp` - Concurrency control
  - `metrics_registry.cpp` - Observability metrics

**Configuration Files (config/):**
- `llm.json` - LLM API settings (base URL, key, model, proxy)
- `valuation.json` - Valuation module settings (DB profile, proxy)
- `rules.yaml` - Risk rule definitions
- `rate_limit.yaml`, `timeout.yaml`, `circuit_breaker.yaml`, `concurrency.yaml`, `observability.yaml` - Reliability configs

**Key Design Patterns:**
- **Profile-based DB switching:** The fundamentals database supports multiple profiles (main, demo variants) switchable at runtime via `/api/v1/settings`. Priority: `FINGUARD_FUNDAMENTALS_DB_PATH` env var > `valuation.json.valuecell_db_profile` > "main" default.
- **Curl fallback:** LLM client automatically falls back to curl.exe subprocess when Drogon's HTTP client encounters DNS/SSL issues (common on Windows).
- **Modular routes:** Route handlers are split by domain (`chat_routes.cpp`, `valuecell_routes.cpp`, etc.) and registered in `routes.cpp`.
- **Static core library:** `finguard_core` static library contains all business logic; `finguard` executable only has `main.cpp`.

### Frontend Structure

**Tech Stack:** React 19, TypeScript, Vite, Tauri 2, Zustand (state), TailwindCSS

**Key Pages:**
- `src/pages/StartupPage.tsx` - Initial loading and settings sync
- `src/pages/ValueCellPage.tsx` - Stock valuation analysis UI
- `src/pages/PlanPage.tsx` - Investment plan generation
- `src/pages/SettingsPage.tsx` - Configuration (LLM, DB profile, proxy)

**State Management:**
- `src/store/useAppStore.ts` - Zustand store for app-wide state
- `src/api/store.ts` - Tauri plugin-store for persistent settings

**API Client:**
- `src/api/client.ts` - Centralized HTTP client for backend communication

### Data Flow

**ValueCell Analysis:**
1. User inputs stock symbol in `ValueCellPage`
2. Frontend calls `POST /api/v1/valuecell` with symbol
3. Backend workflow:
   - Check SQLite fundamentals DB for historical data
   - If missing/stale, fetch from Yahoo Finance API
   - Calculate safety margin (PE vs historical PE mean, PEG < 1.0)
   - Generate LLM analysis prompt with structured financial data
   - Stream LLM response back to frontend
4. Frontend renders Markdown report with metrics table

**Settings Persistence:**
- Frontend saves to Tauri store (`src/api/store.ts`)
- On startup, syncs to backend via `POST /api/v1/settings`
- Backend persists to `config/llm.json` and `config/valuation.json`
- DB profile changes take effect immediately without restart

## Important Constraints

**Windows-specific:**
- Use `curl.exe` (not `curl`) in code paths
- Proxy settings must use `http://` scheme (not `socks5://`)
- Paths use forward slashes in bash shell context

**LLM Client:**
- Always check `use_curl_fallback` config before using Drogon HTTP client
- Curl fallback is the default due to Windows DNS issues
- Proxy must be passed via `--proxy` flag to curl

**Database:**
- Never modify `main` profile DB in demo mode
- Demo profiles are read-only snapshots for frontend validation
- Use `FINGUARD_FUNDAMENTALS_DB_PATH` env var only for testing

**Testing:**
- All new valuation logic must have corresponding tests in `tests/test_valuation_metrics.cpp`
- DB tests use in-memory SQLite (`:memory:`)
- Route tests mock LLM responses to avoid external API calls

## Common Pitfalls

- **Don't** add new config files without registering them in `CMakeLists.txt` POST_BUILD section
- **Don't** use `std::filesystem::current_path()` for config resolution; use `resolve_finguard_root_path()` from `fundamentals_db.h`
- **Don't** assume Drogon HTTP client works on Windows; always provide curl fallback path
- **Don't** hardcode API keys in source; use `.example` config files and `.gitignore`
- **Don't** modify frontend Tauri config (`tauri.conf.json`) without updating `build.rs` sidecar paths

## Key Documentation

- `next_plan.md` - Current development status and handoff notes
- `phase10.md` - ValueCell feature specification (historical, includes deprecated Tavily approach)
- `优化/基本面数据库.md` - Fundamentals database design and usage (Chinese)
- `README.md` - Project overview and quick start

## API Endpoints

**Core:**
- `GET /api/v1/health` - Health check
- `POST /api/v1/chat` - LLM chat (SSE streaming)
- `POST /api/v1/valuecell` - Stock valuation analysis
- `POST /api/v1/plan` - Investment plan generation
- `GET /api/v1/settings` - Get current settings
- `POST /api/v1/settings` - Update settings
- `GET /api/v1/profiles` - List risk profiles
- `POST /api/v1/profiles` - Save risk profile
- `POST /api/v1/allocation` - Calculate asset allocation

**Response Format:**
- Chat/Plan: Server-Sent Events (SSE) with `data:` prefix
- ValueCell: JSON with embedded Markdown in `investment_conclusion` field
- Settings: JSON with nested config objects
