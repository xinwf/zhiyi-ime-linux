cmake_minimum_required(VERSION 3.15)
project(zhiyi-ime VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)
include(CTest)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# The Fcitx 4 SDK is unpacked into .deps/sysroot by scripts/fetch-sdk.sh so a
# build needs no root privileges. On a machine with libfcitx-dev installed
# system-wide, point ZHIYI_FCITX_SYSROOT at / instead.
set(ZHIYI_FCITX_SYSROOT "${CMAKE_SOURCE_DIR}/.deps/sysroot" CACHE PATH
    "Root containing usr/include/fcitx and the Fcitx 4 link stubs")
set(FCITX4_INCLUDE_DIR "${ZHIYI_FCITX_SYSROOT}/usr/include")
set(FCITX4_LIBRARY_DIR "${ZHIYI_FCITX_SYSROOT}/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}")

# ---- Framework selection ----------------------------------------------------
# The project ships the same engine for both input method frameworks. By
# default (AUTO) every addon is built for which the host provides a usable
# SDK: an addon is only built when its framework can be found, so an fcitx5
# machine builds the fcitx5 addon and an fcitx4 machine keeps building the
# fcitx4 one. Force a side with ON/OFF to override detection.
set(ZHIYI_ENABLE_FCITX4 "AUTO" CACHE STRING "Build the Fcitx 4 addon: AUTO/ON/OFF")
set(ZHIYI_ENABLE_FCITX5 "AUTO" CACHE STRING "Build the Fcitx 5 addon: AUTO/ON/OFF")
set_property(CACHE ZHIYI_ENABLE_FCITX4 PROPERTY STRINGS AUTO ON OFF)
set_property(CACHE ZHIYI_ENABLE_FCITX5 PROPERTY STRINGS AUTO ON OFF)

# Fcitx 4 detection: headers in the fetched sysroot or on the system.
set(ZHIYI_FCITX4_FOUND FALSE)
if(EXISTS "${FCITX4_INCLUDE_DIR}/fcitx/ime.h")
    set(ZHIYI_FCITX4_FOUND TRUE)
elseif(EXISTS "/usr/include/fcitx/ime.h")
    set(ZHIYI_FCITX4_FOUND TRUE)
    set(FCITX4_INCLUDE_DIR "/usr/include")
    if(NOT EXISTS "${FCITX4_LIBRARY_DIR}")
        set(FCITX4_LIBRARY_DIR "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}")
    endif()
endif()

# Fcitx 5 detection: distro dev packages (libfcitx5core-dev) or the same
# fetched sysroot. Old fcitx5 snapshots without CMake package support are
# treated as "not available" so AUTO keeps the Fcitx 4 build on such hosts.
set(ZHIYI_FCITX5_SYSROOT "${CMAKE_SOURCE_DIR}/.deps/sysroot" CACHE PATH
    "Root containing usr/include/Fcitx5 (scripts/fetch-sdk.sh)")
if(EXISTS "${ZHIYI_FCITX5_SYSROOT}/usr")
    # Make any CMake packages unpacked by fetch-sdk.sh (e.g. Fcitx5Core /
    # Fcitx5Module on hosts without the distro dev packages) discoverable.
    list(APPEND CMAKE_PREFIX_PATH "${ZHIYI_FCITX5_SYSROOT}/usr")
endif()
find_package(Fcitx5Core QUIET)
set(ZHIYI_FCITX5_FOUND ${Fcitx5Core_FOUND})
# Optional: fcitx5's testfrontend module enables the in-process end-to-end
# test of the fcitx5 addon (tools/zhiyi_fcitx5_test.cpp).
find_package(Fcitx5Module QUIET COMPONENTS TestFrontend)

macro(zhiyi_resolve_framework OPTION FOUND ERROR_HINT)
    if("${${OPTION}}" STREQUAL "AUTO")
        if(${FOUND})
            set(${OPTION} ON)
        else()
            set(${OPTION} OFF)
        endif()
    elseif(${OPTION} AND NOT ${FOUND})
        message(FATAL_ERROR "${ERROR_HINT}")
    endif()
