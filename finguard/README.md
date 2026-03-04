# FinGuard

面向个人资产配置的高性能 AI 网关与风控中台。

## 架构概览

```
┌─────────────┐     HTTP      ┌─────────────────────────────────────┐
│  浏览器/前端  │ ◄──────────► │           finguard 后端              │
│  (Vite+React)│              │                                     │
└─────────────┘              │  ┌───────────┐  ┌────────────────┐  │
                              │  │ routes.cpp │  │ http_server.cpp│  │
                              │  │  /health   │  │  Drogon 4线程  │  │
                              │  │  /plan     │  └────────────────┘  │
                              │  │  /chat/stream                    │
                              │  │  /settings │                     │
                              │  │  /metrics  │                     │
                              │  └─────┬──────┘                     │
                              │        │                            │
                              │  ┌─────▼──────┐  ┌──────────────┐  │
                              │  │ llm_client  │  │ rule_engine  │  │
                              │  │ (Qwen/GPT)  │  │ (风控规则)    │  │
                              │  └─────┬──────┘  └──────────────┘  │
                              │        │                            │
                              │  ┌─────▼──────────────────────────┐│
                              │  │  可靠性层                       ││
                              │  │  令牌桶 · 熔断器 · 并发限制器    ││
                              │  │  配置缓存(5s TTL) · 指标收集    ││
                              │  └────────────────────────────────┘│
                              └─────────────────────────────────────┘
                                              │
                                              ▼
                                   ┌──────────────────┐
                                   │  外部 LLM API     │
                                   │  (Qwen / OpenAI)  │
                                   └──────────────────┘
```

## 接口列表

| 方法   | 路径                     | 说明                         |
|--------|--------------------------|------------------------------|
| GET    | `/health`                | 健康检查                     |
| POST   | `/api/v1/plan`           | 资产配置建议（mock）          |
| POST   | `/api/v1/chat/stream`    | 流式 AI 对话（SSE）          |
| POST   | `/api/v1/profile/upsert` | 问卷档案写入                 |
| GET    | `/api/v1/settings`       | 获取当前 LLM 配置（Key 掩码）|
| POST   | `/api/v1/settings`       | 更新 LLM 配置（部分更新）    |
| GET    | `/metrics`               | 内部指标（请求数、延迟等）    |

### curl 示例

**健康检查：**
```bash
curl http://localhost:8080/health
# -> {"status":"ok"}
```

**资产配置建议：**
```bash
curl -X POST http://localhost:8080/api/v1/plan \
  -H "Content-Type: application/json" \
  -d '{"profile":{"age":35,"risk":"medium","capital":500000}}'
```

**流式 AI 对话（SSE）：**
```bash
curl -N -X POST http://localhost:8080/api/v1/chat/stream \
  -H "Content-Type: application/json" \
  -H "X-API-Key: <your_api_key>" \
  -H "X-User-Id: user1" \
  -d '{"prompt":"请分析贵州茅台的基本面"}'
```

SSE 事件格式（每行 `data:` 为 JSON）：
```json
{"type":"token","payload":"..."}
{"type":"cite","payload":"..."}
{"type":"metric","payload":{"prompt_tokens":123,"completion_tokens":456,"total_tokens":579}}
{"type":"warning","payload":"..."}
{"type":"done","payload":"done"}
```

**获取设置：**
```bash
curl http://localhost:8080/api/v1/settings
# -> {"api_base":"...","api_key_configured":true,"api_key_hint":"****ab3f","model":"qwen-plus","temperature":0.7,"timeout_ms":30000}
```

**更新设置（部分更新）：**
```bash
curl -X POST http://localhost:8080/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"api_key":"sk-xxxx","model":"gpt-4o-mini"}'
# -> {"status":"ok","message":"settings updated"}
```

**指标：**
```bash
curl http://localhost:8080/metrics
# -> {"requests_total":42,"rate_limit_rejects_total":0,"circuit_breaker_trips_total":0,"latency_p95_ms":1.2,"latency_p99_ms":2.5,"external_call_latency_ms_p95":0}
```

## 配置文件

