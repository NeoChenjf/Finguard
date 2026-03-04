# FinGuard 从零开始完整教程

> **适合人群**：有编程基础（读得懂代码逻辑），但没有 C++ 经验。  
> **目标**：跟着本教程，把 FinGuard 从源码一步步跑起来，理解每个阶段在做什么、为什么这样做。

---

## 前言：这个项目做什么，最终长什么样

FinGuard 是一个"AI 驱动的个人资产配置风控平台"。

用大白话说：你告诉它你的年龄、投资风格、手上持有的股票 ETF，它帮你：
1. 检查持仓是否合理（风控）
2. 给出配置建议（加仓/减仓/持有）
3. 提供流式 AI 问答（像 ChatGPT，但专门聊投资）
4. 给单只股票做价值分析（估值高不高，该不该买）

**最终形态**：一个 Windows 桌面 App（.exe 安装包），双击打开，不需要任何命令行。

**技术实现**：C++ 后端（高性能 HTTP 服务） + React 前端 + Tauri 桌面壳。

---

## 第零章：环境准备

### 你需要安装的东西

#### 1. Visual Studio 2022（C++ 开发核心）

下载地址：https://visualstudio.microsoft.com/

安装时，勾选 **"使用 C++ 的桌面开发"** 工作负载。

> 为什么需要它？C++ 代码需要编译成 .exe，Visual Studio 自带的 MSVC 编译器才能做到这件事。

#### 2. CMake（C++ 构建系统）

下载地址：https://cmake.org/download/

安装时勾选 "Add CMake to system PATH"。

> CMake 是 C++ 世界的"构建描述文件"。你不直接写如何编译，而是写 CMakeLists.txt 描述规则，CMake 帮你生成 Visual Studio 的工程文件。

#### 3. vcpkg（C++ 包管理器）

```powershell
# 克隆到合适位置，例如 C:\vcpkg
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
```

> vcpkg 相当于 C++ 世界的 npm，帮你下载和编译第三方库（比如 HTTP 框架、JSON 解析库等）。

#### 4. Node.js（前端开发环境）

下载地址：https://nodejs.org/（选 LTS 版本）

安装后验证：
```powershell
node --version   # v20.x 或更高
npm --version    # 10.x 或更高
```

#### 5. Rust（打包桌面应用需要）

下载地址：https://rustup.rs/

按提示安装，结束后重开 PowerShell，验证：
```powershell
rustc --version  # rustc 1.70.x 或更高
cargo --version
```

#### 6. Python 3（性能测试脚本需要）

下载地址：https://www.python.org/

安装时勾选 "Add Python to PATH"。

#### 7. Git

下载地址：https://git-scm.com/

---

## 第一章：跑起来第一个请求（Phase 1）

### 项目结构说明

```
finguard/               ← C++ 后端源码
  src/                  ← 主要代码
  tests/                ← 单元测试
  config/               ← 配置文件（JSON/YAML）
  CMakeLists.txt        ← 构建描述文件
  build/                ← 编译输出（自动生成）
frontend/               ← React 前端
tools/                  ← 压测、诊断脚本
```

### 第一步：安装 C++ 依赖

项目依赖的第三方库（HTTP 框架、JSON 库等）全部通过 vcpkg 管理，已在 `finguard/CMakeLists.txt` 中声明好了。

```powershell
cd d:\AI_Investment\finguard
```

