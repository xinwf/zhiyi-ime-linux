// Copyright (c) 2026 Zhiyi IME Linux port contributors. GPL-3.0-only.
//
// Fcitx 5 input method front end. All pinyin logic lives in the
// framework-independent zhiyi-core library; this file only translates
// Fcitx key events / display primitives to and from that engine.
//
// Design notes (mirroring src/fcitx/zhiyi_addon.cpp where applicable):
//   * The engine owns composition, candidates, paging and punctuation. The
//     candidate list handed to the UI only ever contains the currently
//     visible page, exactly like the Fcitx 4 front end; digit selection,
//     space, paging etc. all run through Engine::process.
//   * Chinese/English switching follows the Fcitx 5 convention: tapping a
//     modifier hotkey (fcitx5 global config, AltTriggerKeys=Shift_L by
//     default) or Ctrl+Space toggles the whole input method in the core.
//     This addon therefore keeps the engine permanently in Chinese mode -
//     the engine's internal English submode (a Fcitx 4 presentation detail)
//     is disabled here, see ZhiyiAddon::applyConfigDefaults.
//   * ~/.config/zhiyi/config.json is watched with inotify so zhiyi-config
//     and the tray hot-reload the engine without restarting fcitx5, which
//     is what the Fcitx 4 addon achieves via its ReloadConfig hook.
#include <sys/inotify.h>
#include <unistd.h>

#include <fcitx-utils/event.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/keysymgen.h>
#include <fcitx-utils/log.h>
#include <fcitx/text.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/userinterface.h>

#include <cerrno>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/engine.h"

#ifndef ZHIYI_DATA_DIR
#define ZHIYI_DATA_DIR "/usr/share/zhiyi/data"
#endif

#define ZHIYI5_ERROR() FCITX_ERROR() << "[zhiyi] "
#define ZHIYI5_WARN() FCITX_WARN() << "[zhiyi] "
#define ZHIYI5_DEBUG() FCITX_DEBUG() << "[zhiyi] "

namespace fcitx {

std::string installed_data_dir() {
    const char* override_dir = getenv("ZHIYI_DATA_DIR");
    return override_dir && *override_dir ? override_dir : ZHIYI_DATA_DIR;
}

// User configuration directory, mirroring config_file_path() in
// src/core/engine.cpp (kept in sync by the dictionary_pinyin tests).
std::string config_dir_path() {
    const char* xdg_config = getenv("XDG_CONFIG_HOME");
    std::string dir = (xdg_config && *xdg_config)
                          ? std::string(xdg_config)
                          : std::string(getenv("HOME") ? getenv("HOME") : ".") +
                                "/.config";
    dir += "/zhiyi";
    return dir;
}

// Translates a normalized Fcitx 5 key into the engine's Key. The mapping is
// identical to make_key() in the Fcitx 4 front end, so both front ends feed
// the engine with the same key stream (replayable via tools/query.cpp).
zhiyi::Key make_key(const Key& key) {
    zhiyi::Key result;
    const KeyStates states = key.states();
    result.ctrl = states.test(KeyState::Ctrl);
    result.alt = states.test(KeyState::Alt);
    result.shift = states.test(KeyState::Shift);

    const KeySym sym = key.sym();
    if (sym >= FcitxKey_a && sym <= FcitxKey_z) {
        result.kind = zhiyi::KeyKind::kChar;
        result.ch = static_cast<char>('a' + (sym - FcitxKey_a));
    } else if (sym >= FcitxKey_A && sym <= FcitxKey_Z) {
        result.kind = zhiyi::KeyKind::kChar;
        result.ch = static_cast<char>('A' + (sym - FcitxKey_A));
    } else if (sym >= FcitxKey_0 && sym <= FcitxKey_9) {
        result.kind = zhiyi::KeyKind::kDigit;
        result.ch = static_cast<char>('0' + (sym - FcitxKey_0));
    } else {
        switch (sym) {
            case FcitxKey_BackSpace:
                result.kind = zhiyi::KeyKind::kBackspace;
                break;
            case FcitxKey_Return:
            case FcitxKey_KP_Enter:
                result.kind = zhiyi::KeyKind::kReturn;
                break;
            case FcitxKey_Escape:
                result.kind = zhiyi::KeyKind::kEscape;
                break;
            case FcitxKey_space:
            case FcitxKey_KP_Space:
                result.kind = zhiyi::KeyKind::kSpace;
                break;
            case FcitxKey_Prior:
            case FcitxKey_KP_Prior:
            case FcitxKey_Up:
            case FcitxKey_minus:
                result.kind = zhiyi::KeyKind::kPageUp;
                break;
            case FcitxKey_Next:
            case FcitxKey_KP_Next:
            case FcitxKey_Down:
            case FcitxKey_equal:
                result.kind = zhiyi::KeyKind::kPageDown;
                break;
            default:
                if (sym >= 0x20 && sym <= 0x7e) {
                    result.kind = zhiyi::KeyKind::kChar;
                    result.ch = static_cast<char>(sym);
                }
                break;
        }
    }
    return result;
}

KeyList selection_keys() {
    // Same choose keys as the Fcitx 4 addon: FcitxCandidateWordSetChoose(list,
    // "1234567890"). These are display labels only; the digit keys are
    // processed by the engine in keyEvent.
    KeyList keys;
    for (KeySym sym : {FcitxKey_1, FcitxKey_2, FcitxKey_3, FcitxKey_4,
                       FcitxKey_5, FcitxKey_6, FcitxKey_7, FcitxKey_8,
                       FcitxKey_9, FcitxKey_0}) {
        keys.emplace_back(sym);
    }
    return keys;
}

class ZhiyiAddon;

// A candidate word backed by the engine's visible page. The UI calls select()
// for mouse clicks; keyboard selection goes through Engine::process instead.
class ZhiyiCandidateWord : public CandidateWord {
public:
    ZhiyiCandidateWord(ZhiyiAddon* addon, std::size_t index, std::string text)
        : CandidateWord(Text(std::move(text))), addon_(addon), index_(index) {}

