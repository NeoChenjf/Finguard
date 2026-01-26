# VisitQwenProblem.md

## 1. 问题概述
在本项目中，服务进程（`finguard.exe`）通过 Drogon 的 `HttpClient` 访问 Qwen（百炼）OpenAI 兼容接口时失败，错误表现为 `ReqResult::BadServerAddress`（在 SSE 中被记录为 `llm_reqresult_3`）。
同一台机器上使用 `curl` 直连 Qwen 兼容接口返回 200 OK，说明 **API Key 与接口本身是有效的**，问题集中在**服务进程内的网络访问**。

## 2. 复现与现象
### 2.1 直连验证（成功）
使用同机 `curl` 直连 Qwen 兼容接口：
```bash
curl -X POST https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions \
  -H "Authorization: Bearer <YOUR_KEY>" \
  -H "Content-Type: application/json" \
  -d "{\"model\":\"qwen-plus\",\"messages\":[{\"role\":\"user\",\"content\":\"hello\"}]}"
```
结果：返回 200 OK + JSON。

### 2.2 服务内请求（失败）
服务进程通过 Drogon `HttpClient` 同步请求：
- 错误：`ReqResult::BadServerAddress`（枚举值 3）
- SSE warning：`llm_request_failed` + `llm_reqresult_3`
- 表现：自动降级为 mock 输出

## 3. 本项目的请求实现细节
### 3.1 配置来源
- `finguard/config/llm.json`
- 关键字段：
  - `api_base`: `https://dashscope.aliyuncs.com/compatible-mode/v1`
  - `api_key`: 用户填写
  - `model`: `qwen-plus`

### 3.2 客户端实现（简化流程）
- 解析 `api_base` 为 host / prefix / 端口 / SSL
- 通过 `drogon::HttpClient::newHttpClient` 创建客户端
- 请求路径：`<prefix>/chat/completions`
- 请求头：
  - `Authorization: Bearer <api_key>`
  - `Content-Type: application/json`
- Body（OpenAI 兼容格式，stream=false）：
```json
{
  "model": "qwen-plus",
  "temperature": 0.7,
  "stream": false,
  "messages": [
    {"role": "system", "content": "You are FinGuard AI assistant."},
    {"role": "user", "content": "..."}
  ]
}
```

### 3.3 错误处理
- `ReqResult != Ok` 或 HTTP 非 200 时触发降级
- 降级输出 SSE 事件：token / cite / metric / warning / done

## 4. 可能原因分析（从高到低）
1) **服务进程网络访问被拦截（安全软件 / 策略）**
   - curl 可以访问，但进程内访问失败
   - 可能被安全软件或企业策略阻断

2) **Drogon HttpClient 未走系统代理**
   - 系统/用户代理只对 curl 生效
   - Drogon 不会自动继承代理设置

3) **DNS 解析异常（进程内解析失败）**
   - Qwen 域名解析优先 IPv6 或被策略劫持
   - 服务进程 DNS 解析与 curl 不一致

4) **证书链或 TLS 握手问题**
   - 本机证书或 TLS 设置对 curl 有效，但对 Drogon 不一致

5) **HttpClient host 解析/拼接问题**
   - api_base 解析出错，host/prefix 不一致
   - 路径拼接导致错误访问

## 5. 解决方案与权衡
### 方案 A：检查并放行服务进程出站
**做法**：Windows 防火墙/安全软件允许 `finguard.exe` 出站
- 优点：无需改代码，最接近真实部署
- 缺点：需要管理员权限；企业环境可能无法修改
- 适用：本机权限可控、网络策略宽松

### 方案 B：让服务进程走系统代理
**做法**：为 Drogon HttpClient 显式设置代理，或使用 WinHTTP 读取系统代理
- 优点：不改接口协议；能与企业代理兼容
- 缺点：实现复杂；需要确认代理地址
- 适用：公司/内网环境强制代理

### 方案 C：强制 IPv4 / 固定 IP
**做法**：解析 `dashscope.aliyuncs.com` 的 IPv4，直接访问 IP + Host header
- 优点：可绕开 DNS/IPv6 问题
- 缺点：IP 可能变；需要手工维护
- 适用：DNS 不稳定但 IP 可访问

### 方案 D：改用 curl 子进程（临时兜底）
**做法**：在 LLM client 内调用 curl 并解析输出
- 优点：最快验证与打通；能复用现有 curl 访问能力
- 缺点：依赖外部工具；不够工程化
- 适用：阶段验收需要快速打通

### 方案 E：改用其他 HTTP 库
**做法**：引入 libcurl / WinHTTP / cpprestsdk 替代 Drogon HttpClient
- 优点：可控性更高，支持代理/TLS
- 缺点：增加依赖与工作量
- 适用：长期稳定接入需求

## 6. 当前已尝试项
- 已切换 Qwen 中国区 endpoint（`dashscope.aliyuncs.com`）
- 已尝试 Windows 防火墙放行（控制面板设置）
- curl 直连成功，但服务内仍返回 `BadServerAddress`

## 7. 期望专家协助的问题
1) Drogon HttpClient 在 Windows 上的 DNS/代理/TLS 行为是否与 curl 不一致？
2) 如何在 Drogon 中可靠设置系统代理（或禁用 IPv6）？
3) `ReqResult::BadServerAddress` 在 Drogon 内的具体触发条件？
4) 最稳妥的工程化解决方案（短期/长期）？

---
本文件用于对外咨询与复现说明，可直接转发给专家。

## 8. 最新尝试与结果（2026-01-21）
- 已在 LLM client 中加入 **curl fallback**（子进程调用）作为临时兜底：
  - 当 Drogon 请求失败时，使用 curl 直连 Qwen 兼容接口并解析返回。
  - SSE 端返回真实内容，warning 为 `curl_fallback_used`。
- 结论：
  - Qwen API 与 Key 是可用的；
  - Drogon 直连失败仍未解决（BadServerAddress）。

## 9. 继续请教专家的问题
1) Drogon 在 Windows 下如何显式使用系统代理或指定代理？
2) 是否有办法强制 Drogon 只走 IPv4（避免 IPv6 导致的 BadServerAddress）？
3) 是否有推荐的调试方法（开启 DNS/TLS 详细日志）来定位 BadServerAddress？
4) 若保持 curl fallback 作为长期方案，是否有更安全/工程化的替代实现？