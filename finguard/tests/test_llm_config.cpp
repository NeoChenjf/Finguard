// Phase 6: LLM 配置解析单元测试
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <cstdlib>

#include "llm/llm_client.h"

namespace {

// 临时配置目录路径（测试前设置工作目录）
const auto kConfigDir = std::filesystem::current_path() / "config";

// 辅助：写入临时 llm.json
void write_llm_json(const std::string &content) {
    std::filesystem::create_directories(kConfigDir);
    std::ofstream out(kConfigDir / "llm.json");
    out << content;
}

// 辅助：删除临时 llm.json
void remove_llm_json() {
    const auto p = kConfigDir / "llm.json";
    if (std::filesystem::exists(p)) {
        std::filesystem::remove(p);
    }
}

} // namespace

// ── 正常 JSON 解析 ──
TEST(LlmConfig, ParseValidJson) {
    write_llm_json(R"({
        "api_base": "https://example.com/v1",
        "api_key": "sk-test1234",
        "model": "gpt-4o",
        "temperature": 0.5,
        "timeout_ms": 20000
    })");

    finguard::llm::LlmClient client;
    auto cfg = client.load_config();

    EXPECT_EQ(cfg.api_base, "https://example.com/v1");
    EXPECT_EQ(cfg.api_key, "sk-test1234");
    EXPECT_EQ(cfg.model, "gpt-4o");
    EXPECT_DOUBLE_EQ(cfg.temperature, 0.5);
    EXPECT_EQ(cfg.timeout_ms, 20000);
}

// ── 缺失字段使用默认值 ──
TEST(LlmConfig, MissingFieldsUseDefaults) {
    write_llm_json(R"({
        "api_key": "sk-abc"
    })");

    finguard::llm::LlmClient client;
    auto cfg = client.load_config();

    // api_key 应被正确读取
    EXPECT_EQ(cfg.api_key, "sk-abc");
    // model 应回退默认
    EXPECT_EQ(cfg.model, "qwen-plus");
    // temperature 应回退默认
    EXPECT_DOUBLE_EQ(cfg.temperature, 0.7);
    // timeout_ms 应回退默认
    EXPECT_EQ(cfg.timeout_ms, 30000);
}

// ── 文件不存在时使用默认值 ──
TEST(LlmConfig, FileNotFoundUsesDefaults) {
    remove_llm_json();

    finguard::llm::LlmClient client;
    auto cfg = client.load_config();

    // api_key 应为空（默认配置无 key）
    EXPECT_TRUE(cfg.api_key.empty());
    // model 应为默认值
    EXPECT_EQ(cfg.model, "qwen-plus");
}

// ── 无效 JSON 不崩溃，回退默认 ──
TEST(LlmConfig, InvalidJsonFallsBackToDefaults) {
    write_llm_json("this is not json{{{");

    finguard::llm::LlmClient client;
    auto cfg = client.load_config();

    // 应该正常返回默认值而不崩溃
    EXPECT_EQ(cfg.model, "qwen-plus");
    EXPECT_TRUE(cfg.api_key.empty());
}

// ── 空 JSON 对象 ──
TEST(LlmConfig, EmptyJsonObject) {
    write_llm_json("{}");

    finguard::llm::LlmClient client;
    auto cfg = client.load_config();

    EXPECT_EQ(cfg.model, "qwen-plus");
    EXPECT_TRUE(cfg.api_key.empty());
}
