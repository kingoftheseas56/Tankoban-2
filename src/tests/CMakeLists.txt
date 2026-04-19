# ── Tankoban main-app unit tests ────────────────────────────────────────────
# Mirror of native_sidecar/tests/CMakeLists.txt pattern:
# - Monolithic test executable bundling test files + the source files under
#   test, NOT linked against the main Tankoban target (which is a Qt GUI exe
#   with no library split).
# - No FetchContent for gtest; relies on GTEST_ROOT / GTEST_INCLUDE_DIR set
#   by the top-level CMakeLists.txt (see its EOF block).
# - `qt_add_executable` (not `add_executable`) so AUTOMOC picks up Q_OBJECT
#   classes like StreamPieceWaiter. Inherited AUTOMOC from the parent project
#   means no extra moc wiring here.

set(TANKOBAN_TEST_SOURCES
    test_stream_piece_waiter.cpp
    test_stream_seek_classifier.cpp
    test_stream_prioritizer.cpp
    ../core/stream/StreamPieceWaiter.cpp
    ../core/stream/StreamSeekClassifier.cpp
    ../core/stream/StreamPrioritizer.cpp
)

qt_add_executable(tankoban_tests ${TANKOBAN_TEST_SOURCES})

target_include_directories(tankoban_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."   # matches Tankoban target's /src include
    "${GTEST_INCLUDE_DIR}"
)

# Only Qt6::Core — StreamPieceWaiter touches QObject + QMutex + QWaitCondition
# + QElapsedTimer + QCoreApplication + QDir + QFile + QDateTime + QThread, all
# in QtCore. Deliberately no Widgets/Network/OpenGL/WebEngine — keeps link time
# minimal and prevents GUI pollution in a headless test context.
target_link_libraries(tankoban_tests PRIVATE
    Qt6::Core
    "${GTEST_LIBRARY}"
    "${GTEST_MAIN_LIBRARY}"
)

add_test(NAME tankoban_tests COMMAND tankoban_tests)

# Suppress telemetry writes during ctest runs. Without these, StreamPieceWaiter
# would write into the build-tree `stream_telemetry.log` (per its
# QCoreApplication::applicationDirPath() fallback). The STREAM_PIECE_WAITER_POLL
# env var exercises the fallback poll branch — null-engine tests use the
# short-circuit at line 109, not the poll path, but setting both here keeps
# the env deterministic for future Stage-3 tests that DO exercise either.
set_tests_properties(tankoban_tests PROPERTIES
    ENVIRONMENT "TANKOBAN_STREAM_TELEMETRY=0;STREAM_PIECE_WAITER_POLL=1"
)
