#include "valuation/valuation_config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace finguard::valuation {

namespace {

std::string trim_copy(std::string s) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string get_env_trimmed(const char *name) {
    if (name == nullptr) {
        return {};
    }
    const char *value = std::getenv(name);
    if (value == nullptr) {
        return {};
    }
    return trim_copy(std::string(value));
}

} // namespace

ValuationConfig load_valuation_config() {
    ValuationConfig cfg;
    const std::filesystem::path path =
        std::filesystem::current_path() / "config" / "valuation.json";

    std::ifstream f(path);
    if (!f.is_open()) {
        // 文件不存在时使用默认值（空 API key = 降级 / 禁用对应功能）
        return cfg;
    }

    try {
        nlohmann::json j;
        f >> j;
        cfg.alpha_vantage_api_key   = j.value("alpha_vantage_api_key", "");
        cfg.finnhub_api_key         = j.value("finnhub_api_key", "");
        cfg.yahoo_timeout_ms        = j.value("yahoo_timeout_ms", 5000);
        cfg.pe_history_quarters     = j.value("pe_history_quarters", 6);
        cfg.http_proxy              = j.value("http_proxy", "");
        cfg.us_stocks_data_source_priority =
            j.value("us_stocks_data_source_priority",
                    std::vector<std::string>{"yahoo_finance", "alpha_vantage", "finnhub", "simfin"});
        cfg.net_income_cache_ttl_hours = j.value("net_income_cache_ttl_hours", 24);
        cfg.valuecell_db_profile = trim_copy(j.value("valuecell_db_profile", "main"));

        if (cfg.us_stocks_data_source_priority.empty()) {
            cfg.us_stocks_data_source_priority = {"yahoo_finance", "alpha_vantage", "finnhub", "simfin"};
        }
        if (cfg.valuecell_db_profile.empty()) {
            cfg.valuecell_db_profile = "main";
        }
    } catch (...) {
        // JSON 解析失败：使用默认值，后续请求会缺少 API key 而优雅降级
    }

    const std::string env_proxy = get_env_trimmed("FINGUARD_VALUATION_HTTP_PROXY");
    if (!env_proxy.empty()) {
        cfg.http_proxy = env_proxy;
    } else if (cfg.http_proxy.empty()) {
        const std::string https_proxy = get_env_trimmed("HTTPS_PROXY");
        if (!https_proxy.empty()) {
            cfg.http_proxy = https_proxy;
        } else {
            const std::string http_proxy = get_env_trimmed("HTTP_PROXY");
            if (!http_proxy.empty()) {
                cfg.http_proxy = http_proxy;
            }
        }
    }

    return cfg;
}

} // namespace finguard::valuation
