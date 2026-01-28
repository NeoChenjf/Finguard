---
title: src 学习路线与理解指南
tags: [tutorial, src, backend]
last_updated: 2026-01-27
source: 用户会话
status: draft
---

# src 学习路线与理解指南

> 目标：从“能跑通”到“能解释每一行”，完整理解 `src/` 的结构与核心逻辑。

---

## 0. 当前起点（请先确认）
- **http_server**：启动 Drogon 服务，配置线程与监听端口。
- **routes**：注册路由、读取请求、返回响应（含 SSE）。
- **llm**：与外部模型 API 交互，承担“外部服务调用”。

以上覆盖主链路。未覆盖但需掌握的部分包括：配置加载、鉴权/安全、SSE 细节、错误降级、构建与运行。

---

## 1. 学习路线（分阶段，从易到难）

### 阶段 A：整体链路（先通，再细）
1) 阅读 `src/main.cpp`：入口如何调用 `start_http_server()`。
2) 阅读 `src/server/http_server.cpp`/`.h`：启动 Drogon 的配置链与主循环。
3) 阅读 `src/server/routes.cpp`/`.h`：路由注册、请求处理、响应返回（含 SSE 实现）。
4) 阅读 `src/llm/llm_client.h` / `.cpp`：理解与外部模型交互的接口与调用流程。

**阶段目标**：能用自己的话描述“请求进来 → 经过 routes → 调用 llm → 返回结果”。

### 阶段 B：配置与安全（核心细节）
1) 打开 `config/llm.json`（若存在），理解每个字段含义。
2) 查看 `LlmClient::load_config()`：如何读取配置，默认值与错误处理策略。
3) 检查 `routes.cpp` 中鉴权逻辑：`X-API-Key` 在何处被检查，检查失败时返回何种 HTTP 状态与错误体。

**阶段目标**：理解何时会返回 401/500，以及哪个分支或条件触发。

### 阶段 C：SSE 流式输出
1) 阅读 `serialize_event()`（或类似实现）：为什么要“一行 JSON”并将其作为 SSE 数据行。
2) 理解 payload 拼接逻辑：token / cite / metric / warning / done 的输出顺序与语义。
3) 阅读 `newStreamResponse` 或流式响应回调：如何分块写出并触发客户端事件。

**阶段目标**：能解释“为什么 SSE 使用 `data: ...` 加上 `\n\n` 来分隔消息”。

### 阶段 D：错误处理与降级策略
1) 阅读 `llm/llm_client.cpp`：调用失败时是否存在 fallback（如 curl），及 fallback 条件。
2) 理解配置项 `use_curl_fallback`、`timeout_ms` 等的作用与优先级。

**阶段目标**：能描述“主路径失败时的降级路径与处理策略”。

### 阶段 E：构建 / 运行 / 验证
1) 查找项目构建方式（顶级或 `finguard/` 下的 `CMakeLists.txt`）。
2) 学会本地启动后端并观察日志输出。
3) 使用 `curl` 或 Postman 验证接口：
   - `/health`
   - `/api/v1/plan`
   - `/api/v1/chat/stream`（流式测试）

**阶段目标**：能独立跑通后端服务并解释返回结果。

---

## 2. 学习结果的自测清单
完成本列表即可视为“理解 src”：

- `start_http_server()` 做了哪些关键配置？（端口、线程、log）
- `setup_routes()` 中各路由的职责是什么？
- 请求中的 JSON 是如何解析为变量并传递给后续模块？
- `X-API-Key` 校验失败会返回何种响应？
- SSE 为什么输出 `data: ...` 并以 `\n\n` 结尾？
- `llm_client` 的配置从何而来？默认值是什么？
- 外部 API 失败时如何处理？是否存在 fallback？

---

## 3. 推荐学习顺序（按文件）
1) `src/main.cpp`
2) `src/server/http_server.h` / `src/server/http_server.cpp`
3) `src/server/routes.h` / `src/server/routes.cpp`
4) `src/llm/llm_client.h` / `src/llm/llm_client.cpp`
5) `config/llm.json`（如存在）

---

## 4. 实操 / 验证命令示例
（示例为通用 CMake 项目在 Windows PowerShell 的构建步骤，按项目实际情况调整）

```powershell
# 在项目根或包含 CMakeLists.txt 的目录执行：
mkdir build
cd build
cmake ..
cmake --build . --config Debug

# 运行（以 Debug 为例）：
.\Debug\your_executable.exe

# 健康检查与接口调用：
curl http://127.0.0.1:8000/health
curl -X POST http://127.0.0.1:8000/api/v1/plan -d '{"input":"hello"}' -H "Content-Type: application/json"

# 流式 SSE 测试（示例）
curl -N http://127.0.0.1:8000/api/v1/chat/stream
```

注：实际可执行文件名与配置路径依据项目 CMake/README 而定，请先确认构建目录与可执行位置。

---

## 5. 深入建议（可选）
- 增加本地接口测试脚本（`scripts/test_endpoints.sh` 或 PowerShell 脚本）。
- 跟随一次请求在代码中打上日志点，观察日志与实际执行路径。
- 阅读 `LlmMetrics` 的实现和记录位置，理解如何统计与上报。