### 第二步：生成构建文件

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
```

**逐段解释**：
- `cmake`：调用 CMake 工具
- `-S .`：Source 目录是当前目录（`.`，即 `finguard/`）
- `-B build`：Build 目录放在 `build/` 子文件夹
- `-DCMAKE_TOOLCHAIN_FILE=...`：告诉 CMake "用 vcpkg 来找第三方库"
- `-DVCPKG_TARGET_TRIPLET=x64-windows`：目标是 64 位 Windows 动态链接库

> 第一次运行会很慢（10-30 分钟），vcpkg 要下载并编译所有依赖。喝杯茶，等它跑完。

### 第三步：编译

```powershell
cmake --build build --config Debug
```

成功后，`build/Debug/finguard.exe` 就生成了。

### 第四步：准备配置文件

后端读取 `config/` 目录下的配置文件。确认这些文件存在（首次可能需要从样本复制）：
- `config/llm.json`：AI 大模型连接配置
- `config/rules.yaml`：风控规则
- `config/rate_limit.json`：限流配置
- `config/timeout.json`：超时配置
- `config/circuit_breaker.json`：熔断配置
- `config/concurrency.json`：并发配置

### 第五步：启动后端

```powershell
cd d:\AI_Investment\finguard\build\Debug
.\finguard.exe
```

看到类似这样的输出说明启动成功：
```
[INFO] Server running on http://0.0.0.0:8080
```

### 第六步：验证

打开新的 PowerShell 窗口：

```powershell
# 健康检查
curl http://localhost:8080/health

# 期望返回
{"status":"ok"}
```

```powershell
# 测试投资组合建议接口（会返回 mock 数据）
curl -X POST http://localhost:8080/api/v1/plan `
  -H "Content-Type: application/json" `
  -d '{"profile":{"age":35,"type":"novice"},"holdings":[]}'
```

---

## 第二章：接入 AI 大模型（Phase 2）

Phase 1 里后端只是能跑起来，AI 问答功能还不能用。这一章配置 AI 接入。

### 核心概念：SSE 流式输出

ChatGPT 为什么能一个字一个字蹦出来，而不是等全部生成完再显示？

这用到了 **SSE（Server-Sent Events）**——服务端把数据分成小块，陆续推送给客户端。每个小块叫一个"事件"，格式像这样：
```
data: {"type":"token","content":"你好"}

data: {"type":"token","content":"，今天"}

data: {"type":"done"}
```

FinGuard 的后端用 C++ 实现了这个流式推送，前端接收到 `token` 事件就立刻渲染文字，实现打字机效果。

### 配置 llm.json

编辑 `finguard/config/llm.json`：

```json
{
  "api_key": "你的API_Key",
  "api_base": "https://dashscope.aliyuncs.com/compatible-mode/v1",
  "model": "qwen-plus",
  "max_tokens": 2048,
  "temperature": 0.7,
  "use_curl_fallback": true
}
```

> **api_key**：到阿里云百炼平台（https://bailian.console.aliyun.com/）申请，免费额度够用。  
> **use_curl_fallback**：这个设为 `true` 是必须的！（下面解释为什么）

### 一个有趣的 Bug：IPv6 和 curl 兜底

Drogon（C++ HTTP 框架）内置用 c-ares 库解析域名，c-ares 在某些网络环境下优先返回 IPv6 地址，而 Windows 本机到阿里云的 IPv6 路由不通，导致请求失败。

**解决方案**：把 `use_curl_fallback` 设为 `true`，后端会改用系统的 `curl.exe` 发送 HTTP 请求。curl 走的是系统网络栈，IPv6 路由问题自动避开。

这个方案被接受为正式生产方案（不是临时措施），稳定性已验证。

### 测试流式问答

```powershell
curl -X POST http://localhost:8080/api/v1/chat/stream `
  -H "Content-Type: application/json" `
  -H "X-API-Key: 你的API_Key" `
  -d '{"messages":[{"role":"user","content":"什么是价值投资？"}]}'
```

你会看到数据一块一块返回，这就是 SSE 在工作。

---

## 第三章：加入金融风控规则（Phase 3）

到这里，AI 能问答了，但没有任何金融风控。这一章加入规则引擎，让 AI 知道"这个用户能不能问这个问题"。

### 核心概念：规则引擎

规则引擎就是一段"如果…那么…"的逻辑处理器。

FinGuard 的规则引擎做两件事：
1. **检查请求**（`check_request`）：用户问的问题有没有违禁词？用户的风险等级允不允许问高风险问题？
2. **检查响应**（`check_response`）：AI 回答里有没有不合规的内容（比如给出绝对化承诺"一定涨"）？

### rules.yaml 结构说明

```yaml
# 问卷方案：定义用户画像的字段
questionnaire_schema:
  fields:
    - name: age         # 年龄（决定债券占比）
    - name: type        # 投资人类型（小白/经验丰富/专业）
    - name: years_experience  # 经验年限

