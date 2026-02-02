#include "risk/profile_store.h"

#include <drogon/drogon.h>

#include <filesystem>
#include <fstream>
#include <mutex>

namespace finguard::risk {

namespace {

// Local profile store path: <cwd>/config/profiles.json
std::filesystem::path profile_path() {
    return std::filesystem::current_path() / "config" / "profiles.json";
}

// Load existing profiles (initialize empty structure if missing)
bool load_profiles(Json::Value *root, std::string *error) {
    if (!root) {
        if (error) {
            *error = "invalid_root";
        }
        return false;
    }

    root->clear();

    const auto path = profile_path();
    if (!std::filesystem::exists(path)) {
        (*root)["version"] = 1;
        (*root)["users"] = Json::objectValue;
        return true;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) {
            *error = "profiles_open_failed";
        }
        return false;
    }

    Json::CharReaderBuilder builder;
    JSONCPP_STRING errs;
    if (!Json::parseFromStream(builder, in, root, &errs)) {
        if (error) {
            *error = "profiles_parse_failed: " + errs;
        }
        return false;
    }

    if (!root->isObject()) {
        if (error) {
            *error = "profiles_root_not_object";
        }
        return false;
    }

    if (!root->isMember("version")) {
        (*root)["version"] = 1;
    }

    if (!root->isMember("users") || !(*root)["users"].isObject()) {
        (*root)["users"] = Json::objectValue;
    }

    return true;
}

// Persist profiles to file
bool save_profiles(const Json::Value &root, std::string *error) {
    const auto path = profile_path();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) {
            *error = "profiles_write_open_failed";
        }
        return false;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &out);
    out << "\n";
    return true;
}

std::mutex &profiles_mutex() {
    static std::mutex mutex;
    return mutex;
}

}

bool upsert_profile(const std::string &user_id, const Json::Value &questionnaire, std::string *error) {
    if (user_id.empty()) {
        if (error) {
            *error = "missing_user_id";
        }
        return false;
    }
    if (!questionnaire.isObject()) {
        if (error) {
            *error = "questionnaire_not_object";
        }
        return false;
    }

    std::lock_guard<std::mutex> guard(profiles_mutex());

    Json::Value root;
    if (!load_profiles(&root, error)) {
        return false;
    }

    root["users"][user_id]["questionnaire"] = questionnaire;

    if (!save_profiles(root, error)) {
        return false;
    }

    LOG_INFO << "Profile upserted: user_id=" << user_id;
    return true;
}

bool load_profile(const std::string &user_id, Json::Value *questionnaire, std::string *error) {
    if (user_id.empty()) {
        if (error) {
            *error = "missing_user_id";
        }
        return false;
    }
    if (!questionnaire) {
        if (error) {
            *error = "invalid_questionnaire_ptr";
        }
        return false;
    }

    std::lock_guard<std::mutex> guard(profiles_mutex());

    Json::Value root;
    if (!load_profiles(&root, error)) {
        return false;
    }

    const auto &users = root["users"];
    if (!users.isObject() || !users.isMember(user_id)) {
        if (error) {
            *error = "profile_not_found";
        }
        return false;
    }

    const auto &entry = users[user_id];
    if (!entry.isObject() || !entry.isMember("questionnaire")) {
        if (error) {
            *error = "profile_missing_questionnaire";
        }
        return false;
    }

    *questionnaire = entry["questionnaire"];
    return true;
}

}
