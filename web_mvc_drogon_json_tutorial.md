# Web 与 Drogon 入门小白教程（HTTP / URL / MVC / JSON）

> 目标：按“HTTP → MVC + Drogon → Drogon 调用 API → JSON 深入”的脉络，帮你用生活例子建立直觉。

---

## 1. HTTP 与 URL：Web 请求/响应是什么

### 1.1 HTTP 是什么（生活例子）
把 Web 想成“餐厅点餐”。
- **你（客户端）**：顾客
- **后端服务（服务器）**：厨房
- **HTTP 请求**：点餐单
- **HTTP 响应**：厨房回的菜

顾客点餐（请求） → 厨房做菜（处理） → 端菜（响应）

### 1.2 URL 是什么（生活例子）
URL 就像“餐厅地址 + 房间号 + 菜单项”。
- `https://example.com`：餐厅地址
- `/api/v1/plan`：菜单项（要点哪道菜）
- `?id=123`：额外备注（小票上的备注）

### 1.3 请求与响应的基本结构
**请求**（你发出去的）：
- **方法**：GET / POST（吃饭 vs 点外卖？）
- **路径**：/api/v1/plan
- **头信息**：如 `X-API-Key`（会员卡）
- **请求体**：JSON（点餐内容）

**响应**（服务器返回的）：
- **状态码**：200/401/500（成功/未授权/出错）
- **响应体**：JSON（菜品或错误信息）

---

## 2. MVC 与 Drogon：Web 应用怎么组织

### 2.1 MVC 是什么（生活例子）
把系统拆成三部分：
- **Model（数据）**：菜谱/库存（数据库）
- **View（展示）**：菜单/前台展示（前端页面）
- **Controller（控制）**：服务员（接单、转给厨房、端菜）

**目的**：职责清晰，互不打架。

### 2.2 Drogon 是什么
Drogon 是 **C++ 的 Web 后端框架**，它帮你完成：
- 开服务器
- 接请求
- 路由分发
- 返回响应

在你们项目中：
- `http_server.cpp`：启动服务
- `routes.cpp`：注册路由与处理逻辑
- `llm_client.cpp`：与外部模型 API 交互

---

## 3. Drogon 的基本使用（小白能懂）

### 3.1 注册路由（接单）
```cpp
app().registerHandler("/health", [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
    Json::Value body;
    body["status"] = "ok";
    auto resp = HttpResponse::newHttpJsonResponse(body);
    cb(resp);
}, {Get});
```
**理解方式**：
- “当有人访问 `/health`，就回一个 JSON。”

### 3.2 启动服务（开门营业）
```cpp
drogon::app()
    .setThreadNum(4)
    .addListener("0.0.0.0", 8080)
    .run();
```
**理解方式**：
- **setThreadNum(4)**：开 4 个窗口接单
- **addListener**：在 8080 端口开门
- **run**：正式营业

---

## 4. Drogon 与 API 交互：像“打电话问别家餐厅”

有时候你的后端需要“再去问别的服务”，比如 LLM API。
这就像你在餐厅里**打电话问别家餐厅的菜谱**。

### 4.1 流程概念
- 你接到请求
- 你去调用外部 API
- 外部 API 返回 JSON
- 你解析 JSON，把结果回给前端

### 4.2 C++ 中“发请求 → 收 JSON”
Drogon 可以做 HTTP 客户端请求（示意）：
```cpp
auto client = drogon::HttpClient::newHttpClient("https://api.example.com");
auto req = drogon::HttpRequest::newHttpRequest();
req->setMethod(drogon::Post);
req->setPath("/v1/chat");
req->setBody("{\"prompt\":\"hi\"}");

client->sendRequest(req, [](ReqResult r, const HttpResponsePtr &resp) {
    if (r == ReqResult::Ok && resp) {
        auto json = resp->getJsonObject();
        // 读取 json 字段
    }
});
```

---

## 5. JSON 深入理解（生活例子 + 技术理解）

### 5.1 JSON 是什么（生活例子）
JSON 就像“快递单/表格”，用统一格式写清楚：
```json
{
  "name": "张三",
  "phone": "13800000000",
  "address": "北京",
  "item": "书",
  "count": 2
}
```

它的好处：**各语言都能看懂**。

