// Copyright (c) 2026 Zhiyi IME Linux port contributors. GPL-3.0-only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dict_format.h"

namespace zhiyi {

struct DictCandidate {
    std::string text;
    std::string code;
    std::int32_t freq = 0;
};

// Read-only mmap'ed view over a generated dictionary file.
class Dictionary {
public:
    Dictionary() = default;
    ~Dictionary();
    Dictionary(const Dictionary&) = delete;
    Dictionary& operator=(const Dictionary&) = delete;

    bool open(const std::string& path);
    void close();
    bool is_open() const { return data_ != nullptr; }
    std::size_t entry_count() const { return count_; }

    // All entries whose code == `code`, ordered by frequency descending.
    std::vector<DictCandidate> exact(const std::string& code, std::size_t limit) const;

    // Highest-frequency entries whose code starts with `prefix`. The range is
    // contiguous because the index is sorted by code; a bounded top-N heap
    // keeps this cheap even for short prefixes.
    std::vector<DictCandidate> prefix(const std::string& prefix, std::size_t limit) const;

    // Longest code in the dictionary that is a prefix of `input`, plus its
    // best candidate. Used by the greedy sentence composer.
    bool longest_prefix_word(const std::string& input, std::size_t* consumed,
                             DictCandidate* out) const;

    bool has_code(const std::string& code) const;

private:
    const char* data_ = nullptr;
    std::size_t size_ = 0;
    const DictEntry* entries_ = nullptr;
    const char* strings_ = nullptr;
    std::size_t count_ = 0;
    std::size_t strings_size_ = 0;

    std::string code_at(const DictEntry& e) const;
    std::string text_at(const DictEntry& e) const;
    // First entry with code >= key (byte-wise strcmp semantics).
    std::size_t lower_bound(const std::string& key) const;
};

}  // namespace zhiyi
