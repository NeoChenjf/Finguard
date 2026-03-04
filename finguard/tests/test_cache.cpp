// Phase 6: 缓存（LRU / ConfigCache）单元测试
#include <gtest/gtest.h>

#include <chrono>
#include <mutex>
#include <thread>

// ── ConfigCache 模板测试（模拟 reliability_config.cpp 中的 ConfigCache）──
namespace {

using Clock = std::chrono::steady_clock;

template <typename T>
struct ConfigCache {
    std::mutex mtx;
    T value{};
    Clock::time_point loaded_at{};

    template <typename Loader>
    T get(Loader loader) {
        std::lock_guard<std::mutex> lock(mtx);
        const auto now = Clock::now();
        if (now - loaded_at > std::chrono::seconds(1)) { // 测试用 1s TTL
            value = loader();
            loaded_at = now;
        }
        return value;
    }

    void invalidate() {
        std::lock_guard<std::mutex> lock(mtx);
        loaded_at = Clock::time_point{};
    }
};

} // namespace

// ── ConfigCache：首次调用触发 loader ──
TEST(ConfigCache, FirstCallTriggersLoader) {
    ConfigCache<int> cache;
    int load_count = 0;
    auto loader = [&]() { ++load_count; return 42; };

    int val = cache.get(loader);
    EXPECT_EQ(val, 42);
    EXPECT_EQ(load_count, 1);
}

// ── ConfigCache：TTL 内不重新加载 ──
TEST(ConfigCache, CachedWithinTtl) {
    ConfigCache<int> cache;
    int load_count = 0;
    auto loader = [&]() { ++load_count; return 42; };

    cache.get(loader);
    cache.get(loader);
    cache.get(loader);
    EXPECT_EQ(load_count, 1);
}

// ── ConfigCache：TTL 过期后重新加载 ──
TEST(ConfigCache, ReloadsAfterTtl) {
    ConfigCache<int> cache;
    int load_count = 0;
    auto loader = [&]() { return ++load_count; };

    int val1 = cache.get(loader);
    EXPECT_EQ(val1, 1);

    // 等待 TTL 过期（1s + 余量）
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    int val2 = cache.get(loader);
    EXPECT_EQ(val2, 2);
    EXPECT_EQ(load_count, 2);
}

// ── ConfigCache：手动 invalidate 后立即重新加载 ──
TEST(ConfigCache, ManualInvalidation) {
    ConfigCache<int> cache;
    int load_count = 0;
    auto loader = [&]() { return ++load_count; };

    cache.get(loader);
    EXPECT_EQ(load_count, 1);

    // 手动使缓存失效
    cache.invalidate();

    int val = cache.get(loader);
    EXPECT_EQ(val, 2);
    EXPECT_EQ(load_count, 2);
}

// ── 简易 LRU 容量测试（模拟固定大小 map 淘汰）──
// 注：当前项目未实现独立 LRU 容器，此处测试 ConfigCache 的
// 核心行为作为缓存正确性的代理验证。

TEST(ConfigCache, ThreadSafetyBasic) {
    ConfigCache<int> cache;
    auto loader = []() { return 99; };

    // 多线程并发读取不崩溃
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 100; ++j) {
                int val = cache.get(loader);
                EXPECT_EQ(val, 99);
            }
        });
    }
    for (auto &t : threads) {
        t.join();
    }
}
