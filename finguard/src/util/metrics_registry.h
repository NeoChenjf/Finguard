#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace finguard::util {

struct MetricsSnapshot {
    long long requests_total = 0;
    long long rate_limit_rejects_total = 0;
    long long circuit_breaker_trips_total = 0;
    double latency_p95_ms = 0.0;
    double latency_p99_ms = 0.0;
    double external_call_latency_ms_p95 = 0.0;
};

class MetricsRegistry {
public:
    void record_request(double latency_ms);
    void record_rate_limit_reject();
    void record_circuit_breaker_trip();
    void record_external_call_latency(double latency_ms);

    MetricsSnapshot snapshot() const;

private:
    static double percentile(const std::vector<double> &values, double p);

    std::atomic<long long> requests_total_{0};
    std::atomic<long long> rate_limit_rejects_{0};
    std::atomic<long long> circuit_breaker_trips_{0};

    mutable std::mutex mutex_;
    std::vector<double> latencies_;
    std::vector<double> external_latencies_;
};

MetricsRegistry &global_metrics();

} // namespace finguard::util
