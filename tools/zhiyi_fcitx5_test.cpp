// Copyright (c) 2026 Zhiyi IME Linux port contributors. GPL-3.0-only.
//
// End-to-end test of the Fcitx 5 front end: runs a real in-process fcitx5
// Instance, loads the built zhiyi addon and replays key sequences through
// fcitx5's testfrontend, asserting commits and input panel contents. The
// structure follows fcitx5-chinese-addons' testpinyin.cpp.
//
// The exported testfrontend functions are declared here instead of including
// testing/testfrontend_public.h, which fcitx5-modules-dev does not ship.
#include <fcitx/addoninstance.h>
#include <fcitx/inputcontext.h>

FCITX_ADDON_DECLARE_FUNCTION(TestFrontend, createInputContext,
                             ICUUID(const std::string &));
FCITX_ADDON_DECLARE_FUNCTION(TestFrontend, destroyInputContext, void(ICUUID));
FCITX_ADDON_DECLARE_FUNCTION(TestFrontend, keyEvent,
                             void(ICUUID, const Key &, bool isRelease));
FCITX_ADDON_DECLARE_FUNCTION(TestFrontend, sendKeyEvent,
                             bool(ICUUID, const Key &, bool isRelease));
FCITX_ADDON_DECLARE_FUNCTION(TestFrontend, pushCommitExpectation,
                             void(std::string));

#include <fcitx-utils/event.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/testing.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>

#include <sys/stat.h>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#ifndef ZHIYI_TEST_DATA_DIR
#define ZHIYI_TEST_DATA_DIR "."
#endif
#ifndef ZHIYI_TEST_BUILD_DIR
#define ZHIYI_TEST_BUILD_DIR "."
#endif

using namespace fcitx;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            FCITX_FATAL() << "check failed at line " << __LINE__ << ": "     \
                          << #cond;                                          \
        }                                                                    \
    } while (0)

static void run_tests_part2(Instance* instance, const ICUUID& uuid,
                            InputContext* ic, const std::string& config_dir);

// Event loop sources must be kept alive: the returned handles remove their
// event when destroyed (testfrontend follows the same pattern).
static std::unique_ptr<EventSourceTime> next_phase_event;

static void type(Instance* instance, const ICUUID& uuid,
                 const std::vector<std::string>& keys) {
    auto* frontend = instance->addonManager().addon("testfrontend", true);
    for (const auto& k : keys) {
        frontend->call<ITestFrontend::keyEvent>(uuid, Key(k), false);
    }
}

static std::string first_candidate(InputContext* ic) {
    auto& panel = ic->inputPanel();
    auto list = panel.candidateList();
    CHECK(list);
    CHECK(list->size() > 0);
    return list->candidate(0).text().toString();
}

