#include "server/routes_internal.h"

#include <drogon/drogon.h>

#include <chrono>
#include <string>
#include <vector>

#include <json/json.h>

#include "llm/llm_client.h"
#include "risk/profile_store.h"
#include "risk/rule_engine.h"
#include "valuation/analysis_workflow.h"
#include "valuation/valuation_config.h"
#include "util/metrics_registry.h"
#include "util/reliability_config.h"

namespace finguard::server::internal {

namespace {

std::string extract_prompt(const drogon::HttpRequestPtr &req) {
    if (!req) {
        return "";
    }
    auto json = req->getJsonObject();
    if (json && json->isMember("prompt") && (*json)["prompt"].isString()) {
        return (*json)["prompt"].asString();
    }
    return "";
}

std::string extract_symbol(const drogon::HttpRequestPtr &req, const std::string &prompt) {
    if (req) {
        auto json = req->getJsonObject();
        if (json && json->isMember("symbol") && (*json)["symbol"].isString()) {
            return (*json)["symbol"].asString();
        }
    }
    return valuation::extract_symbol_candidate(prompt);
}

std::vector<Json::Value> build_warning_events(const llm::StreamResult &result,
                                              const risk::RuleResult &rule_result,
                                              const risk::RuleEngine &rule_engine,
                                              bool has_profile,
                                              const std::string &profile_error,
                                              const std::string &rules_error) {
    std::vector<Json::Value> warning_events;
    for (const auto &warning : result.warnings) {
        warning_events.emplace_back(warning);
    }
    for (const auto &warning : rule_result.warnings) {
        warning_events.emplace_back(warning);
    }
    if (!has_profile) {
        if (!profile_error.empty()) {
            warning_events.emplace_back("profile_error:" + profile_error);
        } else {
            warning_events.emplace_back("profile_missing_or_unreadable");
        }
    }
    if (!rules_error.empty()) {
        warning_events.emplace_back("rules_load_failed");
    }

    const auto response_warnings = rule_engine.check_response(result.full_text);
    for (const auto &warning : response_warnings) {
        warning_events.push_back(warning);
    }
    if (warning_events.empty()) {
        warning_events.emplace_back("none");
    }
    return warning_events;
}

llm::LlmMetrics normalize_stream_metrics(const llm::StreamResult &result) {
    llm::LlmMetrics metrics = result.metrics;
    if (!metrics.has_usage) {
        metrics.has_usage = true;
        metrics.prompt_tokens = 0;
        metrics.completion_tokens = static_cast<int>(result.tokens.size());
        metrics.total_tokens = metrics.completion_tokens;
    }
    return metrics;
}

std::string build_sse_payload(const llm::StreamResult &result,
                              const llm::LlmMetrics &metrics,
                              std::vector<std::string> cites,
                              const std::vector<Json::Value> &warning_events) {
    if (cites.empty()) {
        cites.push_back("none");
    }

    std::string payload;
    payload.reserve(256);

    for (const auto &token : result.tokens) {
        append_sse_data_event(payload, "token", Json::Value(token));
    }
    for (const auto &cite : cites) {
        append_sse_data_event(payload, "cite", Json::Value(cite));
    }
    if (metrics.has_usage) {
        Json::Value metric_payload;
        metric_payload["prompt_tokens"] = metrics.prompt_tokens;
        metric_payload["completion_tokens"] = metrics.completion_tokens;
        metric_payload["total_tokens"] = metrics.total_tokens;
        append_sse_data_event(payload, "metric", metric_payload);
    }
    for (const auto &warning : warning_events) {
        append_sse_data_event(payload, "warning", warning);
    }
    append_sse_data_event(payload, "done", Json::Value("done"));
    return payload;
}

} // namespace

void register_chat_routes() {
    using namespace drogon;

    app().registerHandler(
        "/api/v1/chat/stream",
        [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            const auto trace_id = get_or_create_trace_id(req);
            const auto start = std::chrono::steady_clock::now();
            const auto route = req ? req->path() : "/api/v1/chat/stream";
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

            std::string prompt = extract_prompt(req);
            const std::string prompt_symbol = extract_symbol(req, prompt);

            Json::Value questionnaire;
            std::string profile_error;
            const auto user_id = req ? req->getHeader("X-User-Id") : "";
            const bool has_profile =
                !user_id.empty() && risk::load_profile(user_id, &questionnaire, &profile_error);

            risk::RuleEngine rule_engine;
            std::string rules_error;
            rule_engine.load_config(&rules_error);

            auto rule_result = rule_engine.check_request(prompt, questionnaire);

            llm::LlmClient client;
            llm::LlmConfig config = client.load_config();
            if (config.api_key.empty()) {
                Json::Value body;
                body["error"] = "server_missing_api_key";
                auto resp = HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(k500InternalServerError);
                resp->addHeader("X-Trace-Id", trace_id);
                cb(resp);
                const auto latency_ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                              start)
                        .count();
                log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
                concurrency_limiter().release();
                return;
            }

            auto request_key = req ? req->getHeader("X-API-Key") : "";
            if (request_key.empty()) {
                request_key = config.api_key;
            }
            if (request_key != config.api_key) {
                Json::Value body;
                body["error"] = "unauthorized";
                auto resp = HttpResponse::newHttpJsonResponse(body);
                resp->setStatusCode(k401Unauthorized);
                resp->addHeader("X-Trace-Id", trace_id);
                cb(resp);
                const auto latency_ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                              start)
                        .count();
                log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
                concurrency_limiter().release();
                return;
            }

            const auto valuation_config = valuation::load_valuation_config();
            const auto analysis_context =
                prompt_symbol.empty()
                    ? valuation::AnalysisContext{}
                    : valuation::load_analysis_context(prompt_symbol, valuation_config, true);
            const auto final_prompt = valuation::build_llm_analysis_prompt(
                prompt, analysis_context, false);

            auto result = client.stream_chat(final_prompt);
            if (analysis_context.fetched_from_network) {
                result.warnings.push_back("db_network_bootstrap_used");
            }
            if (analysis_context.persisted_to_db) {
                result.warnings.push_back("db_network_bootstrap_persisted");
            }
            if (analysis_context.used_llm_knowledge_fallback) {
                result.warnings.push_back("llm_general_knowledge_fallback");
            }

            const auto warning_events =
                build_warning_events(result, rule_result, rule_engine, has_profile, profile_error,
                                     rules_error);
            const auto metrics = normalize_stream_metrics(result);
            auto resp = make_sse_response(
                build_sse_payload(result, metrics, result.cites, warning_events), trace_id);
            cb(resp);

            const auto latency_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
            log_request_metrics(trace_id, route, resp->getStatusCode(), latency_ms);
            concurrency_limiter().release();
        },
        {Post});
}

} // namespace finguard::server::internal
