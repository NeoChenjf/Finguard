#include "util/simple_yaml.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace finguard::util {

namespace {

std::string trim_copy(const std::string &value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string unquote_copy(const std::string &value) {
    if (value.size() >= 2) {
        if ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

std::vector<std::string> split_inline_list(const std::string &value) {
    std::vector<std::string> out;
    std::string inner = value;
    if (!inner.empty() && inner.front() == '[') {
        inner.erase(inner.begin());
    }
    if (!inner.empty() && inner.back() == ']') {
        inner.pop_back();
    }
    std::stringstream ss(inner);
    std::string item;
    while (std::getline(ss, item, ',')) {
        auto trimmed = trim_copy(item);
        if (!trimmed.empty()) {
            out.push_back(unquote_copy(trimmed));
        }
    }
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

} // namespace

bool parse_simple_yaml(const std::string &content, YamlDoc &out, std::string *error) {
    struct StackEntry {
        int indent = 0;
        std::string key;
    };

    out.scalars.clear();
    out.lists.clear();

    std::vector<StackEntry> stack;
    std::string current_list_path;
    int current_list_indent = -1;

    std::istringstream input(content);
    std::string raw;
    while (std::getline(input, raw)) {
        std::string line = raw;
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
                if (!item.empty()) {
                    out.lists[current_list_path].push_back(item);
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
            continue;
        }

        value = trim_copy(unquote_copy(value));
        std::vector<std::string> keys;
        for (const auto &entry : stack) {
            keys.push_back(entry.key);
        }
        const auto path = join_path(keys, key);

        if (!value.empty() && value.front() == '[' && value.back() == ']') {
            out.lists[path] = split_inline_list(value);
        } else {
            out.scalars[path] = value;
        }
    }

    if (error) {
        *error = "";
    }
    return true;
}

} // namespace finguard::util
