#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace finguard::util {

struct YamlDoc {
    std::unordered_map<std::string, std::string> scalars;
    std::unordered_map<std::string, std::vector<std::string>> lists;
};

// Minimal YAML parser for key/value + nested maps + simple lists.
// Supports:
// - key: value
// - nested maps via indentation
// - list items "- value" under a key
// - inline list: [a, b, c]
bool parse_simple_yaml(const std::string &content, YamlDoc &out, std::string *error);

} // namespace finguard::util
