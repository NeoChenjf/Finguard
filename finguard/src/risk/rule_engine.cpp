#include "risk/rule_engine.h"

#include <drogon/drogon.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
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

std::string trim_copy_local(const std::string &value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

struct SectionRange {
    std::string title;
    std::size_t start = 0;
    std::size_t end = 0;
};

std::vector<SectionRange> collect_sections(const std::string &text,
                                           const std::vector<std::string> &titles) {
    struct Hit {
        std::string title;
        std::size_t pos = std::string::npos;
        std::size_t header_len = 0;
    };

    std::vector<Hit> hits;
    hits.reserve(titles.size());
    for (const auto &title : titles) {
        const std::string bracketed = "【" + title + "】";
        std::size_t pos = text.find(bracketed);
        std::size_t header_len = bracketed.size();
        if (pos == std::string::npos) {
            pos = text.find(title);
            header_len = title.size();
        }
        if (pos != std::string::npos) {
            hits.push_back({title, pos, header_len});
        }
    }

    std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.pos < b.pos; });
    std::vector<SectionRange> ranges;
    ranges.reserve(hits.size());
    for (std::size_t i = 0; i < hits.size(); ++i) {
        const auto &hit = hits[i];
        const std::size_t content_start = hit.pos + hit.header_len;
        const std::size_t content_end = (i + 1 < hits.size()) ? hits[i + 1].pos : text.size();
        ranges.push_back({hit.title, content_start, content_end});
    }
    return ranges;
}

std::string get_section_content(const std::string &text,
                                const std::vector<SectionRange> &ranges,
                                const std::string &title) {
    for (const auto &range : ranges) {
        if (range.title == title && range.start < range.end && range.end <= text.size()) {
            return trim_copy_local(text.substr(range.start, range.end - range.start));
        }
    }
    return "";
}

std::string section_for_position(const std::vector<SectionRange> &ranges, std::size_t pos) {
    for (const auto &range : ranges) {
        if (pos >= range.start && pos < range.end) {
            return range.title;
        }
    }
    return "";
}

bool extract_number_after(const std::string &text,
                          const std::string &label,
                          double &value_out,
                          std::string &match_out) {
    const auto pos = text.find(label);
    if (pos == std::string::npos) {
        return false;
    }
    const auto window_end = (std::min)(text.size(), pos + label.size() + 40);
    const std::string window = text.substr(pos, window_end - pos);
    static const std::regex number_re("([-+]?[0-9]*\\.?[0-9]+)\\s*(%?)");
    std::smatch match;
    if (std::regex_search(window, match, number_re) && match.size() >= 2) {
        try {
            value_out = std::stod(match.str(1));
            match_out = match.str(0);
            return true;
        } catch (const std::exception &) {
            return false;
        }
    }
    return false;
}