---

## 6. 文档规则（遵循 `workbook/README.md` 的“加入规则”指南）

- 文件头：本文件顶部包含 YAML 元数据（`title`、`tags`、`last_updated`、`source`、`status`）。
- 条目结构：每个教学/规则条目应包含“描述、适用场景、示例、风险/注意事项、变更日志”。
- 变更日志格式：`[YYYY-MM-DD] (自动加入|手动更新|需复核) 来源摘要`。
- 若将本次学习结果抽象为规则并加入 `workbook/*.md`，agent 应按 `workbook/README.md` 指南进行去重/相似度判断与变更记录。

---

## 7. 变更日志（本文件）
- [2026-01-27] (自动生成) 来源：用户请求，将学习路线与规则按 `workbook` 指南格式化并写入 `src_tutorial.md`。

---

## 8. 逐文件详解与“解释版代码”（跟着这个部分逐行走读）

下面按文件顺序给出关键代码片段、逐行解释与调试/验证建议。按本节顺序操作可以让你从“能跑通”走到“能解释每一行”。

注意：示例代码来源于 `finguard/src`，请在阅读前打开对应文件以便逐行对照。

1) `finguard/src/main.cpp`

```cpp
// 程序入口：只负责启动 HTTP 服务
#include "server/http_server.h"

int main() {
   // 启动 Drogon HTTP 服务
   finguard::start_http_server();
   return 0;
}
```

- 解释：`main` 只负责调用 `start_http_server()` 并退出，所有运行时逻辑由 `http_server` 与 `routes` 负责。
- 验证：在本地构建并运行可观察到服务启动（监听端口）。

2) `finguard/src/server/http_server.cpp`（关键片段）

```cpp
void start_http_server() {
   // 注册全部 HTTP 路由
   setup_routes();

   // 启动 Drogon 服务器
   drogon::app()
      .setThreadNum(4)
      .addListener("0.0.0.0", 8080)
      .run();
}
```

- 逐行说明：
  - `setup_routes();`：在启动前注册所有路由处理器，必须在 `app().run()` 之前完成。
  - `setThreadNum(4)`：Drogon 工作线程数，影响并发请求处理能力（开发环境可按 CPU 核心数调整）。
  - `addListener("0.0.0.0", 8080)`：监听地址与端口，开发时通常使用 127.0.0.1 或 0.0.0.0。
  - `.run()`：进入事件循环并开始接收请求。

3) `finguard/src/server/routes.cpp`（分路由详解）

- 健康检查 `/health`：简单 JSON 返回，用于监控与容器就绪探针。

关键代码片段（健康）：

```cpp
app().registerHandler("/health", [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
   Json::Value body;
   body["status"] = "ok";
   auto resp = HttpResponse::newHttpJsonResponse(body);
   cb(resp);
}, {Get});
```

- 说明：通过回调 `cb(resp)` 返回响应；`{Get}` 指定仅允许 GET 方法。

- 资产配置示例 `/api/v1/plan`：返回 mock json，用于理解路由如何读取请求与构建 JSON 响应。

关键代码片段（plan）：

```cpp
app().registerHandler("/api/v1/plan", [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
   (void)req; // 本示例未使用请求体
   Json::Value body;
   body["proposed_portfolio"]["SPY"] = 0.30;
   // ... 其他字段
   auto resp = HttpResponse::newHttpJsonResponse(body);
   cb(resp);
}, {Post});
```

- 说明：该处理器示例展示了如何生成嵌套 JSON，并通过 `newHttpJsonResponse` 快速返回。

4) 流式问答 `/api/v1/chat/stream`（SSE）——这是关键且稍复杂的部分

总体流程：
- 解析请求体中的 `prompt` 字段
- 验证服务端配置是否包含 `api_key`（服务必须配置）
- 验证请求端 `X-API-Key` 与服务端 `api_key` 是否匹配（鉴权）
- 调用 `llm::LlmClient::stream_chat(prompt)` 获取流式结果（包含 tokens、cites、warnings、metrics）
- 逐 token / cite / metric / warning / done 构造 SSE 消息（每条为一行 JSON）并通过 `newStreamResponse` 分块写给客户端

关键片段（鉴权与错误返回）：

```cpp
const auto config = client.load_config();
if (config.api_key.empty()) {
   Json::Value body; body["error"] = "server_missing_api_key";
   auto resp = HttpResponse::newHttpJsonResponse(body);
   resp->setStatusCode(k500InternalServerError);
   cb(resp);
   return;
}

const auto request_key = req ? req->getHeader("X-API-Key") : "";
if (request_key != config.api_key) {
   Json::Value body; body["error"] = "unauthorized";
   auto resp = HttpResponse::newHttpJsonResponse(body);
   resp->setStatusCode(k401Unauthorized);
   cb(resp);
   return;
}
```

- 说明：服务端缺少 `api_key` 被视为服务器配置错误，返回 500；若客户端未带或带错 `X-API-Key`，返回 401。

关键片段（SSE 组装与发送）：

