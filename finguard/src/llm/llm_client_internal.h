#pragma once

#include "llm/llm_client.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace finguard::llm::internal {

struct HostInfo {
    std::string host;
    std::string prefix;
    uint16_t port = 80;
    bool use_ssl = false;
};

struct ProxyInfo {
    bool enabled = false;
    std::string host;
    uint16_t port = 0;
    bool use_ssl = false;
};

std::string trim_copy(const std::string &value);
std::string truncate_copy(const std::string &value, std::size_t max_len);
std::string get_env_trimmed(const char *name);
bool parse_env_bool(const std::string &value, bool fallback);
int parse_env_int(const std::string &value, int fallback);
LlmConfig default_config();
std::string model_family(const std::string &model);
HostInfo parse_base(const std::string &api_base);
std::string resolve_host_ipv4(const std::string &hostname);
ProxyInfo parse_proxy(const std::string &proxy_raw);
std::string join_url(const std::string &base, const std::string &path);
bool parse_completion_payload(const std::string &body,
                              std::string &message,
                              LlmMetrics &metrics,
                              std::vector<std::string> &warnings);
bool curl_fallback_request(const LlmConfig &cfg,
                           const nlohmann::json &body,
                           std::string &message,
                           LlmMetrics &metrics,
                           std::vector<std::string> &warnings);

LlmConfig load_config_impl();
LlmConfig load_cached_config();
void invalidate_llm_config_cache_impl();

} // namespace finguard::llm::internal
