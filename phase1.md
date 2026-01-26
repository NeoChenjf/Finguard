# Phase 1 文档

## 1. 一阶段总览
- 目标：搭建 C++ 服务骨架并提供基础接口与最小可运行链路。
- 范围：项目目录结构、CMake 骨架、基础路由（/health、/api/v1/plan）、风控规则占位、README 最小交付。

## 2. Todo List
- 已完成
  - 建立项目目录结构（`finguard/` + `src/`, `config/`, `tests/`, `docker/`）。
  - 创建 `CMakeLists.txt`（Drogon、spdlog、nlohmann/json、gtest 占位）。
  - 实现 `/health` 与 `/api/v1/plan` mock 路由。
  - 添加 `config/rules.yaml` 风控规则占位。
  - 编写 `README.md` 启动说明与 curl 示例。
- 未完成
  - 无（Phase 1 既定范围内任务已完成）。

## 3. 一阶段工作日志
- 2026-01-21：建立项目骨架与最小服务路由；补齐规则配置与 README。
- 问题与解决
  - 中文文档乱码风险：统一以 UTF-8（带 BOM）保存。

### 验收与结果（2026-01-21）
- 验收目标：确认 Phase 1 的目录骨架、构建配置与基础路由就绪。
- 执行步骤：
  1) `cmake -S . -B build`（在 `finguard/` 目录）
- 结果：
  - 失败：缺少 Drogon 包（未找到 `DrogonConfig.cmake`）。
  - 结论：Phase 1 的文件与骨架已完成，但本机环境尚未安装 Drogon 依赖，构建验收需在依赖就绪后复验。

### 依赖安装流程记录（2026-01-21）
- 目标：安装 vcpkg 并获取 Drogon / spdlog / nlohmann-json / gtest 依赖。
- 实际流程（最便捷路径：vcpkg）：
  1) 克隆 vcpkg：
     - `git clone --depth 1 https://github.com/microsoft/vcpkg.git d:\AI_Investment\vcpkg2`
     - `git clone --depth 1 https://github.com/microsoft/vcpkg.git C:\Users\11956\vcpkg`
  2) 启动 bootstrap：
     - `.\bootstrap-vcpkg.bat`（在 vcpkg 目录）
  3) 计划安装依赖：
     - `vcpkg install drogon spdlog nlohmann-json gtest`
- 结果与问题：
  - 在 `d:\AI_Investment\vcpkg2`：bootstrap 下载 vcpkg.exe 失败（访问被拒绝）。
  - 在 `C:\Users\11956\vcpkg`：bootstrap 下载 vcpkg.exe 失败（vcpkg.exe 写入被拒绝，文件大小为 0）。
  - 直接下载 vcpkg.exe（Invoke-WebRequest）时出现访问被拒绝或无法连接。
- 结论：
  - 依赖安装被系统权限/网络限制阻断，当前无法完成 vcpkg bootstrap。
  - 待确认系统是否启用“受控文件夹访问/安全策略”，或以管理员权限在非受保护目录执行。

### 依赖安装追加尝试（VPN 已开启）
- 2026-01-21：
  - `git clone --depth 1 https://github.com/microsoft/vcpkg.git d:\AI_Investment\tools\vcpkg`
  - 结果：失败（`Recv failure: Connection was reset`）。

### 依赖安装追加尝试（镜像路径）
- 2026-01-21：
  - 使用 Gitee 镜像克隆 vcpkg：
    - `git clone --depth 1 https://gitee.com/mirrors/vcpkg.git d:\AI_Investment\tools\vcpkg`（成功）
  - vcpkg bootstrap：
    - `.\bootstrap-vcpkg.bat`（失败，vcpkg.exe 下载超时，生成 0 字节文件）
  - 备用下载 vcpkg.exe：
    - `https://gitee.com/jackboosy/vcpkg-tool/releases/download/2025-12-16/vcpkg.exe`（成功下载到 `d:\AI_Investment\tools\vcpkg.exe`）
    - 使用方式：`$env:VCPKG_ROOT="d:\AI_Investment\tools\vcpkg"; & "d:\AI_Investment\tools\vcpkg.exe" version`（成功）
  - 安装依赖：
    - `vcpkg install drogon spdlog nlohmann-json gtest --triplet x64-windows`（失败）
    - 失败原因：vcpkg 需要 PowerShell 7.5.4，下载 `PowerShell-7.5.4-win-x64.zip` 时失败（WinHttp 12152）
  - PowerShell 备用安装尝试：
    - `winget install --id Microsoft.PowerShell --source winget`（失败，InternetOpenUrl 0x80072efd）
    - `ghproxy / fastgit` 镜像下载（失败，无法连接）
    - `https://gitee.com/mirrors/powershell/releases/...`（404）

