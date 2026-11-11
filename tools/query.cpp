// Copyright (c) 2026 Zhiyi IME Linux port contributors. GPL-3.0-only.
//
// Headless engine probe: replays a key sequence and prints the preedit plus
// the visible candidates. Also used by ctest to verify the generated
// dictionary and the whole key -> candidate pipeline on Linux.
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "core/engine.h"

namespace {

zhiyi::Key key_from_char(char ch) {
    zhiyi::Key key;
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
        key.kind = zhiyi::KeyKind::kChar;
        key.ch = ch;
    } else if (ch >= '0' && ch <= '9') {
        key.kind = zhiyi::KeyKind::kDigit;
        key.ch = ch;
    } else if (ch == ' ') {
        key.kind = zhiyi::KeyKind::kSpace;
    } else {
        key.kind = zhiyi::KeyKind::kChar;
        key.ch = ch;
    }
    return key;
}

std::vector<zhiyi::Key> parse_keys(const std::string& spec) {
    std::vector<zhiyi::Key> keys;
    for (std::size_t i = 0; i < spec.size(); ++i) {
        if (spec[i] != '\\' || i + 1 >= spec.size()) {
            keys.push_back(key_from_char(spec[i]));
            continue;
        }
        ++i;
        zhiyi::Key key;
        switch (spec[i]) {
            case 'B': key.kind = zhiyi::KeyKind::kBackspace; break;
            case 'R': key.kind = zhiyi::KeyKind::kReturn; break;
            case 'E': key.kind = zhiyi::KeyKind::kEscape; break;
            case 'S': key.kind = zhiyi::KeyKind::kSpace; break;
            case 'N': key.kind = zhiyi::KeyKind::kPageDown; break;
            case 'P': key.kind = zhiyi::KeyKind::kPageUp; break;
            case 'T': key.kind = zhiyi::KeyKind::kToggleEnglish; break;
            default: key = key_from_char(spec[i]); break;
        }
        keys.push_back(key);
    }
    return keys;
}

}  // namespace

int main(int argc, char** argv) {
    std::string data_dir = "data";
    std::string user_dir;
    std::string keys = "nihao";
    std::string expect;
    std::string expect_commit;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string {
            return i + 1 < argc ? argv[++i] : std::string{};
        };
        if (arg == "--data") data_dir = next();
        else if (arg == "--user") user_dir = next();
        else if (arg == "--keys") keys = next();
        else if (arg == "--expect") expect = next();
        else if (arg == "--expect-commit") expect_commit = next();
        else if (arg == "--help" || arg == "-h") {
            std::cout << "usage: zhiyi-query --data DIR [--user DIR] "
                         "[--keys SEQUENCE] [--expect WORD]\n"
                         "  escapes: \\B backspace \\R return \\E escape \\S space "
                         "\\N next page \\P prev page \\T toggle English\n";
            return 0;
        }
    }

    zhiyi::Engine engine;
    if (!engine.initialize(data_dir, user_dir)) {
        std::cerr << "failed to open dictionary in " << data_dir << "\n";
        return 2;
    }

    std::set<std::string> all_candidates;
    std::string commits;
    for (const auto& key : parse_keys(keys)) {
        const auto result = engine.process(key);
        commits += result.commit;
        if (!result.display_changed) continue;
        const auto candidates = engine.visible_candidates();
        std::cout << "preedit=\"" << engine.preedit() << "\" page="
                  << engine.page() << "/" << engine.page_count()
                  << " candidates=[";
        for (std::size_t i = 0; i < candidates.size() && i < 9; ++i) {
            if (i) std::cout << " ";
            std::cout << (i + 1) << "." << candidates[i].text;
        }
        std::cout << "]\n";
        for (const auto& candidate : candidates) {
            all_candidates.insert(candidate.text);
        }
    }
    std::cout << "commit=\"" << commits << "\"\n";

    int status = 0;
    if (!expect.empty()) {
        const bool found = all_candidates.count(expect) > 0 ||
                           commits.find(expect) != std::string::npos;
        std::cout << "expect=\"" << expect << "\" -> "
                  << (found ? "FOUND" : "NOT FOUND") << "\n";
        if (!found) status = 1;
    }
    if (!expect_commit.empty()) {
        const bool found = commits == expect_commit;
        std::cout << "expect-commit=\"" << expect_commit << "\" -> "
                  << (found ? "OK" : "FAILED") << "\n";
        if (!found) status = 1;
    }
    return status;
}
