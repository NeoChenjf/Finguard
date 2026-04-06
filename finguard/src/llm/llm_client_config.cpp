#include "llm/llm_client_internal.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace finguard::llm {
namespace {
std::mutex g_llm_cfg_mtx;
LlmConfig g_llm_cfg;
std::chrono::steady_clock::time_point g_llm_cfg_loaded_at{};
} // namespace
} // namespace finguard::llm

namespace finguard::llm::internal {

LlmConfig load_config_impl() {
    LlmConfig cfg = default_config();
    const std::filesystem::path path = std::filesystem::current_path() / "config" / "llm.json";

    if (std::filesystem::exists(path)) {
        std::ifstream in(path);
        if (in) {
            nlohmann::json doc;
            try {
                in >> doc;
                if (doc.contains("api_base") && doc["api_base"].is_string()) {
                    cfg.api_base = trim_copy(doc["api_base"].get<std::string>());
                }
                if (doc.contains("api_key") && doc["api_key"].is_string()) {
                    cfg.api_key = trim_copy(doc["api_key"].get<std::string>());
                }
                if (doc.contains("model") && doc["model"].is_string()) {
                    cfg.model = trim_copy(doc["model"].get<std::string>());
                }
                if (doc.contains("temperature") && doc["temperature"].is_number()) {
                    cfg.temperature = doc["temperature"].get<double>();
                }
                if (doc.contains("timeout_ms") && doc["timeout_ms"].is_number_integer()) {
                    cfg.timeout_ms = doc["timeout_ms"].get<int>();
                }
                if (doc.contains("use_curl_fallback") && doc["use_curl_fallback"].is_boolean()) {
                    cfg.use_curl_fallback = doc["use_curl_fallback"].get<bool>();
                }
                if (doc.contains("curl_path") && doc["curl_path"].is_string()) {
                    cfg.curl_path = trim_copy(doc["curl_path"].get<std::string>());
                }
                if (doc.contains("http_proxy") && doc["http_proxy"].is_string()) {
                    cfg.http_proxy = trim_copy(doc["http_proxy"].get<std::string>());
                }
            } catch (const std::exception &) {
            }
        }
    }

    const auto env_api_base = get_env_trimmed("FINGUARD_LLM_API_BASE");
    if (!env_api_base.empty()) {
        cfg.api_base = env_api_base;
    }
    const auto env_api_key = get_env_trimmed("FINGUARD_LLM_API_KEY");
    if (!env_api_key.empty()) {
        cfg.api_key = env_api_key;
    }
    const auto env_model = get_env_trimmed("FINGUARD_LLM_MODEL");
    if (!env_model.empty()) {
        cfg.model = env_model;
    }
    const auto env_timeout = get_env_trimmed("FINGUARD_LLM_TIMEOUT_MS");
    if (!env_timeout.empty()) {
        cfg.timeout_ms = parse_env_int(env_timeout, cfg.timeout_ms);
    }
    const auto env_proxy = get_env_trimmed("FINGUARD_LLM_HTTP_PROXY");
    if (!env_proxy.empty()) {
        cfg.http_proxy = env_proxy;
    }
    const auto env_use_curl = get_env_trimmed("FINGUARD_LLM_USE_CURL_FALLBACK");
    if (!env_use_curl.empty()) {
        cfg.use_curl_fallback = parse_env_bool(env_use_curl, cfg.use_curl_fallback);
    }

    return cfg;
}

LlmConfig load_cached_config() {
    std::lock_guard<std::mutex> lock(::finguard::llm::g_llm_cfg_mtx);
    const auto now = std::chrono::steady_clock::now();
    if (now - ::finguard::llm::g_llm_cfg_loaded_at > std::chrono::seconds(5)) {
        ::finguard::llm::g_llm_cfg = load_config_impl();
        ::finguard::llm::g_llm_cfg_loaded_at = now;
    }
    return ::finguard::llm::g_llm_cfg;
}

void invalidate_llm_config_cache_impl() {
    std::lock_guard<std::mutex> lock(::finguard::llm::g_llm_cfg_mtx);
    ::finguard::llm::g_llm_cfg_loaded_at = std::chrono::steady_clock::time_point{};
}

} // namespace finguard::llm::internal

namespace finguard::llm {

void invalidate_llm_config_cache() {
    internal::invalidate_llm_config_cache_impl();
}

LlmConfig LlmClient::load_config() const {
    return internal::load_config_impl();
}

} // namespace finguard::llm