# 不同类型用户的持仓限制
profile_limits:
  novice:              # 小白投资人
    max_single_stock: 0.0    # 不允许持个股
    allowed_assets: [VOO, VGIT, GLD]
  experienced:         # 经验丰富
    max_single_stock: 0.5    # 个股最多占 50%
  professional:        # 专业投资人
    max_single_stock: 1.0    # 无限制

# 违禁词/高风险词
risk_keywords:
  - "保证收益"
  - "一定涨"
```

### 触发风险警告

填写用户画像时，如果持仓超过限制，后端会在 SSE 流中插入一个 `warning` 事件：

```json
{
  "type": "warning",
  "code": "POSITION_LIMIT_EXCEEDED",
  "message": "小白投资人不建议持有个股，请考虑 ETF 代替",
  "severity": "WARN"
}
```

前端收到后，用特殊样式渲染这条警告（黄色/红色提示框）。

---

## 第四章：让服务更稳定（Phase 4）

后端通了，AI 也接上了，但如果用户很多，或者 AI 接口不稳定，服务会崩吗？这一章加入四个稳定性机制。

### 四个稳定性配置

| 配置文件 | 功能 | 通俗理解 |
|---|---|---|
| `rate_limit.json` | 令牌桶限流 | 每秒最多接 N 个请求，超了排队或拒绝 |
| `circuit_breaker.json` | 熔断器 | AI 接口连续失败 N 次就"断路"，一段时间内不再尝试，防止雪崩 |
| `timeout.json` | 超时配置 | 等 AI 最多等多少毫秒，超了就报错 |
| `concurrency.json` | 并发限制 | 同时处理最多 N 个请求，超了直接返回"忙" |

### 令牌桶限流（详细解释）

想象一个桶，桶里装着令牌。每秒会往桶里加 N 个令牌（这就是"速率"）。来一个请求，就从桶里取一个令牌；桶空了，请求等待或被拒绝。

```json
// rate_limit.json
{
  "global_rps": 100,    // 整体每秒最多 100 个请求
  "chat_rps": 20,       // /chat/stream 每秒最多 20 个
  "capacity": 200       // 桶的容量（允许短时突发）
}
```

### 熔断器（详细解释）

三种状态：
- **Closed（正常）**：正常转发请求
- **Open（断路）**：AI 接口失败太多，暂时不发请求，直接返回错误
- **Half-Open（试探）**：断路一段时间后，放一个请求进去试试，成功了就恢复正常

```json
// circuit_breaker.json
{
  "failure_threshold": 5,     // 连续失败 5 次就断路
  "recovery_timeout_ms": 30000, // 断路 30 秒后进入半开状态
  "success_threshold": 2       // 试探成功 2 次才完全恢复
}
```

### 验证限流效果

```powershell
# 快速发 50 个请求，看哪些被限流（返回 429）
1..50 | ForEach-Object { 
  curl -s -o /dev/null -w "%{http_code}\n" http://localhost:8080/health 
}
```

---

## 第五章：性能测试（Phase 5）

功能做完了，现在测一下"能扛多少用户"。

### 核心发现：配置文件热加载的内存泄漏

在性能测试中发现一个严重问题：**服务器内存持续增长**，运行 10 分钟后 RSS（内存占用）增长了 57.76%。

**根本原因**：每次请求都要从磁盘读一次配置文件（`rate_limit.json`、`timeout.json` 等），大量的文件 I/O 操作导致内存无法及时回收。

**解决方案：ConfigCache 模板缓存**

```cpp
// C++ 代码，读懂大意就好
template <typename T>   // T 可以是任何类型（限流配置、超时配置等）
struct ConfigCache {
    std::mutex mtx;           // 互斥锁，保证线程安全
    T value{};                // 缓存的值
    time_point loaded_at{};   // 上次加载的时间
    
