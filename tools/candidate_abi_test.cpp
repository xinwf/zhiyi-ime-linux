// Copyright (c) 2026 Zhiyi IME Linux port contributors. GPL-3.0-only.
//
// ABI contract test for Fcitx 4's candidate word list.
//
// FcitxCandidateWordFree (the utarray element destructor) calls free() on
// strWord, strExtra *and* priv. Every field we hand over to
// FcitxCandidateWordAppend must therefore be a malloc'ed pointer or NULL.
// A previous version of the addon stored the candidate index directly in
// `priv` as reinterpret_cast<void*>(index), which made fcitx segfault in
// FcitxCandidateWordReset as soon as a second key was pressed. This test
// replays many rounds of the reset -> append -> reset lifecycle so that
// regression is caught at build time.
#include <fcitx/candidate.h>
#include <fcitx/ui.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "irv_contract.h"

// The two invariants that keep the candidate window visible and committed
// text intact (see irv_contract.h for the fcitx call sequence).
static_assert((zhiyi::kStateChangeResult & IRV_FLAG_UPDATE_INPUT_WINDOW) != 0,
              "GetCandWords must ask fcitx to redraw the input window");
static_assert(!(zhiyi::kCommitResult & IRV_FLAG_UPDATE_CANDIDATE_WORDS),
              "a commit must not let GetCandWords overwrite the commit flags");
static_assert((zhiyi::kCommitResult & IRV_FLAG_PENDING_COMMIT_STRING) != 0,
              "a commit must keep the pending-commit flag");
static_assert((zhiyi::kDismissWindowResult & IRV_FLAG_RESET_INPUT) != 0,
              "dismissing the window must reset the input state");
static_assert(!(zhiyi::kDismissWindowResult & IRV_FLAG_UPDATE_INPUT_WINDOW),
              "dismissing the window must not redraw it");
static_assert((zhiyi::kAsciiCommitResult & IRV_FLAG_PENDING_COMMIT_STRING) != 0,
              "English-mode ASCII must still be committed");
static_assert(!(zhiyi::kAsciiCommitResult & IRV_FLAG_UPDATE_INPUT_WINDOW) &&
              !(zhiyi::kAsciiCommitResult & IRV_FLAG_DO_PHRASE_TIPS),
              "English-mode ASCII must not reopen or reset the input window");

namespace {

struct CandidatePriv {
    void* addon;
    std::size_t index;
};

INPUT_RETURN_VALUE on_chosen(void*, FcitxCandidateWord*) {
    return IRV_TO_PROCESS;
}

}  // namespace

int main() {
    FcitxCandidateWordList* list = FcitxCandidateWordNewList();
    if (!list) return 1;

    for (int round = 0; round < 32; ++round) {
        FcitxCandidateWordReset(list);
        FcitxCandidateWordSetPageSize(list, 9);
        FcitxCandidateWordSetChoose(list, "1234567890");
        FcitxCandidateWordSetLayoutHint(list, CLH_Vertical);
        for (std::size_t index = 0; index < 9; ++index) {
            FcitxCandidateWord word;
            std::memset(&word, 0, sizeof(word));
            word.strWord = strdup(index % 2 ? "你好" : "中国");
            word.strExtra = (index % 3 == 0) ? nullptr : strdup("nihao");
            word.wordType = MSG_CODE;
            word.extraType = MSG_TIPS;
            word.owner = nullptr;
            auto* holder = static_cast<CandidatePriv*>(
                std::malloc(sizeof(CandidatePriv)));
            holder->addon = list;
            holder->index = index;
            word.priv = holder;
            word.callback = on_chosen;
            FcitxCandidateWordAppend(list, &word);
        }
    }
    FcitxCandidateWordReset(list);
    FcitxCandidateWordFreeList(list);
    return 0;
}
