// Copyright (c) 2026 Zhiyi IME Linux port contributors. GPL-3.0-only.
#pragma once

#include <cstdint>
#include <string>

namespace zhiyi {

// Fuzzy pinyin groups, same bits as the upstream schema's fuzzy_groups.
enum FuzzyBit : std::uint32_t {
    kFuzzyZZh = 1u << 0,
    kFuzzyCCh = 1u << 1,
    kFuzzySSh = 1u << 2,
    kFuzzyNL = 1u << 3,
    kFuzzyAnAng = 1u << 4,
    kFuzzyEnEng = 1u << 5,
    kFuzzyInIng = 1u << 6,
};

struct Config {
    int page_size = 9;
    bool chinese_punctuation = true;
    bool full_shape = false;
    bool enable_initials = true;
    bool enable_sentence = true;
    bool default_english = false;
    std::uint32_t fuzzy = 0;
    // "", "xiaohe", "ziranma", "microsoft", "sogou", "abc"
    std::string shuangpin;
    // Candidate window layout: "vertical" (rows) or "horizontal" (one line)
    std::string layout = "vertical";

    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

}  // namespace zhiyi
