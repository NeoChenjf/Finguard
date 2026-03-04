#include "util/metrics_registry.h"

#include <algorithm>

namespace finguard::util {

void MetricsRegistry::record_request(double latency_ms) {
    requests_total_.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);
    latencies_.push_back(latency_ms);
    if (latencies_.size() > 2048) {
        latencies_.erase(latencies_.begin(), latencies_.begin() + 1024);
    }
}

void MetricsRegistry::record_rate_limit_reject() {
    rate_limit_rejects_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::record_circuit_breaker_trip() {
    circuit_breaker_trips_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::record_external_call_latency(double latency_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    external_latencies_.push_back(latency_ms);
    if (external_latencies_.size() > 2048) {
        external_latencies_.erase(external_latencies_.begin(), external_latencies_.begin() + 1024);
    }
}

double MetricsRegistry::percentile(const std::vector<double> &values, double p) {
    if (values.empty()) {
        return 0.0;
    }
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const double rank = p * (sorted.size() - 1);
    const std::size_t idx = static_cast<std::size_t>(rank);
    return sorted[idx];
}

MetricsSnapshot MetricsRegistry::snapshot() const {
    MetricsSnapshot snap;
    snap.requests_total = requests_total_.load(std::memory_order_relaxed);
    snap.rate_limit_rejects_total = rate_limit_rejects_.load(std::memory_order_relaxed);
    snap.circuit_breaker_trips_total = circuit_breaker_trips_.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);
    snap.latency_p95_ms = percentile(latencies_, 0.95);
    snap.latency_p99_ms = percentile(latencies_, 0.99);
    snap.external_call_latency_ms_p95 = percentile(external_latencies_, 0.95);
    return snap;
}

MetricsRegistry &global_metrics() {
    static MetricsRegistry registry;
    return registry;
}

} // namespace finguard::util
