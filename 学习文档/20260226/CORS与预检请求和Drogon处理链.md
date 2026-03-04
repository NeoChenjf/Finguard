---
title: CORS、预检请求与 Drogon 处理链入门（Phase 6）
tags: [tutorial, phase6, cors, options, drogon, vite]
last_updated: 2026-02-26
source: phase6.md
status: draft
---

# CORS、预检请求与 Drogon 处理链入门（Phase 6）

## 标题与目标

目标：把你提到的概念一次讲清：`CORS`、`OPTIONS 预检`、`Vite dev server:5173`、`跨域`、`204`、`Drogon 过滤器/doBeforeHandling/handler`，以及“Drogon 遗留问题 + 降级方案”对 Phase 6 的影响。

## 学习路线/分层

1. 先搞懂浏览器为什么拦截（跨域与 CORS）。
2. 再搞懂请求为什么先发 `OPTIONS`（预检）。
3. 再看你项目里 Drogon 是怎么落地的（全局头 + OPTIONS handler）。
4. 最后回答“遗留问题与降级方案会不会影响 CORS”。

## 核心概念解释（带类比）

- `跨域请求`：浏览器里，协议/域名/端口三者任一不同，就算跨域。  
  例：`http://localhost:5173` 请求 `http://localhost:8080`，端口不同（5173 vs 8080），所以跨域。  
  类比：同一小区不同楼栋也算“跨楼栋通行”，门禁规则会不同。

- `CORS 支持`：服务器用一组响应头告诉浏览器“这个跨域请求我允许”。没有这些头，浏览器会拦截。  
  类比：门卫放行条；后端同意了，浏览器才放行给前端代码。

- `OPTIONS 预检请求`：浏览器在“有风险/非简单请求”前，先发一个 `OPTIONS` 询问服务器是否允许。  
  常见触发：`Content-Type: application/json`、自定义头（如 `X-API-Key`）、方法不是简单方法等。  
  类比：正式进场前先打电话问“我这批人和设备能进吗？”。

- `Vite dev server`：Vite 在开发阶段启动的前端本地服务器，支持热更新（HMR）。  
  `5173` 是 Vite 的默认开发端口（可改，不是硬性）。Phase 6 文档用它是因为前端默认常从这个端口发请求。  
  类比：前端开发时的“临时指挥部”。

- 你列的这组头：
  - `Access-Control-Allow-Origin: *`
  - `Access-Control-Allow-Methods: GET, POST, OPTIONS`
  - `Access-Control-Allow-Headers: Content-Type, X-User-Id, X-Trace-Id, X-Api-Key`  
  是的，它们就是典型 CORS 响应头。

- `返回 204`：HTTP `204 No Content`，表示“请求成功，但响应体为空”。  
  预检场景常用 204，因为只需要“允许/不允许”的头，不需要业务数据。

- `handler`：某个路由对应的处理函数。比如 `POST /api/v1/settings` 的那段 lambda，就是 handler。

- `Drogon 过滤器`：在进入业务 handler 前/后做统一逻辑（鉴权、限流、统一加头等）的机制。  
  Phase 6 里你们还用了“全局 advice”来统一加 CORS 头，本质也是全局拦截思想。

- `全局 doBeforeHandling`：可理解为“在业务 handler 之前统一处理请求”的位置。  
  你项目当前实现里主要用了 `registerPostHandlingAdvice()`（响应后统一加头）+ 独立 `OPTIONS` handler；不是把所有逻辑都放在 doBeforeHandling。

## 关键流程/步骤（可复刻）

### 浏览器跨域 + 预检完整链路

1. 前端（`localhost:5173`）调用后端（`localhost:8080/api/v1/settings`）。
2. 浏览器判断跨域，且请求带 JSON/自定义头，先发 `OPTIONS` 预检。
3. 后端返回 `204` + CORS 头（允许的方法、头、来源）。
4. 浏览器确认允许后，再发真实 `GET/POST`。
5. 后端真实响应也带 CORS 头，浏览器才把结果交给前端 JS。

### 你项目里的落地点

1. `http_server.cpp` 全局注册后处理 advice，为响应统一补 CORS 头。
2. `routes.cpp` 注册 `OPTIONS /api/v1/{path}`，专门处理预检并返回 204。
3. 各业务 handler（如 `/api/v1/settings`）负责真正业务逻辑。

## 真实代码片段（逐行注释）

### 1) 全局 CORS 头（`finguard/src/server/http_server.cpp`）

```cpp
drogon::app().registerPostHandlingAdvice(                    // 注册“响应后”全局钩子
    [](const drogon::HttpRequestPtr &req,                    // 入参1：当前请求（这里未使用）
       const drogon::HttpResponsePtr &resp) {                // 入参2：将要返回的响应
        resp->addHeader("Access-Control-Allow-Origin", "*"); // 允许所有来源（开发期方便）
        resp->addHeader("Access-Control-Allow-Methods",       // 声明允许的方法
                        "GET, POST, OPTIONS");
        resp->addHeader("Access-Control-Allow-Headers",       // 声明允许携带的请求头
                        "Content-Type, X-User-Id, X-Trace-Id, X-Api-Key");
    });                                                       // 钩子结束
```

