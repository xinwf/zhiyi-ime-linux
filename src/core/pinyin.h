// Copyright (c) 2026 Zhiyi IME Linux port contributors. GPL-3.0-only.
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace zhiyi {

// Legal syllables come from the upstream `spellings` table (exported by
// scripts/build_dictionary.py into zhiyi.syllables).
class PinyinTable {
public:
    bool load(const std::string& path);
    bool empty() const { return syllables_.empty(); }

    // Dynamic-programming segmentation, longest syllable first.
    std::vector<std::string> split(const std::string& input) const;
    // First letters of the segmented syllables ("nihao" -> "nh").
    std::string initials(const std::string& input) const;

    // Additional spellings produced by the enabled fuzzy groups. The original
    // spelling is always the first element. Capped so lookups stay bounded.
    static std::vector<std::string> fuzzy_variants(const std::string& input,
                                                   std::uint32_t mask);

private:
    std::set<std::string> syllables_;
    std::size_t max_len_ = 6;
};

// Shuangpin (double pinyin) uses the upstream schema JSON directly.
class ShuangpinTable {
public:
    bool load(const std::string& schema_path);
    bool valid() const { return loaded_; }
    // Two keys per syllable; an odd trailing key is returned as-is so the UI
    // can still show what the user typed.
    std::string convert(const std::string& keys) const;

private:
    bool loaded_ = false;
    std::unordered_map<char, std::string> initial_by_key_;
    std::unordered_map<char, std::string> final_by_key_;
    std::unordered_map<std::string, std::string> zero_initial_;
};

}  // namespace zhiyi
