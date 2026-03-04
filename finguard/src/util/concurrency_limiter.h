#pragma once

#include <atomic>

namespace finguard::util {

class ConcurrencyLimiter {
public:
    explicit ConcurrencyLimiter(int max_inflight)
        : max_inflight_(max_inflight) {}

    bool try_acquire();
    void release();
    void set_max_inflight(int max_inflight);
    int max_inflight() const { return max_inflight_.load(); }

private:
    std::atomic<int> max_inflight_{1};
    std::atomic<int> inflight_{0};
};

} // namespace finguard::util
