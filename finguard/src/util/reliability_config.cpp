#include "util/reliability_config.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

#include "util/simple_yaml.h"

namespace finguard::util {

namespace {

std::string load_file(const std::filesystem::path &path) {
    std::ifstream in(path);
    if (!in) {
        return "";
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

RateLimitConfig load_rate_limit_config() {
    RateLimitConfig cfg;
    cfg.model_limits["qwen"] = {2.0, 6};
    cfg.model_limits["deepseek"] = {1.0, 3};

    const auto path = std::filesystem::current_path() / "config" / "rate_limit.yaml";
    if (!std::filesystem::exists(path)) {
        return cfg;
    }
    YamlDoc doc;
    std::string error;
    if (!parse_simple_yaml(load_file(path), doc, &error)) {
        return cfg;
    }

    if (doc.scalars.count("entry.key")) {
        cfg.entry.key = doc.scalars["entry.key"];
    }
    if (doc.scalars.count("entry.rate_rps")) {
        cfg.entry.rate_rps = std::stod(doc.scalars["entry.rate_rps"]);
    }
    if (doc.scalars.count("entry.capacity")) {
        cfg.entry.capacity = std::stoi(doc.scalars["entry.capacity"]);
    }
    if (doc.scalars.count("entry.on_reject")) {
        cfg.entry.on_reject = doc.scalars["entry.on_reject"];
    }

    for (const auto &item : doc.scalars) {
        const auto &key = item.first;
        if (key.rfind("model.", 0) != 0) {
            continue;
        }
        const auto rest = key.substr(6);
        const auto dot_pos = rest.find('.');
        if (dot_pos == std::string::npos) {
            continue;
        }
        const auto model = rest.substr(0, dot_pos);
        const auto field = rest.substr(dot_pos + 1);
        auto &model_cfg = cfg.model_limits[model];
        if (field == "rate_rps") {
            model_cfg.rate_rps = std::stod(item.second);
        } else if (field == "capacity") {
            model_cfg.capacity = std::stoi(item.second);
        }
    }

    return cfg;
}

TimeoutConfig load_timeout_config() {
    TimeoutConfig cfg;
    cfg.external_call_timeout_ms = 15000;
    cfg.external_call_max_retries = 1;
    cfg.external_call_backoff_ms = 500;
    cfg.route_overrides["chat_stream"] = {20000, 1};
    cfg.route_overrides["plan"] = {15000, 1};

    const auto path = std::filesystem::current_path() / "config" / "timeout.yaml";
    if (!std::filesystem::exists(path)) {
        return cfg;
    }
    YamlDoc doc;
    std::string error;
    if (!parse_simple_yaml(load_file(path), doc, &error)) {
        return cfg;
    }

    if (doc.scalars.count("external_call_timeout_ms")) {
        cfg.external_call_timeout_ms = std::stoi(doc.scalars["external_call_timeout_ms"]);
    }
    if (doc.scalars.count("external_call_max_retries")) {
        cfg.external_call_max_retries = std::stoi(doc.scalars["external_call_max_retries"]);
    }
    if (doc.scalars.count("external_call_backoff_ms")) {
        cfg.external_call_backoff_ms = std::stoi(doc.scalars["external_call_backoff_ms"]);
    }

    for (const auto &item : doc.scalars) {
        const auto &key = item.first;
        if (key.rfind("routes.", 0) == 0) {
            const auto rest = key.substr(7);
            const auto dot_pos = rest.find('.');
            if (dot_pos == std::string::npos) {
                continue;
            }
            const auto route = rest.substr(0, dot_pos);
            const auto field = rest.substr(dot_pos + 1);
            auto &route_cfg = cfg.route_overrides[route];
            if (field == "timeout_ms") {
                route_cfg.timeout_ms = std::stoi(item.second);
            } else if (field == "max_retries") {
                route_cfg.max_retries = std::stoi(item.second);
            }
        }
        if (key.rfind("models.", 0) == 0) {
            const auto rest = key.substr(7);
            const auto dot_pos = rest.find('.');
            if (dot_pos == std::string::npos) {
                continue;
            }
            const auto model = rest.substr(0, dot_pos);
            const auto field = rest.substr(dot_pos + 1);
            auto &model_cfg = cfg.model_overrides[model];
            if (field == "timeout_ms") {
                model_cfg.timeout_ms = std::stoi(item.second);
            } else if (field == "max_retries") {
                model_cfg.max_retries = std::stoi(item.second);
            }
        }
    }

    return cfg;
}

CircuitBreakerConfig load_circuit_breaker_config() {
    CircuitBreakerConfig cfg;
    const auto path = std::filesystem::current_path() / "config" / "circuit_breaker.yaml";
    if (!std::filesystem::exists(path)) {
        return cfg;
    }
    YamlDoc doc;
    std::string error;
    if (!parse_simple_yaml(load_file(path), doc, &error)) {
        return cfg;
    }
    if (doc.scalars.count("error_rate_threshold")) {
        cfg.error_rate_threshold = std::stod(doc.scalars["error_rate_threshold"]);
    }
    if (doc.scalars.count("window_seconds")) {
        cfg.window_seconds = std::stoi(doc.scalars["window_seconds"]);
    }
    if (doc.scalars.count("half_open_max_trials")) {
        cfg.half_open_max_trials = std::stoi(doc.scalars["half_open_max_trials"]);
    }
    if (doc.scalars.count("min_samples")) {
        cfg.min_samples = std::stoi(doc.scalars["min_samples"]);
    }
    return cfg;
}

ConcurrencyConfig load_concurrency_config() {
    ConcurrencyConfig cfg;
    const auto path = std::filesystem::current_path() / "config" / "concurrency.yaml";
    if (!std::filesystem::exists(path)) {
        return cfg;
    }
    YamlDoc doc;
    std::string error;
    if (!parse_simple_yaml(load_file(path), doc, &error)) {
        return cfg;
    }
    if (doc.scalars.count("max_inflight")) {
        cfg.max_inflight = std::stoi(doc.scalars["max_inflight"]);
    }
    return cfg;
}

ObservabilityConfig load_observability_config() {
    ObservabilityConfig cfg;
    cfg.log_fields = {"trace_id", "route", "status", "latency_ms"};
    cfg.metrics = {"requests_total", "latency_p95_ms", "latency_p99_ms", "rate_limit_rejects_total",
                   "circuit_breaker_trips_total", "external_call_latency_ms"};

    const auto path = std::filesystem::current_path() / "config" / "observability.yaml";
    if (!std::filesystem::exists(path)) {
        return cfg;
    }
    YamlDoc doc;
    std::string error;
    if (!parse_simple_yaml(load_file(path), doc, &error)) {
        return cfg;
    }
    if (doc.lists.count("log_fields")) {
        cfg.log_fields = doc.lists["log_fields"];
    }
    if (doc.lists.count("metrics")) {
        cfg.metrics = doc.lists["metrics"];
    }
    return cfg;
}

// ── P5 perf: cached config loaders (5-second TTL, thread-safe) ──────
namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kCacheTtl = std::chrono::seconds(5);

template <typename T>
struct ConfigCache {
    std::mutex mtx;
    T value{};
    Clock::time_point loaded_at{};

    template <typename Loader>
    T get(Loader loader) {
        std::lock_guard<std::mutex> lock(mtx);
        const auto now = Clock::now();
        if (now - loaded_at > kCacheTtl) {
            value = loader();
            loaded_at = now;
        }
        return value;
    }
};

ConfigCache<RateLimitConfig> g_rate_limit_cache;
ConfigCache<TimeoutConfig> g_timeout_cache;
ConfigCache<CircuitBreakerConfig> g_cb_cache;
ConfigCache<ConcurrencyConfig> g_conc_cache;

} // namespace

RateLimitConfig cached_rate_limit_config() {
    return g_rate_limit_cache.get(load_rate_limit_config);
}

TimeoutConfig cached_timeout_config() {
    return g_timeout_cache.get(load_timeout_config);
}

CircuitBreakerConfig cached_circuit_breaker_config() {
    return g_cb_cache.get(load_circuit_breaker_config);
}

ConcurrencyConfig cached_concurrency_config() {
    return g_conc_cache.get(load_concurrency_config);
}

} // namespace finguard::util
