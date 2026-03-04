#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace finguard::util {

class TokenBucket {
public:
    bool allow(const std::string &key, double rate_rps, int capacity);

private:
    struct BucketState {
        double tokens = 0.0;
        std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    };

    std::mutex mutex_;
    std::unordered_map<std::string, BucketState> buckets_;
};

} // namespace finguard::util