// The engine is driven through the real front end, so the expected commits
// are pushed before the keys that produce them (testfrontend compares
// synchronously and aborts on any mismatch, which CHECK-escapes this test).
static void run_tests_part1(Instance* instance, const ICUUID& uuid,
                            InputContext* ic, const std::string& config_dir) {
    auto* frontend = instance->addonManager().addon("testfrontend", true);
    CHECK(instance->inputMethodManager().entry("zhiyi"));

    // ---- full pinyin + digit selection ----
    frontend->call<ITestFrontend::pushCommitExpectation>("你好");
    type(instance, uuid, {"n", "i", "h", "a", "o"});
    {
        auto list = ic->inputPanel().candidateList();
        CHECK(list);
        CHECK(ic->inputPanel().clientPreedit().toString() == "nihao");
        CHECK(list->size() == 9);  // default page_size
        CHECK(first_candidate(ic) == "你好");
        frontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
        CHECK(!ic->inputPanel().candidateList());
    }

    // ---- space commits the first candidate ----
    frontend->call<ITestFrontend::pushCommitExpectation>("你好");
    type(instance, uuid, {"n", "i", "h", "a", "o", "space"});
    CHECK(!ic->inputPanel().candidateList());

    // ---- sentence / word lookup ----
    frontend->call<ITestFrontend::pushCommitExpectation>("我爱你");
    type(instance, uuid, {"w", "o", "a", "i", "n", "i", "space"});
    frontend->call<ITestFrontend::pushCommitExpectation>("你好");
    type(instance, uuid, {"n", "h", "space"});  // initials abbreviation

    // ---- Chinese punctuation with empty composition ----
    frontend->call<ITestFrontend::pushCommitExpectation>("，");
    type(instance, uuid, {","});

    // ---- punctuation flushes the pending composition ----
    // Engine behavior (identical on fcitx4): the unconfirmed spelling goes
    // out raw, followed by the punctuation sign.
    frontend->call<ITestFrontend::pushCommitExpectation>("nihao，");
    type(instance, uuid, {"n", "i", "h", "a", "o", ","});

    // ---- Enter commits the raw spelling ----
    frontend->call<ITestFrontend::pushCommitExpectation>("nihao");
    type(instance, uuid, {"n", "i", "h", "a", "o", "Return"});

    // ---- Escape clears the composition ----
    type(instance, uuid, {"n", "i", "h", "a", "o", "Escape"});
    CHECK(!ic->inputPanel().candidateList());
    CHECK(ic->inputPanel().clientPreedit().toString().empty());
    frontend->call<ITestFrontend::pushCommitExpectation>("ni");
    type(instance, uuid, {"n", "i", "Return"});

    // ---- Backspace ----
    frontend->call<ITestFrontend::pushCommitExpectation>("nih");
    type(instance, uuid,
         {"n", "i", "h", "a", "o", "BackSpace", "BackSpace", "Return"});

    // ---- paging ----
    type(instance, uuid, {"n"});
    {
        const std::string before = first_candidate(ic);
        frontend->call<ITestFrontend::keyEvent>(uuid, Key("Down"), false);
        const std::string after = first_candidate(ic);
        CHECK(before != after);
        frontend->call<ITestFrontend::keyEvent>(uuid, Key("Up"), false);
        CHECK(first_candidate(ic) == before);
        frontend->call<ITestFrontend::keyEvent>(uuid, Key("Escape"), false);
    }

    // ---- mouse click selection (CandidateWord::select) ----
    frontend->call<ITestFrontend::pushCommitExpectation>("你好");
    type(instance, uuid, {"n", "i", "h", "a", "o"});
    {
        auto list = ic->inputPanel().candidateList();
        CHECK(list);
        list->candidate(0).select(ic);
        CHECK(!ic->inputPanel().candidateList());
    }

    // ---- Shift+letter keeps composing ----
    frontend->call<ITestFrontend::pushCommitExpectation>("n");
    type(instance, uuid, {"Shift+n", "Return"});

    // ---- config hot reload via inotify ----
    // With the composition "n" open, rewrite config.json the way
    // zhiyi-config does (write + close). The addon's inotify watch reloads
    // the engine and redraws focused input contexts.
    const std::string config_file = config_dir + "/zhiyi/config.json";
    type(instance, uuid, {"n"});
    {
        std::ofstream out(config_file, std::ios::trunc);
        out << "{\"page_size\": 3}";
        out.close();
        CHECK(out.good());
        // Continue after the event loop had a chance to deliver inotify.
        next_phase_event = instance->eventLoop().addTimeEvent(
            CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 500000, 0,
            [instance, uuid, ic, config_dir](EventSourceTime*, uint64_t) {
                next_phase_event.reset();
                run_tests_part2(instance, uuid, ic, config_dir);
                return true;
            });
    }
}

