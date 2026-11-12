cmake_minimum_required(VERSION 3.15)
project(zhiyi-ime VERSION 0.1.5 LANGUAGES CXX)

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
if(NOT EXISTS "${FCITX4_INCLUDE_DIR}/fcitx/ime.h")
    message(FATAL_ERROR
        "Fcitx 4 headers not found. Run: bash scripts/fetch-sdk.sh")
endif()
if(NOT EXISTS "${FCITX4_LIBRARY_DIR}")
    set(FCITX4_LIBRARY_DIR "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}")
endif()

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

# ---- Tests ----
if(BUILD_TESTING)
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
endif()

# ---- Installation ----
install(TARGETS zhiyi-addon LIBRARY
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx")
install(FILES "${CMAKE_SOURCE_DIR}/data/fcitx-zhiyi.conf"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx/addon")
install(FILES "${CMAKE_SOURCE_DIR}/data/zhiyi.conf"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx/inputmethod")
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
install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/skins/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx/skin")
install(PROGRAMS "${CMAKE_SOURCE_DIR}/scripts/zhiyi-config"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
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
# script after install, when Installed-Size is known.
