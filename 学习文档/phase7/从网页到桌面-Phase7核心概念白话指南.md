# 从网页到桌面：Phase 7 核心概念白话指南

> 目标：看完后你能用自己的话解释 SPA、Tauri、Zustand/Redux/Hooks/useAppStore、SSE 流式通信相关概念，并知道它们在你项目里的位置。

## 1) 学习路线（从易到难）

1. 先搞懂“页面怎么组织”：SPA、路由、桌面端（Tauri）。
2. 再搞懂“数据怎么共享”：Hooks、Redux、Zustand、useAppStore。
3. 最后搞懂“消息怎么流式返回”：EventSource、fetch、ReadableStream、SSE、fetch-event-source。

## 2) 核心概念解释（白话 + 类比）

### SPA 架构（Single Page Application）
- 白话：整个网站通常只加载一次主页面，后续切换 `/settings`、`/plan`、`/chat` 主要是前端在同一页面里“切视图”。
- 类比：像商场里同一栋楼切楼层，不是每次都重盖一栋楼。
- 在你项目里：React Router 负责把不同 URL 映射到不同页面组件。

### Tauri 桌面端
- 白话：Tauri 可以把你现在的前端网页（React）打包成 Windows/macOS/Linux 桌面应用。
- 类比：把“网页店铺”搬进“本地商场柜台”，用户双击就能开。
- 好处：安装包小、性能好、可调用本地能力。

### Hooks
- 白话：Hooks 是 React 提供的“功能钩子”，让函数组件拥有状态和副作用能力。
- 常见：`useState`（本地状态）、`useEffect`（副作用）、`useMemo`（缓存计算）。
- 类比：组件里的“插座”，插上不同 Hook 就获得不同能力。

### Redux
- 白话：Redux 是经典全局状态管理方案，规范强、生态成熟，但写法相对重。
- 特点：有 action、reducer、store 等固定模式。
- 适合：大型、多人协作、状态逻辑非常复杂的项目。

### Zustand
- 白话：Zustand 也是全局状态管理库，但更轻、更简洁，学习成本低。
- 特点：直接创建 store，组件按需取值，不必写很多模板代码。
- 在你项目里：用它存“后端连接状态、是否有 API Key、当前配置”等共享状态很合适。

### useAppStore
- 白话：`useAppStore` 是你项目里自定义的 Zustand Hook，不是 React 内置 API。
- 作用：统一管理全局状态，例如 `isBackendConnected`、`hasApiKey`。
- 用法：页面组件里调用 `useAppStore()` 读取或更新全局状态。

### @microsoft/fetch-event-source
- 白话：这是一个库，帮你更稳地用 `fetch` 处理 SSE（尤其是 POST 场景）。
- 为什么常用它：自动重连、事件处理更方便、比手写流解析省心。

### 原生 fetch、ReadableStream、Server-Sent Events（SSE）
- `fetch`：浏览器发 HTTP 请求的原生 API。
- `ReadableStream`：可“边到边读”的数据流对象，不用等整包返回完。
- `Server-Sent Events`：服务端持续推送文本事件（如 token、done）的协议，常见格式是 `text/event-stream`。
- 合起来：`fetch` 发 POST，请求返回流后，通过 `ReadableStream` 一段段读取 SSE 事件，实现“打字机输出”。

### EventSource
- 白话：浏览器原生 SSE 客户端。
- 限制：只支持 GET，不支持自定义 POST body。
- 在你项目里：因为 `/api/v1/chat/stream` 是 POST，所以通常不用它，改用 `fetch` 方案。

### Vite
- 白话：前端开发工具，负责本地开发启动和生产构建打包。
- 你能感受到的好处：启动快、热更新快、开发体验好。

## 3) 关键流程（可复刻）

