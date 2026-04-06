#include "server/routes_internal.h"

#include "util/concurrency_limiter.h"
#include "util/metrics_registry.h"
#include "util/token_bucket.h"

#include <algorithm>
#include <cstring>
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <memory>

namespace finguard::server::internal {

namespace {
util::TokenBucket g_entry_bucket;
util::ConcurrencyLimiter g_concurrency_limiter(4);
std::mutex g_settings_mutex;
} // namespace

std::string serialize_event(const Json::Value &event) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, event);
}

util::TokenBucket &entry_bucket() {
    return g_entry_bucket;
}

util::ConcurrencyLimiter &concurrency_limiter() {
    return g_concurrency_limiter;
}

std::mutex &settings_mutex() {
    return g_settings_mutex;
}

std::string get_or_create_trace_id(const drogon::HttpRequestPtr &req) {
    if (req) {
        const auto tid = req->getHeader("X-Trace-Id");
        if (!tid.empty()) {
            return tid;
        }
    }
    return drogon::utils::getUuid();
}

std::string get_user_id(const drogon::HttpRequestPtr &req) {
    if (!req) {
        return "anonymous";
    }
    const auto user_id = req->getHeader("X-User-Id");
    if (!user_id.empty()) {
        return user_id;
    }
    return "anonymous";
}

std::string make_entry_key(const drogon::HttpRequestPtr &req) {
    const auto user_id = get_user_id(req);
    const auto route = req ? req->path() : "";
    return "entry:user:" + user_id + ":route:" + route;
}

Json::Value rate_limit_error_body() {
    Json::Value body;
    body["error"]["code"] = "RATE_LIMITED";
    body["error"]["message"] = "rate limited";
    body["error"]["retry_after_ms"] = 1000;
    return body;
}

void log_request_metrics(const std::string &trace_id,
                         const std::string &route,
                         int status,
                         double latency_ms) {
    util::global_metrics().record_request(latency_ms);
    LOG_INFO << "trace_id=" << trace_id << " route=" << route << " status=" << status
             << " latency_ms=" << latency_ms;
}

void add_cors_headers(const drogon::HttpResponsePtr &resp) {
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers",
                    "Content-Type, X-User-Id, X-Trace-Id, X-Api-Key");
}

void append_sse_event(std::string &payload, const Json::Value &event) {
    payload += "data: ";
    payload += serialize_event(event);
    payload += "\n\n";
}

void append_sse_data_event(std::string &payload,
                           const std::string &type,
                           const Json::Value &event_payload) {
    Json::Value event;
    event["type"] = type;
    event["payload"] = event_payload;
    append_sse_event(payload, event);
}

drogon::HttpResponsePtr make_sse_response(std::string payload,
                                          const std::string &trace_id) {
    auto data = std::make_shared<std::string>(std::move(payload));
    auto offset = std::make_shared<std::size_t>(0);

    auto resp = drogon::HttpResponse::newStreamResponse(
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

    resp->setContentTypeString("text/event-stream");
    resp->addHeader("Cache-Control", "no-cache");
    resp->addHeader("Connection", "keep-alive");
    resp->addHeader("X-Trace-Id", trace_id);
    return resp;
}

} // namespace finguard::server::internal