bool contains_any(const std::string &text, const std::vector<std::string> &terms) {
    for (const auto &term : terms) {
        if (!term.empty() && text.find(term) != std::string::npos) {
            return true;
        }
    }
    return false;
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
    config_.response_forbidden_terms = {
        "买入",
        "卖出",
        "加仓",
        "减仓",
        "仓位",
        "资产配置",
        "配置比例",
        "收益承诺",
        "保证收益",
        "稳赚",
        "保本",
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
        if (path == "stock_selection.peg_max") {
            parse_double(value, &config_.peg_max);
            continue;
        }
        if (path == "stock_selection.debt_ratio_max") {
            parse_double(value, &config_.debt_ratio_max);
            continue;
        }
        if (path == "stock_selection.roe_min") {
            parse_double(value, &config_.roe_min);
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

std::vector<Json::Value> RuleEngine::check_response(const std::string &response_text) const {
    std::vector<Json::Value> warnings;

    const std::vector<std::string> sections = {
        "公司信息",
        "财务状况（定量指标，含好价格判断）",
        "商业模式与护城河",
        "管理层结构与风格",
        "优点与风险点",
        "预期收益（模型估算，必须注明不确定性）",
        "结论（分析性总结）",
        "合规与数据声明",
    };

    const auto ranges = collect_sections(response_text, sections);

    auto make_warning = [](const std::string &code,
                           const std::string &section,
                           const std::string &reason,
                           const std::string &basis,
                           const std::vector<std::string> &trigger_terms,
                           const std::string &evidence,
                           const std::string &severity) {
        Json::Value warning;
        warning["code"] = code;
        warning["section"] = section;
        warning["reason"] = reason;
        warning["basis"] = basis;
        warning["trigger_terms"] = Json::arrayValue;
        for (const auto &term : trigger_terms) {
            warning["trigger_terms"].append(term);
        }
        warning["evidence"] = evidence;
        warning["severity"] = severity;
        return warning;
    };

    auto record_warning = [&](const Json::Value &warning) {
        warnings.push_back(warning);
        if (warning.isMember("code")) {
            LOG_INFO << "[RISK_WARNING] code=" << warning["code"].asString();
        }
    };

    for (const auto &section : sections) {
        bool found = false;
        for (const auto &range : ranges) {
            if (range.title == section) {
                found = true;
                break;
            }
        }
        if (!found) {
            record_warning(make_warning(
                "STRUCTURE.MISSING_SECTION",
                section,
                "结构缺失（001）缺少章节：" + section,
                "阈值=必须包含章节；来源=模板要求",
                {},
                "未提供",
                "warn"));
        }
    }

    const std::string company_info = get_section_content(response_text, ranges, "公司信息");
    if (!company_info.empty()) {
        struct FieldSpec {
            std::string label;
            std::vector<std::string> keywords;
        };
        const std::vector<FieldSpec> fields = {
            {"公司名称", {"公司名称"}},
            {"交易所或代码", {"交易所", "代码", "Ticker"}},
            {"行业或赛道", {"行业", "赛道"}},
            {"核心产品或服务", {"核心产品", "核心业务", "产品", "服务"}},
        };
        for (const auto &field : fields) {
            if (!contains_any(company_info, field.keywords)) {
                record_warning(make_warning(
                    "CONTENT.MISSING_COMPANY_FIELD",
                    "公司信息",
                    "内容缺失（003）公司信息缺少字段：" + field.label,
                    "阈值=必须包含公司信息字段；来源=模板要求",
                    {},
                    "未提供",
                    "warn"));
            }
        }
    }

    const std::string financial = get_section_content(response_text, ranges, "财务状况（定量指标，含好价格判断）");
    if (!financial.empty()) {
        const std::vector<std::pair<std::string, std::vector<std::string>>> metrics = {
            {"ROE", {"ROE"}},
            {"负债率", {"负债率"}},
            {"PEG", {"PEG"}},
            {"现金流", {"现金流", "自由现金流"}},
            {"5 年净利润 CAGR", {"CAGR", "净利润 CAGR", "净利润CAGR"}},
        };
        for (const auto &metric : metrics) {
            if (!contains_any(financial, metric.second)) {
                record_warning(make_warning(
                    "CONTENT.MISSING_METRIC",
                    "财务状况（定量指标，含好价格判断）",
                    "内容缺失（001）缺少关键财务指标：" + metric.first,
                    "阈值=必须包含指标；来源=模板要求",
                    {},
                    "未提供",
                    "warn"));
            }
        }
    }

    if (!financial.empty()) {
        double value = 0.0;
        std::string raw_match;
        if (extract_number_after(financial, "PEG", value, raw_match)) {
            if (value > config_.peg_max) {
                record_warning(make_warning(
                    "NUMERIC.THRESHOLD_BREACH",
                    "财务状况（定量指标，含好价格判断）",
                    "数值超标（001）指标超出阈值：PEG",
                    "阈值=PEG<=2；来源=守拙价值多元化基金理念",
                    {"PEG " + raw_match},
                    raw_match,
                    "warn"));
            }
        }

        if (extract_number_after(financial, "负债率", value, raw_match)) {
            double ratio = value;
            if (raw_match.find('%') != std::string::npos || ratio > 1.0) {
                ratio = ratio / 100.0;
            }
            if (ratio > config_.debt_ratio_max) {
                record_warning(make_warning(
                    "NUMERIC.THRESHOLD_BREACH",
                    "财务状况（定量指标，含好价格判断）",
                    "数值超标（001）指标超出阈值：负债率",
                    "阈值=负债率<=50%；来源=守拙价值多元化基金理念",
                    {"负债率 " + raw_match},
                    raw_match,
                    "warn"));
            }
        }

        if (extract_number_after(financial, "ROE", value, raw_match)) {
            double ratio = value;
            if (raw_match.find('%') != std::string::npos || ratio > 1.0) {
                ratio = ratio / 100.0;
            }
            if (ratio <= config_.roe_min) {
                record_warning(make_warning(
                    "NUMERIC.THRESHOLD_BREACH",
                    "财务状况（定量指标，含好价格判断）",
                    "数值超标（001）指标超出阈值：ROE",
                    "阈值=ROE>20%；来源=守拙价值多元化基金理念",
                    {"ROE " + raw_match},
                    raw_match,
                    "warn"));
            }
        }

        const std::vector<std::string> semantic_terms = {"偏高", "偏低", "过高", "过低"};
        if (contains_any(financial, semantic_terms)) {
            bool has_basis = false;
            static const std::regex numeric_re("[0-9]+(\\.[0-9]+)?%?");
            if (std::regex_search(financial, numeric_re)) {
                has_basis = true;
            }
            if (financial.find("阈值") != std::string::npos || financial.find("依据") != std::string::npos) {
                has_basis = true;
            }
            if (!has_basis) {
                record_warning(make_warning(
                    "SEMANTIC.NO_BASIS",
                    "财务状况（定量指标，含好价格判断）",
                    "语义无依据（001）语义判断缺少阈值与来源",
                    "阈值=需给出阈值与来源；来源=业务规则",
                    {},
                    "未提供",
                    "warn"));
            }
        }
    }

    const std::string conclusion = get_section_content(response_text, ranges, "结论（分析性总结）");
    if (!conclusion.empty()) {
        int points = 0;
        std::istringstream iss(conclusion);
        std::string line;
        while (std::getline(iss, line)) {
            const auto trimmed = trim_copy_local(line);
            if (trimmed.rfind("-", 0) == 0 || trimmed.rfind("•", 0) == 0 || trimmed.rfind("1.", 0) == 0) {
                points++;
            }
        }
        if (points < 1 || points > 3) {
            record_warning(make_warning(
                "CONTENT.MISSING_CONCLUSION_POINTS",
                "结论（分析性总结）",
                "内容缺失（005）结论要点数量不符合 1-3 条",
                "阈值=1-3 条要点；来源=模板要求",
                {},
                "未提供",
                "warn"));
        }
    }

    const std::string expected = get_section_content(response_text, ranges, "预期收益（模型估算，必须注明不确定性）");
    if (!expected.empty()) {
        static const std::regex number_re("[0-9]+(\\.[0-9]+)?");
        if (!std::regex_search(expected, number_re)) {
            record_warning(make_warning(
                "CONTENT.MISSING_RETURN_INPUT",
                "预期收益（模型估算，必须注明不确定性）",
                "内容缺失（004）预期收益缺少数值代入",
                "阈值=必须包含数值代入；来源=模板要求",
                {},
                "未提供",
                "warn"));
        }
    }

    const std::string compliance = get_section_content(response_text, ranges, "合规与数据声明");
    if (!compliance.empty()) {
        if (compliance.find("来源") == std::string::npos && compliance.find("source") == std::string::npos) {
            record_warning(make_warning(
                "CONTENT.MISSING_SOURCE",
                "合规与数据声明",
                "内容缺失（002）缺少引用来源",
                "阈值=必须提供引用来源；来源=业务规则",
                {},
                "未提供",
                "warn"));
        }
    }

    for (const auto &term : config_.response_forbidden_terms) {
        const auto pos = response_text.find(term);
        if (pos != std::string::npos) {
            const auto section = section_for_position(ranges, pos);
            const auto section_name = section.empty() ? "合规与数据声明" : section;
            record_warning(make_warning(
                "COMPLIANCE.PROHIBITED",
                section_name,
                "合规禁止（001）出现禁止内容：" + term,
                "阈值=禁止项；来源=业务规则",
                {term},
                term,
                "error"));
        }
    }

    return warnings;
}

} // namespace finguard::risk
