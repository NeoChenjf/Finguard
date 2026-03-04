#include "util/circuit_breaker.h"

#include <algorithm>

namespace finguard::util {

CircuitBreaker::CircuitBreaker(CircuitBreakerConfig config)
    : config_(std::move(config)) {}

void CircuitBreaker::prune_events(StateData &data, const std::chrono::steady_clock::time_point &now) const {
    const auto window = std::chrono::seconds(config_.window_seconds);
    const auto cutoff = now - window;
    auto &events = data.events;
    events.erase(std::remove_if(events.begin(), events.end(), [&](const Event &e) { return e.ts < cutoff; }),
                 events.end());
}

double CircuitBreaker::failure_rate(const StateData &data) const {
    if (data.events.empty()) {
        return 0.0;
    }
    int failures = 0;
    for (const auto &event : data.events) {
        if (event.failure) {
            failures++;
        }
    }
    return static_cast<double>(failures) / static_cast<double>(data.events.size());
}

bool CircuitBreaker::allow(const std::string &key) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    auto &data = states_[key];
    prune_events(data, now);

    if (data.state == State::Open) {
        if (now >= data.open_until) {
            data.state = State::HalfOpen;
            data.half_open_trials = 0;
        } else {
            return false;
        }
    }

    if (data.state == State::HalfOpen) {
        if (data.half_open_trials >= config_.half_open_max_trials) {
            return false;
        }
        data.half_open_trials++;
        return true;
    }

    return true;
}

void CircuitBreaker::record_success(const std::string &key) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    auto &data = states_[key];
    prune_events(data, now);
    data.events.push_back({now, false});

    if (data.state == State::HalfOpen) {
        if (data.half_open_trials >= config_.half_open_max_trials) {
            data.state = State::Closed;
            data.events.clear();
            data.half_open_trials = 0;
        }
    }
}

void CircuitBreaker::record_failure(const std::string &key) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    auto &data = states_[key];
    prune_events(data, now);
    data.events.push_back({now, true});

    const auto total = static_cast<int>(data.events.size());
    const auto rate = failure_rate(data);
    if (total >= config_.min_samples && rate >= config_.error_rate_threshold) {
        data.state = State::Open;
        data.open_until = now + std::chrono::seconds(config_.window_seconds);
        data.half_open_trials = 0;
    }

    if (data.state == State::HalfOpen) {
        data.state = State::Open;
        data.open_until = now + std::chrono::seconds(config_.window_seconds);
        data.half_open_trials = 0;
    }
}

} // namespace finguard::util
