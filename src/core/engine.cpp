// Copyright (c) 2026 Zhiyi IME Linux port contributors. GPL-3.0-only.
#include "engine.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unordered_set>

namespace zhiyi {
namespace {

std::string join_path(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    return dir.back() == '/' ? dir + name : dir + "/" + name;
}

bool ensure_dir(const std::string& path) {
    if (path.empty()) return false;
    std::string partial;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            if (!partial.empty() && ::mkdir(partial.c_str(), 0700) != 0) {
                struct stat st {};
                if (::stat(partial.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
                    return false;
                }
            }
        }
        if (i < path.size()) partial.push_back(path[i]);
    }
    return true;
}

std::string user_data_dir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg) return std::string(xdg) + "/zhiyi";
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.local/share/zhiyi";
}

std::string config_file_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::string dir = (xdg && *xdg) ? std::string(xdg)
                                    : std::string(std::getenv("HOME")
                                                      ? std::getenv("HOME")
                                                      : ".") + "/.config";
    return dir + "/zhiyi/config.json";
}

std::string dirname_of(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string{} : path.substr(0, slash);
}

std::string utf8_to_fullwidth(char ch) {
    // Full-width ASCII starts at U+FF01 for '!' (0x21).
    if (ch < 0x21 || ch > 0x7e) return std::string(1, ch);
    const unsigned cp = ch == ' ' ? 0x3000u : 0xff00u + (static_cast<unsigned>(ch) - 0x20u);
    std::string out;
    out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    return out;
}

}  // namespace

void Engine::load_config() {
    config_.load(config_file_path());
    english_ = config_.default_english;
    if (!config_.shuangpin.empty()) {
        shuangpin_.load(join_path(data_dir_, "schemas/pinyin." +
                                  config_.shuangpin + ".schema.json"));
    }
}

void Engine::load_user_dict() {
    user_freq_.clear();
    std::ifstream in(join_path(user_dir_, "user.tsv"));
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        try {
            user_freq_[line.substr(0, tab)] = std::stoi(line.substr(tab + 1));
        } catch (const std::exception&) {
            // Ignore a malformed line; the rest of the dictionary is usable.
        }
    }
}

bool Engine::initialize(const std::string& data_dir, const std::string& user_dir) {
    data_dir_ = data_dir;
    user_dir_ = user_dir.empty() ? user_data_dir() : user_dir;
    ensure_dir(user_dir_);
    load_config();
    load_user_dict();
    pinyin_.load(join_path(data_dir_, "zhiyi.syllables"));
    if (!dict_.open(join_path(data_dir_, "zhiyi.dict.bin"))) return false;
    initials_.open(join_path(data_dir_, "zhiyi.initials.bin"));
    return true;
}

void Engine::reload_config() {
    load_config();
    refresh_candidates();
}

void Engine::reset_composition() {
    raw_input_.clear();
    input_.clear();
    candidates_.clear();
    page_ = 0;
}

void Engine::reset() { reset_composition(); }

void Engine::save() {
    if (user_dir_.empty()) return;
    ensure_dir(user_dir_);
    ensure_dir(dirname_of(config_file_path()));
    config_.save(config_file_path());
    std::ofstream out(join_path(user_dir_, "user.tsv"), std::ios::trunc);
    if (!out) return;
    for (const auto& item : user_freq_) {
        if (item.second > 0 && item.first.find('\n') == std::string::npos &&
            item.first.find('\t') == std::string::npos) {
            out << item.first << '\t' << item.second << '\n';
        }
    }
}

std::vector<Candidate> Engine::visible_candidates() const {
    std::vector<Candidate> result;
    const std::size_t begin = static_cast<std::size_t>(page_) * config_.page_size;
    if (begin >= candidates_.size()) return result;
    const std::size_t end = std::min(candidates_.size(), begin + config_.page_size);
    result.assign(candidates_.begin() + begin, candidates_.begin() + end);
    return result;
}

