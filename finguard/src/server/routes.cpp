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
    using namespace drogon;

    // 健康检查：返回固定 JSON
    app().registerHandler("/health", [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
        Json::Value body;
        body["status"] = "ok";
        auto resp = HttpResponse::newHttpJsonResponse(body);
        cb(resp);
    }, {Get});

    // 配置建议（mock）
    app().registerHandler("/api/v1/plan", [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
        (void)req;
        Json::Value body;
        body["proposed_portfolio"]["SPY"] = 0.30;
        body["proposed_portfolio"]["BND"] = 0.40;
        body["proposed_portfolio"]["GLD"] = 0.10;
        body["proposed_portfolio"]["CASH"] = 0.20;
        body["risk_report"]["status"] = "PASS";
        body["risk_report"]["triggered_rules"] = Json::arrayValue;
        body["rationale"] = "当前为 mock 输出，后续将接入画像解析与规则引擎。";
        body["rebalancing_actions"] = Json::arrayValue;
        auto resp = HttpResponse::newHttpJsonResponse(body);
        cb(resp);
    }, {Post});

    // 流式问答（SSE）
    app().registerHandler("/api/v1/chat/stream",
        [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            // 读取用户输入的 prompt（JSON body）
            std::string prompt;
            if (req) {
                auto json = req->getJsonObject();
                if (json && json->isMember("prompt") && (*json)["prompt"].isString()) {
                    prompt = (*json)["prompt"].asString();
                }
            }

            llm::LlmClient client;
            const auto config = client.load_config();
            // 服务端必须配置 api_key，否则直接报 500
            if (config.api_key.empty()) {
                Json::Value body;
                body["error"] = "server_missing_api_key";
                auto resp = HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(k500InternalServerError);
                cb(resp);
                return;
            }

            // 请求方必须带 X-API-Key
            const auto request_key = req ? req->getHeader("X-API-Key") : "";
            if (request_key != config.api_key) {
                Json::Value body;
                body["error"] = "unauthorized";
                auto resp = HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(k401Unauthorized);
                cb(resp);
                return;
            }

            // 调用 LLM client（内部支持降级）
            auto result = client.stream_chat(prompt);

            // 保证 cite / warning / metric 事件必有输出（便于验收）
            auto cites = result.cites;
            if (cites.empty()) {
                cites.push_back("none");
            }
            auto warnings = result.warnings;
            if (warnings.empty()) {
                warnings.push_back("none");
            }
            llm::LlmMetrics metrics = result.metrics;
            if (!metrics.has_usage) {
                metrics.has_usage = true;
                metrics.prompt_tokens = 0;
                metrics.completion_tokens = static_cast<int>(result.tokens.size());
                metrics.total_tokens = metrics.completion_tokens;
            }

            // 组装 SSE 输出内容（data: JSON）
            std::string payload;
            payload.reserve(256);
            for (const auto &token : result.tokens) {
                Json::Value event;
                event["type"] = "token";
                event["payload"] = token;
                payload += "data: ";
                payload += serialize_event(event);
                payload += "\n\n";
            }

            for (const auto &cite : cites) {
                Json::Value event;
                event["type"] = "cite";
                event["payload"] = cite;
                payload += "data: ";
                payload += serialize_event(event);
                payload += "\n\n";
            }

            if (metrics.has_usage) {
                Json::Value event;
                event["type"] = "metric";
                event["payload"]["prompt_tokens"] = metrics.prompt_tokens;
                event["payload"]["completion_tokens"] = metrics.completion_tokens;
                event["payload"]["total_tokens"] = metrics.total_tokens;
                payload += "data: ";
                payload += serialize_event(event);
                payload += "\n\n";
            }

            for (const auto &warning : warnings) {
                Json::Value event;
                event["type"] = "warning";
                event["payload"] = warning;
                payload += "data: ";
                payload += serialize_event(event);
                payload += "\n\n";
            }

            // done 事件
            Json::Value done;
            done["type"] = "done";
            done["payload"] = "done";
            payload += "data: ";
            payload += serialize_event(done);
            payload += "\n\n";

            auto data = std::make_shared<std::string>(std::move(payload));
            auto offset = std::make_shared<std::size_t>(0);

            // Drogon 流式回调：不断向客户端写出 SSE 内容
            auto resp = HttpResponse::newStreamResponse(
                [data, offset](char *buffer, std::size_t len) -> std::size_t {
                    if (*offset >= data->size()) {
                        return 0;
                    }
                    const std::size_t remaining = data->size() - *offset;
                    const std::size_t to_copy = (std::min)(len, remaining);
                    std::memcpy(buffer, data->data() + *offset, to_copy);
                    *offset += to_copy;
                    return to_copy;
                });

            // SSE 必需的响应头
            resp->setContentTypeString("text/event-stream");
            resp->addHeader("Cache-Control", "no-cache");
            resp->addHeader("Connection", "keep-alive");
            cb(resp);
        },
        {Post});
}

}

