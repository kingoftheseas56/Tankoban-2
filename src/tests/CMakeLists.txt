# ── Tankoban main-app unit tests ────────────────────────────────────────────
# Activated via -DTANKOBAN_BUILD_TESTS=ON at the top-level CMakeLists.txt.
# GoogleTest is provided via FetchContent (imported targets GTest::gtest +
# GTest::gtest_main), not via find_library — the MinGW .a files at
# C:/tools/googletest/lib/ cannot be linked by MSVC.
#
# STREAM_SERVER_PIVOT Phase 3 (2026-04-25) — `test_stream_seek_classifier.cpp`
# + `test_stream_prioritizer.cpp` deleted along with the legacy engine.
# AudiobookMetaCache test carries the target on its own; if it becomes the
# only entry, the tests target stays — we'd rather have one pure-logic test
# live than delete the whole apparatus.

set(TANKOBAN_TEST_SOURCES
    test_audiobook_meta_cache.cpp
    ../core/AudiobookMetaCache.cpp
)

qt_add_executable(tankoban_tests ${TANKOBAN_TEST_SOURCES})

target_include_directories(tankoban_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."   # matches Tankoban target's /src include
)

# Qt6::Core only — Prioritizer + SeekClassifier are pure C++ with a QList +
# QPair return type. No GUI, no widgets, no network. Deliberately narrow.
target_link_libraries(tankoban_tests PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME tankoban_tests COMMAND tankoban_tests)

# Suppress any stray telemetry writes during ctest runs.
set_tests_properties(tankoban_tests PROPERTIES
    ENVIRONMENT "TANKOBAN_STREAM_TELEMETRY=0"
)
