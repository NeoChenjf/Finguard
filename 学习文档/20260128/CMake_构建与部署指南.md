## CMake 构建与部署指南（从代码到可执行文件的生活化说明）

### 目标

把“写完代码”到“得到 `finguard.exe`”的完整流程，用生活化的比喻与关键命令写成一份教学文档，便于团队新人快速理解构建流程与常见注意事项。

### 快速结论（一句话）

代码是产品的设计稿，CMake 把设计稿变成装配单，生成器（如 Visual Studio）按装配单生产零件并组装成成品 `finguard.exe`。

---

### 全流程分解（生活化比喻）

1. 写代码 —— 画设计图

   - 你把功能、类、路由写进 `.cpp` / `.h`，就像在纸上画出电路与产品外形。

2. 配置（cmake configure） —— 签合同、定材料规格

   - 命令示例：

```powershell
cmake -S d:\AI_Investment\finguard -B d:\AI_Investment\finguard\build
```

   - 类比：和供应商/施工队商量要用哪种材料（C++标准、编译选项）、是否要测试（-DBUILD_TESTS=ON），并把选择写进合同（`CMakeCache.txt`）。

3. 取材（find_package / vcpkg） —— 去材料超市采购零件

   - 常见第三方：`nlohmann_json`（JSON 解析器）、`spdlog`（日志）、`Drogon`（HTTP 框架）。通常由 `vcpkg` 或系统包管理器提供。

   - 参考配置文件：[finguard/config/llm.json.example](finguard/config/llm.json.example)

4. 生成施工单（cmake generate → .sln/.vcxproj） —— 施工队把合同变成工单

   - 生成器会把 `CMakeLists.txt` 转为 Visual Studio 的 `.sln`/`.vcxproj`，或 Makefile。

5. 加工零件（编译：.cpp -> .obj） —— 工人把材料做成零件

   - 编译示例（使用 cmake --build）：

```powershell
cmake --build d:\AI_Investment\finguard\build --config Debug
```

   - 每个源文件被编译成对象文件（Windows 下通常是 `.obj`）。

6. 装配（链接：.obj + 库 -> .exe/.dll） —— 把零件焊接组装成整机

   - 链接时会把项目自己的 `.obj` 和第三方库（静态或动态）组合成可执行文件 `finguard.exe` 或若干 DLL。

7. 质检（可选测试） —— 出厂前的检验

   - 若在配置时启用测试：`-DBUILD_TESTS=ON`，CMake 会生成测试目标（例如 `finguard_tests`），并可用 `ctest` 运行。

8. 运行与调试 —— 试机与记录故障日志

   - 运行时需要的配置/凭证（示例：`finguard/config/llm.json.example` 中的 `api_key`）必须填写好。

   - 用 `spdlog` 写的日志相当于车间记录本，便于定位问题。

9. 打包与部署 —— 装箱、配送到客户

   - 把可执行和所需 DLL、配置文件打包成安装包或放到服务器上运行。

10. 交付与维护 —— 固件升级与更换部件

    - 线上问题修复、依赖升级、配置调整都是后续迭代工作。

---

### 关键文件与命令（操作提示）

- 查看当前生成器与路径信息（在 `build` 目录里打开 `CMakeCache.txt`）：记录了 `CMAKE_GENERATOR`、依赖路径等。
- 常用命令：

```powershell
# 配置并生成构建系统
cmake -S d:\AI_Investment\finguard -B d:\AI_Investment\finguard\build

# 编译（在 Windows/Visual Studio 环境下）
cmake --build d:\AI_Investment\finguard\build --config Release

# 运行测试（如果启用了测试）
ctest --test-dir d:\AI_Investment\finguard\build --output-on-failure
```

### 常见问题与排查建议

- 依赖找不到：确认 `vcpkg` 或系统包已安装，并且 `CMake` 能看到其路径。查看 `CMakeCache.txt` 中相关条目。
- 运行时报错缺 DLL：说明链接为动态库，需把对应 DLL 放到可执行同级目录或系统 PATH 中。
- 日志为空或不完整：查看 `spdlog` 配置与日志级别，确保日志目录有写权限。

---

### 写作与组织规范参考

- 团队文档风格、写作规范请参见员工手册： [workbook/README.md](workbook/README.md)
- 学习文档目录说明请参见： [学习文档/README.md](学习文档/README.md)

---

### 建议的后续操作（供新人使用）

1. 在本地按文档运行一次完整流程：`cmake configure` → `cmake build` → 运行 `finguard.exe`。
2. 把 `finguard/config/llm.json.example` 复制为 `llm.json` 并填入真实 `api_key`（注意不要提交真实密钥）。
3. 若要 CI 自动化，写一个简单的脚本把上面的命令放进流水线，保证可重复构建。

---

如果你希望我把文档迁移到其他语言、加入图示、或把每个 CMakeLists 段落逐行映射到产物（比如哪些源文件生成哪些目标），告诉我下一步我继续完善。