```cpp
// 逐 token 构造 SSE 事件
for (const auto &token : result.tokens) {
   Json::Value event; event["type"] = "token"; event["payload"] = token;
   payload += "data: ";
   payload += serialize_event(event); // 一行 JSON
   payload += "\n\n"; // SSE 消息间隔
}

// 最终包装为 newStreamResponse 回调分块写出
auto resp = HttpResponse::newStreamResponse(
   [data, offset](char *buffer, std::size_t len) -> std::size_t { /* copy logic */ });

resp->setContentTypeString("text/event-stream");
resp->addHeader("Cache-Control", "no-cache");
resp->addHeader("Connection", "keep-alive");
cb(resp);
```

- 解释：SSE 要求每条消息以 `data: ` 开头并以空行结束（`\n\n`），客户端按此分割并解析每条事件。把事件 JSON 序列化为一行可以避免客户端多行解析问题。

5) `finguard/src/llm/llm_client.{h,cpp}`（调用外部模型、降级逻辑）

快速说明（具体路径查看文件）：
- `load_config()`：读取 `config/llm.json` 或默认配置，返回包含 `api_key`、`use_curl_fallback`、`timeout_ms` 等字段的结构体。
- `stream_chat(prompt)`：发起流式调用，返回包含 `tokens`（逐 token 文本）、`cites`、`warnings`、`metrics` 等的结构体；在失败时可能触发 curl fallback（若配置允许）。

验证点：
- 在 `llm_client` 中找到 `use_curl_fallback` 的判断点，确认当主调用失败如何记录日志并触发 fallback。`

示例：如何本地追踪一次请求的完整链路
1. 启动服务（在项目可执行所在目录运行二进制）。
2. 使用 `curl -v` 发起 POST 到 `/api/v1/chat/stream`，带上 `X-API-Key` 头并带 JSON body：

```bash
curl -N -H "Content-Type: application/json" -H "X-API-Key: <your_key>" \
  -d '{"prompt":"Hello"}' http://127.0.0.1:8080/api/v1/chat/stream
```

3. 观察服务端日志与 curl 输出，确认 token 流、cite、metric、warning、done 等事件顺序正确。

---

## 9. 常见问题与排查指南（快速参考）

- 启动时报 `app 未定义`：见 `workbook/automation.md` 中的“后端启动常见错误与修复（app 未定义）”。要点：确保在 `backend` 或可运行的工作目录下执行 uvicorn / 可执行文件，并检查导出变量名。
- SSE 客户端收不到事件：检查响应头是否 `text/event-stream`、是否有 `\n\n` 分隔、网络是否有代理或中间件截断流。
- 401 鉴权失败：确认 `X-API-Key` 与服务端 `config.api_key` 一致；查看 `config/llm.json` 或环境变量配置来源。

---

## 10. 自测答案参考（对照第 2 节自测清单）

- `start_http_server()`：注册路由（`setup_routes()`）、设置线程数（`setThreadNum`）、绑定监听地址/端口（`addListener`）并 `run()` 进入事件循环。
- `setup_routes()`：包含 `/health`（GET）、`/api/v1/plan`（POST mock）、`/api/v1/chat/stream`（POST SSE 流式问答）等路由；每个路由通过 `app().registerHandler` 注册并在回调中构造并返回 `HttpResponse`。
- JSON 解析：通过 `req->getJsonObject()` 获取 Json::Value，然后通过 `isMember` 与 `asString` 等方法读取字段。
- `X-API-Key` 校验失败：代码返回 JSON 错误体并设置 HTTP 状态为 401（`k401Unauthorized`）。
- SSE 为什么输出 `data: ...` 和 `\n\n`：SSE 协议要求每条事件以 `data:` 行开始，空行作为事件边界，客户端按此解析。将事件序列化为单行 JSON 可避免多行解析混淆。
- `llm_client` 配置来源：通常从 `config/llm.json` 或默认值加载，含 `api_key`、`timeout_ms`、`use_curl_fallback` 等。
- 外部 API 失败时降级：若开启 `use_curl_fallback`，会在主调用失败后尝试备用实现（如 curl）；具体实现请查看 `llm_client.cpp` 中的 fallback 分支。

---

## 11. 练习题（验收与实践）

1) 本地启动服务并使用 `curl` 完成一次 `/api/v1/chat/stream` 的流式请求，记录并说明 token/metric/warning/done 的顺序。
2) 人为触发 `unauthorized`：在请求中传入错误的 `X-API-Key`，观察并记录响应体与状态码。
3) 修改 `http_server.cpp` 的 `setThreadNum` 为 1，重新构建并观察并发性能（可用并行 curl 请求进行简单压测）。

---

## 12. 下一步（如果需要我带你逐文件走读）

- 我可以按你的节奏逐文件解释，每个文件生成“解释版代码”注释（会在文件旁新建 `explained/` 目录保存注释版本），或者我可以将关键函数抽成单独的学习卡片并加入 `workbook` 作为规则条目。

---

## 13. 变更日志（本文件）
- [2026-01-27] (自动生成) 来源：用户请求，将学习路线扩展为完整教学文档，加入逐文件详解、排查指南与练习。


