// Copyright (c) 2026 Zhiyi IME Linux port contributors. GPL-3.0-only.
//
// Fcitx 4 input method front end. All pinyin logic lives in the
// framework-independent zhiyi-core library; this file only translates
// Fcitx key events / display primitives to and from that engine.
#include <X11/keysym.h>

#include <fcitx/candidate.h>
#include <fcitx/fcitx.h>
#include <fcitx/ime.h>
#include <fcitx/instance.h>
#include <fcitx/ui.h>
#include <fcitx-utils/keysym.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "core/engine.h"
#include "irv_contract.h"

#ifndef ZHIYI_DATA_DIR
#define ZHIYI_DATA_DIR "/usr/share/zhiyi/data"
#endif

namespace {

struct ZhiyiAddon {
    FcitxInstance* instance = nullptr;
    zhiyi::Engine engine;
    bool shift_down = false;
    bool shift_used = false;
    std::string sub_mode;
};

// Stored in FcitxCandidateWord::priv. Fcitx frees this pointer itself in
// FcitxCandidateWordFree, so it must be malloc'ed and must never be freed by
// us. Keeping the addon pointer here also makes the callback independent of
// what fcitx passes as its first argument (owner on 4.2.x, but other builds
// may hand over the instance).
struct CandidatePriv {
    ZhiyiAddon* addon;
    std::size_t index;
};

std::string installed_data_dir() {
    const char* override_dir = std::getenv("ZHIYI_DATA_DIR");
    return override_dir && *override_dir ? override_dir : ZHIYI_DATA_DIR;
}

zhiyi::Key make_key(FcitxKeySym sym, unsigned int state, bool english) {
    zhiyi::Key key;
    key.ctrl = (state & FcitxKeyState_Ctrl) != 0;
    key.alt = (state & FcitxKeyState_Alt) != 0;
    key.shift = (state & FcitxKeyState_Shift) != 0;

    if (english && sym >= 0x20 && sym <= 0x7e) {
        // Every printable ASCII key is a literal character in English mode:
        // no digit-candidate selection, no -/= page turning, no punctuation
        // mapping. The engine commits it verbatim.
        key.kind = zhiyi::KeyKind::kChar;
        key.ch = static_cast<char>(sym);
        return key;
    }

    if (sym >= XK_a && sym <= XK_z) {
        key.kind = zhiyi::KeyKind::kChar;
        key.ch = static_cast<char>('a' + (sym - XK_a));
    } else if (sym >= XK_A && sym <= XK_Z) {
        key.kind = zhiyi::KeyKind::kChar;
        key.ch = static_cast<char>('A' + (sym - XK_A));
    } else if (sym >= XK_0 && sym <= XK_9) {
        key.kind = zhiyi::KeyKind::kDigit;
        key.ch = static_cast<char>('0' + (sym - XK_0));
    } else {
        switch (sym) {
            case XK_BackSpace: key.kind = zhiyi::KeyKind::kBackspace; break;
            case XK_Return:
            case XK_KP_Enter: key.kind = zhiyi::KeyKind::kReturn; break;
            case XK_Escape: key.kind = zhiyi::KeyKind::kEscape; break;
            case XK_space:
            case XK_KP_Space: key.kind = zhiyi::KeyKind::kSpace; break;
            case XK_Prior:
            case XK_KP_Prior:
            case XK_Up:
            case XK_minus: key.kind = zhiyi::KeyKind::kPageUp; break;
            case XK_Next:
            case XK_KP_Next:
            case XK_Down:
            case XK_equal: key.kind = zhiyi::KeyKind::kPageDown; break;
            default:
                if (sym >= 0x20 && sym <= 0x7e) {
                    key.kind = zhiyi::KeyKind::kChar;
                    key.ch = static_cast<char>(sym);
                }
                break;
        }
    }
    return key;
}

INPUT_RETURN_VALUE write_commit(ZhiyiAddon* addon, const std::string& text) {
    FcitxInputState* input = FcitxInstanceGetInputState(addon->instance);
    char* output = FcitxInputStateGetOutputString(input);
    std::strncpy(output, text.c_str(), MAX_USER_INPUT - 1);
    output[MAX_USER_INPUT - 1] = '\0';
    // Never add IRV_FLAG_UPDATE_CANDIDATE_WORDS here: in
    // FcitxInstanceDoInputCallback that flag calls GetCandWords, whose return
    // value *overwrites* retVal and would drop IRV_FLAG_PENDING_COMMIT_STRING
    // (the text would never reach the application). The next key press
    // rebuilds the candidate list through IRV_DISPLAY_CANDWORDS.
    return static_cast<INPUT_RETURN_VALUE>(zhiyi::kCommitResult);
}

// English-mode ASCII: commit the character but do not touch the input window.
INPUT_RETURN_VALUE write_plain_commit(ZhiyiAddon* addon, const std::string& text) {
    FcitxInputState* input = FcitxInstanceGetInputState(addon->instance);
    char* output = FcitxInputStateGetOutputString(input);
    std::strncpy(output, text.c_str(), MAX_USER_INPUT - 1);
    output[MAX_USER_INPUT - 1] = '\0';
    return static_cast<INPUT_RETURN_VALUE>(zhiyi::kAsciiCommitResult);
}

// Fill the preedit/aux messages. Called from GetCandWords, i.e. after fcitx
// has already run FcitxInstanceCleanInputWindow(), so the messages are empty.
void update_display(ZhiyiAddon* addon) {
    FcitxInputState* input = FcitxInstanceGetInputState(addon->instance);
    const std::string text = addon->engine.preedit();

    FcitxMessages* preedit = FcitxInputStateGetPreedit(input);
    FcitxMessages* client_preedit = FcitxInputStateGetClientPreedit(input);
    FcitxMessagesSetMessageCount(preedit, 0);
    FcitxMessagesSetMessageCount(client_preedit, 0);
    if (!text.empty()) {
        FcitxMessagesAddMessageAtLast(preedit, MSG_CODE, "%s", text.c_str());
        FcitxMessagesAddMessageAtLast(client_preedit, MSG_INPUT, "%s",
                                      text.c_str());
    }
    FcitxInputStateSetCursorPos(input, static_cast<int>(text.size()));
    FcitxInputStateSetClientCursorPos(input, static_cast<int>(text.size()));
    FcitxInputStateSetShowCursor(input, true);

    // Intentionally leave AuxUp empty. Classic-UI renders it on the same line
    // as the preedit, so a status label here shows up as a "知意" prefix in
    // front of the raw pinyin. The tray/sub-mode still reports 中 / EN.
    FcitxMessagesSetMessageCount(FcitxInputStateGetAuxUp(input), 0);
}

extern "C" {

static void* ZhiyiCreate(FcitxInstance* instance) {
    auto* addon = new ZhiyiAddon();
    addon->instance = instance;
    if (!addon->engine.initialize(installed_data_dir(), std::string{})) {
        // Keep the addon alive so Fcitx can still report a clean state; the
        // engine simply produces no candidates without its dictionary.
        FcitxLog(WARNING, "Zhiyi IME: dictionary not found in %s",
                 installed_data_dir().c_str());
    }

    FcitxIMIFace iface;
    std::memset(&iface, 0, sizeof(iface));
    iface.ResetIM = [](void* arg) {
        static_cast<ZhiyiAddon*>(arg)->engine.reset();
    };
    iface.DoInput = [](void* arg, FcitxKeySym sym, unsigned int state) -> INPUT_RETURN_VALUE {
        auto* addon = static_cast<ZhiyiAddon*>(arg);
        if (sym == XK_Shift_L || sym == XK_Shift_R) {
            addon->shift_down = true;
            addon->shift_used = false;
            return IRV_TO_PROCESS;
        }
        if (addon->shift_down) addon->shift_used = true;
        const zhiyi::Key key = make_key(sym, state, addon->engine.english());
        const auto result = addon->engine.process(key);
        if (!result.commit.empty()) {
            if (addon->engine.english()) {
                return write_plain_commit(addon, result.commit);
            }
            return write_commit(addon, result.commit);
        }
        // English mode forwards any remaining (non-printable) key untouched;
        // never redraw the input window here or it stays open as a status box.
        if (addon->engine.english()) return IRV_TO_PROCESS;
        // IRV_FLAG_UPDATE_CANDIDATE_WORDS makes fcitx call GetCandWords,
        // which then returns IRV_DISPLAY_CANDWORDS so the window is redrawn.
        if (result.consumed) return static_cast<INPUT_RETURN_VALUE>(zhiyi::kStateChangeResult);
        return IRV_TO_PROCESS;
    };
    iface.DoReleaseInput = [](void* arg, FcitxKeySym sym,
                              unsigned int state) -> INPUT_RETURN_VALUE {
        auto* addon = static_cast<ZhiyiAddon*>(arg);
        FCITX_UNUSED(state);
        if ((sym == XK_Shift_L || sym == XK_Shift_R) && addon->shift_down &&
            !addon->shift_used) {
            addon->shift_down = false;
            const auto result = addon->engine.process(
                zhiyi::Key{zhiyi::KeyKind::kToggleEnglish, 0, false, false, false});
            if (!result.commit.empty()) return write_commit(addon, result.commit);
            // Entering English mode closes the input window. Returning a
            // display result here leaves the (now empty) box on screen.
            if (addon->engine.english()) {
                return static_cast<INPUT_RETURN_VALUE>(zhiyi::kDismissWindowResult);
            }
            return static_cast<INPUT_RETURN_VALUE>(zhiyi::kStateChangeResult);
        }
        addon->shift_down = false;
        return IRV_TO_PROCESS;
    };
    iface.GetCandWords = [](void* arg) -> INPUT_RETURN_VALUE {
        auto* addon = static_cast<ZhiyiAddon*>(arg);
        // Nothing to show: English mode, or a composition that was just
        // cleared (Esc / commit / reset). IRV_CLEAN closes the window rather
        // than painting an empty box that follows the caret around.
        if (addon->engine.english() ||
            (addon->engine.preedit().empty() &&
             addon->engine.candidate_count() == 0)) {
            return static_cast<INPUT_RETURN_VALUE>(zhiyi::kDismissWindowResult);
        }
        FcitxInputState* input = FcitxInstanceGetInputState(addon->instance);
        FcitxCandidateWordList* list = FcitxInputStateGetCandidateList(input);
        // Fcitx calls FcitxInstanceCleanInputWindow() (which resets this list)
        // immediately before GetCandWords, so resetting again is unnecessary
        // and would risk a double free of the previous round's strings.
        const int page_size = addon->engine.config().page_size;
        FcitxCandidateWordSetPageSize(list, page_size);
        FcitxCandidateWordSetChoose(list, "1234567890");
        // Horizontal/vertical candidate display follows the "layout" config
        // key (zhiyi-config --set layout horizontal); classic-UI renders both.
        FcitxCandidateWordSetLayoutHint(
            list, addon->engine.config().layout == "horizontal" ? CLH_Horizontal
                                                                : CLH_Vertical);

        std::size_t index = 0;
        for (const auto& candidate : addon->engine.visible_candidates()) {
            FcitxCandidateWord word;
            std::memset(&word, 0, sizeof(word));
            word.strWord = strdup(candidate.text.c_str());
            // No strExtra: the pinyin code appended to every candidate is
            // visual noise. FcitxCandidateWordFree skips NULL extras.
            word.strExtra = nullptr;
            word.wordType = MSG_CODE;
            word.extraType = MSG_TIPS;
            word.owner = addon;
            // Fcitx owns `priv` and calls free() on it from
            // FcitxCandidateWordFree (along with strWord/strExtra). It must be
            // a heap pointer or NULL - an integer cast to void* makes fcitx
            // segfault in FcitxCandidateWordReset on the next key press.
            auto* holder = static_cast<CandidatePriv*>(
                std::malloc(sizeof(CandidatePriv)));
            holder->addon = addon;
            holder->index = index;
            word.priv = holder;
            word.callback = [](void* owner, FcitxCandidateWord* word) -> INPUT_RETURN_VALUE {
                FCITX_UNUSED(owner);
                const auto* holder = static_cast<const CandidatePriv*>(word->priv);
                if (!holder || !holder->addon) return IRV_TO_PROCESS;
                ZhiyiAddon* addon = holder->addon;
                const std::string text = addon->engine.select_visible(holder->index);
                if (text.empty()) return IRV_TO_PROCESS;
                return write_commit(addon, text);
            };
            FcitxCandidateWordAppend(list, &word);
            ++index;
        }
        update_display(addon);
        // FcitxInstanceDoInputCallback assigns this return value to retVal;
        // IRV_FLAG_UPDATE_INPUT_WINDOW is what makes FcitxUIUpdateInputWindow
        // run and the candidate window appear. Returning IRV_TO_PROCESS here
        // (as 0.1.1 did) silently discards DoInput's refresh flags.
        return static_cast<INPUT_RETURN_VALUE>(zhiyi::kStateChangeResult);
    };
    iface.Init = [](void* arg) -> boolean {
        auto* addon = static_cast<ZhiyiAddon*>(arg);
        addon->engine.reset();
        return true;
    };
    iface.Save = [](void* arg) {
        static_cast<ZhiyiAddon*>(arg)->engine.save();
    };
    iface.ReloadConfig = [](void* arg) {
        static_cast<ZhiyiAddon*>(arg)->engine.reload_config();
    };
    iface.GetSubModeName = [](void* arg) -> const char* {
        auto* addon = static_cast<ZhiyiAddon*>(arg);
        addon->sub_mode = addon->engine.english() ? "EN" : "中";
        return addon->sub_mode.c_str();
    };

    FcitxInstanceRegisterIMv2(instance, addon, "zhiyi", "知意输入法", "zhiyi",
                              iface, 10, "zh_CN");
    return addon;
}

static void ZhiyiDestroy(void* arg) { delete static_cast<ZhiyiAddon*>(arg); }

static void ZhiyiReloadConfig(void* arg) {
    static_cast<ZhiyiAddon*>(arg)->engine.reload_config();
}

}  // extern "C"

}  // namespace

FCITX_DEFINE_PLUGIN(fcitx_zhiyi, ime2, FcitxIMClass2) = {
    ZhiyiCreate,
    ZhiyiDestroy,
    ZhiyiReloadConfig,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};
