#include "util/concurrency_limiter.h"

namespace finguard::util {

bool ConcurrencyLimiter::try_acquire() {
    int current = inflight_.load();
    while (current < max_inflight_) {
        if (inflight_.compare_exchange_weak(current, current + 1)) {
            return true;
        }
    }
    return false;
}

void ConcurrencyLimiter::release() {
    inflight_.fetch_sub(1);
}

void ConcurrencyLimiter::set_max_inflight(int max_inflight) {
    if (max_inflight > 0) {
        max_inflight_.store(max_inflight);
    }
}

} // namespace finguard::util
