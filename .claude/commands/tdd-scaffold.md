---
description: Scaffold a pure-logic GoogleTest file (Codex #4 Stage 3a pattern). Use when authoring tests for a new logic primitive.
---

You are scaffolding a pure-logic GoogleTest file for Tankoban 2.

**Arguments:**
- `<class-name>` — required, target class to test (e.g. `StreamPackParser`)
- `<domain>` — optional, subsystem dir under `tests/` (default: `core/stream`)

**Output location:** `tests/<domain>/test_<class_name_snake>.cpp`

**Per-agent shortlist:** Agent 4 / 4B (Stream + Sources domains have the most pure-logic primitives suitable for this pattern; Codex #4 Stage 3a originator).

**Procedure:**

1. **Compute snake_case from class name:** `StreamPackParser` → `stream_pack_parser`. Hold consecutive caps together (`HTTPClient` → `http_client`).

2. **Verify the source class exists.** Grep `src/` for `class <ClassName>` to find the header. If not found, abort with `Class not found: <ClassName>. Provide a class that exists under src/.`.

3. **Construct the test file path** and check for collision. If the test file already exists, abort with `Test file already exists at <path> — use Edit to update, not /tdd-scaffold to create.`.

4. **Scaffold the test file:**

```cpp
// Test file for <ClassName> — pure-logic primitives, frozen fixtures.
// Per Codex #4 Stage 3a pattern. Linked into tankoban_tests target via CMakeLists.txt.

#include "gtest/gtest.h"
#include "<path/to/ClassName.h>"

namespace {

// ---- Frozen fixtures ----
// Per project pattern: load fixture data once, deterministic across runs.
// See tests/core/stream/test_stream_pack_parser.cpp for the canonical example.

constexpr const char* kFixtureMinimal = R"(
<!-- paste a stable fixture body here -->
)";

// ---- Test cases ----

TEST(<ClassName>Test, ConstructorIsDefaulted) {
    <ClassName> instance;
    SUCCEED();
}

TEST(<ClassName>Test, MinimalInputReturnsExpectedOutput) {
    // Arrange
    <ClassName> instance;
    // Act
    auto result = instance.<methodName>(<minimalInput>);
    // Assert
    EXPECT_EQ(result.<field>, <expected>);
}

// Add more cases below. Pattern:
//   TEST(<ClassName>Test, BehaviorDescription) {
//       Arrange + Act + Assert
//   }

}  // namespace
```

5. **Update `CMakeLists.txt`** — add the test file to the `tankoban_tests` target. Find the existing test sources block (grep for `test_stream_pack_parser.cpp` or similar) and append your new file in alphabetical order.

6. **Print build + test commands** for the agent to verify:

```
Test file scaffolded at tests/<domain>/test_<class_name_snake>.cpp.

To build + run:
  cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON
  cmake --build out --target tankoban_tests
  cd out && ctest --output-on-failure -R <ClassName>Test
```

**Quality gates:**
- Source class header is verified to exist
- Snake_case conversion is correct (consecutive caps handled)
- CMakeLists.txt entry preserves alphabetical order in the test sources block
- Initial test scaffolds 1 trivial-passing + 1 minimal-input test (red-green-refactor warmup)
- No tabs in C++ file (4-space indent matches project convention)
- `namespace { ... }` anonymous-namespace wrapper (project pattern)

**Examples:**

For `/tdd-scaffold StreamLibrary core/stream`:
- Creates `tests/core/stream/test_stream_library.cpp`
- Adds entry to CMakeLists.txt test sources block alphabetically
- Prints build + test invocation
