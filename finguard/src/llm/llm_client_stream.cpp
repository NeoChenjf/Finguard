#include "llm/llm_client_internal.h"

#include <drogon/drogon.h>
#include <trantor/net/Resolver.h>
#include <chrono>
#include <memory>
#include <sstream>
#include <thread>
#include <tuple>

#include "util/circuit_breaker.h"
#include "util/metrics_registry.h"
#include "util/reliability_config.h"
#include "util/token_bucket.h"

namespace finguard::llm {

StreamResult LlmClient::stream_chat(const std::string &prompt) const {
    StreamResult result;
    LlmConfig cfg = internal::load_cached_config();
    const auto family = internal::model_family(cfg.model);

    const auto rate_cfg = util::cached_rate_limit_config();
    const auto timeout_cfg = util::cached_timeout_config();
    const auto cb_cfg = util::cached_circuit_breaker_config();

    static util::TokenBucket model_bucket;
    static std::unique_ptr<util::CircuitBreaker> circuit_breaker;
    static util::CircuitBreakerConfig last_cb_cfg;
    if (!circuit_breaker || last_cb_cfg.error_rate_threshold != cb_cfg.error_rate_threshold ||
        last_cb_cfg.window_seconds != cb_cfg.window_seconds ||
        last_cb_cfg.half_open_max_trials != cb_cfg.half_open_max_trials ||
        last_cb_cfg.min_samples != cb_cfg.min_samples) {
        circuit_breaker = std::make_unique<util::CircuitBreaker>(cb_cfg);
        last_cb_cfg = cb_cfg;
    }

    const auto model_it = rate_cfg.model_limits.find(family);
    const auto model_limit =
        (model_it != rate_cfg.model_limits.end()) ? model_it->second : util::RateLimitModelConfig{};
    if (!model_bucket.allow("model:" + family, model_limit.rate_rps, model_limit.capacity)) {
        util::global_metrics().record_rate_limit_reject();
        result.degraded = true;
        result.error = "model_rate_limited";
        result.warnings.push_back("model_rate_limited");
        result.full_text = "FinGuard fallback response due to model rate limiting.";
        return result;
    }

    if (!circuit_breaker->allow("model:" + family)) {
        util::global_metrics().record_circuit_breaker_trip();
        result.degraded = true;
        result.error = "circuit_breaker_open";
        result.warnings.push_back("circuit_breaker_open");
        result.full_text = "FinGuard fallback response due to circuit breaker.";
        return result;
    }

    if (cfg.api_key.empty()) {
        result.degraded = true;
        result.warnings.push_back("missing_api_key");
    }

    std::string message;
    if (cfg.api_key.empty()) {
        if (prompt.empty()) {
            message = "FinGuard mock stream response. Provide prompt in JSON {\"prompt\":\"...\"}.";
        } else {
            message = "FinGuard mock stream response for: " + prompt;
        }
    } else {
        int timeout_ms = timeout_cfg.external_call_timeout_ms;
        int max_retries = timeout_cfg.external_call_max_retries;
        const auto route_it = timeout_cfg.route_overrides.find("chat_stream");
        if (route_it != timeout_cfg.route_overrides.end()) {
            timeout_ms = route_it->second.timeout_ms;
            max_retries = route_it->second.max_retries;
        }
        const auto model_timeout_it = timeout_cfg.model_overrides.find(family);
        if (model_timeout_it != timeout_cfg.model_overrides.end()) {
            timeout_ms = model_timeout_it->second.timeout_ms;
            max_retries = model_timeout_it->second.max_retries;
        }
        cfg.timeout_ms = timeout_ms;

        nlohmann::json body;
        body["model"] = cfg.model;
        body["temperature"] = cfg.temperature;
        body["stream"] = false;
        const std::string system_prompt =
            "You are FinGuard AI investment analysis assistant. "
            "If the user message contains structured financial data, treat that data as the highest-priority source of truth. "
            "Never claim to have used web scraping or real-time search results unless the user explicitly provided them. "
            "When structured data is incomplete, clearly separate data-backed observations from general-knowledge inferences. "
            "Never recommend allocation percentages, explicit buy/sell actions, or guaranteed returns. "
            "Whenever citing financial metrics, always include both the metric label and the explicit numeric value.";
        body["messages"] = nlohmann::json::array({
            {{"role", "system"}, {"content", system_prompt}},
            {{"role", "user"}, {"content", prompt.empty() ? "请输出一段示例流式回复" : prompt}},
        });

        const auto info = internal::parse_base(cfg.api_base);
        const auto proxy = internal::parse_proxy(cfg.http_proxy);
        const std::string default_port = info.use_ssl ? "443" : "80";
        std::string host_url = (info.use_ssl ? "https://" : "http://") + info.host;
        if (!info.host.empty() && info.port != static_cast<uint16_t>(std::stoi(default_port))) {
            host_url += ":" + std::to_string(info.port);
        }
        if (!cfg.http_proxy.empty() && !proxy.enabled) {
            LOG_WARN << "LLM proxy config invalid; ignoring http_proxy=" << cfg.http_proxy;
        }
        if (proxy.enabled) {
            LOG_INFO << "LLM proxy enabled: proxy=" << proxy.host << ":" << proxy.port
                     << " proxy_ssl=" << (proxy.use_ssl ? "true" : "false")
                     << " target=" << host_url;
        } else {
            LOG_INFO << "LLM direct connect: target=" << host_url;
        }

        LOG_INFO << "[P1 DNS] c-ares enabled: " << (trantor::Resolver::isCAresUsed() ? "YES" : "NO");
        LOG_INFO << "[P1 DNS] Attempting to create HttpClient for host: " << info.host
                 << " port: " << info.port << " ssl: " << (info.use_ssl ? "true" : "false");

        drogon::HttpClientPtr client;
        if (proxy.enabled) {
            client = drogon::HttpClient::newHttpClient(proxy.host, proxy.port, proxy.use_ssl);
        } else {
            client = drogon::HttpClient::newHttpClient(host_url);
        }
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(drogon::Post);
        const std::string path = info.prefix.empty() ? "/chat/completions" : (info.prefix + "/chat/completions");
        if (proxy.enabled) {
            const std::string scheme = info.use_ssl ? "https://" : "http://";
            std::string target = scheme + info.host;
            std::string host_header = info.host;
            if (!info.host.empty() && info.port != static_cast<uint16_t>(std::stoi(default_port))) {
                target += ":" + std::to_string(info.port);
                host_header += ":" + std::to_string(info.port);
            }
            req->setPath(target + path);
            req->addHeader("Host", host_header);
        } else {
            req->setPath(path);
        }
        req->addHeader("Authorization", "Bearer " + cfg.api_key);
        req->addHeader("Content-Type", "application/json");
        req->setBody(body.dump());

        const auto timeout_sec = static_cast<double>(cfg.timeout_ms) / 1000.0;
        LOG_INFO << "LLM request dispatch: api_base=" << cfg.api_base
                 << " timeout_sec=" << timeout_sec
                 << " use_proxy=" << (proxy.enabled ? "true" : "false");
        drogon::ReqResult res = drogon::ReqResult::Ok;
        drogon::HttpResponsePtr resp;
        for (int attempt = 0; attempt <= max_retries; ++attempt) {
            const auto attempt_start = std::chrono::steady_clock::now();
            std::tie(res, resp) = client->sendRequest(req, timeout_sec);
            const auto attempt_latency_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - attempt_start).count();
            util::global_metrics().record_external_call_latency(attempt_latency_ms);
            if (res == drogon::ReqResult::Ok && resp && resp->getStatusCode() == drogon::k200OK) {
                break;
            }
            LOG_WARN << "LLM attempt failed; attempt=" << attempt
                     << " max_retries=" << max_retries
                     << " req_result=" << drogon::to_string(res)
                     << " http_status=" << (resp ? std::to_string(resp->getStatusCode()) : "none")
                     << " attempt_latency_ms=" << attempt_latency_ms;
            if (attempt < max_retries) {
                std::this_thread::sleep_for(std::chrono::milliseconds(timeout_cfg.external_call_backoff_ms));
            }
        }

        if (res != drogon::ReqResult::Ok || !resp) {
            result.degraded = true;
            result.error = "llm_request_failed";
            result.warnings.push_back("llm_request_failed");
            circuit_breaker->record_failure("model:" + family);
            if (resp) {
                result.warnings.push_back("llm_http_status_" + std::to_string(resp->getStatusCode()));
            } else {
                result.warnings.push_back("llm_reqresult_" + std::to_string(static_cast<int>(res)));
            }

            LOG_ERROR << "LLM request failed: result=" << drogon::to_string(res)
                      << " result_code=" << static_cast<int>(res)
                      << " has_response=" << (resp ? "true" : "false")
                      << " use_proxy=" << (proxy.enabled ? "true" : "false")
                      << " target_host=" << info.host
                      << " target_port=" << info.port
                      << " use_ssl=" << (info.use_ssl ? "true" : "false");

            if (resp) {
                LOG_ERROR << "LLM HTTP response: status=" << resp->getStatusCode()
                          << " content_type=" << resp->getContentType()
                          << " body_size=" << resp->getBody().size();
            }
            if (cfg.use_curl_fallback) {
                std::vector<std::string> curl_warnings;
                if (internal::curl_fallback_request(cfg, body, message, result.metrics, curl_warnings)) {
                    result.degraded = false;
                    result.warnings.clear();
                    result.warnings.push_back("curl_fallback_used");
                    circuit_breaker->record_success("model:" + family);
                    LOG_WARN << "LLM fallback used: curl_fallback_used";
                } else {
                    result.warnings.insert(result.warnings.end(), curl_warnings.begin(), curl_warnings.end());
                    message = "FinGuard fallback response due to LLM request failure.";
                    LOG_ERROR << "LLM fallback failed; returning mock response.";
                }
            } else {
                message = "FinGuard fallback response due to LLM request failure.";
            }
        } else if (resp->getStatusCode() != drogon::k200OK) {
            result.degraded = true;
            result.error = "llm_bad_status";
            result.warnings.push_back("llm_bad_status_" + std::to_string(resp->getStatusCode()));
            const auto body_text = internal::truncate_copy(std::string(resp->getBody()), 200);
            if (!body_text.empty()) {
                result.warnings.push_back("llm_body_" + body_text);
            }
            LOG_ERROR << "LLM bad status: status=" << resp->getStatusCode()
                      << " use_proxy=" << (proxy.enabled ? "true" : "false");
            if (resp->getStatusCode() >= 500) {
                circuit_breaker->record_failure("model:" + family);
            }
            if (cfg.use_curl_fallback) {
                std::vector<std::string> curl_warnings;
                if (internal::curl_fallback_request(cfg, body, message, result.metrics, curl_warnings)) {
                    result.degraded = false;
                    result.warnings.clear();
                    result.warnings.push_back("curl_fallback_used");
                    circuit_breaker->record_success("model:" + family);
                    LOG_WARN << "LLM fallback used: curl_fallback_used";
                } else {
                    result.warnings.insert(result.warnings.end(), curl_warnings.begin(), curl_warnings.end());
                    message = "FinGuard fallback response due to LLM HTTP status.";
                    LOG_ERROR << "LLM fallback failed; returning mock response.";
                }
            } else {
                message = "FinGuard fallback response due to LLM HTTP status.";
            }
        } else {
            if (!internal::parse_completion_payload(std::string(resp->getBody()), message, result.metrics, result.warnings)) {
                result.degraded = true;
                result.error = "llm_parse_failed";
                message = "FinGuard fallback response due to LLM parse failure.";
                LOG_ERROR << "LLM parse failed.";
            } else {
                circuit_breaker->record_success("model:" + family);
                LOG_INFO << "LLM request succeeded: status=" << resp->getStatusCode()
                         << " use_proxy=" << (proxy.enabled ? "true" : "false");
            }
        }
    }

    result.full_text = message;
    std::istringstream iss(message);
    std::string token;
    while (iss >> token) {
        result.tokens.push_back(token + " ");
    }

    return result;
}

} // namespace finguard::llm

