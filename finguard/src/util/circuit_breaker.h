#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "util/reliability_config.h"

namespace finguard::util {

class CircuitBreaker {
public:
    explicit CircuitBreaker(CircuitBreakerConfig config);

    // Returns true if request can proceed.
    bool allow(const std::string &key);
    void record_success(const std::string &key);
    void record_failure(const std::string &key);

private:
    enum class State { Closed, Open, HalfOpen };

    struct Event {
        std::chrono::steady_clock::time_point ts;
        bool failure = false;
    };

    struct StateData {
        State state = State::Closed;
        std::chrono::steady_clock::time_point open_until;
        int half_open_trials = 0;
        std::vector<Event> events;
    };

    void prune_events(StateData &data, const std::chrono::steady_clock::time_point &now) const;
    double failure_rate(const StateData &data) const;

    CircuitBreakerConfig config_;
    std::mutex mutex_;
    std::unordered_map<std::string, StateData> states_;
};

} // namespace finguard::util
