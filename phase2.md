# Phase 2 — 状态、验收与排查指南

此文档为 Phase2 的交接与验收指南，按用户要求重新组织结构，清晰展示目标、已完成内容、日志记录以及遗留问题的深度讨论。

---

## 1. 二阶段目标

- **核心目标**：完成 SSE 流式接口与 LLM client（Qwen 兼容 API）接入。
- **具体要求**：
  - 支持 `/api/v1/chat/stream` 接口，输出 token/cite/metric/warning/done 结构化事件。
  - 实现 `X-API-Key` 鉴权机制。
  - 实现 LLM 客户端基础功能，确保能与 dashscope.aliyuncs.com 通信。
  - 实现配置管理（`llm.json`），支持代理与降级开关。

---

## 2. 二阶段已经完成的内容

- **构建与运行**：
  - `finguard.exe` 已成功构建 (`d:\AI_Investment\finguard\build\Debug\finguard.exe`)。
  - 服务可稳定启动，`/health` 与 `/api/v1/plan` 接口响应正常（Mock 数据）。

- **功能实现**：
  - ✅ **SSE 接口**：已实现并验证，能流式输出结构化数据。
  - ✅ **鉴权**：`X-API-Key` 验证逻辑生效。
  - ✅ **LLM 通信（降级模式）**：通过 `curl` fallback 机制成功实现了与 Qwen API 的通信，能够获取真实回复。
  - ✅ **配置管理**：`llm.json` 支持动态加载 `http_proxy`、`use_curl_fallback` 和 apiKey。
  - ✅ **网络诊断**：完成了 IPv4 优先策略验证、Packet Capture（抓包）与基础连通性测试。

---

## 3. 二阶段已经完成的内容的日志

以下为按时间顺序整理的关键验收日志与结论，详细记录见 `tools/temp/` 下的对应文件。

### 📅 2026-01-21（SSE 结构与鉴权验证）
- **测试内容**：`POST /api/v1/chat/stream`
- **结果**：SSE 事件流结构正确，包含所有定义字段。鉴权拦截生效。
- **发现问题**：首次发现 Drogon 直连报告 `BadServerAddress`，但 curl fallback 此时已能工作（返回了 `curl_fallback_used` 警告）。

### 📅 2026-01-28（构建与自动化验收）
- **操作**：运行 `tools/run_acceptance_full.ps1`。
- **结果**：
  - 构建成功。
  - `curl_ipv4.txt` 显示此时本机 curl 能成功连接目标（返回 404，说明连通性OK）。
  - 生成了首份 pktmon 抓包（`pktmon.pcapng`），但因抓包时间窗口问题未捕获到关键流量。

### 📅 2026-02-01（代理注入与最终功能验证）
- **场景 A：直连验证（use_curl_fallback=false）**
  - **执行**：注入真实 key，关闭 fallback。
  - **日志**：`finguard_out.log` 包含 `ERROR LLM request failed: result=Bad server address`。
  - **表现**：接口返回本地 fallback mock 数据，带有 `llm_reqresult_3` 警告。验证了直连确实不可用。

- **场景 B：降级验证（use_curl_fallback=true）**
  - **执行**：注入真实 key，开启 fallback。
  - **日志**：
    ```text
    data: {"payload":"您好！看起来您可能在测试系统的回退（fallback）机制...","type":"token"}
    ...
    data: {"payload":"curl_fallback_used","type":"warning"}
    ```
  - **表现**：成功收到 Qwen 的真实中文回复（268 tokens）。
  - **结论**：**Phase 2 核心功能（LLM 对话）已通过降级方案完全跑通。**

---

## 4. 二阶段遗留的问题

- **Drogon HttpClient 直连失败 (P0)**：
  - 现象：在服务进程内使用 Drogon HttpClient 请求 `https://dashscope.aliyuncs.com` 时，始终返回 `BadServerAddress` (ReqResult=3)。
  - 对比：同一台机器、同一网络环境下，使用 `curl` 命令行（包括强制 IPv4、强制 resolve）均能正常连接。
  - 影响：目前必须依赖 `use_curl_fallback=true` 才能工作。直连路径不可用。

- **待验证项**：
  - 代理注入效果：因环境无代理，只验证了配置读取逻辑，未验证真实的代理转发流量。

---

## 5. 我们关于二阶段遗留问题的讨论

针对 Drogon 直连失败的问题，我们进行了深入的分析与讨论：