所有配置位于 `config/` 目录（构建后自动复制到 `build/config/`）。

| 文件                   | 说明                                                   |
|------------------------|--------------------------------------------------------|
| `llm.json`             | LLM 连接配置（api_base, api_key, model, temperature 等）|
| `llm.json.example`     | 配置模板（不含真实 Key）                                |
| `rules.yaml`           | 风控规则（禁止关键词、投资人限制、资产配置参数等）       |
| `rate_limit.yaml`      | 入口限流（entry.rate_rps, entry.capacity）与模型限流    |
| `timeout.yaml`         | 外部调用超时、重试策略、路由/模型级覆盖                 |
| `circuit_breaker.yaml` | 熔断器参数（错误率阈值、窗口期、半开探测次数）          |
| `concurrency.yaml`     | 并发限制（max_inflight）                                |
| `observability.yaml`   | 可观测性（日志字段、指标列表）                          |

### llm.json 字段说明

```json
{
  "api_base": "https://dashscope.aliyuncs.com/compatible-mode/v1",
  "api_key": "sk-your-key-here",
  "model": "qwen-plus",
  "temperature": 0.7,
  "timeout_ms": 30000,
  "use_curl_fallback": true,
  "curl_path": "curl.exe",
  "http_proxy": ""
}
```

环境变量覆盖（优先级高于文件）：`FINGUARD_LLM_API_BASE`、`FINGUARD_LLM_API_KEY`、`FINGUARD_LLM_MODEL`、`FINGUARD_LLM_TIMEOUT_MS`、`FINGUARD_LLM_HTTP_PROXY`、`FINGUARD_LLM_USE_CURL_FALLBACK`。

### rules.yaml 风控规则示例

```yaml
allocation:
  gold_percent: 0.10
  bonds:
    method: age_decade
    default_age: 35
risk_limits:
  min_single_asset_percent: 0.025
stock_selection:
  peg_max: 2.0
  debt_ratio_max: 0.50
  roe_min: 0.20
warnings:
  keyword_rules:
    forbidden_keywords:
      - "博彩"
      - "高杠杆"
      - "ST"
```

## 构建与运行

### 前置依赖

- CMake ≥ 3.20
- MSVC (Visual Studio 2022)
- vcpkg（Drogon, nlohmann_json, spdlog, GTest）

### Debug 构建

```powershell
cd finguard
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="<vcpkg>/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Debug
.\build\Debug\finguard.exe    # 启动服务 → http://localhost:8080
```

### Release 构建

```powershell
cmake --build build --config Release
.\build\Release\finguard.exe
```

### 运行单元测试

```powershell
cmake -S . -B build -DBUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE="<vcpkg>/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Debug
cd build
ctest --build-config Debug --output-on-failure
```

## Phase 5 压测数据摘要

| 场景 | 指标 | Debug 基线 | 优化后 | 变化 |
|------|------|-----------|--------|------|
| S1 Health | RPS | 5,240 | 5,551 | +5.9% |
| S2 Plan | RPS | 2,270 | 5,551 | +145% |
| S2 Plan | P95 | 4.94ms | 1.92ms | -61% |
| S3 Chat (Mock) | RPS | 171 | 252 | +47% |
| S3 Chat (Mock) | P95 | 64.7ms | 40.5ms | -37% |
| S5 Stability | RSS漂移 | +57.76% | -0.09% | 内存泄漏修复 |

Phase 6 Release 构建 /health P99: ~5ms（Debug 14ms → Release 5ms）。

## 项目结构

```
finguard/
├── config/              # 配置文件
├── src/
│   ├── main.cpp         # 入口
│   ├── llm/             # LLM 客户端（Qwen/OpenAI 兼容）
│   ├── risk/            # 风控规则引擎 + 用户档案
│   ├── server/          # HTTP 服务器 + 路由注册
│   └── util/            # 工具库（令牌桶、熔断器、配置缓存、指标等）
├── tests/               # gtest 单元测试
├── tools/               # 压测/诊断脚本
└── CMakeLists.txt       # 构建配置（finguard_core 静态库 + finguard exe + tests）
```