endmacro()

zhiyi_resolve_framework(ZHIYI_ENABLE_FCITX4 ZHIYI_FCITX4_FOUND
    "ZHIYI_ENABLE_FCITX4=ON but Fcitx 4 headers not found. Run: bash scripts/fetch-sdk.sh")
zhiyi_resolve_framework(ZHIYI_ENABLE_FCITX5 ZHIYI_FCITX5_FOUND
    "ZHIYI_ENABLE_FCITX5=ON but Fcitx5Core was not found. Install libfcitx5core-dev or run: bash scripts/fetch-sdk.sh")
if(NOT ZHIYI_ENABLE_FCITX4 AND NOT ZHIYI_ENABLE_FCITX5)
    message(FATAL_ERROR
        "Neither Fcitx 4 nor Fcitx 5 SDK was found.\n"
        "  Fcitx 4: bash scripts/fetch-sdk.sh (or install fcitx-libs-dev)\n"
        "  Fcitx 5: install libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev")
endif()
message(STATUS "Zhiyi IME front ends: Fcitx4=${ZHIYI_ENABLE_FCITX4} Fcitx5=${ZHIYI_ENABLE_FCITX5}")

# ---- Runtime dictionaries, generated from the upstream SQLite word list ----
set(ZHIYI_GENERATED_DATA "${CMAKE_BINARY_DIR}/data")
set(ZHIYI_DICT_FILES
    "${ZHIYI_GENERATED_DATA}/zhiyi.dict.bin"
    "${ZHIYI_GENERATED_DATA}/zhiyi.initials.bin"
    "${ZHIYI_GENERATED_DATA}/zhiyi.syllables"
)
add_custom_command(
    OUTPUT ${ZHIYI_DICT_FILES}
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/scripts/build_dictionary.py"
            --source "${CMAKE_SOURCE_DIR}/vendor/data/pinyin.dict.db.zip"
            --output-dir "${ZHIYI_GENERATED_DATA}"
            --quiet
    DEPENDS "${CMAKE_SOURCE_DIR}/scripts/build_dictionary.py"
            "${CMAKE_SOURCE_DIR}/vendor/data/pinyin.dict.db.zip"
    COMMENT "Generating Linux runtime dictionaries from the upstream lexicon"
    VERBATIM
)
add_custom_target(zhiyi-dictionaries ALL DEPENDS ${ZHIYI_DICT_FILES})