int Engine::page_count() const {
    if (candidates_.empty()) return 0;
    return static_cast<int>((candidates_.size() + config_.page_size - 1) /
                            config_.page_size);
}

void Engine::add_candidates(const std::vector<DictCandidate>& source, int kind,
                            std::size_t consumed, std::size_t limit) {
    for (const auto& item : source) {
        if (item.text.empty()) continue;
        if (candidates_.size() >= limit) break;
        Candidate c;
        c.text = item.text;
        c.comment = item.code;
        c.consumed = consumed;
        c.kind = kind;
        const auto learned = user_freq_.find(item.text);
        const std::int64_t boost = learned == user_freq_.end()
                                       ? 0
                                       : static_cast<std::int64_t>(learned->second) * 200000;
        c.score = static_cast<std::int64_t>(10 - kind) * 1000000000LL +
                  item.freq + boost;
        candidates_.push_back(std::move(c));
    }
}

std::string Engine::compose_greedy(const std::string& input) const {
    std::string out;
    std::string rest = input;
    for (int guard = 0; !rest.empty() && guard < 20; ++guard) {
        std::size_t consumed = 0;
        DictCandidate best;
        if (!dict_.longest_prefix_word(rest, &consumed, &best) || consumed == 0) {
            return {};
        }
        out += best.text;
        rest.erase(0, consumed);
    }
    return rest.empty() ? out : std::string{};
}

std::string Engine::compose_syllables(const std::string& input) const {
    if (pinyin_.empty()) return {};
    const auto parts = pinyin_.split(input);
    if (parts.size() < 2) return {};
    std::string out;
    for (const auto& syllable : parts) {
        const auto hits = dict_.exact(syllable, 1);
        if (hits.empty()) return {};
        out += hits.front().text;
    }
    return out;
}

void Engine::refresh_candidates() {
    candidates_.clear();
    page_ = 0;
    if (input_.empty()) return;

    constexpr std::size_t kMaxCandidates = 120;
    const auto variants = PinyinTable::fuzzy_variants(input_, config_.fuzzy);

    // 0. A full sentence built from the whole input is the most likely intent.
    if (config_.enable_sentence) {
        std::vector<Candidate> sentence;
        std::set<std::string> seen_sentence;
        for (const auto& variant : variants) {
            for (const std::string& text :
                 {compose_greedy(variant), compose_syllables(variant)}) {
                if (text.empty() || text.size() < 6 ||
                    !seen_sentence.insert(text).second) {
                    continue;
                }
                Candidate c;
                c.text = text;
                c.comment = "句";
                c.consumed = input_.size();
                c.kind = 0;
                c.score = 10LL * 1000000000LL;
                sentence.push_back(std::move(c));
            }
        }
        for (auto& c : sentence) {
            if (candidates_.size() >= kMaxCandidates) break;
            candidates_.push_back(std::move(c));
        }
    }

    // 1. Exact whole-word matches, then shuangpin/initials abbreviations and
    //    prefix completions for an unfinished spelling.
    for (const auto& variant : variants) {
        if (candidates_.size() < kMaxCandidates) {
            add_candidates(dict_.exact(variant, 20), 1, input_.size(),
                           kMaxCandidates);
        }
        if (candidates_.size() < kMaxCandidates) {
            add_candidates(dict_.prefix(variant, 16), 2, input_.size(),
                           kMaxCandidates);
        }
        // Abbreviation matches are ranked below full-pinyin completions: the
        // upstream syllable_ids column is occasionally mis-segmented, so a
        // short full-pinyin prefix ("nih") must not be hijacked by an
        // unrelated initials entry ("那还剩").
        if (config_.enable_initials && initials_.is_open() &&
            candidates_.size() < kMaxCandidates) {
            add_candidates(initials_.exact(variant, 12), 3, input_.size(),
                           kMaxCandidates);
        }
    }

    // 4. Single characters for the first syllable, so choosing one leaves the
    //    rest of the spelling in the preedit (partial selection).
    if (!pinyin_.empty()) {
        const auto parts = pinyin_.split(input_);
        if (!parts.empty() && parts.front().size() < input_.size()) {
            add_candidates(dict_.exact(parts.front(), 9), 4, parts.front().size(),
                           kMaxCandidates);
        }
    }

    // Keep the highest scoring candidate per distinct text.
    std::unordered_set<std::string> seen;
    std::vector<Candidate> unique;
    unique.reserve(candidates_.size());
    for (auto& c : candidates_) {
        if (!seen.insert(c.text).second) continue;
        unique.push_back(std::move(c));
    }
    std::stable_sort(unique.begin(), unique.end(),
                     [](const Candidate& a, const Candidate& b) {
                         return a.score > b.score;
                     });
    candidates_ = std::move(unique);
}

