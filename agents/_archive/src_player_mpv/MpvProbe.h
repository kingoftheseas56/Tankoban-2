#pragma once

// MpvProbe — Phase 1 smoke for MPV_RENDER_API_INTEGRATION_TODO.
// Verifies libmpv loads + initializes + the libmpv/MSVC CRT alloc/free
// boundary is safe. Wired via TANKOBAN_MPV_PROBE=1 env var at the top of
// main() (early-exits before QApplication construction).
//
// Deleted in Phase 3 once IPlayerBackend smoke tests subsume it.

namespace tankoban {

// Returns 0 on success, non-zero on failure (caller exits with this code).
int runMpvProbe();

} // namespace tankoban
