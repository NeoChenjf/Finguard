#include "server/routes_internal.h"

#include <drogon/drogon.h>

#include <chrono>

#include <json/json.h>

#include "risk/profile_store.h"
#include "util/metrics_registry.h"
#include "util/reliability_config.h"

namespace finguard::server::internal {

void register_profile_routes() {
    using namespace drogon;

    app().registerHandler(
        "/api/v1/profile/upsert",
        [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            const auto trace_id = get_or_create_trace_id(req);
            const auto start = std::chrono::steady_clock::now();
            const auto route = req ? req->path() : "/api/v1/profile/upsert";
            const auto rate_cfg = util::cached_rate_limit_config();

            if (!entry_bucket().allow(make_entry_key(req), rate_cfg.entry.rate_rps,
                                      rate_cfg.entry.capacity)) {
                util::global_metrics().record_rate_limit_reject();
                Json::Value body = rate_limit_error_body();
                auto resp = HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(k429TooManyRequests);
                resp->addHeader("X-Trace-Id", trace_id);
                cb(resp);
                const auto latency_ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                              start)
                        .count();
                log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
                return;
            }

            const auto user_id = req ? req->getHeader("X-User-Id") : "";
            if (user_id.empty()) {
                Json::Value body;
                body["error"] = "missing_user_id";
                auto resp = HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(k400BadRequest);
                resp->addHeader("X-Trace-Id", trace_id);
                cb(resp);
                const auto latency_ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                              start)
                        .count();
                log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
                return;
            }

            auto json = req ? req->getJsonObject() : nullptr;
            if (!json || !json->isMember("questionnaire") || !(*json)["questionnaire"].isObject()) {
                Json::Value body;
                body["error"] = "missing_questionnaire";
                auto resp = HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(k400BadRequest);
                resp->addHeader("X-Trace-Id", trace_id);
                cb(resp);
                const auto latency_ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                              start)
                        .count();
                log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
                return;
            }

            std::string error;
            if (!risk::upsert_profile(user_id, (*json)["questionnaire"], &error)) {
                Json::Value body;
                body["error"] = "profile_upsert_failed";
                body["detail"] = error;
                auto resp = HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(k500InternalServerError);
                resp->addHeader("X-Trace-Id", trace_id);
                cb(resp);
                const auto latency_ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                              start)
                        .count();
                log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
                return;
            }

            Json::Value body;
            body["status"] = "ok";
            body["user_id"] = user_id;
            auto resp = HttpResponse::newHttpJsonResponse(body);
            resp->addHeader("X-Trace-Id", trace_id);
            cb(resp);
            const auto latency_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
            log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
        },
        {Post});
}

} // namespace finguard::server::internal