static void run_tests_part2(Instance* instance, const ICUUID& uuid,
                            InputContext* ic, const std::string& config_dir) {
    auto* frontend = instance->addonManager().addon("testfrontend", true);
    {
        auto list = ic->inputPanel().candidateList();
        CHECK(list);
        if (!list || list->size() > 3) {
            FCITX_ERROR() << "inotify reload probe: list size="
                          << (list ? list->size() : -1) << " preedit=["
                          << ic->inputPanel().clientPreedit().toString()
                          << "]";
        }
        CHECK(list->size() <= 3);
        frontend->call<ITestFrontend::keyEvent>(uuid, Key("Escape"), false);
    }

    // ---- direct reload path (what the tray's ReloadAddonConfig reaches) ----
    {
        auto* addon = instance->addonManager().addon("zhiyi", true);
        CHECK(addon);
        std::ofstream out(config_dir + "/zhiyi/config.json", std::ios::trunc);
        out << "{\"page_size\": 7}";
        out.close();
        CHECK(out.good());
        addon->reloadConfig();
        type(instance, uuid, {"n"});
        auto list = ic->inputPanel().candidateList();
        CHECK(list);
        CHECK(list->size() <= 7);
        frontend->call<ITestFrontend::keyEvent>(uuid, Key("Escape"), false);
    }

    // ---- deactivate / reactivate with Ctrl+Space ----
    {
        const auto& group = instance->inputMethodManager().currentGroup();
        FCITX_ERROR() << "group probe: name=" << group.name()
                      << " items=" << group.inputMethodList().size()
                      << " entry_kb="
                      << (instance->inputMethodManager().entry("testkb") ? "yes"
                                                                         : "no")
                      << " entry_zhiyi="
                      << (instance->inputMethodManager().entry("zhiyi") ? "yes"
                                                                        : "no");
    }
    type(instance, uuid, {"n", "i"});
    CHECK(ic->inputPanel().candidateList());
    bool consumed =
        frontend->call<ITestFrontend::sendKeyEvent>(uuid, Key("Control+space"),
                                                    false);
    CHECK(consumed);  // the toggle hotkey is swallowed by the core
    CHECK(!ic->inputPanel().candidateList());
    consumed = frontend->call<ITestFrontend::sendKeyEvent>(uuid, Key("n"),
                                                           false);
    CHECK(!consumed);  // forwarded verbatim while deactivated
    CHECK(!ic->inputPanel().candidateList());
    frontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"), false);
    consumed = frontend->call<ITestFrontend::sendKeyEvent>(uuid, Key("n"),
                                                           false);
    CHECK(consumed);  // composing again after reactivation
    frontend->call<ITestFrontend::keyEvent>(uuid, Key("Escape"), false);

    frontend->call<ITestFrontend::destroyInputContext>(uuid);
    instance->exit();
}

int main() {
    // Hermetic per-user state: the engine reads ~/.config/zhiyi/config.json
    // and writes ~/.local/share/zhiyi/user.tsv via XDG env vars.
    std::string tmpl_str = "/tmp/zhiyi-fcitx5-test-XXXXXX";
    std::vector<char> tmpl(tmpl_str.begin(), tmpl_str.end());
    tmpl.push_back('\0');
    char* dir = mkdtemp(tmpl.data());
    CHECK(dir != nullptr);
    std::string config_home = std::string(dir) + "/config";
    std::string data_home = std::string(dir) + "/data";
    mkdir(config_home.c_str(), 0755);
    mkdir(data_home.c_str(), 0755);
    mkdir((config_home + "/zhiyi").c_str(), 0755);
    setenv("ZHIYI_CONFIG_DIR", config_home.c_str(), 1);
    setenv("XDG_CONFIG_HOME", config_home.c_str(), 1);
    setenv("XDG_DATA_HOME", data_home.c_str(), 1);
    // The addon is compiled with the install-time data dir; point it at the
    // freshly generated dictionaries in the build tree.
    setenv("ZHIYI_DATA_DIR", ZHIYI_TEST_DATA_DIR, 1);

    setupTestingEnvironment(
        ZHIYI_TEST_BUILD_DIR, {"fcitx5"},
        {"test/fcitx5", StandardPath::fcitxPath("pkgdatadir")});
    char arg0[] = "zhiyi-fcitx5-test";
    char arg1[] = "--disable=all";
    // testim provides the pass-through "testkb" entry so the group is a real
    // two-member list: while deactivated, keys are forwarded verbatim.
    char arg2[] = "--enable=testfrontend,zhiyi,testim";
    char* argv[] = {arg0, arg1, arg2};
    Instance instance(3, argv);
    instance.addonManager().registerDefaultLoader(nullptr);

    EventDispatcher dispatcher;
    dispatcher.attach(&instance.eventLoop());
    dispatcher.schedule([&instance]() {
        // Addon loading and IM group setup must happen once the instance is
        // running (see fcitx5-chinese-addons testpinyin.cpp).
        auto* frontend = instance.addonManager().addon("testfrontend", true);
        CHECK(frontend);
        auto group = instance.inputMethodManager().currentGroup();
        group.inputMethodList().clear();
        group.inputMethodList().push_back(InputMethodGroupItem("testkb"));
        group.inputMethodList().push_back(InputMethodGroupItem("zhiyi"));
        group.setDefaultInputMethod("");
        instance.inputMethodManager().setGroup(std::move(group));

        auto uuid =
            frontend->call<ITestFrontend::createInputContext>("testapp");
        auto* ic = instance.inputContextManager().findByUUID(uuid);
        CHECK(ic);
        // Activate zhiyi; fcitx5 core handles Ctrl+Space before the IM.
        frontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"),
                                                false);
        run_tests_part1(&instance, uuid, ic,
                        std::string(getenv("ZHIYI_CONFIG_DIR")));
    });
    instance.exec();
    return 0;
}