### 依赖安装追加尝试（2026-01-21）
- 目标：安装 PowerShell 7.5.4 以满足 vcpkg 依赖安装要求。
- 尝试方式：
  - `winget install --id Microsoft.PowerShell --source winget`
  - `Invoke-WebRequest https://github.com/PowerShell/PowerShell/releases/download/v7.5.4/PowerShell-7.5.4-win-x64.msi`
  - `ghproxy / fastgit` 镜像下载
- 结果：
  - `winget` 下载长时间无响应，临时目录生成 0 字节文件。
  - `Invoke-WebRequest` 无法连接到远程服务器。
  - 镜像下载失败（无文件生成）。
- 结论：
  - 当前环境网络访问受限，无法获取 PowerShell 7.5.4 安装包，导致 vcpkg 依赖安装仍被阻断。
### 依赖安装跟进记录（2026-01-21）
- 现状判断：本机未安装 PowerShell 7.5.4（仅有 Windows PowerShell 5.1），因此 vcpkg 依赖安装被阻断。
- 证据：系统仅存在 `powershell.exe`，未发现 `pwsh.exe`；安装包路径未提供。
- 已尝试：
  - `winget install --id Microsoft.PowerShell --source winget`（下载卡住，临时文件 0 字节）
  - `Invoke-WebRequest` 直连 GitHub 下载 MSI（无法连接）
  - 代理镜像下载（无文件生成）
- 结论：网络访问受限，无法在线获取 PowerShell 7.5.4 安装包。
- 下一步建议：
  1) 由用户提供 PowerShell 7.5.4 离线安装包（MSI 或 ZIP）本地路径；
  2) 或开放下载白名单/代理，允许从 GitHub 获取安装包；
  3) 或由管理员在可联网机器下载后通过 U 盘拷贝。
### 依赖安装与验收跟进（2026-01-21）
- PowerShell 7.5.4 获取：
  - 直连 GitHub 下载 MSI：`d:\AI_Investment\tools\PowerShell-7.5.4-win-x64.msi`（成功）
  - MSI 安装失败：写注册表 `HKLM\Software\Microsoft\PowerShellCore` 权限不足（Return value 3）。
  - 改用 ZIP 便携包：下载并解压到 `d:\AI_Investment\tools\pwsh-7.5.4`，使用 `pwsh.exe`。
- vcpkg 依赖安装：
  - `d:\AI_Investment\tools\vcpkg\vcpkg.exe install drogon spdlog nlohmann-json gtest --triplet x64-windows`
  - 结果：成功安装。
- 构建与验收：
  - `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=d:/AI_Investment/tools/vcpkg/scripts/buildsystems/vcpkg.cmake`
  - `cmake --build build`
  - 结果：构建成功，生成 `build/Debug/finguard.exe`。
### 验收与结果（2026-01-21 追加）
- 验收目标：完成依赖安装并成功构建 Phase 1。
- 执行步骤：
  1) `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=d:/AI_Investment/tools/vcpkg/scripts/buildsystems/vcpkg.cmake`
  2) `cmake --build build`
- 结果：
  - 成功：生成 `build/Debug/finguard.exe`。
  - 结论：Phase 1 验收通过。
- 已达到的效果：构建通过并生成可执行文件，Phase 1 基础接口代码已具备运行条件，依赖安装可复用。
### 运行验收记录（2026-01-21 追加）
- 执行：`d:\AI_Investment\finguard\build\Debug\finguard.exe`
- 请求：
  - `GET http://localhost:8080/health` -> `{ "status": "ok" }`
  - `POST http://localhost:8080/api/v1/plan` -> 返回 mock 配置建议
- 结果：
  - 成功：服务可启动，基础接口可访问并返回预期结果。
  - 结论：Phase 1 完成且运行验证通过。