    template <typename Loader>
    T get(Loader loader) {    // 获取配置
        auto now = Clock::now();
        if (now - loaded_at > 5s) {   // 超过 5 秒才重新加载
            value = loader();          // 调用传入的加载函数
            loaded_at = now;
        }
        return value;
    }
};
```

用大白话说：**给每种配置建一个"缓存盒子"，5 秒内同一种配置只读一次磁盘，多次请求共享这个缓存**。

这个 `ConfigCache<T>` 是 FinGuard 中**唯一真正用到 C++ 模板**的地方。

**优化效果**：

| 指标 | 优化前 | 优化后 | 提升 |
|---|---|---|---|
| 内存增长（10分钟） | +57.76% | -0.09% | 消除泄漏 |
| 配置建议 P95 延迟 | ~640ms | ~250ms | 减少 61% |
| 配置建议 RPS | ~55 | ~135 | 提升 145% |
| 问答 RPS | ~17 | ~25 | 提升 47% |

### 运行压测脚本

项目自带 Python 压测工具：

```powershell
# 先确保后端在运行，然后：
cd d:\AI_Investment\tools
python .\p5_bench.py
```

脚本会并发发请求，输出类似：
```
=== 配置建议 /api/v1/plan ===
持续: 60s  并发: 20
RPS: 134.5  P50: 142ms  P95: 248ms  P99: 412ms
成功率: 99.8%
```

---

## 第六章：单元测试与生产构建（Phase 6）

### 单元测试

FinGuard 使用 **gtest**（Google Test）做单元测试，共 30 个测试用例，覆盖：
- 令牌桶算法正确性
- 熔断器状态机转换
- 风控规则匹配
- 配置解析

```powershell
cd d:\AI_Investment\finguard\build
ctest --output-on-failure
```

全部 PASS 才能上生产。

### Release 构建（生产版本）

Debug 版本体积大、速度慢，生产环境用 Release：

```powershell
cmake --build build --config Release
```

输出在 `build/Release/finguard.exe`，体积约 2-3MB（Debug 版本约 10MB+）。

### Settings API

Phase 6 新增了 Settings 接口，用于在不重启的情况下热更新 AI 配置：

```powershell
# 查看当前配置（API Key 会被掩码显示）
curl http://localhost:8080/api/v1/settings

# 更新配置
curl -X POST http://localhost:8080/api/v1/settings `
  -H "Content-Type: application/json" `
  -d '{"api_key":"新的Key","model":"qwen-max"}'
```

---

## 第七章：前端界面（Phase 7）

后端功能完整了，现在做用户界面。

### 技术栈

| 技术 | 用途 | 类比 |
|---|---|---|
| Vite | 构建工具 | 相当于 webpack，但快很多 |
| React 18 | UI 框架 | 组件化 UI |
| TypeScript | 类型安全的 JS | 加了类型检查的 JavaScript |
| TailwindCSS v4 | 样式 | CSS 工具类，不写 CSS 文件直接用类名 |
| Zustand | 状态管理 | 比 Redux 简单的全局 store |
| React Router v6 | 路由 | 单页应用的页面跳转 |

### 安装依赖并启动

```powershell
cd d:\AI_Investment\frontend
npm install           # 安装所有依赖（约 178 个包）
npm run dev           # 启动开发服务器
```

浏览器打开 http://localhost:5173 就能看到界面了。

> 注意：前端需要后端在 8080 端口运行，才能正常使用。

### 四个页面说明

**1. 启动页（自动显示）**

第一次打开，或后端未连接时显示。它每隔 1 秒访问一次 `http://localhost:8080/health`，连上后自动跳转。

**2. 设置页（`/settings`）**

填写 API Key 和模型信息。保存后会同时：
- 更新后端配置（POST /api/v1/settings）
- 持久化到本地存储（下次打开不用重填）

**3. 配置建议页（`/plan`）**

