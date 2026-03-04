// Phase 6: 熔断器状态机单元测试
#include <gtest/gtest.h>

#include "util/circuit_breaker.h"

#include <thread>

namespace {

finguard::util::CircuitBreakerConfig make_config(double threshold = 0.5,
                                                  int window_sec = 60,
                                                  int half_open_trials = 2,
                                                  int min_samples = 3) {
    finguard::util::CircuitBreakerConfig cfg;
    cfg.error_rate_threshold = threshold;
    cfg.window_seconds = window_sec;
    cfg.half_open_max_trials = half_open_trials;
    cfg.min_samples = min_samples;
    return cfg;
}

} // namespace

// ── 初始状态为 Closed，允许请求 ──
TEST(CircuitBreaker, InitialStateClosed) {
    auto cb = finguard::util::CircuitBreaker(make_config());
    EXPECT_TRUE(cb.allow("svc"));
}

// ── 连续成功不触发熔断 ──
TEST(CircuitBreaker, SuccessesKeepClosed) {
    auto cb = finguard::util::CircuitBreaker(make_config(0.5, 60, 2, 3));
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(cb.allow("svc"));
        cb.record_success("svc");
    }
}

// ── Closed → Open：失败率超过阈值后拒绝请求 ──
TEST(CircuitBreaker, ClosedToOpen) {
    // min_samples=3, threshold=0.5
    auto cb = finguard::util::CircuitBreaker(make_config(0.5, 60, 2, 3));

    // 3 次全部失败 → failure_rate=1.0 > 0.5
    cb.allow("svc"); cb.record_failure("svc");
    cb.allow("svc"); cb.record_failure("svc");
    cb.allow("svc"); cb.record_failure("svc");

    // 现在应该 Open，拒绝请求
    EXPECT_FALSE(cb.allow("svc"));
}

// ── Open → HalfOpen：超过 window 后允许探测 ──
TEST(CircuitBreaker, OpenToHalfOpen) {
    // window=1s 以便快速测试
    auto cb = finguard::util::CircuitBreaker(make_config(0.5, 1, 2, 3));

    // 触发熔断
    cb.allow("svc"); cb.record_failure("svc");
    cb.allow("svc"); cb.record_failure("svc");
    cb.allow("svc"); cb.record_failure("svc");
    EXPECT_FALSE(cb.allow("svc"));

    // 等待 window 过期
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    // 应进入 HalfOpen，允许探测
    EXPECT_TRUE(cb.allow("svc"));
}

// ── HalfOpen → Closed：探测成功后恢复 ──
TEST(CircuitBreaker, HalfOpenToClosed) {
    auto cb = finguard::util::CircuitBreaker(make_config(0.5, 1, 2, 3));

    // 触发熔断
    cb.allow("svc"); cb.record_failure("svc");
    cb.allow("svc"); cb.record_failure("svc");
    cb.allow("svc"); cb.record_failure("svc");
    EXPECT_FALSE(cb.allow("svc"));

    // 等待过期
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    // HalfOpen 探测（half_open_max_trials=2）
    EXPECT_TRUE(cb.allow("svc"));
    cb.record_success("svc");
    EXPECT_TRUE(cb.allow("svc"));
    cb.record_success("svc");

    // 探测全部成功 → 应恢复 Closed
    EXPECT_TRUE(cb.allow("svc"));
}

// ── HalfOpen → Open：探测失败后重新熔断 ──
TEST(CircuitBreaker, HalfOpenToOpenOnFailure) {
    auto cb = finguard::util::CircuitBreaker(make_config(0.5, 1, 2, 3));

    // 触发熔断
    cb.allow("svc"); cb.record_failure("svc");
    cb.allow("svc"); cb.record_failure("svc");
    cb.allow("svc"); cb.record_failure("svc");

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    // HalfOpen 探测失败
    EXPECT_TRUE(cb.allow("svc"));
    cb.record_failure("svc");

    // 再次打开，后续请求被拒绝
    EXPECT_FALSE(cb.allow("svc"));
}

// ── 不同 key 互不影响 ──
TEST(CircuitBreaker, DifferentKeysIndependent) {
    auto cb = finguard::util::CircuitBreaker(make_config(0.5, 60, 2, 3));

    // key "a" 触发熔断
    cb.allow("a"); cb.record_failure("a");
    cb.allow("a"); cb.record_failure("a");
    cb.allow("a"); cb.record_failure("a");
    EXPECT_FALSE(cb.allow("a"));

    // key "b" 应不受影响
    EXPECT_TRUE(cb.allow("b"));
}