### 2) OPTIONS 预检处理器（`finguard/src/server/routes.cpp`）

```cpp
app().registerHandler("/api/v1/{path}",                      // 匹配 /api/v1/* 路径
    [](const HttpRequestPtr &,                               // 预检请求体通常不关心
       std::function<void(const HttpResponsePtr &)> &&cb) {  // cb 用于回写响应
        auto resp = HttpResponse::newHttpResponse();         // 创建空响应对象
        resp->setStatusCode(drogon::k204NoContent);          // 返回 204（成功但无响应体）
        add_cors_headers(resp);                              // 手动补 CORS 头
        cb(resp);                                            // 发送响应
    },                                                       // handler 结束
    {Options});                                              // 仅处理 OPTIONS 方法
```

### 3) 业务 handler 示例（`finguard/src/server/routes.cpp` 的 settings）

```cpp
app().registerHandler("/api/v1/settings",                    // 路由：/api/v1/settings
    [](const HttpRequestPtr &req,                            // 入参：HTTP 请求
       std::function<void(const HttpResponsePtr &)> &&cb) {  // 入参：回调发送响应
        auto json_ptr = req ? req->getJsonObject() : nullptr;// 解析 JSON body
        if (!json_ptr) {                                     // 若 body 非法
            Json::Value err;                                 // 构造错误 JSON
            err["error"] = "invalid_json";                   // 错误码
            auto resp = HttpResponse::newHttpJsonResponse(err);// 生成 JSON 响应
            resp->setStatusCode(k400BadRequest);             // 返回 400
            add_cors_headers(resp);                          // 仍需带 CORS 头
            cb(resp);                                        // 发送响应
            return;                                          // 结束处理
        }
        // ... 这里继续做 settings 合并写入等业务逻辑
    },
    {Post});                                                 // 仅处理 POST 方法
```

### 4) “Drogon 遗留问题 + 降级方案”相关片段（`finguard/src/llm/llm_client.cpp`）

```cpp
std::tie(res, resp) = client->sendRequest(req, timeout_sec); // 默认仍用 Drogon HttpClient 请求模型
if (res != drogon::ReqResult::Ok || !resp) {                 // 若请求失败或无响应
    if (cfg.use_curl_fallback) {                             // 若开启 curl 降级开关
        if (curl_fallback_request(cfg, body, message,        // 则改用 curl 再试一次
                                  result.metrics, curl_warnings)) {
            result.warnings.push_back("curl_fallback_used"); // 标记已走降级链路
        }
    }
}
```

## “我们不是 Drogon，而是降级方案”对这里的影响

先给结论：**当前项目仍在使用 Drogon 作为 HTTP 服务框架**；“降级方案”主要发生在“后端调用外部 LLM”这一段，不等于整站脱离 Drogon。

- 对 CORS/预检的影响：
  - 基本无影响。CORS 是浏览器到你后端（5173 → 8080）的规则。
  - 而 `use_curl_fallback` 是你后端到外部模型 API 的容错策略。
  - 所以即便触发 curl 降级，前端跨域是否通过，仍由你后端返回的 CORS 头决定。

- 真正会影响 CORS 的情况：
  - 如果未来你把后端框架从 Drogon 改成别的框架，就要在新框架里重新实现“全局 CORS 头 + OPTIONS 204”。

## 小结 + 自测题（含答案）

小结：
- 5173 到 8080 因端口不同属于跨域，请求会受浏览器 CORS 约束。
- 你列的 `Access-Control-Allow-*` 就是 CORS 关键头。
- `OPTIONS + 204` 是预检常见实现。
- 当前项目里 CORS 在 Drogon 服务层处理；curl 降级属于下游模型调用容错，二者不是一层问题。

自测题：
1. 为什么 `localhost:5173 -> localhost:8080` 算跨域？
2. 为什么预检常返回 204 而不是 200 大 JSON？
3. `Access-Control-Allow-Headers` 不包含 `X-API-Key` 会怎样？
4. `use_curl_fallback=true` 是否能“修复浏览器跨域报错”？

答案：
1. 因为同源要求协议/域名/端口都一致，这里端口不同。
2. 预检只需要“许可信息”，不需要业务数据，204 语义更贴切。
3. 浏览器会拦截带该自定义头的跨域请求（预检不通过）。
4. 不能。它只影响后端调用外部模型，不改变浏览器 CORS 判定。

## 下一步建议

1. 用浏览器 DevTools Network 面板观察一次 `OPTIONS -> POST` 完整链路。  
2. 试着删除一个 CORS 头（本地实验）再请求，感受浏览器报错差异。  
3. 若后续要带 Cookie/凭证，再补学 `Access-Control-Allow-Credentials` 与精确 Origin 策略。  

变更日志
- [2026-02-26] (手动更新) 新增 CORS/预检/Drogon 处理链教学文档，并回答“Drogon 与降级方案的关系”。