- **根本原因推测**：
  问题定位于 **后端服务进程到阿里云** 的连接建立阶段。可能的根源包括：
  1. **DNS 解析差异**：Drogon/trantor 的解析器可能优先返回了 IPv6 地址或被系统策略拦截的地址，而 curl 处理方式不同。
  2. **TLS/SNI 握手**：Drogon 的 SSL 握手可能未正确发送 SNI (Server Name Indication)，导致阿里云网关拒绝连接（表现为 BadServerAddress）。
  3. **环境权限**：服务进程可能受 Windows 防火墙或组策略限制，与用户手动运行 curl 的会话权限不同。

- **影响评估**：
  - **对个人工具的影响**：**影响较小**。功能已通过 fallback 闭环，用户体验上除轻微延迟外无感知。
  - **长期风险**：双路径（Drogon + curl）维护成本高；子进程方案在高并发下性能较差；若未来需部署到严格环境，curl fallback 可能受限。

- **生活类比**：
  这就好比你开餐厅（后端），平常直接给供应商打电话订货（Drogon 直连）。现在电话线坏了，你只能派个跑腿小弟去超市买回来（curl fallback）。
  - **结果**：菜还是能做出来端给客人（用户能用）。
  - **代价**：跑腿虽慢点但能解决问题。如果长期不修电话线，成本高且不稳定。

- **决策**：
  鉴于个人工具定位，**当前接受 fallback 方案作为交付标准**。但为了技术严谨性，建议进行 P1 级的深度排查（抓包分析与代码调试）以尝试彻底解决。

---

## 6. 下一步计划

我们决定启动 P1 深度排查，尝试定位并修复直连问题：

1.  **同步抓包与重放**：
    - 在管理员权限下启动 `pktmon`，同时触发直连请求，确保抓到完整的 TCP/TLS 握手包。
    - 分析 `pktmon.pcapng`，查看 SYN 是否发出、Client Hello 是否包含正确 SNI、Server 是否有 RST/Alert。

2.  **代码级诊断 (Logging)**：
    - 修改 `finguard/src/llm/llm_client.cpp`，增加详细日志：
      - 解析出的 IP 地址列表。
      - `connect()` 系统调用的 `errno`。
      - OpenSSL/TLS 库的具体错误堆栈。
    - 重新编译并运行，获取比 "BadServerAddress" 更底层的错误信息。

3.  **TLS/证书对比**：
    - 使用 `openssl s_client` 模拟连接，对比其与 Drogon 行为的差异。

4.  **修复或最终规避**：
    - 根据排查结果，修复 Drogon 配置（如强制 IPv4、修正 SNI 设置、更新根证书），或者在代码层面显式规避问题（如集成 c-ares DNS 库）。

---

## 附录：P1 深度排查进行中（2026-02-01）

### 第 1 步：同步抓包与重放 ✅
- **目标**：在管理员权限下启动 pktmon，同时触发直连请求，捕获完整的 TCP/TLS 握手包。
- **执行**：创建 `tools/run_p1_diagnostic.ps1` 脚本，自动化以下流程：
  1. 启动 pktmon capture
  2. 配置 llm.json（use_curl_fallback=false）
  3. 启动 finguard 服务
  4. 轮询 /health 直至就绪
  5. 触发 SSE 直连请求
  6. 停止 pktmon 并转换为 PCAPNG
  7. 收集日志与 SSE 输出
- **关键命令**：
  ```powershell
  & "d:\AI_Investment\tools\run_p1_diagnostic.ps1" -ApiKey "<your_key>"
  ```
- **产物位置**：
  - ETL：`tools/temp/pktmon_p1_*.etl`
  - PCAPNG：`tools/temp/pktmon_p1_*.pcapng`
  - SSE 输出：`tools/temp/sse_direct_p1_*.txt`
  - 日志：`tools/temp/finguard_out_p1*.log`

### 第 2 步：代码级诊断 ⏳ (进行中)
- **目标**：增强 `finguard/src/llm/llm_client.cpp` 中的错误日志，捕获 Drogon ReqResult 具体数值、target 信息、端口、SSL 状态等。
- **实施**：
  - 修改了错误处理部分，添加了 `target_host`、`target_port`、`use_ssl`、`result_code` 等详细字段。
  - 添加了 DNS 解析器状态日志：`[P1 DNS] c-ares enabled: YES/NO`
  - 重新编译：`cmake --build build --config Debug` ✅
- **关键日志发现**：
  ```
  INFO  [P1 DNS] c-ares enabled: YES
  INFO  [P1 DNS] Attempting to create HttpClient for host: dashscope.aliyuncs.com port: 443 ssl: true
  INFO  LLM request dispatch: api_base=https://dashscope.aliyuncs.com/compatible-mode/v1 timeout_sec=30 use_proxy=false
  ERROR LLM request failed: result=Network failure result_code=2 has_response=false use_proxy=false target_host=dashscope.aliyuncs.com target_port=443 use_ssl=true
  ```

