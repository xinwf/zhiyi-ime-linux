// Copyright (c) 2026 Zhiyi IME Linux port contributors. GPL-3.0-only.
#include "dictionary.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <queue>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace zhiyi {
namespace {

int compare_code(const char* a, std::size_t alen, const std::string& b) {
    const std::size_t n = std::min(alen, b.size());
    const int cmp = std::memcmp(a, b.data(), n);
    if (cmp != 0) return cmp;
    if (alen < b.size()) return -1;
    if (alen > b.size()) return 1;
    return 0;
}

struct ScoreGreater {
    bool operator()(const DictCandidate& a, const DictCandidate& b) const {
        return a.freq > b.freq;  // min-heap on frequency
    }
};

}  // namespace

Dictionary::~Dictionary() { close(); }

void Dictionary::close() {
    if (data_) {
        ::munmap(const_cast<char*>(data_), size_);
        data_ = nullptr;
    }
    entries_ = nullptr;
    strings_ = nullptr;
    count_ = 0;
    size_ = 0;
    strings_size_ = 0;
}

bool Dictionary::open(const std::string& path) {
    close();
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st {};
    if (::fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(sizeof(DictHeader))) {
        ::close(fd);
        return false;
    }
    const std::size_t file_size = static_cast<std::size_t>(st.st_size);
    void* mapped = ::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (mapped == MAP_FAILED) return false;
    ::madvise(mapped, file_size, MADV_WILLNEED);

    const auto* header = static_cast<const DictHeader*>(mapped);
    if (std::memcmp(header->magic, kDictMagic, sizeof(kDictMagic)) != 0 ||
        header->version != kDictVersion ||
        header->index_bytes != header->entry_count * sizeof(DictEntry) ||
        sizeof(DictHeader) + header->index_bytes + header->strings_bytes > file_size) {
        ::munmap(mapped, file_size);
        return false;
    }

    data_ = static_cast<const char*>(mapped);
    size_ = file_size;
    count_ = header->entry_count;
    strings_size_ = header->strings_bytes;
    entries_ = reinterpret_cast<const DictEntry*>(data_ + sizeof(DictHeader));
    strings_ = data_ + sizeof(DictHeader) + header->index_bytes;
    return true;
}

std::string Dictionary::code_at(const DictEntry& e) const {
    if (e.code_off + e.code_len > strings_size_) return {};
    return std::string(strings_ + e.code_off, e.code_len);
}

std::string Dictionary::text_at(const DictEntry& e) const {
    if (e.text_off + e.text_len > strings_size_) return {};
    return std::string(strings_ + e.text_off, e.text_len);
}

std::size_t Dictionary::lower_bound(const std::string& key) const {
    std::size_t lo = 0, hi = count_;
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        const DictEntry& e = entries_[mid];
        if (compare_code(strings_ + e.code_off, e.code_len, key) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

std::vector<DictCandidate> Dictionary::exact(const std::string& code,
                                             std::size_t limit) const {
    std::vector<DictCandidate> result;
    if (!is_open() || code.empty() || limit == 0) return result;
    std::size_t i = lower_bound(code);
    for (; i < count_ && result.size() < limit; ++i) {
        const DictEntry& e = entries_[i];
        if (code_at(e) != code) break;
        result.push_back(DictCandidate{text_at(e), code, e.freq});
    }
    return result;
}

bool Dictionary::has_code(const std::string& code) const {
    if (!is_open() || code.empty()) return false;
    const std::size_t i = lower_bound(code);
    return i < count_ && code_at(entries_[i]) == code;
}

std::vector<DictCandidate> Dictionary::prefix(const std::string& prefix,
                                              std::size_t limit) const {
    std::vector<DictCandidate> result;
    if (!is_open() || prefix.empty() || limit == 0) return result;
    const std::size_t start = lower_bound(prefix);
    std::priority_queue<DictCandidate, std::vector<DictCandidate>, ScoreGreater> heap;
    std::size_t scanned = 0;
    constexpr std::size_t kMaxScan = 40000;
    for (std::size_t i = start; i < count_ && scanned < kMaxScan; ++i, ++scanned) {
        const DictEntry& e = entries_[i];
        const std::string code = code_at(e);
        if (code.size() < prefix.size() ||
            code.compare(0, prefix.size(), prefix) != 0) {
            break;
        }
        if (heap.size() < limit) {
            heap.push(DictCandidate{text_at(e), code, e.freq});
        } else if (e.freq > heap.top().freq) {
            heap.pop();
            heap.push(DictCandidate{text_at(e), code, e.freq});
        }
    }
    result.reserve(heap.size());
    while (!heap.empty()) {
        result.push_back(heap.top());
        heap.pop();
    }
    std::sort(result.begin(), result.end(),
              [](const DictCandidate& a, const DictCandidate& b) {
                  if (a.freq != b.freq) return a.freq > b.freq;
                  return a.text < b.text;
              });
    return result;
}

bool Dictionary::longest_prefix_word(const std::string& input, std::size_t* consumed,
                                     DictCandidate* out) const {
    if (!is_open() || input.empty()) return false;
    // The index is sorted by code, so walking backwards from the position of
    // the longest possible prefix finds the longest matching code first.
    std::size_t best_len = 0;
    DictCandidate best;
    for (std::size_t len = std::min<std::size_t>(input.size(), 24); len > 0; --len) {
        const std::string key = input.substr(0, len);
        const auto hits = exact(key, 1);
        if (!hits.empty()) {
            best = hits.front();
            best_len = len;
            break;
        }
    }
    if (best_len == 0) return false;
    if (consumed) *consumed = best_len;
    if (out) *out = std::move(best);
    return true;
}

}  // namespace zhiyi
