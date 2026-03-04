// Phase 6: 令牌桶单元测试
#include <gtest/gtest.h>

#include "util/token_bucket.h"

#include <thread>

// ── 初始容量允许 burst ──
TEST(TokenBucket, InitialCapacityAllowsBurst) {
    finguard::util::TokenBucket bucket;
    const std::string key = "test:burst";

    // capacity=5，连续 5 次应全部允许
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(bucket.allow(key, 1.0, 5))
            << "Request " << i << " should be allowed within initial capacity";
    }
    // 第 6 次应被拒绝（tokens 不足）
    EXPECT_FALSE(bucket.allow(key, 1.0, 5));
}

// ── 消耗后恢复 ──
TEST(TokenBucket, TokensRefillOverTime) {
    finguard::util::TokenBucket bucket;
    const std::string key = "test:refill";

    // 消耗全部 tokens
    for (int i = 0; i < 3; ++i) {
        bucket.allow(key, 10.0, 3);
    }
    EXPECT_FALSE(bucket.allow(key, 10.0, 3));

    // 等待 200ms（rate=10 RPS → 200ms 恢复约 2 个 token）
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 恢复后应允许
    EXPECT_TRUE(bucket.allow(key, 10.0, 3));
}

// ── 超限拒绝 ──
TEST(TokenBucket, ExceedCapacityRejects) {
    finguard::util::TokenBucket bucket;
    const std::string key = "test:reject";

    // 消耗全部 2 个 token
    EXPECT_TRUE(bucket.allow(key, 0.01, 2));
    EXPECT_TRUE(bucket.allow(key, 0.01, 2));
    // rate 极低（0.01 RPS），短时间内无法恢复
    EXPECT_FALSE(bucket.allow(key, 0.01, 2));
    EXPECT_FALSE(bucket.allow(key, 0.01, 2));
}

// ── 不同 key 互不影响 ──
TEST(TokenBucket, DifferentKeysIndependent) {
    finguard::util::TokenBucket bucket;

    // key A 消耗完
    EXPECT_TRUE(bucket.allow("a", 1.0, 1));
    EXPECT_FALSE(bucket.allow("a", 1.0, 1));

    // key B 仍有 token
    EXPECT_TRUE(bucket.allow("b", 1.0, 1));
}

// ── rate<=0 或 capacity<=0 时始终允许 ──
TEST(TokenBucket, ZeroRateOrCapacityAlwaysAllows) {
    finguard::util::TokenBucket bucket;
    EXPECT_TRUE(bucket.allow("test", 0.0, 10));
    EXPECT_TRUE(bucket.allow("test", -1.0, 10));
    EXPECT_TRUE(bucket.allow("test", 10.0, 0));
    EXPECT_TRUE(bucket.allow("test", 10.0, -1));
}