# ---- Framework-independent engine ----
add_library(zhiyi-core STATIC
    src/core/config.cpp
    src/core/dictionary.cpp
    src/core/pinyin.cpp
    src/core/engine.cpp
)
set_target_properties(zhiyi-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(zhiyi-core PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/vendor/include"
)
target_compile_options(zhiyi-core PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ---- Headless verification tool ----
add_executable(zhiyi-query tools/query.cpp)
target_link_libraries(zhiyi-query PRIVATE zhiyi-core)
add_dependencies(zhiyi-query zhiyi-dictionaries)

# ---- Fcitx 4 input method addon ----
if(ZHIYI_ENABLE_FCITX4)
    add_library(zhiyi-addon MODULE src/fcitx/zhiyi_addon.cpp)
    set_target_properties(zhiyi-addon PROPERTIES
        PREFIX ""
        OUTPUT_NAME "fcitx-zhiyi"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/addon"
    )
    target_include_directories(zhiyi-addon SYSTEM PRIVATE
        "${FCITX4_INCLUDE_DIR}"
        "${CMAKE_SOURCE_DIR}/vendor/include"
    )
    target_include_directories(zhiyi-addon PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(zhiyi-addon PRIVATE
        ZHIYI_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/zhiyi/data"
    )
    target_link_directories(zhiyi-addon PRIVATE "${FCITX4_LIBRARY_DIR}")
    target_link_libraries(zhiyi-addon PRIVATE zhiyi-core fcitx-core fcitx-config fcitx-utils)
    target_compile_options(zhiyi-addon PRIVATE -Wall -Wextra -Wno-unused-parameter
        -fvisibility=hidden)
    add_dependencies(zhiyi-addon zhiyi-dictionaries)
endif()

# ---- Fcitx 5 input method addon ----
if(ZHIYI_ENABLE_FCITX5)
    add_library(zhiyi-addon-fcitx5 MODULE src/fcitx5/zhiyi_addon.cpp)
    set_target_properties(zhiyi-addon-fcitx5 PROPERTIES
        PREFIX ""
        OUTPUT_NAME "libzhiyi"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fcitx5"
    )
    target_include_directories(zhiyi-addon-fcitx5 PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(zhiyi-addon-fcitx5 PRIVATE
        ZHIYI_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/zhiyi/data"
    )
    target_link_libraries(zhiyi-addon-fcitx5 PRIVATE Fcitx5::Core zhiyi-core)
    target_compile_options(zhiyi-addon-fcitx5 PRIVATE -Wall -Wextra
        -Wno-unused-parameter -fvisibility=hidden)
    add_dependencies(zhiyi-addon-fcitx5 zhiyi-dictionaries)
endif()

# ---- Tests ----
if(BUILD_TESTING)
    if(ZHIYI_ENABLE_FCITX4)
        # ABI contract test: FcitxCandidateWordFree frees strWord/strExtra/priv.
        add_executable(zhiyi-candidate-abi-test tools/candidate_abi_test.cpp)
        target_include_directories(zhiyi-candidate-abi-test SYSTEM PRIVATE
            "${FCITX4_INCLUDE_DIR}")
        target_include_directories(zhiyi-candidate-abi-test PRIVATE
            "${CMAKE_SOURCE_DIR}/src/fcitx")
        target_link_directories(zhiyi-candidate-abi-test PRIVATE
            "${FCITX4_LIBRARY_DIR}")
        target_link_libraries(zhiyi-candidate-abi-test PRIVATE
            fcitx-core fcitx-config fcitx-utils)
        add_test(NAME fcitx_candidate_abi COMMAND zhiyi-candidate-abi-test)
        # On hosts without a system-wide fcitx4 runtime (link-and-test against
        # the fetched sysroot), libfcitx-core's own dependencies must be
        # resolvable too; RUNPATH does not apply to transitive lookups.
        if(NOT EXISTS "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/libfcitx-core.so.0")
            set_tests_properties(fcitx_candidate_abi PROPERTIES ENVIRONMENT
                "LD_LIBRARY_PATH=${FCITX4_LIBRARY_DIR}")
        endif()
    endif()

    add_test(NAME dictionary_pinyin
        COMMAND zhiyi-query --data "${ZHIYI_GENERATED_DATA}"
                --user "${CMAKE_BINARY_DIR}/test-user"
                --keys nihao --expect "你好")
    add_test(NAME dictionary_sentence
        COMMAND zhiyi-query --data "${ZHIYI_GENERATED_DATA}"
                --user "${CMAKE_BINARY_DIR}/test-user"
                --keys woaini --expect "我爱你")
    add_test(NAME dictionary_initials
        COMMAND zhiyi-query --data "${ZHIYI_GENERATED_DATA}"
                --user "${CMAKE_BINARY_DIR}/test-user"
                --keys nh --expect "你好")
    add_test(NAME backspace_and_commit
        COMMAND zhiyi-query --data "${ZHIYI_GENERATED_DATA}"
                --user "${CMAKE_BINARY_DIR}/test-user"
                --keys "nihao\\B\\B\\S" --expect "你好")
    add_test(NAME commit_first_candidate
        COMMAND zhiyi-query --data "${ZHIYI_GENERATED_DATA}"
                --user "${CMAKE_BINARY_DIR}/test-user"
                --keys "nihao\\S" --expect-commit "你好")
    add_test(NAME chinese_punctuation
        COMMAND zhiyi-query --data "${ZHIYI_GENERATED_DATA}"
                --user "${CMAKE_BINARY_DIR}/test-user"
                --keys "," --expect-commit "，")
    add_test(NAME select_by_digit
        COMMAND zhiyi-query --data "${ZHIYI_GENERATED_DATA}"
                --user "${CMAKE_BINARY_DIR}/test-user"
                --keys "nihao1" --expect-commit "你好")
    # English mode must emit verbatim ASCII: fcitx's global fcitx-punc module
    # otherwise converts ',' to '，' regardless of the IME's English state.
    add_test(NAME english_mode_punctuation
        COMMAND zhiyi-query --data "${ZHIYI_GENERATED_DATA}"
                --user "${CMAKE_BINARY_DIR}/test-user"
                --keys "\\T," --expect-commit ",")
    add_test(NAME english_mode_ascii
        COMMAND zhiyi-query --data "${ZHIYI_GENERATED_DATA}"
                --user "${CMAKE_BINARY_DIR}/test-user"
                --keys "\\Tabc.1" --expect-commit "abc.1")
    add_test(NAME punctuation_round_trip
        COMMAND zhiyi-query --data "${ZHIYI_GENERATED_DATA}"
                --user "${CMAKE_BINARY_DIR}/test-user"
                --keys "\\T,\\T," --expect-commit ",，")

    if(ZHIYI_ENABLE_FCITX5 AND TARGET Fcitx5::Module::TestFrontend)
        # End-to-end test of the real fcitx5 addon .so inside an in-process
        # fcitx5 Instance, driving keys through fcitx5's own testfrontend
        # addon and asserting commits / panel contents (same approach as
        # fcitx5-chinese-addons' testpinyin).
        file(MAKE_DIRECTORY
            "${CMAKE_BINARY_DIR}/test/fcitx5/addon"
            "${CMAKE_BINARY_DIR}/test/fcitx5/inputmethod")
        configure_file("${CMAKE_SOURCE_DIR}/data/fcitx5-addon-zhiyi.conf"
            "${CMAKE_BINARY_DIR}/test/fcitx5/addon/zhiyi.conf" COPYONLY)
        configure_file("${CMAKE_SOURCE_DIR}/data/fcitx5-im-zhiyi.conf"
            "${CMAKE_BINARY_DIR}/test/fcitx5/inputmethod/zhiyi.conf" COPYONLY)
        file(WRITE "${CMAKE_BINARY_DIR}/test/fcitx5/addon/testfrontend.conf"
            "[Addon]\nName=testfrontend\nType=SharedLibrary\n"
            "Library=libtestfrontend\nCategory=Frontend\n")
        # A pass-through "keyboard slot" entry backed by fcitx5's testim addon
        # (system libtestim.so): makes the group a real two-member list so
        # the core trigger keys can deactivate zhiyi.
        file(WRITE "${CMAKE_BINARY_DIR}/test/fcitx5/inputmethod/testkb.conf"
            "[InputMethod]\nName=TestKB\nIcon=fcitx\nLabel=T\nLangCode=en\n"
            "Addon=testim\n")
        add_executable(zhiyi-fcitx5-test tools/zhiyi_fcitx5_test.cpp)
        target_link_libraries(zhiyi-fcitx5-test PRIVATE
            Fcitx5::Core Fcitx5::Module::TestFrontend)
        target_include_directories(zhiyi-fcitx5-test PRIVATE
            "${CMAKE_SOURCE_DIR}/src")
        target_compile_definitions(zhiyi-fcitx5-test PRIVATE
            ZHIYI_TEST_DATA_DIR="${CMAKE_BINARY_DIR}/data"
            ZHIYI_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}")
        add_dependencies(zhiyi-fcitx5-test zhiyi-dictionaries zhiyi-addon-fcitx5)
        add_test(NAME fcitx5_end_to_end COMMAND zhiyi-fcitx5-test)
    endif()
endif()

# ---- Installation ----
if(ZHIYI_ENABLE_FCITX4)
    install(TARGETS zhiyi-addon LIBRARY
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx")
    install(FILES "${CMAKE_SOURCE_DIR}/data/fcitx-zhiyi.conf"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx/addon")
    install(FILES "${CMAKE_SOURCE_DIR}/data/zhiyi.conf"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx/inputmethod")
endif()
if(ZHIYI_ENABLE_FCITX5)
    # FCITX_INSTALL_ADDONDIR is exported by Fcitx5Core's CMake package
    # (/usr/lib/<triplet>/fcitx5); the addon descriptor and the input method
    # registration follow fcitx5's pkgdata layout.
    install(TARGETS zhiyi-addon-fcitx5 LIBRARY
            DESTINATION "${FCITX_INSTALL_ADDONDIR}")
    install(FILES "${CMAKE_SOURCE_DIR}/data/fcitx5-addon-zhiyi.conf"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon"
            RENAME "zhiyi.conf")
    install(FILES "${CMAKE_SOURCE_DIR}/data/fcitx5-im-zhiyi.conf"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod"
            RENAME "zhiyi.conf")
    install(FILES "${CMAKE_SOURCE_DIR}/data/icons/zhiyi-tray-cn.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/24x24/apps"
            RENAME "fcitx-zhiyi.png")
    # Classic-ui themes generated from the Qingjian skins by
    # scripts/generate_fcitx5_themes.py (committed, no Pillow needed).
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/fcitx5-themes/"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/themes")
endif()
install(FILES ${ZHIYI_DICT_FILES}
        DESTINATION "${CMAKE_INSTALL_DATADIR}/zhiyi/data")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/vendor/schemas/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/zhiyi/data/schemas"
        FILES_MATCHING PATTERN "*.json")
install(FILES "${CMAKE_SOURCE_DIR}/vendor/data/default.json"
              "${CMAKE_SOURCE_DIR}/vendor/data/punctuation.json"
              "${CMAKE_SOURCE_DIR}/vendor/data/themes.json"
              "${CMAKE_SOURCE_DIR}/vendor/data/english.words.tsv"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/zhiyi/data")
# Fcitx4 classic-ui skins ported from the Qingjian project (GPL-3.0-or-later):
# /usr/share/fcitx/skin/<id>/fcitx_skin.conf + PNGs, selectable in fcitx
# appearance settings. Generated by scripts/generate_skin.py, committed so the
# build does not need Pillow.
if(ZHIYI_ENABLE_FCITX4)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/skins/"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx/skin")
endif()
install(PROGRAMS "${CMAKE_SOURCE_DIR}/scripts/zhiyi-config"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
# Tray settings daemon (AppIndicator): icon shows only while zhiyi is the
# active input method; the menu exposes every config key plus skin switching.
# Works with both fcitx4 and fcitx5 (auto-detected via dbus at startup).
install(PROGRAMS "${CMAKE_SOURCE_DIR}/scripts/zhiyi-tray"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${CMAKE_SOURCE_DIR}/data/zhiyi-tray.desktop"
        DESTINATION "/etc/xdg/autostart")
install(FILES "${CMAKE_SOURCE_DIR}/data/icons/zhiyi-tray-cn.png"
              "${CMAKE_SOURCE_DIR}/data/icons/zhiyi-tray-en.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/zhiyi/icons")
install(FILES "${CMAKE_SOURCE_DIR}/README.md"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}")
# GPL-3.0 project: ship the license, notices and the upstream dictionary
# licenses (rime-ice GPL-3.0, Apache-2.0, LGPL, ...) with the binary package.
install(FILES "${CMAKE_SOURCE_DIR}/vendor/licenses/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}" RENAME copyright)
install(FILES "${CMAKE_SOURCE_DIR}/vendor/licenses/NOTICE"
              "${CMAKE_SOURCE_DIR}/vendor/licenses/THIRD_PARTY_NOTICES.txt"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/vendor/licenses/data/"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}/licenses")

# ---- Debian package ----
# Packaging is done by scripts/build-deb.sh via `cmake --install` into a
# staging tree followed by `dpkg-deb -b -Zgzip -z1`. CPack's default xz
# compression takes a very long time on the ~46 MiB binary dictionaries, so
# it is intentionally not used here. packaging/control.in is expanded by that
# script after install, when Installed-Size and the framework dependencies
# (whatever front ends were built) are known.
