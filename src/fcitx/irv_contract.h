// Copyright (c) 2026 Zhiyi IME Linux port contributors. GPL-3.0-only.
//
// Return values shared by the Fcitx front end and its ABI contract test.
//
// FcitxInstanceDoInputCallback (fcitx 4.2.x, ime.c) does:
//
//   if (retVal & IRV_FLAG_UPDATE_CANDIDATE_WORDS) {
//       FcitxInstanceCleanInputWindow(instance);
//       retVal = currentIM->GetCandWords(currentIM->klass);   // OVERWRITES
//       FcitxInstanceProcessUpdateCandidates(instance);
//   }
//   ...
//   FcitxInstanceProcessInputReturnValue(instance, retVal);
//
// Only IRV_FLAG_UPDATE_INPUT_WINDOW makes FcitxUIUpdateInputWindow run, and
// only IRV_FLAG_PENDING_COMMIT_STRING makes the committed text reach the app.
// Hence:
//   * GetCandWords must return IRV_DISPLAY_CANDWORDS, not IRV_TO_PROCESS;
//   * a commit must NOT carry IRV_FLAG_UPDATE_CANDIDATE_WORDS, because that
//     would call GetCandWords and overwrite the commit flags.
#pragma once

#include <fcitx/ime.h>

namespace zhiyi {

inline constexpr int kStateChangeResult = IRV_DISPLAY_CANDWORDS;
inline constexpr int kCommitResult =
    IRV_COMMIT_STRING | IRV_FLAG_UPDATE_INPUT_WINDOW;
// Nothing to display (English mode, or an empty composition). IRV_CLEAN makes
// FcitxInstanceProcessInputReturnValue call FcitxUICloseInputWindow, instead
// of leaving a small empty box on screen while the user types English.
inline constexpr int kDismissWindowResult = IRV_CLEAN;
// Plain ASCII committed in English mode. A non-zero value skips fcitx's post
// input filters (fcitx-punc would otherwise turn ',' into '，'), while
// avoiding UPDATE_INPUT_WINDOW / DO_PHRASE_TIPS so the input window stays
// closed and the input state is not reset on every keystroke.
inline constexpr int kAsciiCommitResult = IRV_FLAG_PENDING_COMMIT_STRING;

}  // namespace zhiyi
