#pragma once

#include <json/json.h>

#include <string>

namespace finguard::risk {

// Save questionnaire into local profile store
bool upsert_profile(const std::string &user_id, const Json::Value &questionnaire, std::string *error);

// Load questionnaire for a user
bool load_profile(const std::string &user_id, Json::Value *questionnaire, std::string *error);

}