### 5.2 JSON 在程序里如何“变成变量”
JSON 本质是**文本**，不会自动变成变量。
流程是：
1) 收到 JSON 文本
2) 解析成对象（字典/结构）
3) 手动取字段赋值

### 5.3 在 C++ 中构造 JSON
```cpp
Json::Value body;
body["status"] = "ok";
body["count"] = 2;
```

### 5.4 在 C++ 中读取 JSON
```cpp
auto json = req->getJsonObject();
if (json && (*json)["prompt"].isString()) {
    std::string prompt = (*json)["prompt"].asString();
}
```

### 5.5 JSON 与 SSE（流式输出）
你们项目里用 **SSE**，会把 JSON 转成一行文本：
```cpp
Json::StreamWriterBuilder builder;
builder["indentation"] = "";
std::string line = Json::writeString(builder, event);
```
**原因**：SSE 每条事件必须是“单行文本”，所以不能格式化成多行。

---

## 6. 小结（把一整条链路串起来）

你可以把整个系统理解成一条流水线：

1) **浏览器发请求（HTTP + URL）**
2) **Drogon 接收并路由（Controller）**
3) **内部处理或调用外部 API（Model）**
4) **返回 JSON 响应（View 需要的数据）**

像开一家餐厅：
- 客人点菜（请求）
- 服务员接单（路由）
- 厨房做菜（业务逻辑）
- 端菜给客人（响应）

---

## 7. 你们项目的真实代码片段 + 解释（入门版）

下面用你们项目里的真实代码做串讲，帮助把概念落地。

### 7.1 启动服务（http_server.cpp）
```cpp
drogon::app()
    .setThreadNum(4)
    .addListener("0.0.0.0", 8080)
    .run();
```
**解释（生活例子）**  
这就像“开门营业”：  
- `setThreadNum(4)` = 开 4 个窗口接待客人  
- `addListener` = 在 8080 号门口等人来  
- `run` = 开始营业，持续接单

### 7.2 注册路由（routes.cpp）
```cpp
app().registerHandler("/health", [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
    Json::Value body;
    body["status"] = "ok";
    auto resp = HttpResponse::newHttpJsonResponse(body);
    cb(resp);
}, {Get});
```
**解释**  
有人访问 `/health`，就回一句“我很好”。  
- `body` 是 JSON 响应体  
- `newHttpJsonResponse` 把 JSON 包成 HTTP 响应  
- `cb(resp)` 把响应发回去  

### 7.3 从请求里读 JSON（routes.cpp）
```cpp
auto json = req->getJsonObject();
if (json && json->isMember("prompt") && (*json)["prompt"].isString()) {
    prompt = (*json)["prompt"].asString();
}
```
**解释（生活例子）**  
像是“打开顾客的点菜单”，看看有没有写 `prompt`（他要点什么），  
- 有就把内容读出来放进 `prompt` 变量  
- 没有就当空白处理

### 7.4 鉴权（routes.cpp）
```cpp
const auto request_key = req ? req->getHeader("X-API-Key") : "";
if (request_key != config.api_key) {
    Json::Value body;
    body["error"] = "unauthorized";
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(k401Unauthorized);
    cb(resp);
    return;
}
```
**解释（生活例子）**  
像是“检查会员卡”。  
- 没带或不对：回 401（没权限）

### 7.5 SSE 输出（routes.cpp）
```cpp
Json::Value event;
event["type"] = "token";
event["payload"] = token;
payload += "data: ";
payload += serialize_event(event);
payload += "\n\n";
```
**解释**  
把每个 token 打包成一条“直播消息”，  
- `data: ...` 是 SSE 规定格式  
- `\n\n` 表示一条消息结束

### 7.6 LLM 客户端配置结构（llm_client.h）
```cpp
struct LlmConfig {
    std::string api_base;
    std::string api_key;
    std::string model;
    double temperature = 0.7;
    int timeout_ms = 30000;
    bool use_curl_fallback = false;
    std::string curl_path = "curl.exe";
};
```
**解释**  
这是一张“配置单”，告诉系统：  
- 去哪里（api_base）  
- 用什么钥匙（api_key）  
- 用哪个模型（model）  
- 超时多久、是否降级用 curl

---

如果你愿意，我可以继续：
1) 把这些片段拆成“逐行讲解版本”  
2) 给每个片段画一条“请求→响应”的小流程图  
3) 补一段“从前端调用 /api/v1/chat/stream 的示例”
