# FinGuard

面向个人资产配置的高性能 AI 网关与风控中台。

## 启动

```powershell
cmake -S . -B build
cmake --build build
./build/finguard
```

## 配置

编辑 `config/llm.json`，填入 Qwen 兼容 API 的 `api_key` 与模型名称。

## 接口示例

健康检查：
```bash
curl http://localhost:8080/health
```

配置建议（mock）：
```bash
curl -X POST http://localhost:8080/api/v1/plan
```

流式聊天（SSE mock）：
```bash
curl -N -H "Accept: text/event-stream" -H "Content-Type: application/json" \
  -H "X-API-Key: <your_api_key>" \
  -X POST http://localhost:8080/api/v1/chat/stream \
  -d "{\"prompt\":\"请给出风险提示\"}"
```

流式事件格式（每行 `data:` 为 JSON）：
```json
{"type":"token","payload":"..."}
{"type":"cite","payload":"..."}
{"type":"metric","payload":{"prompt_tokens":123,"completion_tokens":456,"total_tokens":579}}
{"type":"warning","payload":"..."}
{"type":"done","payload":"done"}
```