左侧填表单，右侧看结果：
- 年龄滑块（影响债券比例，"年龄的十位数" = 债券占比）
- 投资人类型三选一（决定能不能买个股）
- 当前持仓（支持动态增删，实时计算合计比例）
- 右侧显示：建议权重（进度条）、风控报告（PASS/WARN/FAIL 徽章）、操作清单（买/卖/持有）

**4. 流式问答页（`/chat`）**

ChatGPT 风格界面：
- 消息气泡（用户蓝色，AI 灰色）
- 打字机效果（SSE 逐 token 渲染）
- `cite`（引用数据）、`warning`（风控提示）、`metric`（耗时信息）以特殊卡片展示
- Enter 发送，Shift+Enter 换行
- 红色停止按钮可中断生成

### 守拙价值配置理念

FinGuard 内置了"守拙价值多元化基金"的配置理念，算法如下：

```
黄金：固定 10%
债券：年龄的十位数（比如 35 岁 → 30%）
股票：剩余部分（100% - 10% - 30% = 60%）
  其中：
    港股 : A股 : 美股 = 1 : 3 : 16
    个股占比：小白 0%，经验丰富 ≤50%，专业 ≤ 全部股票仓位
```

---

## 第八章：打包成桌面应用（Phase 8）

现在后端是独立的 .exe，前端是在浏览器里运行的网页。这一章用 **Tauri** 把它们打包成一个桌面应用。

### Tauri 是什么

Tauri 是一个跨平台桌面应用框架，用 Rust 写的。它的核心思想是：
- **前端**：继续用 Web 技术（React），放在系统自带的 WebView 里显示
- **后端原生层**：用 Rust 代码管理窗口、系统托盘、文件等原生功能

在 FinGuard 里，Tauri 还有一个特殊用途：管理 finguard.exe（C++ 后端）的进程生命周期。

### Sidecar 机制

**Sidecar** 就是"跟随主程序一起跑的伴随进程"。

用户双击 FinGuard 桌面应用时：
1. Tauri（Rust）主程序启动
2. 主程序自动启动 finguard.exe（C++ 后端），这就是 sidecar
3. 前端（React）加载，轮询 `/health` 直到后端就绪
4. 用户关闭窗口时，主程序先 kill finguard.exe，然后自己退出

这样用户看到的就是一个应用，不需要手动管理两个进程。

### 开发模式启动

```powershell
cd d:\AI_Investment\frontend
npm run tauri:dev
```

第一次运行会编译 Rust 代码（约 3-5 分钟），之后热更新很快。

### 关键配置文件

**`frontend/src-tauri/tauri.conf.json`**：Tauri 主配置文件
```json
{
  "app": {
    "windows": [{ "width": 1280, "height": 800 }]
  },
  "bundle": {
    "externalBin": ["binaries/finguard"]  // ← sidecar 声明
  }
}
```

**`frontend/src-tauri/src/lib.rs`**：Rust 核心逻辑
- 启动 sidecar（`spawn_sidecar()`）
- 杀 sidecar（`kill_sidecar()`）
- 监听 sidecar 崩溃，自动重启（最多 3 次）
- 系统托盘（显示窗口 / 退出）

### 已知问题与解决

| 问题 | 原因 | 解决 |
|---|---|---|
| sidecar 启动即崩溃（0xC0000135） | 10 个 DLL 没跟着复制 | `build.rs` 自动复制 DLL |
| 设置保存失败（write_failed） | sidecar 工作目录没有 config/ | 通过 `cmd.current_dir()` 设置正确工作目录 |
| 关闭后进程残留 | 仅隐藏了窗口，没退出 | 改为关闭即退出 + kill sidecar |

---

## 第九章：生成安装包（Phase 9）

桌面应用做好了，现在打包成 .exe 安装包，发给别人直接用。

### 一键打包

```powershell
cd d:\AI_Investment\frontend
npm run tauri build
```

这个命令会：
1. 编译 Release 版 Rust 代码
2. Build Release 版前端（npm run build）
3. 用 NSIS 打包成 Windows 安装程序

