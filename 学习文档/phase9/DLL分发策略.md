# 把 DLL 放到正确位置：Tauri Sidecar 的分发策略入门（Phase 9）

> 目标：看完后你能清楚解释这四个概念——`DLL`、`bundle.resources`、`<install_dir>`、`sidecar exe`，并能独立检查你的打包是否正确。

## 1) 学习路线（从易到难）

1. 先理解 DLL 是什么，为什么 `finguard.exe` 运行要依赖它。
2. 再理解 sidecar exe 是什么，它和 Tauri 主程序是什么关系。
3. 再看 `tauri.conf.json` 的 `bundle.resources`：它如何把 DLL 带进安装包。
4. 最后理解 `<install_dir>`：安装后文件真实落地在哪里，为什么要和 sidecar 放同级。

## 2) 核心概念解释（白话 + 类比）

### 2.1 DLL 是什么？
- 白话：DLL 是“动态链接库”，可以理解为很多可复用功能的“工具箱文件”。
- 在你项目里：`finguard.exe` 本身不包含全部能力，它启动时会去找 `drogon.dll`、`libssl-3-x64.dll` 等依赖。
- 如果找不到：程序会在启动时直接报“缺少 xxx.dll”。

### 2.2 sidecar exe 是什么？
- 白话：sidecar exe 就是由 Tauri 主程序托管启动的外部可执行文件。
- 在你项目里：`finguard.exe` 是 sidecar；前端窗口由 Tauri 承载，后端能力由 sidecar 提供。

### 2.3 `bundle.resources` 是什么？
- 白话：它是“把额外文件打进安装包并指定安装位置”的清单。
- 你当前策略：把 10 个 DLL 都声明在 `bundle.resources`，并映射到 `./`，即安装后与 sidecar 同目录。

### 2.4 `<install_dir>` 是什么？
- 白话：`<install_dir>` 是“安装根目录”的占位写法，不是字面目录名。
- 真实路径由安装器和用户选择决定，例如可能是 `C:\Program Files\FinGuard\`（示例）。
- 你的目标位置：`<install_dir>\finguard.exe` 与 `<install_dir>\*.dll` 同级。

### 2.5 在你的项目里：`finguard.exe` 和 `finguard-desktop.exe` 的关系
- `finguard-desktop.exe`：桌面主程序（Tauri 壳），负责窗口、托盘、生命周期管理。
- `finguard.exe`：sidecar 后端进程，负责本地 API 与业务计算。
- 启动关系：主程序启动时自动拉起 `finguard.exe`。
- 退出关系：主程序退出前会主动终止 `finguard.exe`，避免残留后台进程。
- 你可以把它理解成“车头 + 发动机”：桌面 exe 是车头，sidecar exe 是发动机。

### 生活类比（开餐厅）
- `finguard.exe` 像主厨，DLL 像调料盒。
- `bundle.resources` 像“开店物资清单”，确保调料跟主厨一起送到后厨。
- `<install_dir>` 就是这家店的门店地址。
- 把 DLL 放在 sidecar 同级，等于把调料放在主厨手边，开工不缺料。

## 3) 关键流程（可复刻）

1. 在 `frontend/src-tauri/binaries/` 准备 sidecar 和 DLL：
   - `finguard.exe`
   - 10 个 DLL：`brotlicommon.dll`、`brotlidec.dll`、`brotlienc.dll`、`cares.dll`、`drogon.dll`、`jsoncpp.dll`、`libcrypto-3-x64.dll`、`libssl-3-x64.dll`、`trantor.dll`、`zlib1.dll`
2. 在 `tauri.conf.json` 配置 `bundle.externalBin` 声明 sidecar。
3. 在 `tauri.conf.json` 配置 `bundle.resources`，将每个 DLL 映射到 `./`。
4. 执行打包（如 `npm run tauri build`），生成安装包。
5. 安装后到 `<install_dir>` 检查：
   - `finguard.exe` 与 10 个 DLL 是否同级。
6. 启动应用并触发 sidecar，确认不再出现“找不到 DLL”类错误。

## 4) 真实代码片段（逐行注释）

```json
{
  "bundle": {
    "externalBin": [
      "binaries/finguard"
    ],
    "resources": {
      "binaries/brotlicommon.dll": "./",
      "binaries/brotlidec.dll": "./",
      "binaries/brotlienc.dll": "./",
      "binaries/cares.dll": "./",
      "binaries/drogon.dll": "./",
      "binaries/jsoncpp.dll": "./",
      "binaries/libcrypto-3-x64.dll": "./",
      "binaries/libssl-3-x64.dll": "./",
      "binaries/trantor.dll": "./",
      "binaries/zlib1.dll": "./"
    }
  }
}
```

- 第 1 行：JSON 根对象开始。
- 第 2 行：进入打包配置 `bundle`。
- 第 3 行：`externalBin` 表示要随应用一起分发的外部可执行文件列表。
- 第 4 行：声明 sidecar 的源路径前缀（Tauri 构建时会按平台处理扩展名）。
- 第 5 行：`externalBin` 数组结束。
- 第 6 行：进入 `resources` 映射配置。
- 第 7-16 行：每一行都是“源文件 -> 目标目录”的映射。
- 第 7-16 行中的 `"./"`：目标是安装目录当前层，即 `<install_dir>` 根层。
- 第 17 行：`resources` 结束。
- 第 18 行：`bundle` 结束。
- 第 19 行：JSON 根对象结束。

> 说明：`build.rs` 常用于开发态（如 `tauri dev`）的本地复制；生产安装包是否带上 DLL，最终以 `bundle.resources` 为准。

## 5) 小结 + 自测题（含答案）

### 小结
- DLL 是 sidecar 运行所需的动态库，不带齐就会启动失败。
- `bundle.resources` 决定“安装包里带哪些文件、安装到哪里”。
- `<install_dir>` 是安装根目录占位符，不是固定字符串目录。
- sidecar exe 与 DLL 同级是 Windows 下最稳妥的加载方式之一。
- 在你的安装目录里看到两个 exe 是正常现象：桌面 exe 管理 sidecar exe。

### 自测题
1. 为什么要把 DLL 放到和 `finguard.exe` 同级？
   - 答：因为 Windows 会优先在 exe 同目录查找 DLL，减少路径配置和找库失败风险。
2. `bundle.resources` 的核心作用是什么？
   - 答：把非代码资源（如 DLL、配置文件）打进安装包并指定安装落点。
3. `<install_dir>` 是固定目录名吗？
   - 答：不是，它是安装根目录的占位符，真实路径由安装过程决定。
4. 为什么安装后会同时有 `finguard-desktop.exe` 和 `finguard.exe`？
   - 答：前者是桌面主程序，后者是被主程序托管启动的后端 sidecar，职责不同。

## 6) 下一步建议

1. 我可以再给你写一篇《如何在安装后 1 分钟内排查 DLL 缺失问题（带检查命令）》。
2. 也可以写《`build.rs` 与 `bundle.resources` 的职责边界（开发态 vs 生产态）》帮助你彻底不混淆。