    void select(InputContext* inputContext) const override;

private:
    ZhiyiAddon* addon_;
    std::size_t index_;
};

class ZhiyiAddon : public InputMethodEngineV2 {
public:
    ZhiyiAddon(Instance* instance);
    ~ZhiyiAddon() override;

    void keyEvent(const InputMethodEntry& entry, KeyEvent& event) override;
    void reset(const InputMethodEntry& entry,
               InputContextEvent& event) override;

    void reloadConfig() override;
    void save() override { engine_.save(); }

    void selectCandidate(InputContext* inputContext, std::size_t index);
    void updateUI(InputContext* inputContext);

private:
    void applyConfigDefaults();
    void addConfigWatches();
    void startConfigMonitor();
    void stopConfigMonitor();

    Instance* instance_;
    zhiyi::Engine engine_;
    int inotify_fd_ = -1;
    int config_watch_ = -1;
    std::unique_ptr<EventSourceIO> monitor_source_;
};

void ZhiyiCandidateWord::select(InputContext* inputContext) const {
    if (!addon_) return;
    addon_->selectCandidate(inputContext, index_);
}

ZhiyiAddon::ZhiyiAddon(Instance* instance) : instance_(instance) {
    if (!engine_.initialize(installed_data_dir(), std::string{})) {
        // Keep the addon alive so fcitx5 can still report a clean state; the
        // engine simply produces no candidates without its dictionary.
        ZHIYI5_ERROR() << "Zhiyi IME: dictionary not found in "
                       << installed_data_dir();
    }
    applyConfigDefaults();
    startConfigMonitor();
}

ZhiyiAddon::~ZhiyiAddon() { stopConfigMonitor(); }

// The engine reads default_english from the shared config.json. That option
// describes the Fcitx 4 in-IM English submode; on Fcitx 5 the framework owns
// the active/inactive state, so the engine must always start (and reload) in
// Chinese mode, otherwise every keystroke would go through the ASCII
// passthrough path with no way back.
void ZhiyiAddon::applyConfigDefaults() { engine_.set_english(false); }

void ZhiyiAddon::reloadConfig() {
    engine_.reload_config();
    applyConfigDefaults();
    // Redraw any live composition with the new settings (page size, layout,
    // shuangpin...), mirroring what a reload achieves mid-composition on
    // Fcitx 4.
    instance_->inputContextManager().foreachFocused([this](InputContext* ic) {
        if (!engine_.preedit().empty() || engine_.candidate_count() > 0) {
            updateUI(ic);
        }
        return true;
    });
}

// Watches both ~/.config (so the zhiyi directory is picked up when
// zhiyi-config creates it later) and ~/.config/zhiyi itself. Any event in
// either directory simply triggers a reload: reading one JSON file is cheap,
// and the extra reloads from unrelated config writes are harmless.
void ZhiyiAddon::addConfigWatches() {
    const std::string dir = config_dir_path();
    const std::string parent =
        dir.substr(0, dir.find_last_of('/') == std::string::npos
                          ? 0
                          : dir.find_last_of('/'));
    for (const std::string& path : {dir, parent}) {
        const int wd = ::inotify_add_watch(
            inotify_fd_, path.c_str(),
            IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE | IN_MODIFY);
        if (wd < 0) {
            // Common when the directory does not exist yet; the tray/dbus
            // reload path still works in that case.
            ZHIYI5_DEBUG() << "Zhiyi IME: cannot watch " << path << ": "
                           << std::strerror(errno);
        }
    }
}

void ZhiyiAddon::startConfigMonitor() {
    if (inotify_fd_ >= 0) return;
    inotify_fd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd_ < 0) {
        ZHIYI5_WARN() << "Zhiyi IME: inotify_init failed: "
                      << std::strerror(errno);
        return;
    }
    addConfigWatches();
    monitor_source_ = instance_->eventLoop().addIOEvent(
        inotify_fd_, IOEventFlag::In,
        [this](EventSourceIO*, int fd, IOEventFlags) {
            alignas(inotify_event) char buffer[4096];
            ssize_t nread;
            bool changed = false;
            while ((nread = ::read(fd, buffer, sizeof(buffer))) > 0) {
                changed = true;
            }
            ZHIYI5_DEBUG() << "Zhiyi IME: config monitor fired changed="
                           << changed;
            if (changed) {
                reloadConfig();
            }
            return true;
        });
}

