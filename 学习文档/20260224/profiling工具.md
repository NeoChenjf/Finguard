---
title: Profiling 工具入门（Visual Studio / WSL-Docker-Linux / Windows+MSVC）
tags: [tutorial, performance, profiling, phase5]
last_updated: 2026-02-24
source: phase5.md
status: draft
---

# Profiling 工具入门（Visual Studio / WSL-Docker-Linux / Windows+MSVC）

## 标题与目标

目标：用最少术语理解三个概念——`Visual Studio Performance Profiler`、`WSL/Docker/Linux`、`Windows + MSVC`，并知道 Phase5 为什么选择它们的组合。

## 学习路线/分层

1) 先理解 Profiling 是什么（只看概念）
2) 再认识 Visual Studio Performance Profiler（能用一句话解释）
3) 再区分 WSL/Docker/Linux 与 Windows + MSVC（知道“运行环境”差异）
4) 最后把它们串成 Phase5 的选择理由（能复述原因）

## 核心概念解释（带生活类比）

- Profiling（性能剖析）：像“给车装行车记录仪”，记录哪里最耗油、哪里最堵。
- Visual Studio Performance Profiler：就是 Windows 下 Visual Studio 自带的“行车记录仪”，能看 CPU、内存、锁竞争。
- `.diagsession`：Visual Studio Profiler 生成的“诊断会话文件”，保存一次采样的性能数据，像“行车记录的录像文件”。
- WSL/Docker/Linux：像“在 Windows 里开一个 Linux 小房间（WSL）或一个集装箱（Docker）”，适合跑 Linux 环境的工具。
- Windows + MSVC：就是“直接在 Windows 里用微软的 C/C++ 编译器（MSVC）开工”，无需折腾 Linux 环境。

## 关键流程/步骤（可复刻）

1) 确认本机是否有 Visual Studio + MSVC
2) 在 Windows 直接运行项目（不走 WSL/Docker）
3) 用 Visual Studio Profiler 采集 CPU/内存/锁竞争数据
4) 根据报告定位热点，再做优化

## 真实命令/代码示例（带逐行注释）

### 1) 检查 MSVC 编译器是否可用（Windows）

```powershell
# 查看是否能找到 MSVC 编译器 cl.exe
where cl
```

说明：
- 能找到 `cl.exe` 表示 MSVC 已安装。
- 找不到则说明需要安装 Visual Studio 的 C++ 工具链。

### 2) 检查 WSL 是否启用（可选）

```powershell
# 查看 WSL 状态（可选，仅做理解）
wsl --status
```

说明：
- Phase5 明确“不走 WSL/Docker/Linux 路线”。
- 这条命令只是帮助你理解 WSL 是什么，并非必须。

### 3) Visual Studio Profiler 的最短路径（操作步骤）

- 打开 Visual Studio
- 打开解决方案（.sln）
- 顶部菜单：`Debug` → `Performance Profiler...`
- 勾选：`CPU Usage`、`Memory Usage`、`Concurrency`（锁竞争）
- 点击 `Start` 运行并采集

说明：
- 这些数据能告诉你“哪个函数最耗时”、“哪里内存分配最频繁”。

## 小结 + 自测问题（含答案）

小结：
- Profiling 就是“找出性能瓶颈”。
- Visual Studio Performance Profiler 是 Windows 下自带的性能剖析工具。
- WSL/Docker/Linux 是另一套运行环境，但 Phase5 不走这条路线。
- Windows + MSVC 代表“在 Windows 直接构建与分析”，减少工具链复杂度。

自测题：
1) Profiling 像生活中的什么？
2) 为什么 Phase5 不走 WSL/Docker/Linux？
3) MSVC 是什么？

答案：
1) 像给车装行车记录仪，看哪里最耗油。
2) 因为当前构建链是 CMake + MSVC，Windows 下的 VS Profiler 无需额外安装，集成快。
3) MSVC 是微软的 C/C++ 编译器工具链。

## 下一步建议

1) 实际打开 Visual Studio Profiler 跑一次 CPU 采样。
2) 记录 Top 3 热点函数名称。
3) 再对比优化前后结果。

变更日志
- [2026-02-24] (手动更新) 生成 Profiling 工具教学文档。
