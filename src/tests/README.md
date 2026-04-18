# src/tests — main-app unit tests

Pure-gtest harness mirroring [native_sidecar/tests/](../../native_sidecar/tests/) shape. Targets logic-level primitives that can be tested without a GUI event loop. Agent-invokable via `ctest` from the configured build tree.

## Running

```bash
cd out
ctest --output-on-failure -R tankoban_tests
```

Or invoke the binary directly:

```
./out/src/tests/tankoban_tests.exe
```

## Adding a test

1. Drop `test_<primitive>.cpp` next to this README.
2. Add it to `TANKOBAN_TEST_SOURCES` in [CMakeLists.txt](CMakeLists.txt), plus any `../path/to/Primitive.cpp` source files the test directly exercises.
3. If the primitive you test uses `QObject` / signals, no extra AUTOMOC wiring needed — the top-level [CMakeLists.txt](../../CMakeLists.txt) enables AUTOMOC globally.
4. Every test target that touches `QCoreApplication::applicationDirPath()` or Qt event-loop primitives needs a `QCoreApplication` in `main()`; see [test_stream_piece_waiter.cpp](test_stream_piece_waiter.cpp) for the pattern.

## What this harness deliberately does NOT provide

- **No FetchContent.** GoogleTest must be pre-installed at `GTEST_ROOT` (default `C:/tools/googletest`). Matches sidecar convention.
- **No mocking framework.** No gmock. If you need a mock, write a seam interface.
- **No Qt::Test dependency.** Pure gtest with a Qt bootstrap in `main()`.
- **No widget / network / libtorrent coverage.** Only QtCore-level primitives. Widget-level tests would need `QT_QPA_PLATFORM=offscreen` wiring (Stage 4 if ever justified).
- **No notification-path coverage for `StreamPieceWaiter`.** Stage 2 is null-engine short-circuit only; Stage 3 extends when another testable primitive lands.

## Relationship to `build_check.bat`

[build_check.bat](../../build_check.bat) verifies compile + link of the main `Tankoban` target only. It does NOT build or run `tankoban_tests`. If you want both, run:

```bash
./build_check.bat && cmake --build out --target tankoban_tests && (cd out && ctest --output-on-failure -R tankoban_tests)
```

A `--with-tests` flag may be added to `build_check.bat` later if usage justifies; not now (keep Stage 1 minimal).

## What `ctest` does NOT replace

This harness catches logic regressions in isolated primitives. It does NOT catch:

- GUI paint / layout bugs
- Real libtorrent swarm behavior
- GPU / D3D11 / sidecar IPC bugs
- Actual stream playback end-to-end

For those, run `build_and_run.bat` and smoke-test in the GUI. `ctest` supplements human smoke; it does not replace it.
