# Tauri 与 Sidecar 入门（Phase 8）

> 目标：看完后你能说清楚 Tauri 是什么、Sidecar 怎么工作、系统托盘做什么、Rust 在这里扮演什么角色。

## 1) 学习路线（从易到难）

1. 先理解 Tauri：它为什么能把前端变成桌面应用。
2. 再理解 Rust：为什么 Tauri 要配 Rust 侧代码。
3. 再理解 Sidecar：为什么要把 `finguard.exe` 当 sidecar 管理。
4. 最后理解系统托盘：为什么桌面应用常放到托盘而不是完全退出。

## 2) 核心概念解释（白话 + 类比）

### 先给你一个“理解反馈”（非常好，只有一点点需要纠正）
- 你说 **“Tauri 就是把 web 编程成 webapp 的过程”**：方向对。更准确是“把现有 Web 前端（React/Vite）封装成桌面应用，并提供本地系统能力”。
- 你说 **“sidecar 是把前端和后端联系起来的过程”**：部分对。更准确是“sidecar 是由 Tauri 托管的外部进程机制”；它可以承载后端进程，从而让前后端在本地协作。
- 你说 **“系统托盘是电脑最下面那一排显示运行程序”**：不完全对。那一整排叫任务栏，系统托盘通常是右下角通知区的小图标区域。
- 你说 **“rust 是一种前端编程语言”**：这个不对。Rust 主要是系统/后端方向语言，在 Tauri 里负责桌面壳与系统能力，不写 React 页面。

### Tauri 是什么？
- 白话：Tauri 是一个“桌面应用外壳框架”，前端界面用你熟悉的 React/Vite，后台能力用 Rust 提供。
- 你可以理解成：把网页包进一个本地桌面程序里，还能安全调用本机能力（进程、文件、托盘、窗口等）。

### Rust 是什么？
- 白话：Rust 是一门系统编程语言，性能高、内存安全强。
- 在 Tauri 里：Rust 负责“桌面壳”和系统能力（比如启动 sidecar、监听退出、托盘菜单事件）。
- 类比：前端是“店面”，Rust 是“机房和总控室”。

### Sidecar 是什么机制？
- 白话：Sidecar 就是“由主程序托管的外部子进程”。
- 在你的场景里：Tauri 主程序启动 `finguard.exe`，并负责监控、重启、退出时清理。
- 为什么不用前端直接拉起：前端不适合直接管本地进程生命周期，交给 Rust 更稳定、安全、可控。
- 类比：主机（Tauri）带着一台“副发动机”（finguard），主机开机它启动，主机关机它也要停。

### 系统托盘是什么？
- 白话：系统托盘是任务栏右下角（Windows）的小图标区，程序可以“最小化到托盘”继续后台运行。
- 常见用途：常驻服务、快速开关窗口、显示状态、退出应用。
- 在你的场景里：前端窗口关掉后，后端 sidecar 可能仍要保持运行，此时托盘可以作为控制入口。

## 3) 关键流程（可复刻）

1. 在 `frontend/` 中初始化 Tauri，生成 `src-tauri/`。
2. 把 `finguard` 放到 `src-tauri/binaries/`。
3. 在 `tauri.conf.json` 声明 external binary。
4. 在 Rust `setup` 中启动 sidecar。
5. 监听 sidecar 退出事件，异常时按重试策略重启。
6. 应用退出时主动终止 sidecar，避免残留僵尸进程。
7. 配置系统托盘菜单（显示窗口、退出应用）。

## 4) 真实代码片段（逐行注释）

```rust
use tauri_plugin_shell::process::CommandEvent;
use tauri_plugin_shell::ShellExt;

fn main() {
  tauri::Builder::default()
    .plugin(tauri_plugin_shell::init())
    .setup(|app| {
      let sidecar = app.shell().sidecar("finguard")?;
      let (_rx, mut child) = sidecar.spawn()?;
      app.manage(child);
      Ok(())
    })
    .run(tauri::generate_context!())
    .expect("error while running tauri app");
}
```

- 第 1 行：引入 sidecar 事件类型（后续可用于监听退出/日志）。
- 第 2 行：引入 `ShellExt`，这样 `app.shell()` 才能调用。
- 第 4 行：程序入口 `main`。
- 第 5 行：创建 Tauri 应用构建器。
- 第 6 行：注册 shell 插件（sidecar 功能依赖它）。
- 第 7 行：`setup` 在应用启动时执行。
- 第 8 行：构建名为 `finguard` 的 sidecar 命令（需先在配置里声明）。
- 第 9 行：真正拉起 sidecar 进程，得到子进程句柄 `child`。
- 第 10 行：把 `child` 挂到应用状态里，便于退出时统一管理。
- 第 11 行：`setup` 正常结束。
- 第 13 行：启动 Tauri 主循环。
- 第 14 行：若主循环异常退出，打印错误。

## 5) 小结 + 自测题（含答案）

### 小结
- Tauri：把前端应用变成桌面应用的框架。
- Rust：负责系统层能力和进程管理。
- Sidecar：由 Tauri 托管的外部进程机制，适合运行你的 `finguard` 后端。
- 系统托盘：让应用可后台常驻并保留可见控制入口。

### 自测题
1. Sidecar 在你的项目里主要解决什么问题？  
   答：统一管理 `finguard` 进程的启动、监控、重启和退出清理。
2. 为什么 Tauri 项目里会看到 Rust 代码？  
   答：因为桌面系统能力（进程、托盘、窗口、生命周期）由 Rust 层实现。
3. 系统托盘最核心价值是什么？  
   答：让应用最小化后继续后台运行，并提供快速操作入口。

## 6) 下一步建议

1. 我可以继续给你写一篇《`tauri.conf.json` 每个关键字段怎么填（按你的项目示例）》。
2. 也可以再写《sidecar 自动重启（最多 3 次）的 Rust 实战模板》。