std::string Engine::select(std::size_t index) {
    const std::size_t absolute = static_cast<std::size_t>(page_) * config_.page_size + index;
    if (absolute >= candidates_.size()) return {};
    Candidate chosen = candidates_[absolute];
    user_freq_[chosen.text] += 1;

    std::string remaining;
    if (chosen.consumed > 0 && chosen.consumed < input_.size()) {
        remaining = input_.substr(chosen.consumed);
    }
    const std::string raw_remaining = remaining.empty()
                                          ? std::string{}
                                          : raw_input_.substr(
                                                raw_input_.size() - remaining.size());
    reset_composition();
    if (!remaining.empty()) {
        input_ = remaining;
        raw_input_ = raw_remaining;
        refresh_candidates();
    }
    return chosen.text;
}

std::string Engine::commit_raw() {
    const std::string text = raw_input_;
    reset_composition();
    return text;
}

std::string Engine::english_punctuation(char ch) {
    if (config_.full_shape && ch >= 0x21 && ch <= 0x7e) {
        return utf8_to_fullwidth(ch);
    }
    if (!config_.chinese_punctuation) return std::string(1, ch);
    switch (ch) {
        case ',': return "，";
        case '.': return "。";
        case '?': return "？";
        case '!': return "！";
        case ';': return "；";
        case ':': return "：";
        case '\\': return "、";
        case '`': return "·";
        case '^': return "……";
        case '_': return "——";
        case '$': return "￥";
        case '~': return "～";
        case '<': return "《";
        case '>': return "》";
        case '(': return "（";
        case ')': return "）";
        case '[': return "【";
        case ']': return "】";
        case '{': return "｛";
        case '}': return "｝";
        case '\'': {
            const bool open = quote_open_;
            quote_open_ = !quote_open_;
            return open ? "‘" : "’";
        }
        case '"': {
            const bool open = quote_open_;
            quote_open_ = !quote_open_;
            return open ? "“" : "”";
        }
        default: return std::string(1, ch);
    }
}

