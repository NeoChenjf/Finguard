#include "server/routes_internal.h"

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>

#include "data/fundamentals_db.h"
#include "llm/llm_client.h"
#include "valuation/valuation_config.h"
#include "util/metrics_registry.h"

namespace finguard::server::internal {

void register_system_routes() {
    using namespace drogon;

    app().registerHandler(
        "/metrics",
        [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
            const auto snap = util::global_metrics().snapshot();
            Json::Value body;
            body["requests_total"] = Json::Int64(snap.requests_total);
            body["rate_limit_rejects_total"] = Json::Int64(snap.rate_limit_rejects_total);
            body["circuit_breaker_trips_total"] = Json::Int64(snap.circuit_breaker_trips_total);
            body["latency_p95_ms"] = snap.latency_p95_ms;
            body["latency_p99_ms"] = snap.latency_p99_ms;
            body["external_call_latency_ms_p95"] = snap.external_call_latency_ms_p95;
            auto resp = HttpResponse::newHttpJsonResponse(body);
            cb(resp);
        },
        {Get});

    app().registerHandler(
        "/api/v1/{path}",
        [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k204NoContent);
            add_cors_headers(resp);
            cb(resp);
        },
        {Options});

    app().registerHandler(
        "/api/v1/settings",
        [](const HttpRequestPtr &, std::function<void(const HttpResponsePtr &)> &&cb) {
            llm::LlmClient client;
            const auto cfg = client.load_config();
            const auto valuation_cfg = valuation::load_valuation_config();
            const auto active_db_profile =
                data::resolve_fundamentals_db_profile(valuation_cfg.valuecell_db_profile);
            const auto available_db_profiles = data::available_fundamentals_db_profiles();

            Json::Value body;
            body["api_base"] = cfg.api_base;
            body["model"] = cfg.model;
            body["temperature"] = cfg.temperature;
            body["timeout_ms"] = cfg.timeout_ms;
            body["valuecell_db_profile"] = valuation_cfg.valuecell_db_profile.empty()
                                               ? "main"
                                               : valuation_cfg.valuecell_db_profile;
            body["valuecell_db_path_hint"] = active_db_profile.path.string();
            body["valuecell_db_profile_active"] = active_db_profile.key;
            body["valuecell_db_profile_label"] = active_db_profile.label;
            Json::Value db_profiles(Json::arrayValue);
            for (const auto &profile : available_db_profiles) {
                Json::Value item;
                item["key"] = profile.key;
                item["label"] = profile.label;
                item["description"] = profile.description;
                item["path_hint"] = profile.path.string();
                item["exists"] = profile.exists;
                item["is_demo"] = profile.is_demo;
                db_profiles.append(item);
            }
            body["valuecell_db_profiles"] = db_profiles;

            if (cfg.api_key.empty()) {
                body["api_key_configured"] = false;
                body["api_key_hint"] = "";
            } else {
                body["api_key_configured"] = true;
                if (cfg.api_key.size() > 4) {
                    body["api_key_hint"] = "****" + cfg.api_key.substr(cfg.api_key.size() - 4);
                } else {
                    body["api_key_hint"] = "****";
                }
            }

            auto resp = HttpResponse::newHttpJsonResponse(body);
            add_cors_headers(resp);
            cb(resp);
        },
        {Get});

    app().registerHandler(
        "/api/v1/settings",
        [](const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&cb) {
            auto json_ptr = req ? req->getJsonObject() : nullptr;
            if (!json_ptr) {
                Json::Value err;
                err["error"] = "invalid_json";
                auto resp = HttpResponse::newHttpJsonResponse(err);
                resp->setStatusCode(k400BadRequest);
                add_cors_headers(resp);
                cb(resp);
                return;
            }
            const auto &input = *json_ptr;

            std::lock_guard<std::mutex> lock(settings_mutex());

            const auto llm_config_path = std::filesystem::current_path() / "config" / "llm.json";
            const auto valuation_config_path =
                std::filesystem::current_path() / "config" / "valuation.json";

            nlohmann::json llm_doc = nlohmann::json::object();
            {
                std::ifstream in(llm_config_path);
                if (in) {
                    try {
                        in >> llm_doc;
                    } catch (...) {
                        llm_doc = nlohmann::json::object();
                    }
                }
            }
            nlohmann::json valuation_doc = nlohmann::json::object();
            {
                std::ifstream in(valuation_config_path);
                if (in) {
                    try {
                        in >> valuation_doc;
                    } catch (...) {
                        valuation_doc = nlohmann::json::object();
                    }
                }
            }

            if (input.isMember("api_key") && input["api_key"].isString()) {
                llm_doc["api_key"] = input["api_key"].asString();
            }
            if (input.isMember("api_base") && input["api_base"].isString()) {
                llm_doc["api_base"] = input["api_base"].asString();
            }
            if (input.isMember("model") && input["model"].isString()) {
                llm_doc["model"] = input["model"].asString();
            }
            if (input.isMember("temperature") && input["temperature"].isNumeric()) {
                llm_doc["temperature"] = input["temperature"].asDouble();
            }
            if (input.isMember("timeout_ms") && input["timeout_ms"].isNumeric()) {
                llm_doc["timeout_ms"] = input["timeout_ms"].asInt();
            }
            if (input.isMember("valuecell_db_profile") && input["valuecell_db_profile"].isString()) {
                const auto requested_profile = input["valuecell_db_profile"].asString();
                const auto available_profiles = data::available_fundamentals_db_profiles();
                const auto valid = std::any_of(
                    available_profiles.begin(), available_profiles.end(),
                    [&](const auto &profile) { return profile.key == requested_profile; });
                if (!valid) {
                    Json::Value err;
                    err["error"] = "invalid_valuecell_db_profile";
                    auto resp = HttpResponse::newHttpJsonResponse(err);
                    resp->setStatusCode(k400BadRequest);
                    add_cors_headers(resp);
                    cb(resp);
                    return;
                }
                valuation_doc["valuecell_db_profile"] = requested_profile;
            }

            {
                std::ofstream out(llm_config_path);
                if (!out) {
                    Json::Value err;
                    err["error"] = "write_failed";
                    auto resp = HttpResponse::newHttpJsonResponse(err);
                    resp->setStatusCode(k500InternalServerError);
                    add_cors_headers(resp);
                    cb(resp);
                    return;
                }
                out << llm_doc.dump(4);
            }
            {
                std::ofstream out(valuation_config_path);
                if (!out) {
                    Json::Value err;
                    err["error"] = "write_failed";
                    auto resp = HttpResponse::newHttpJsonResponse(err);
                    resp->setStatusCode(k500InternalServerError);
                    add_cors_headers(resp);
                    cb(resp);
                    return;
                }
                out << valuation_doc.dump(4);
            }

            llm::invalidate_llm_config_cache();

            Json::Value body;
            body["status"] = "ok";
            body["message"] = "settings updated";
            auto resp = HttpResponse::newHttpJsonResponse(body);
            add_cors_headers(resp);
            cb(resp);

            LOG_INFO << "Settings updated via POST /api/v1/settings";
        },
        {Post});
}

} // namespace finguard::server::internal
