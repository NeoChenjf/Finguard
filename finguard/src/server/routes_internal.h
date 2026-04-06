#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <json/json.h>

#include "util/concurrency_limiter.h"
#include "util/token_bucket.h"

namespace finguard::server::internal {

using HttpCallback = std::function<void(const drogon::HttpResponsePtr &)>;

std::string serialize_event(const Json::Value &event);
util::TokenBucket &entry_bucket();
util::ConcurrencyLimiter &concurrency_limiter();
std::mutex &settings_mutex();
std::string get_or_create_trace_id(const drogon::HttpRequestPtr &req);
std::string get_user_id(const drogon::HttpRequestPtr &req);
std::string make_entry_key(const drogon::HttpRequestPtr &req);
Json::Value rate_limit_error_body();
void log_request_metrics(const std::string &trace_id,
                         const std::string &route,
                         int status,
                         double latency_ms);
void add_cors_headers(const drogon::HttpResponsePtr &resp);
void append_sse_event(std::string &payload, const Json::Value &event);
void append_sse_data_event(std::string &payload,
                           const std::string &type,
                           const Json::Value &event_payload);
drogon::HttpResponsePtr make_sse_response(std::string payload,
                                          const std::string &trace_id);

void register_health_routes();
void register_plan_routes();
void register_profile_routes();
void register_chat_routes();
void register_system_routes();
void register_valuecell_routes();

} // namespace finguard::server::internal
