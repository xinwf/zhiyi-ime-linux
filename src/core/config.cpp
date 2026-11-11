// Copyright (c) 2026 Zhiyi IME Linux port contributors. GPL-3.0-only.
#include "config.h"

#include <fstream>
#include <json.hpp>

namespace zhiyi {
namespace {

template <typename T>
void get_if(const nlohmann::json& j, const char* key, T& out) {
    if (j.contains(key)) {
        try {
            out = j.at(key).get<T>();
        } catch (const std::exception&) {
            // Keep the default on malformed user config.
        }
    }
}

}  // namespace

bool Config::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    try {
        nlohmann::json j;
        in >> j;
        get_if(j, "page_size", page_size);
        get_if(j, "chinese_punctuation", chinese_punctuation);
        get_if(j, "full_shape", full_shape);
        get_if(j, "enable_initials", enable_initials);
        get_if(j, "enable_sentence", enable_sentence);
        get_if(j, "default_english", default_english);
        get_if(j, "fuzzy", fuzzy);
        get_if(j, "shuangpin", shuangpin);
    } catch (const std::exception&) {
        return false;
    }
    if (page_size < 1 || page_size > 10) page_size = 9;
    return true;
}

bool Config::save(const std::string& path) const {
    nlohmann::json j = {
        {"page_size", page_size},
        {"chinese_punctuation", chinese_punctuation},
        {"full_shape", full_shape},
        {"enable_initials", enable_initials},
        {"enable_sentence", enable_sentence},
        {"default_english", default_english},
        {"fuzzy", fuzzy},
        {"shuangpin", shuangpin},
    };
    std::ofstream out(path);
    if (!out) return false;
    out << j.dump(4) << "\n";
    return true;
}

}  // namespace zhiyi
