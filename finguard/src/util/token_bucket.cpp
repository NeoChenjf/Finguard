#include "util/token_bucket.h"

#include <algorithm>

namespace finguard::util {

bool TokenBucket::allow(const std::string &key, double rate_rps, int capacity) {
    if (rate_rps <= 0.0 || capacity <= 0) {
        return true;
    }

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, inserted] = buckets_.try_emplace(key);
    auto &state = it->second;
    if (inserted) {
        state.tokens = static_cast<double>(capacity);
        state.last = now;
    }
    if (state.tokens <= 0.0 && state.tokens != 0.0) {
        state.tokens = 0.0;
    }
    const double elapsed = std::chrono::duration<double>(now - state.last).count();
    state.tokens = std::min<double>(capacity, state.tokens + elapsed * rate_rps);
    state.last = now;

    if (state.tokens >= 1.0) {
        state.tokens -= 1.0;
        return true;
    }
    return false;
}

} // namespace finguard::util