输出文件（约 4.4 MB）：
```
frontend/src-tauri/target/release/bundle/nsis/FinGuard_1.0.0_x64-setup.exe
```

### 安装包做了什么

用户双击安装包，NSIS 会：
1. 把 FinGuard 复制到 `C:\Program Files\FinGuard\`
2. 在桌面和开始菜单创建快捷方式
3. 把默认配置文件初始化到 `C:\Users\你的用户名\AppData\Roaming\finguard\`（这是 `%APPDATA%\finguard\`）

### 配置文件存在 APPDATA 的好处

- 用户即使没有管理员权限也能修改配置
- 卸载程序不会删除配置（用户的 API Key 保留）
- 多个用户可以有各自独立的配置

### 首次运行体验

1. 安装完成，双击桌面图标
2. 自动拉起 finguard.exe 后端
3. 启动页显示"正在连接…"
4. 健康检查通过，自动跳到设置页（因为还没有 API Key）
5. 填入 API Key，保存
6. 进入主界面，所有功能可用

---

## 第十章：量化价值分析（Phase 10）

最后一章，给平台加上"价值分析"功能，输入一个股票代码，自动分析它值不值得买。

### 分析流程

```
用户输入 "AAPL"
    ↓
并发发出 3 个外部请求
    ↓
1. Yahoo Finance      2. Tavily 搜索        3. （无需额外请求）
   - 当前 PE             - 护城河分析
   - 历史 PE 均值         - 管理层评价
   - PEG 比率            - 商业模式分析
   - 市净率 PB
    ↓             ↓
         汇总所有信息，构建 LLM Prompt
                    ↓
              AI 模型生成定性分析（0-10 评分）
                    ↓
              计算安全边际（PE < 历史均值 AND PEG < 1.0）
                    ↓
              返回完整报告
```

### 配置 valuation.json

```json
// finguard/config/valuation.json
{
  "tavily_api_key": "tvly-xxxx",    // 到 https://tavily.com 申请，免费 1000次/月
  "alpha_vantage_api_key": "",       // 备用数据源，可不填
  "yahoo_timeout_ms": 5000,
  "search_max_results": 5,
  "pe_history_quarters": 6
}
```

### 安全边际判断逻辑

```
安全边际 = (当前PE < 历史PE均值) AND (PEG < 1.0)
```

| 情况 | 结论 |
|---|---|
| 当前 PE 28 < 历史 PE 均值 32，PEG 0.91 < 1.0 | ✅ 有安全边际（可能低估） |
| 当前 PE 35 > 历史 PE 均值 32 | ❌ 无安全边际 |
| EPS 为负（亏损公司） | ⚠️ 无法计算 PE |

### 调用接口

```powershell
curl -X POST http://localhost:8080/api/v1/valuecell `
  -H "Content-Type: application/json" `
  -d '{"symbol":"AAPL"}'
```

响应（约 3-8 秒后）：
```json
{
  "symbol": "AAPL",
  "qualitative_score": 7.5,
  "current_pe": 28.4,
  "historical_pe_mean": 32.1,
  "current_peg": 0.91,
  "price_to_book": 45.2,
  "safety_margin": true,
  "investment_conclusion": "## Value Cell 分析报告 — AAPL\n..."
}
```

### 前端价值分析页面

前端访问 `/valuecell`，有以下功能：
- 输入框填股票代码（AAPL、MSFT、TSLA 等）
- 快捷按钮（AAPL / MSFT / GOOGL / NVDA）
- 核心指标卡片（安全边际、PE、PEG、PB、定性评分）
- 完整 Markdown 报告渲染
- 智能布局：数据不足时，优先显示定性分析

### 当前限制

- Yahoo Finance 的非官方 API 有时会返回 429（限流）
- Alpha Vantage 免费版每天限 25 次请求
- 代理网络配置可能影响数据获取

---

## 常见报错处理

### 报错：cmake 找不到 vcpkg

**症状**：`CMake Error: Could not find a package configuration file provided by "drogon"`