EngineResult Engine::process(const Key& key) {
    EngineResult result;
    result.display_changed = false;

    if (key.kind == KeyKind::kToggleEnglish) {
        english_ = !english_;
        if (!english_ && !input_.empty()) {
            result.commit = commit_raw();
        } else {
            reset_composition();
        }
        result.consumed = true;
        result.display_changed = true;
        return result;
    }
    if (key.ctrl || key.alt || key.kind == KeyKind::kUnknown) return result;
    if (english_) {
        // Fcitx's global fcitx-punc addon rewrites ',' -> '，' in its post
        // input filter whenever the front end returns IRV_TO_PROCESS. It does
        // not know about this IME's own English state, so switching the
        // keyboard layout cannot turn it off. Commit printable ASCII as text
        // instead: the non-zero front-end result makes fcitx skip all post
        // input filters, and the character arrives exactly as typed.
        switch (key.kind) {
            case KeyKind::kChar:
                if (key.ch >= 0x20 && key.ch <= 0x7e) {
                    result.commit.assign(1, key.ch);
                }
                break;
            case KeyKind::kDigit:
                result.commit.assign(1, key.ch);
                break;
            case KeyKind::kSpace:
                result.commit = " ";
                break;
            default:
                break;
        }
        if (!result.commit.empty()) result.consumed = true;
        return result;
    }

    switch (key.kind) {
        case KeyKind::kChar: {
            const char ch = key.ch;
            if (ch >= 'A' && ch <= 'Z') {
                if (raw_input_.size() < kMaxInput) {
                    raw_input_.push_back(static_cast<char>(ch - 'A' + 'a'));
                    input_ = shuangpin_.valid() ? shuangpin_.convert(raw_input_)
                                                : raw_input_;
                    refresh_candidates();
                }
                result.consumed = true;
                result.display_changed = true;
                break;
            }
            if (ch >= 'a' && ch <= 'z') {
                if (raw_input_.size() < kMaxInput) {
                    raw_input_.push_back(ch);
                    input_ = shuangpin_.valid() ? shuangpin_.convert(raw_input_)
                                                : raw_input_;
                    refresh_candidates();
                }
                result.consumed = true;
                result.display_changed = true;
                break;
            }
            if (ch == '\'' && !raw_input_.empty()) {
                // Syllable separator, common for "xi'an".
                if (raw_input_.size() < kMaxInput) {
                    raw_input_.push_back('\'');
                }
                result.consumed = true;
                result.display_changed = true;
                break;
            }
            // Punctuation: commit the pending composition first, then the sign.
            std::string sign = english_punctuation(ch);
            if (!raw_input_.empty()) sign = commit_raw() + sign;
            result.commit = std::move(sign);
            result.consumed = true;
            result.display_changed = true;
            break;
        }
        case KeyKind::kDigit: {
            const int digit = key.ch == '0' ? 9 : key.ch - '1';
            if (!candidates_.empty() && digit >= 0 && digit < config_.page_size) {
                const std::string text = select(static_cast<std::size_t>(digit));
                if (!text.empty()) {
                    result.commit = text;
                    result.consumed = true;
                    result.display_changed = true;
                    break;
                }
            }
            // No candidates: digits go straight to the application.
            result.commit = commit_raw() + key.ch;
            result.consumed = !raw_input_.empty();
            result.display_changed = result.consumed;
            break;
        }
        case KeyKind::kBackspace: {
            if (!raw_input_.empty()) {
                raw_input_.pop_back();
                input_ = shuangpin_.valid() ? shuangpin_.convert(raw_input_)
                                            : raw_input_;
                refresh_candidates();
                result.consumed = true;
                result.display_changed = true;
            }
            break;
        }
        case KeyKind::kEscape: {
            if (!raw_input_.empty()) {
                reset_composition();
                result.consumed = true;
                result.display_changed = true;
            }
            break;
        }
        case KeyKind::kReturn: {
            if (!raw_input_.empty()) {
                result.commit = commit_raw();
                result.consumed = true;
                result.display_changed = true;
            }
            break;
        }
        case KeyKind::kSpace: {
            if (!candidates_.empty()) {
                result.commit = select(0);
            } else if (!raw_input_.empty()) {
                result.commit = commit_raw();
            } else {
                result.commit = config_.full_shape ? utf8_to_fullwidth(' ') : " ";
            }
            result.consumed = true;
            result.display_changed = true;
            break;
        }
        case KeyKind::kPageUp: {
            if (page_ > 0) {
                --page_;
                result.consumed = true;
                result.display_changed = true;
            } else if (!raw_input_.empty()) {
                result.consumed = true;
            }
            break;
        }
        case KeyKind::kPageDown: {
            if (page_ + 1 < page_count()) {
                ++page_;
                result.consumed = true;
                result.display_changed = true;
            } else if (!raw_input_.empty()) {
                result.consumed = true;
            }
            break;
        }
        default:
            break;
    }
    return result;
}

}  // namespace zhiyi
