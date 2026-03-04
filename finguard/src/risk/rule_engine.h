#pragma once

#include <json/json.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace finguard::risk {

struct RuleResult {
    std::vector<std::string> warnings;
};

class RuleEngine {
public:
    // Load rules from config/rules.yaml (best-effort; falls back to defaults).
    bool load_config(std::string *error);

    // Evaluate request-side rules (keywords, profile limits, defaults).
    RuleResult check_request(const std::string &prompt, const Json::Value &questionnaire) const;

    // Evaluate response-side rules (structure, metrics, compliance).
    std::vector<Json::Value> check_response(const std::string &response_text) const;

private:
    struct ProfileLimit {
        bool allowed = false;
        double max_percent = 0.0;
        bool custom_allocation_allowed = false;
    };

    struct RuleConfig {
        double gold_percent = 0.10;
        int default_age = 35;
        double min_single_asset_percent = 0.025;
        double peg_max = 2.0;
        double debt_ratio_max = 0.50;
        double roe_min = 0.20;
        std::string message_template;
        std::vector<std::string> forbidden_keywords;
        std::vector<std::string> response_forbidden_terms;
        std::unordered_map<std::string, ProfileLimit> profile_limits;
    };

    RuleConfig config_;

    void load_defaults();
    bool parse_yaml(const std::string &content, std::string *error);
    std::string format_warning_terms(const std::vector<std::string> &terms) const;
    static std::string trim_copy(const std::string &value);
    static std::string unquote_copy(const std::string &value);
};

}
