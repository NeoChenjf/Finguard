// Phase 6: 风控规则引擎单元测试
#include <gtest/gtest.h>

#include "risk/rule_engine.h"

#include <filesystem>
#include <fstream>

namespace {

// 辅助：确保 config/rules.yaml 存在（从项目 config 目录复制或创建最小版本）
void ensure_rules_yaml() {
    const auto config_dir = std::filesystem::current_path() / "config";
    std::filesystem::create_directories(config_dir);
    const auto path = config_dir / "rules.yaml";
    if (!std::filesystem::exists(path)) {
        // 写一个最小 rules.yaml 以确保测试可运行
        std::ofstream out(path);
        out << "version: 1\n"
            << "warnings:\n"
            << "  keyword_rules:\n"
            << "    forbidden_keywords:\n"
            << "      - \"博彩\"\n"
            << "      - \"高杠杆\"\n"
            << "      - \"ST\"\n";
    }
}

Json::Value make_questionnaire(const std::string &profile, int age, double stock_pct = -1.0) {
    Json::Value q;
    q["investor_profile"] = profile;
    q["age"] = age;
    if (stock_pct >= 0.0) {
        q["individual_stock_percent"] = stock_pct;
    }
    return q;
}

} // namespace

// ── 规则加载成功 ──
TEST(RulesEngine, LoadConfigSuccess) {
    ensure_rules_yaml();
    finguard::risk::RuleEngine engine;
    std::string error;
    bool ok = engine.load_config(&error);
    // 规则文件存在时应返回 true
    EXPECT_TRUE(ok) << "load_config failed: " << error;
}

// ── 禁用关键词触发告警 ──
TEST(RulesEngine, ForbiddenKeywordTriggersWarning) {
    ensure_rules_yaml();
    finguard::risk::RuleEngine engine;
    std::string error;
    engine.load_config(&error);

    auto questionnaire = make_questionnaire("experienced", 35);
    auto result = engine.check_request("我想投资博彩行业", questionnaire);

    // 应至少有一条告警包含触发词信息
    ASSERT_FALSE(result.warnings.empty());
    bool found_keyword = false;
    for (const auto &w : result.warnings) {
        if (w.find("博彩") != std::string::npos) {
            found_keyword = true;
            break;
        }
    }
    EXPECT_TRUE(found_keyword) << "Expected warning containing '博彩'";
}

// ── 无风险 prompt 不触发告警（仅默认的 profile 信息告警）──
TEST(RulesEngine, CleanPromptNoKeywordWarning) {
    ensure_rules_yaml();
    finguard::risk::RuleEngine engine;
    std::string error;
    engine.load_config(&error);

    auto questionnaire = make_questionnaire("experienced", 35);
    auto result = engine.check_request("请分析一下贵州茅台的基本面", questionnaire);

    // 不应包含禁止关键词相关的告警
    for (const auto &w : result.warnings) {
        EXPECT_EQ(w.find("博彩"), std::string::npos);
        EXPECT_EQ(w.find("高杠杆"), std::string::npos);
    }
}

// ── novice 不允许个股投资 ──
TEST(RulesEngine, NoviceDisallowsIndividualStock) {
    ensure_rules_yaml();
    finguard::risk::RuleEngine engine;
    std::string error;
    engine.load_config(&error);

    auto questionnaire = make_questionnaire("novice", 25, 0.3);
    auto result = engine.check_request("分析个股", questionnaire);

    bool found = false;
    for (const auto &w : result.warnings) {
        if (w.find("novice_disallows_individual_stock") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected profile_limit_violation for novice";
}

// ── experienced 超过 max_percent 触发告警 ──
TEST(RulesEngine, ExperiencedExceedsMaxPercent) {
    ensure_rules_yaml();
    finguard::risk::RuleEngine engine;
    std::string error;
    engine.load_config(&error);

    // experienced 的 max_percent 默认为 0.5，传入 0.6 应触发
    auto questionnaire = make_questionnaire("experienced", 35, 0.6);
    auto result = engine.check_request("分析个股", questionnaire);

    bool found = false;
    for (const auto &w : result.warnings) {
        if (w.find("individual_stock_percent_exceeds_limit") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected profile_limit_violation for exceeding max_percent";
}

// ── 全部通过场景：professional + 合理 prompt ──
TEST(RulesEngine, AllPassScenario) {
    ensure_rules_yaml();
    finguard::risk::RuleEngine engine;
    std::string error;
    engine.load_config(&error);

    auto questionnaire = make_questionnaire("professional", 40, 0.3);
    auto result = engine.check_request("请分析贵州茅台的价值投资逻辑", questionnaire);

    // professional 允许个股，prompt 无禁词，不应有严重告警
    for (const auto &w : result.warnings) {
        EXPECT_EQ(w.find("profile_limit_violation"), std::string::npos);
        EXPECT_EQ(w.find("博彩"), std::string::npos);
    }
}

// ── check_response：禁止词检测 ──
TEST(RulesEngine, ResponseForbiddenTermDetected) {
    ensure_rules_yaml();
    finguard::risk::RuleEngine engine;
    std::string error;
    engine.load_config(&error);

    auto warnings = engine.check_response("建议买入该股票，加仓到50%");

    // 应检测到"买入"和"加仓"
    bool found_buy = false;
    bool found_add = false;
    for (const auto &w : warnings) {
        if (w.isMember("trigger_terms")) {
            for (const auto &t : w["trigger_terms"]) {
                if (t.asString() == "买入") found_buy = true;
                if (t.asString() == "加仓") found_add = true;
            }
        }
    }
    EXPECT_TRUE(found_buy) << "Expected forbidden term '买入'";
    EXPECT_TRUE(found_add) << "Expected forbidden term '加仓'";
}