void ZhiyiAddon::stopConfigMonitor() {
    monitor_source_.reset();
    if (inotify_fd_ >= 0) {
        ::close(inotify_fd_);
        inotify_fd_ = -1;
        config_watch_ = -1;
    }
}

void ZhiyiAddon::selectCandidate(InputContext* inputContext,
                                 std::size_t index) {
    const std::string text = engine_.select_visible(index);
    if (text.empty()) return;
    inputContext->commitString(text);
    inputContext->inputPanel().reset();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

// Rebuilds the whole input panel (preedit + current candidate page). The
// engine decides what is visible; this is the Fcitx 5 counterpart of the
// Fcitx 4 front end's GetCandWords + update_display.
void ZhiyiAddon::updateUI(InputContext* inputContext) {
    auto& panel = inputContext->inputPanel();
    panel.reset();

    const std::string preedit_text = engine_.preedit();
    if (preedit_text.empty() && engine_.candidate_count() == 0) {
        inputContext->updatePreedit();
        inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
        return;
    }

    // Client preedit (embedded in the application, underlined like
    // fcitx5-chinese-addons) and the floating window preedit.
    Text client_preedit;
    client_preedit.append(preedit_text, TextFormatFlag::Underline);
    client_preedit.setCursor(client_preedit.textLength());
    panel.setClientPreedit(client_preedit);
    Text window_preedit;
    window_preedit.append(preedit_text);
    window_preedit.setCursor(window_preedit.textLength());
    panel.setPreedit(window_preedit);

    if (engine_.candidate_count() > 0) {
        auto list = std::make_unique<CommonCandidateList>();
        const int page_size = engine_.config().page_size;
        list->setPageSize(page_size);
        list->setSelectionKey(selection_keys());
        // Horizontal/vertical candidate display follows the "layout" config
        // key (zhiyi-config --set layout horizontal).
        list->setLayoutHint(engine_.config().layout == "horizontal"
                                ? CandidateLayoutHint::Horizontal
                                : CandidateLayoutHint::Vertical);
        std::size_t index = 0;
        for (const auto& candidate : engine_.visible_candidates()) {
            list->append(std::make_unique<ZhiyiCandidateWord>(this, index,
                                                              candidate.text));
            ++index;
        }
        panel.setCandidateList(std::move(list));
    }

    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void ZhiyiAddon::keyEvent(const InputMethodEntry& entry, KeyEvent& event) {
    FCITX_UNUSED(entry);
    auto* inputContext = event.inputContext();

    // Modifier releases and other key-up events carry no input semantics for
    // this IME: fcitx5's core handles the Chinese/English toggle hotkeys
    // (including tapped Shift) before the engine ever sees them.
    if (event.isRelease()) {
        return;
    }

    const zhiyi::Key key = make_key(event.key());
    const auto result = engine_.process(key);

    if (!result.commit.empty()) {
        inputContext->commitString(result.commit);
        inputContext->inputPanel().reset();
        inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
        event.filterAndAccept();
        return;
    }
    if (result.consumed) {
        event.filterAndAccept();
        if (result.display_changed) {
            updateUI(inputContext);
        }
        return;
    }
    // Unhandled (Ctrl/Alt combos, unknown keys): leave unfiltered so the key
    // is forwarded to the application.
}

void ZhiyiAddon::reset(const InputMethodEntry& entry,
                       InputContextEvent& event) {
    FCITX_UNUSED(entry);
    FCITX_UNUSED(event);
    engine_.reset();
    auto* inputContext = event.inputContext();
    if (inputContext) {
        inputContext->inputPanel().reset();
        inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
    }
}

class ZhiyiFactory : public AddonFactory {
public:
    AddonInstance* create(AddonManager* manager) override {
        return new ZhiyiAddon(manager->instance());
    }
};

}  // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::ZhiyiFactory)