1. React + Vite 搭建前端工程。
2. React Router 实现 SPA 路由（`/`、`/settings`、`/plan`、`/chat`）。
3. Zustand 建立 `useAppStore` 管全局共享状态。
4. 聊天页发起 POST 请求到 `/api/v1/chat/stream`。
5. 用 `fetch-event-source` 或原生 `fetch + ReadableStream` 逐段解析 `text/event-stream`。
6. 按 `token/cite/warning/metric/done` 更新 UI，实现流式渲染。

## 4) 真实代码片段（逐行注释）

### 4.1 Zustand + useAppStore 示例

```ts
import { create } from "zustand";

type AppState = {
  isBackendConnected: boolean;
  hasApiKey: boolean;
  setBackendConnected: (v: boolean) => void;
  setHasApiKey: (v: boolean) => void;
};

export const useAppStore = create<AppState>((set) => ({
  isBackendConnected: false,
  hasApiKey: false,
  setBackendConnected: (v) => set({ isBackendConnected: v }),
  setHasApiKey: (v) => set({ hasApiKey: v }),
}));
```

- 第 1 行：引入 Zustand 的 `create`。
- 第 3-8 行：定义全局状态和更新函数的类型。
- 第 10 行：创建并导出 `useAppStore`（这是自定义 Hook）。
- 第 11-12 行：定义两个状态默认值。
- 第 13-14 行：定义两个更新函数，调用 `set` 写回全局状态。

### 4.2 原生 fetch + ReadableStream 处理 SSE（POST）

```ts
const res = await fetch("/api/v1/chat/stream", {
  method: "POST",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify({ message: "你好" }),
});

const reader = res.body?.getReader();
const decoder = new TextDecoder("utf-8");
let buffer = "";

while (reader) {
  const { done, value } = await reader.read();
  if (done) break;
  buffer += decoder.decode(value, { stream: true });

  const events = buffer.split("\n\n");
  buffer = events.pop() ?? "";

  for (const e of events) {
    // 这里可以解析 data: {...} 并按 type 更新页面
    console.log(e);
  }
}
```

- 第 1-5 行：用 `fetch` 发 POST 请求，请求体是 JSON。
- 第 7 行：从响应体拿到流读取器 `reader`。
- 第 8 行：创建文本解码器，把字节转成字符串。
- 第 9 行：`buffer` 暂存不完整事件片段。
- 第 11-14 行：循环读流，直到服务端发送结束。
- 第 16 行：SSE 通常用空行分隔事件，按 `\n\n` 拆包。
- 第 17 行：最后一个可能是半包，留在 `buffer` 等下轮拼接。
- 第 19-22 行：逐条处理完整事件，并更新 React 状态。

## 5) 小结 + 自测题（含答案）

### 小结
- SPA 负责“页面组织方式”；Tauri 负责“网页变桌面应用”。
- Hooks 是 React 能力入口；Zustand/Redux 是全局状态方案（前者更轻）。
- `useAppStore` 是你项目的 Zustand 入口。
- 流式问答核心是 SSE；POST 场景一般用 `fetch + ReadableStream` 或 `@microsoft/fetch-event-source`。
- `EventSource` 很方便，但只支持 GET。

### 自测题
1. 为什么你的聊天流接口不直接用 `EventSource`？  
   答：因为接口是 POST，需要 JSON body，而 `EventSource` 只支持 GET。
2. `useAppStore` 是什么？  
   答：项目自定义的 Zustand Hook，用来读写全局状态。
3. SPA 的核心特征是什么？  
   答：通常只加载一次主页面，后续主要在前端切换视图与数据。
4. Redux 和 Zustand 的直观差别？  
   答：Redux 更规范但更重；Zustand 更轻量、上手更快。

## 6) 下一步建议

1. 我可以再给你写一篇《你项目里的 `/`、`/settings`、`/plan`、`/chat` 路由实战解读》。
2. 也可以做一张“状态流转图”（启动检查 -> 设置 -> 计划 -> 聊天流式）帮助你建立全局视角。
