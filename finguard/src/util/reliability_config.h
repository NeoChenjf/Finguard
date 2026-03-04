#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace finguard::util {

struct RateLimitEntryConfig {
    std::string key = "user+route";
    double rate_rps = 5.0;
    int capacity = 15;
    std::string on_reject = "reject_429";
};

struct RateLimitModelConfig {
    double rate_rps = 1.0;
    int capacity = 3;
};

struct RateLimitConfig {
    RateLimitEntryConfig entry;
    std::unordered_map<std::string, RateLimitModelConfig> model_limits;
};

struct TimeoutRouteConfig {
    int timeout_ms = 15000;
    int max_retries = 1;
};

struct TimeoutModelConfig {
    int timeout_ms = 15000;
    int max_retries = 1;
};

struct TimeoutConfig {
    int external_call_timeout_ms = 15000;
    int external_call_max_retries = 1;
    int external_call_backoff_ms = 500;
    std::unordered_map<std::string, TimeoutRouteConfig> route_overrides;
    std::unordered_map<std::string, TimeoutModelConfig> model_overrides;
};

struct CircuitBreakerConfig {
    double error_rate_threshold = 0.50;
    int window_seconds = 30;
    int half_open_max_trials = 2;
    int min_samples = 5;
};

struct ConcurrencyConfig {
    int max_inflight = 4;
};

struct ObservabilityConfig {
    std::vector<std::string> log_fields;
    std::vector<std::string> metrics;
};

RateLimitConfig load_rate_limit_config();
TimeoutConfig load_timeout_config();
CircuitBreakerConfig load_circuit_breaker_config();
ConcurrencyConfig load_concurrency_config();
ObservabilityConfig load_observability_config();

// ── P5 perf: cached versions with 5-second TTL (thread-safe) ──
// These avoid per-request file I/O; call these in hot paths instead of
// the raw load_* variants above.
RateLimitConfig cached_rate_limit_config();
TimeoutConfig cached_timeout_config();
CircuitBreakerConfig cached_circuit_breaker_config();
ConcurrencyConfig cached_concurrency_config();

} // namespace finguard::util