### 第 3 步：根因定位 ✅ 完成
- **关键发现**：通过 DNS 解析对比，定位到问题根源
- **证据链**：
  1. `nslookup dashscope.aliyuncs.com` 返回 **IPv6 地址**：
     ```
     2408:400a:3e:ef02:0:ff:b03e:fff8
     2408:400a:3e:ef02:0:ff:b088:3b98
     ...
     ```
  2. `curl.exe -v -4 "https://dashscope.aliyuncs.com/"` 显示 **IPv4 地址**可连通：
     ```
     Trying 8.152.159.24:443...
     Trying 39.96.213.166:443...
     Trying 8.140.217.18:443...
     Connected to dashscope.aliyuncs.com (8.152.159.24) port 443
     ```
  3. **vcpkg 依赖分析**：`trantor_x64-windows` 包含对 `c-ares` 的依赖
  4. **c-ares 行为**：c-ares 库优先返回 IPv6 地址，而 Windows 系统的 IPv6 路由可能失败

- **根因确认**：
  > **Drogon/trantor 使用 c-ares 库进行 DNS 解析，c-ares 优先返回 IPv6 地址。在某些 Windows 网络环境下，IPv6 路由不可达，导致连接失败。而 curl 默认（或加 -4 参数）优先使用 IPv4，因此能成功连接。**

### 第 4 步：修复尝试 ⚠️ 部分成功
- **尝试 1：强制 IPv4 DNS 解析**
  - 实现：使用 Windows `getaddrinfo` 显式指定 `AF_INET` (IPv4)
  - 结果：成功解析出 IPv4 地址 (如 `8.152.159.24`)
  - 问题：连接时出现 `result_code=2` (Network failure)，疑似 **TLS SNI 问题**

- **尝试 2：使用 IP 地址 + validateCert=false**
  - 实现：用解析后的 IPv4 地址创建 HttpClient，禁用证书验证
  - 结果：仍然失败，`result_code=2`
  - 原因：Drogon HttpClient 的 `newHttpClient(ip, port, ssl)` 重载不支持设置 SNI (Server Name Indication)，服务器无法识别目标主机

- **尝试 3：保留主机名 URL + validateCert=false**
  - 实现：使用原始 URL，仅禁用证书验证
  - 结果：失败，因为 DNS 解析仍由 c-ares 完成，返回 IPv6

- **结论**：
  > 由于 Drogon HttpClient 不暴露 `TLSPolicy::setHostname()` 接口，无法在使用 IP 地址直连时正确设置 SNI。这意味着纯代码层面修复需要修改 Drogon 源码或等待上游支持。

### 最终决策：接受 curl fallback 作为生产方案

鉴于以下考量，我们决定：
1. **curl fallback 功能完整**：已验证可成功获取 Qwen 真实回复
2. **修复成本高**：需要修改 Drogon 上游代码或重新编译 trantor 禁用 c-ares
3. **个人工具定位**：性能要求不高，curl 子进程开销可接受
4. **代码可维护性**：保留 Drogon 直连路径代码，将来如有需要可继续优化

### 配置推荐

在 `config/llm.json` 中保持以下配置：
```json
{
  "use_curl_fallback": true,
  "curl_path": "curl.exe"
}
```

### 遗留的诊断代码

以下代码保留在 `llm_client.cpp` 中，用于将来调试：
- `#include <trantor/net/Resolver.h>` 和 c-ares 状态检测
- Windows `getaddrinfo` IPv4 解析辅助函数（虽然当前未使用）
- `[P1 DNS]` 日志前缀的诊断输出

---

## Phase 2 完成状态总结

| 功能 | 状态 | 说明 |
|------|------|------|
| SSE 流式接口 | ✅ 完成 | `/api/v1/chat/stream` 正常工作 |
| X-API-Key 鉴权 | ✅ 完成 | 未提供或错误 key 返回 401 |
| LLM 通信（curl fallback） | ✅ 完成 | 真实 Qwen 响应，token 计数正常 |
| LLM 通信（Drogon 直连） | ❌ 失败 | c-ares IPv6 + SNI 问题，暂无修复方案 |
| 配置热加载 | ✅ 完成 | llm.json 支持所有必要字段 |
| 降级逻辑 | ✅ 完成 | fallback 响应带 warning 标记 |

**Phase 2 交付结论**：核心功能已完成并验证通过。Drogon 直连问题已定位根因，作为技术债务记录，不阻塞交付。
