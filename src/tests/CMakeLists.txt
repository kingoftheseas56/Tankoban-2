# ── Tankoban main-app unit tests ────────────────────────────────────────────
# Activated via -DTANKOBAN_BUILD_TESTS=ON at the top-level CMakeLists.txt.
# GoogleTest is provided via FetchContent (imported targets GTest::gtest +
# GTest::gtest_main), not via find_library — the MinGW .a files at
# C:/tools/googletest/lib/ cannot be linked by MSVC.
#
# Scope constraint: test sources are restricted to files with NO dependency
# on TorrentEngine / libtorrent / Qt widgets / D3D11. That's Stage 3a shape:
# StreamPrioritizer (pure functions) + StreamSeekClassifier (pure functions).
# StreamPieceWaiter testing was in Stage 2 but its #connect(engine,
# pieceFinished, ...) call pulls TorrentEngine symbols that the linker
# can't resolve without either a TorrentEngine stub layer or a libtorrent-
# capable test build — that's Stage 3b, deferred.

set(TANKOBAN_TEST_SOURCES
    test_stream_seek_classifier.cpp
    test_stream_prioritizer.cpp
    test_audiobook_meta_cache.cpp
    ../core/stream/StreamSeekClassifier.cpp
    ../core/stream/StreamPrioritizer.cpp
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
