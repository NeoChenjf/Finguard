#include "risk/rule_engine.h"

#include <drogon/drogon.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace finguard::risk {

namespace {

std::string to_lower_copy(const std::string &value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string join_path(const std::vector<std::string> &stack, const std::string &leaf) {
    std::string path;
    for (const auto &part : stack) {
        if (!path.empty()) {
            path += ".";
        }
        path += part;
    }
    if (!leaf.empty()) {
        if (!path.empty()) {
            path += ".";
        }
        path += leaf;
    }
    return path;
}

bool parse_bool(const std::string &value, bool *out) {
    if (!out) {
        return false;
    }
    const auto lower = to_lower_copy(value);
    if (lower == "true") {
        *out = true;
        return true;
    }
    if (lower == "false") {
        *out = false;
        return true;
    }
    return false;
}

bool parse_double(const std::string &value, double *out) {
    if (!out) {
        return false;
    }
    try {
        *out = std::stod(value);
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool parse_int(const std::string &value, int *out) {
    if (!out) {
        return false;
    }
    try {
        *out = std::stoi(value);
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

} // namespace

void RuleEngine::load_defaults() {
    config_ = RuleConfig{};
    config_.message_template = "风险提示：检测到高风险或复杂交易相关内容{terms}。AI 只能提供参考，无法替你决定，请为自己的资产深思熟虑并理性评估风险承受能力。";
    config_.forbidden_keywords = {
        "博彩",
        "高杠杆",
        "ST股票",
        "ST",
        "期货",
        "卖空",
        "做空",
        "期权",
        "融资融券",
        "合约",
    };
    config_.profile_limits = {
        {"novice", {false, 0.0, false}},
        {"experienced", {true, 0.5, true}},
        {"professional", {true, 0.0, true}},
    };
}

std::string RuleEngine::trim_copy(const std::string &value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string RuleEngine::unquote_copy(const std::string &value) {
    if (value.size() >= 2) {
        if ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

bool RuleEngine::parse_yaml(const std::string &content, std::string *error) {
    struct StackEntry {
        int indent = 0;
        std::string key;
    };

    std::vector<StackEntry> stack;
    std::string current_list_path;
    int current_list_indent = -1;
    bool forbidden_list_started = false;

    std::istringstream input(content);
    std::string raw;
    while (std::getline(input, raw)) {
        std::string line = raw;
        // remove comments
        const auto hash_pos = line.find('#');
        if (hash_pos != std::string::npos) {
            line = line.substr(0, hash_pos);
        }
        if (trim_copy(line).empty()) {
            continue;
        }

        int indent = 0;
        while (indent < static_cast<int>(line.size()) && line[indent] == ' ') {
            indent++;
        }
        std::string trimmed = trim_copy(line);

        if (trimmed.rfind("- ", 0) == 0) {
            if (!current_list_path.empty() && indent > current_list_indent) {
                const auto item_raw = trim_copy(trimmed.substr(2));
                const auto item = unquote_copy(item_raw);
                if (current_list_path == "warnings.keyword_rules.forbidden_keywords") {
                    if (!item.empty()) {
                        config_.forbidden_keywords.push_back(item);
                    }
                }
            }
            continue;
        }

        const auto colon_pos = trimmed.find(':');
        if (colon_pos == std::string::npos) {
            continue;
        }
        const auto key = trim_copy(trimmed.substr(0, colon_pos));
        auto value = trim_copy(trimmed.substr(colon_pos + 1));

        while (!stack.empty() && indent <= stack.back().indent) {
            stack.pop_back();
        }

        if (value.empty()) {
            stack.push_back({indent, key});
            current_list_path = join_path([&] {
                std::vector<std::string> keys;
                for (const auto &entry : stack) {
                    keys.push_back(entry.key);
                }
                return keys;
            }(), "");
            current_list_indent = indent;
            if (current_list_path != "warnings.keyword_rules.forbidden_keywords") {
                current_list_path.clear();
                current_list_indent = -1;
                forbidden_list_started = false;
            } else {
                if (!forbidden_list_started) {
                    config_.forbidden_keywords.clear();
                    forbidden_list_started = true;
                }
            }
            continue;
        }

        value = unquote_copy(value);
        std::vector<std::string> keys;
        for (const auto &entry : stack) {
            keys.push_back(entry.key);
        }
        const auto path = join_path(keys, key);

        if (path == "allocation.gold_percent") {
            parse_double(value, &config_.gold_percent);
            continue;
        }
        if (path == "allocation.bonds.default_age") {
            parse_int(value, &config_.default_age);
            continue;
        }
        if (path == "risk_limits.min_single_asset_percent") {
            parse_double(value, &config_.min_single_asset_percent);
            continue;
        }
        if (path == "warnings.message_template") {
            config_.message_template = value;
            continue;
        }

        // profile limits: allocation.equities.individual_stock.profile_limits.<profile>.<field>
        const std::string profile_prefix = "allocation.equities.individual_stock.profile_limits.";
        if (path.rfind(profile_prefix, 0) == 0) {
            const auto rest = path.substr(profile_prefix.size());
            const auto dot_pos = rest.find('.');
            if (dot_pos != std::string::npos) {
                const auto profile = rest.substr(0, dot_pos);
                const auto field = rest.substr(dot_pos + 1);
                auto &limit = config_.profile_limits[profile];
                if (field == "allowed") {
                    parse_bool(value, &limit.allowed);
                } else if (field == "max_percent") {
                    parse_double(value, &limit.max_percent);
                } else if (field == "custom_allocation_allowed") {
                    parse_bool(value, &limit.custom_allocation_allowed);
                }
            }
            continue;
        }
    }

    return true;
}

bool RuleEngine::load_config(std::string *error) {
    load_defaults();

    const auto path = std::filesystem::current_path() / "config" / "rules.yaml";
    if (!std::filesystem::exists(path)) {
        if (error) {
            *error = "rules_not_found";
        }
        return false;
    }

    std::ifstream in(path);
    if (!in) {
        if (error) {
            *error = "rules_open_failed";
        }
        return false;
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    if (!parse_yaml(buffer.str(), error)) {
        if (error && error->empty()) {
            *error = "rules_parse_failed";
        }
        return false;
    }

    return true;
}

std::string RuleEngine::format_warning_terms(const std::vector<std::string> &terms) const {
    if (terms.empty()) {
        return "";
    }
    std::string joined;
    for (const auto &term : terms) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += term;
    }
    return "（触发词：" + joined + "）";
}

RuleResult RuleEngine::check_request(const std::string &prompt, const Json::Value &questionnaire) const {
    RuleResult result;

    std::string investor_profile = "novice";
    int age = config_.default_age;
    double individual_stock_percent = -1.0;

    if (questionnaire.isObject()) {
        if (questionnaire.isMember("investor_profile") && questionnaire["investor_profile"].isString()) {
            investor_profile = questionnaire["investor_profile"].asString();
        } else {
            result.warnings.push_back("missing_investor_profile_defaulted");
        }
        if (questionnaire.isMember("age") && questionnaire["age"].isInt()) {
            age = questionnaire["age"].asInt();
        } else {
            result.warnings.push_back("missing_age_defaulted");
        }
        if (questionnaire.isMember("individual_stock_percent") && questionnaire["individual_stock_percent"].isNumeric()) {
            individual_stock_percent = questionnaire["individual_stock_percent"].asDouble();
        }
    } else {
        result.warnings.push_back("missing_profile_defaulted");
    }

    if (age <= 0) {
        age = config_.default_age;
    }

    std::vector<std::string> hits;
    for (const auto &keyword : config_.forbidden_keywords) {
        if (!keyword.empty() && prompt.find(keyword) != std::string::npos) {
            hits.push_back(keyword);
        }
    }
    if (!hits.empty()) {
        std::string warning = config_.message_template;
        const auto term_text = format_warning_terms(hits);
        const auto pos = warning.find("{terms}");
        if (pos != std::string::npos) {
            warning.replace(pos, 7, term_text);
        } else if (!term_text.empty()) {
            warning += term_text;
        }
        result.warnings.push_back(warning);
    }

    const auto lower_profile = to_lower_copy(investor_profile);
    const auto it = config_.profile_limits.find(lower_profile);
    if (it != config_.profile_limits.end()) {
        const auto &limit = it->second;
        if (!limit.allowed && individual_stock_percent > 0.0) {
            result.warnings.push_back("profile_limit_violation: novice_disallows_individual_stock");
        } else if (limit.allowed && limit.max_percent > 0.0 && individual_stock_percent > limit.max_percent) {
            result.warnings.push_back("profile_limit_violation: individual_stock_percent_exceeds_limit");
        }
    }

    if (individual_stock_percent > 0.0 && individual_stock_percent < config_.min_single_asset_percent) {
        result.warnings.push_back("allocation_warning: individual_stock_percent_below_min_single_asset_percent");
    }

    return result;
}

} // namespace finguard::risk
