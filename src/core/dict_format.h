// Copyright (c) 2026 Zhiyi IME Linux port contributors. GPL-3.0-only.
//
// On-disk format of the runtime dictionary. Fixed-width little-endian PODs only:
// the file is mmap'ed read-only and never byte-swapped, so the generator
// (scripts/build_dictionary.py) must run on a little-endian host.
#pragma once

#include <cstdint>

namespace zhiyi {

inline constexpr char kDictMagic[8] = {'Z', 'Y', 'D', 'I', 'C', 'T', '0', '1'};
inline constexpr std::uint32_t kDictVersion = 1;

struct DictHeader {
    char magic[8];
    std::uint32_t version;
    std::uint32_t entry_count;
    std::uint32_t index_bytes;
    std::uint32_t strings_bytes;
    std::uint32_t reserved[2];
};

// Sorted by (code ascending, frequency descending). Entries sharing a code
// also share the same code_off/code_len, so code strings are stored once.
struct DictEntry {
    std::uint32_t code_off;
    std::uint32_t text_off;
    std::int32_t freq;
    std::uint16_t code_len;
    std::uint16_t text_len;
};

static_assert(sizeof(DictHeader) == 32, "DictHeader layout changed");
static_assert(sizeof(DictEntry) == 16, "DictEntry layout changed");

}  // namespace zhiyi
