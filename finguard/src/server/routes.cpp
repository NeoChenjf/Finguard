#include "server/routes.h"

#include <drogon/drogon.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

#include "llm/llm_client.h"

namespace finguard {

namespace {

// 将 Json::Value 序列化为一行 JSON 文本，便于 SSE 的 data: 输出
std::string serialize_event(const Json::Value &event) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, event);
}

}

void setup_routes() {
    // 引入 drogon 命名空间，便于直接使用 app()、HttpResponse 等符号
    using namespace drogon;

    // 健康检查：返回固定 JSON
    // 注册 GET /health 路由处理器
    app().registerHandler("/health", [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
        // 构造 JSON 响应体
        Json::Value body;
        // 设置状态字段
        body["status"] = "ok";
        // 构造 JSON 响应对象
        auto resp = HttpResponse::newHttpJsonResponse(body);
        // 回调返回响应
        cb(resp);
    // 指定该路由仅支持 GET 方法
    }, {Get});

    // 配置建议（mock）
    // 注册 POST /api/v1/plan 路由处理器
    app().registerHandler("/api/v1/plan", [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
        // 显式标记未使用的请求对象，避免编译告警
        (void)req;
        // 构造 JSON 响应体
        Json::Value body;
        // 设定 mock 的资产配置比例
        body["proposed_portfolio"]["SPY"] = 0.30;
        // 设定 mock 的资产配置比例
        body["proposed_portfolio"]["BND"] = 0.40;
        // 设定 mock 的资产配置比例
        body["proposed_portfolio"]["GLD"] = 0.10;
        // 设定 mock 的资产配置比例
        body["proposed_portfolio"]["CASH"] = 0.20;
        // 设置风控状态
        body["risk_report"]["status"] = "PASS";
        // 设置风控触发规则为空数组
        body["risk_report"]["triggered_rules"] = Json::arrayValue;
        // 设置说明文本
        body["rationale"] = "当前为 mock 输出，后续将接入画像解析与规则引擎。";
        // 设置调仓动作为空数组
        body["rebalancing_actions"] = Json::arrayValue;
        // 构造 JSON 响应对象
        auto resp = HttpResponse::newHttpJsonResponse(body);
        // 回调返回响应
        cb(resp);
    // 指定该路由仅支持 POST 方法
    }, {Post});

    // 流式问答（SSE）
    // 注册 POST /api/v1/chat/stream 路由处理器
    app().registerHandler("/api/v1/chat/stream",
        [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            // 读取用户输入的 prompt（JSON body）
            // 准备接收 prompt 文本
            std::string prompt;
            // 判断请求对象是否存在
            if (req) {
                // 解析请求体 JSON
                auto json = req->getJsonObject();
                // 校验 prompt 字段存在且为字符串
                if (json && json->isMember("prompt") && (*json)["prompt"].isString()) {
                    // 读取 prompt 字段
                    prompt = (*json)["prompt"].asString();
                }
            }

            // 创建 LLM 客户端
            llm::LlmClient client;
            // 加载服务端 LLM 配置
            const auto config = client.load_config();
            // 服务端必须配置 api_key，否则直接报 500
            // 若未配置 api_key 则返回 500
            if (config.api_key.empty()) {
                // 构造错误响应体
                Json::Value body;
                // 设置错误码标识
                body["error"] = "server_missing_api_key";
                // 构造 JSON 响应对象
                auto resp = HttpResponse::newHttpJsonResponse(body);
                // 设置 HTTP 状态码为 500
                resp->setStatusCode(k500InternalServerError);
                // 回调返回响应
                cb(resp);
                // 终止处理
                return;
            }

            // 请求方必须带 X-API-Key
            // 从请求头读取 X-API-Key
            const auto request_key = req ? req->getHeader("X-API-Key") : "";
            // 校验请求方 API Key 是否匹配
            if (request_key != config.api_key) {
                // 构造错误响应体
                Json::Value body;
                // 设置错误码标识
                body["error"] = "unauthorized";
                // 构造 JSON 响应对象
                auto resp = HttpResponse::newHttpJsonResponse(body);
                // 设置 HTTP 状态码为 401
                resp->setStatusCode(k401Unauthorized);
                // 回调返回响应
                cb(resp);
                // 终止处理
                return;
            }

            // 调用 LLM client（内部支持降级）
            // 发起流式请求并获取结果
            auto result = client.stream_chat(prompt);

            // 保证 cite / warning / metric 事件必有输出（便于验收）
            // 复制引用列表
            auto cites = result.cites;
            // 若引用列表为空，则补一个默认值
            if (cites.empty()) {
                // 补默认引用标识
                cites.push_back("none");
            }
            // 复制告警列表
            auto warnings = result.warnings;
            // 若告警列表为空，则补一个默认值
            if (warnings.empty()) {
                // 补默认告警标识
                warnings.push_back("none");
            }
            // 拷贝计量信息
            llm::LlmMetrics metrics = result.metrics;
            // 若计量信息缺失，则补默认值
            if (!metrics.has_usage) {
                // 标记计量信息存在
                metrics.has_usage = true;
                // 设定 prompt token 数
                metrics.prompt_tokens = 0;
                // 设定 completion token 数为 token 列表长度
                metrics.completion_tokens = static_cast<int>(result.tokens.size());
                // 设定 total token 数
                metrics.total_tokens = metrics.completion_tokens;
            }

            // 组装 SSE 输出内容（data: JSON）
            // 初始化输出缓冲区
            std::string payload;
            // 预留一定容量减少扩容
            payload.reserve(256);
            // 逐个 token 构造 SSE 事件
            for (const auto &token : result.tokens) {
                // 构造事件 JSON
                Json::Value event;
                // 标记事件类型为 token
                event["type"] = "token";
                // 设置事件负载
                event["payload"] = token;
                // 写入 SSE 前缀
                payload += "data: ";
                // 序列化事件为一行 JSON 并追加
                payload += serialize_event(event);
                // 追加 SSE 分隔空行
                payload += "\n\n";
            }

            // 逐个引用构造 SSE 事件
            for (const auto &cite : cites) {
                // 构造事件 JSON
                Json::Value event;
                // 标记事件类型为 cite
                event["type"] = "cite";
                // 设置事件负载
                event["payload"] = cite;
                // 写入 SSE 前缀
                payload += "data: ";
                // 序列化事件为一行 JSON 并追加
                payload += serialize_event(event);
                // 追加 SSE 分隔空行
                payload += "\n\n";
            }

            // 仅当计量信息存在时输出 metric 事件
            if (metrics.has_usage) {
                // 构造事件 JSON
                Json::Value event;
                // 标记事件类型为 metric
                event["type"] = "metric";
                // 写入 prompt token 数
                event["payload"]["prompt_tokens"] = metrics.prompt_tokens;
                // 写入 completion token 数
                event["payload"]["completion_tokens"] = metrics.completion_tokens;
                // 写入 total token 数
                event["payload"]["total_tokens"] = metrics.total_tokens;
                // 写入 SSE 前缀
                payload += "data: ";
                // 序列化事件为一行 JSON 并追加
                payload += serialize_event(event);
                // 追加 SSE 分隔空行
                payload += "\n\n";
            }

            // 逐个告警构造 SSE 事件
            for (const auto &warning : warnings) {
                // 构造事件 JSON
                Json::Value event;
                // 标记事件类型为 warning
                event["type"] = "warning";
                // 设置事件负载
                event["payload"] = warning;
                // 写入 SSE 前缀
                payload += "data: ";
                // 序列化事件为一行 JSON 并追加
                payload += serialize_event(event);
                // 追加 SSE 分隔空行
                payload += "\n\n";
            }

            // done 事件
            // 构造完成事件 JSON
            Json::Value done;
            // 标记事件类型为 done
            done["type"] = "done";
            // 设置事件负载
            done["payload"] = "done";
            // 写入 SSE 前缀
            payload += "data: ";
            // 序列化事件为一行 JSON 并追加
            payload += serialize_event(done);
            // 追加 SSE 分隔空行
            payload += "\n\n";

            // 将 payload 包装为共享指针以供流式回调使用
            auto data = std::make_shared<std::string>(std::move(payload));
            // 记录当前已写出的偏移量
            auto offset = std::make_shared<std::size_t>(0);

            // Drogon 流式回调：不断向客户端写出 SSE 内容
            // 创建流式响应对象
            auto resp = HttpResponse::newStreamResponse(
                [data, offset](char *buffer, std::size_t len) -> std::size_t {
                    // 若已写完则返回 0 表示结束
                    if (*offset >= data->size()) {
                        return 0;
                    }
                    // 计算剩余未写长度
                    const std::size_t remaining = data->size() - *offset;
                    // 计算本次可拷贝长度
                    const std::size_t to_copy = (std::min)(len, remaining);
                    // 拷贝数据到输出缓冲区
                    std::memcpy(buffer, data->data() + *offset, to_copy);
                    // 更新已写偏移
                    *offset += to_copy;
                    // 返回本次写出的字节数
                    return to_copy;
                });

            // SSE 必需的响应头
            // 设置响应类型为 SSE
            resp->setContentTypeString("text/event-stream");
            // 禁用缓存
            resp->addHeader("Cache-Control", "no-cache");
            // 保持连接不断开
            resp->addHeader("Connection", "keep-alive");
            // 回调返回响应
            cb(resp);
        },
        // 指定该路由仅支持 POST 方法
        {Post});
}

}

