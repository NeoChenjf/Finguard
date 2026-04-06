#include "server/routes_internal.h"

#include <drogon/drogon.h>

#include <chrono>

#include <json/json.h>

#include "server/allocation_handler.h"
#include "util/metrics_registry.h"
#include "util/reliability_config.h"

namespace finguard::server::internal {

void register_plan_routes() {
    using namespace drogon;

    app().registerHandler(
        "/api/v1/plan",
        [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            const auto trace_id = get_or_create_trace_id(req);
            const auto start = std::chrono::steady_clock::now();
            const auto route = req ? req->path() : "/api/v1/plan";
            const auto rate_cfg = util::cached_rate_limit_config();
            const auto conc_cfg = util::cached_concurrency_config();
            concurrency_limiter().set_max_inflight(conc_cfg.max_inflight);

            if (!concurrency_limiter().try_acquire()) {
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
                log_request_metrics(trace_id, route, 429, latency_ms);
                return;
            }

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
                log_request_metrics(trace_id, route, 429, latency_ms);
                concurrency_limiter().release();
                return;
            }

            auto json_ptr = req ? req->getJsonObject() : nullptr;
            if (!json_ptr || !json_ptr->isMember("profile")) {
                Json::Value err;
                err["error"] = "missing_profile";
                err["message"] = "请求体必须包含 profile 字段";
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k400BadRequest);
                resp->addHeader("X-Trace-Id", trace_id);
                cb(resp);
                concurrency_limiter().release();
                const auto latency_ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                              start)
                        .count();
                log_request_metrics(trace_id, route, 400, latency_ms);
                return;
            }

            try {
                auto rule_name = req->getParameter("rule");
                if (rule_name.empty()) {
                    rule_name = "shouzhe";
                }

                Json::Value portfolio_json;
                if (json_ptr->isMember("portfolio")) {
                    portfolio_json = (*json_ptr)["portfolio"];
                }

                Json::Value body = server::AllocationHandler::handle_plan_request(
                    (*json_ptr)["profile"], portfolio_json, rule_name);

                auto resp = HttpResponse::newHttpJsonResponse(body);
                resp->addHeader("X-Trace-Id", trace_id);
                cb(resp);
                const auto latency_ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                              start)
                        .count();
                log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
                concurrency_limiter().release();
            } catch (const std::exception &e) {
                Json::Value err;
                err["error"] = "allocation_error";
                err["message"] = e.what();
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k400BadRequest);
                resp->addHeader("X-Trace-Id", trace_id);
                cb(resp);
                const auto latency_ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                              start)
                        .count();
                log_request_metrics(trace_id, route, 400, latency_ms);
                concurrency_limiter().release();
            }
        },
        {Post});
}

} // namespace finguard::server::internal