**原因**：没有正确指定 vcpkg toolchain 文件路径

**解决**：
```powershell
# 确认 vcpkg 路径正确
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

---

### 报错：finguard.exe 启动后立刻退出

**症状**：程序一闪而过，没有输出

**原因**：通常是配置文件路径找不到

**解决**：
```powershell
# 切换到包含 config/ 目录的位置再启动
cd d:\AI_Investment\finguard\build\Debug
.\finguard.exe
# 或 Release
cd d:\AI_Investment\finguard\build\Release
.\finguard.exe
```

---

### 报错：配置保存失败（write_failed）

**症状**：POST /api/v1/settings 返回 `{"error":"write_failed"}`

**原因**：当前工作目录下找不到 `config/` 文件夹

**解决**：从 `finguard.exe` 所在目录（即 `config/` 的兄弟目录）启动程序。

---

### 报错：AI 请求失败（IPv6 问题）

**症状**：curl `/api/v1/chat/stream` 长时间等待后报错

**原因**：Drogon 的 DNS 解析走了 IPv6，本机没有到阿里云的 IPv6 路由

**解决**：确保 `config/llm.json` 中 `"use_curl_fallback": true`

---

### 报错：sidecar 启动崩溃（Tauri 开发模式）

**症状**：`npx tauri dev` 后窗口弹出又消失，sidecar-status 显示 error

**原因**：`binaries/` 目录中缺少 finguard.exe 的运行时 DLL

**解决**：
```powershell
# 先编译 Release 版后端
cd d:\AI_Investment\finguard
cmake --build build --config Release
# Tauri build.rs 会自动从 build/Release/ 复制 DLL
npm run tauri:dev
```

---

### 报错：前端 Failed to fetch

**症状**：浏览器控制台 `net::ERR_FAILED`，请求发不到后端

**原因**：开发模式下，Vite（5173 端口）请求后端（8080 端口），触发浏览器同源策略

**解决**：Vite 配置了代理，确保 `vite.config.ts` 中有：
```typescript
proxy: {
  '/api': 'http://localhost:8080',
  '/health': 'http://localhost:8080',
}
```

---

### 报错：配置文件 YAML 解析失败

**症状**：`[ERROR] Failed to parse rules.yaml`

**原因**：文件有 BOM（字节顺序标记），YAML 解析器不认识

**解决**：
```powershell
# 用 PowerShell 保存不含 BOM 的 UTF-8
[System.IO.File]::WriteAllText(
  "d:\AI_Investment\finguard\config\rules.yaml",
  (Get-Content "d:\AI_Investment\finguard\config\rules.yaml" -Raw),
  [System.Text.UTF8Encoding]::new($false)
)
```

---

## 快速参考：所有 API 接口

| 路径 | 方法 | 功能 |
|---|---|---|
| `/health` | GET | 健康检查，返回 `{"status":"ok"}` |
| `/api/v1/settings` | GET | 获取当前 AI 配置（API Key 掩码显示） |
| `/api/v1/settings` | POST | 更新 AI 配置 |
| `/api/v1/plan` | POST | 获取投资组合配置建议 |
| `/api/v1/chat/stream` | POST | 流式 AI 问答（SSE） |
| `/api/v1/valuecell` | POST | 单股价值分析 |

---

## 快速参考：所有配置文件

| 文件 | 说明 |
|---|---|
| `config/llm.json` | AI 模型连接（API Key、模型名、是否用 curl 兜底） |
| `config/rules.yaml` | 风控规则（违禁词、用户画像限制、持仓上限） |
| `config/rate_limit.json` | 限流（每秒最多接多少请求） |
| `config/circuit_breaker.json` | 熔断（失败多少次断路、多久恢复） |
| `config/timeout.json` | 超时（等 AI 最多多少毫秒） |
| `config/concurrency.json` | 并发（同时处理多少请求） |
| `config/valuation.json` | 价值分析（Tavily API Key、Yahoo 超时等） |

---

*最后更新：Phase 10 完成后*
