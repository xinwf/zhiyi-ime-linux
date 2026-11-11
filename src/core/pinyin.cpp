// Copyright (c) 2026 Zhiyi IME Linux port contributors. GPL-3.0-only.
#include "pinyin.h"

#include "config.h"

#include <algorithm>
#include <fstream>
#include <json.hpp>

namespace zhiyi {

bool PinyinTable::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        syllables_.insert(line);
        max_len_ = std::max(max_len_, line.size());
    }
    return !syllables_.empty();
}

std::vector<std::string> PinyinTable::split(const std::string& input) const {
    const std::size_t n = input.size();
    std::vector<int> choice(n + 1, -1);
    std::vector<int> parts(n + 1, 0);
    if (n == 0) return {};
    choice[0] = 0;
    parts[0] = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (choice[i] < 0) continue;
        const std::size_t limit = std::min(max_len_, n - i);
        for (std::size_t len = limit; len > 0; --len) {
            const std::string piece = input.substr(i, len);
            if (syllables_.count(piece) == 0) continue;
            if (choice[i + len] < 0 || parts[i] + 1 < parts[i + len]) {
                choice[i + len] = static_cast<int>(len);
                parts[i + len] = parts[i] + 1;
            }
        }
    }
    std::vector<std::string> result;
    if (choice[n] < 0) {
        // Incomplete trailing syllable: keep the leading complete ones.
        std::size_t i = 0;
        while (i < n && choice[i] >= 0) {
            const int len = choice[i];
            if (len <= 0) break;
            result.push_back(input.substr(i, len));
            i += len;
        }
        if (i < n) result.push_back(input.substr(i));
        return result;
    }
    std::size_t i = n;
    while (i > 0) {
        const int len = choice[i];
        result.push_back(input.substr(i - len, len));
        i -= len;
    }
    std::reverse(result.begin(), result.end());
    return result;
}

std::string PinyinTable::initials(const std::string& input) const {
    std::string out;
    for (const auto& syllable : split(input)) {
        if (!syllable.empty()) out.push_back(syllable[0]);
    }
    return out;
}

std::vector<std::string> PinyinTable::fuzzy_variants(const std::string& input,
                                                     std::uint32_t mask) {
    std::vector<std::string> variants{input};
    if (mask == 0) return variants;
    constexpr std::size_t kMaxVariants = 12;
    auto expand = [&](const std::string& from, const std::string& to) {
        std::vector<std::string> extra;
        for (const auto& v : variants) {
            std::string replaced = v;
            std::size_t pos = 0;
            while ((pos = replaced.find(from, pos)) != std::string::npos) {
                std::string candidate = replaced;
                candidate.replace(pos, from.size(), to);
                if (std::find(variants.begin(), variants.end(), candidate) ==
                        variants.end() &&
                    std::find(extra.begin(), extra.end(), candidate) == extra.end()) {
                    extra.push_back(std::move(candidate));
                }
                pos += from.size();
            }
        }
        for (auto& e : extra) {
            if (variants.size() >= kMaxVariants) break;
            variants.push_back(std::move(e));
        }
    };
    if (mask & kFuzzyZZh) { expand("zh", "z"); expand("z", "zh"); }
    if (mask & kFuzzyCCh) { expand("ch", "c"); expand("c", "ch"); }
    if (mask & kFuzzySSh) { expand("sh", "s"); expand("s", "sh"); }
    if (mask & kFuzzyNL) { expand("n", "l"); expand("l", "n"); }
    if (mask & kFuzzyAnAng) { expand("ang", "an"); expand("an", "ang"); }
    if (mask & kFuzzyEnEng) { expand("eng", "en"); expand("en", "eng"); }
    if (mask & kFuzzyInIng) { expand("ing", "in"); expand("in", "ing"); }
    return variants;
}

bool ShuangpinTable::load(const std::string& schema_path) {
    std::ifstream in(schema_path);
    if (!in) return false;
    try {
        nlohmann::json j;
        in >> j;
        const auto& speller = j.at("speller");
        const auto& initials = speller.at("initials");
        for (auto it = initials.begin(); it != initials.end(); ++it) {
            const std::string key = it.value().get<std::string>();
            if (key.size() == 1) initial_by_key_[key[0]] = it.key();
        }
        const auto& finals = speller.at("finals");
        for (auto it = finals.begin(); it != finals.end(); ++it) {
            const std::string key = it.value().get<std::string>();
            if (key.size() == 1) {
                const auto inserted = final_by_key_.emplace(key[0], it.key());
                // Keep the longest final when a key maps to several.
                if (!inserted.second && it.key().size() > inserted.first->second.size()) {
                    inserted.first->second = it.key();
                }
            }
        }
        if (speller.contains("zero_initials")) {
            for (auto it = speller["zero_initials"].begin();
                 it != speller["zero_initials"].end(); ++it) {
                zero_initial_[it.key()] = it.value().get<std::string>();
            }
        }
    } catch (const std::exception&) {
        return false;
    }
    loaded_ = !initial_by_key_.empty() && !final_by_key_.empty();
    return loaded_;
}

std::string ShuangpinTable::convert(const std::string& keys) const {
    if (!loaded_) return keys;
    std::string out;
    std::size_t i = 0;
    for (; i + 1 < keys.size(); i += 2) {
        const char first = keys[i];
        const char second = keys[i + 1];
        auto initial = initial_by_key_.find(first);
        auto final = final_by_key_.find(second);
        const bool zero = initial == initial_by_key_.end();
        if (zero) {
            // Zero-initial syllables: the two keys encode the whole final.
            const std::string pair = keys.substr(i, 2);
            auto it = zero_initial_.find(pair);
            if (it != zero_initial_.end()) {
                out += it->second;
                continue;
            }
            if (final != final_by_key_.end()) {
                out += final->second;
                continue;
            }
            out += pair;
            continue;
        }
        if (final == final_by_key_.end()) {
            out += initial->second;
            continue;
        }
        out += initial->second + final->second;
    }
    if (i < keys.size()) out += keys.substr(i);
    return out;
}

}  // namespace zhiyi
