# Archive: VIDEO_PLAYER_FIX Phase 3 — Window utilities

**Agent:** 3 (Video Player)
**Reviewer:** Agent 6 (Objective Compliance Reviewer)
**Objective source (dual):**
- **Primary (plan):** `VIDEO_PLAYER_FIX_TODO.md:138-194` — Phase 3 batch list + exit criteria
- **Secondary (audit):** `agents/audits/video_player_2026-04-15.md` — audit P1 #3 (thin desktop window features)

**Outcome:** REVIEW PASSED 2026-04-16 first-read pass. 0 P0, 0 P1, 7 non-blocking P2. One optional clarification (edge case around Ctrl+T while in PiP).

---

## Scope

Three shipped batches closing audit P1 #3. Batch 3.1 always-on-top toggle with `QSettings` persistence. Batch 3.2 snapshot export — **scope enhanced beyond TODO's MVP** to include D3D11 staging-readback in addition to SHM, so snapshot works under zero-copy too. Batch 3.3 ships the TODO-sanctioned mini-mode fallback (frameless 320×180 always-on-top) instead of second-FrameCanvas.

---

## Parity (Present)

### Batch 3.1 — Always-on-top
- `m_alwaysOnTop` read from `QSettings("player/alwaysOnTop")` in constructor at VideoPlayer.cpp:183-184.
- `toggleAlwaysOnTop` applies to top-level `window()`, not `this` (child-widget gotcha documented).
- `setWindowFlag + show()` runtime-flag-change cycle matches Qt requirement.
- One-shot showEvent application via `static bool applied = false` guard.
- Context menu entry with checkable state at VideoContextMenu.cpp:100-106.
- Ctrl+T keybinding with plain-T conflict documented.

### Batch 3.2 — Snapshot export
- `FrameCanvas::captureCurrentFrame()` dual-path at FrameCanvas.cpp:1107-1192.
- **D3D11 staging-readback path** at :1113-1178: textbook pattern (GetDesc → CreateTexture2D(STAGING + CPU_READ) → CopyResource → Map(READ) → row-pitch-aware memcpy → Unmap → Release). Format guard rejects non-BGRA. Row-pitch correctness at :1167-1173.
- **SHM fallback** at :1180-1191 reads `readLatest()` + `QImage.copy()` to deep-copy out of the ring buffer.
- `takeSnapshot` at VideoPlayer.cpp:1346-1382 handles null frame, PicturesLocation setup, baseName fallback for stream URLs, `{baseName}_{HH-MM-SS}_{ptsSec}s.png` format.
- Context menu "Take Snapshot\tCtrl+S" at VideoContextMenu.cpp:108-114.
- Ctrl+S keybinding with plain-S conflict documented.
- Burned-in subtitles captured naturally (sidecar's `subtitle_renderer` blends before publishing).

### Batch 3.3 — Picture-in-Picture (mini-mode)
- Mini-mode fallback choice explicitly justified per TODO:189+:339+:391 sanctioned alternative + Agent 3's D3D11 second-render-surface uncertainty.
- Pre-PiP state capture (`m_prePipFullscreen`, `m_prePipGeometry`, `m_prePipFlags`) at VideoPlayer.cpp:1419-1423 BEFORE mutation.
- Fullscreen exit first (PiP + fullscreen don't compose).
- Window flag OR pattern (FramelessWindowHint | WindowStaysOnTopHint) preserves platform flags.
- Multi-monitor aware positioning via `top->screen()` + `availableGeometry()` + 24px margin.
- Exit restore reuses `toggleFullscreen()` for fullscreen bookkeeping.
- Escape handler preempts `back_to_library` at keyPressEvent:1767-1771.
- HUD hide/show via existing `hideControls/showControls`.
- Window drag via mousePressEvent+mouseMoveEvent (standard globalPos-frameGeometry pattern).
- Context menu label swap ("Picture-in-Picture" / "Exit Picture-in-Picture") at VideoContextMenu.cpp:119-122.
- Ctrl+P keybinding with plain-P conflict documented.

---

## Gaps

**P0:** none.
**P1:** none.
**P2** (all non-blocking):

1. READY FOR REVIEW line at chat.md:12904 says "D3D11 zero-copy readback" — understates scope (both D3D11 AND SHM paths shipped). Archive wording clarification only.
2. `static bool applied = false` in showEvent is file-scoped static. Moot while only one VideoPlayer exists; would need a member if multi-instance ever lands.
3. Batch 3.2 format guard rejects non-BGRA — HDR10 scRGB (`R16G16B16A16_FLOAT`) output from Player Polish Phase 3 Batch 3.5 would fail silently with "Snapshot failed — no frame available" toast. HDR snapshot support deferred scope.
4. `setWindowFlags + show()` PiP cycle triggers Qt window recreation — standard platform-level gotcha, no fix without different architecture.
5. Deferred items (auto-PiP-on-minimize, separate-window PiP, in-PiP overlay) are TODO:174 sanctioned. Not gaps.
6. PiP drag handler doesn't clamp to screen — Qt WM usually handles, Ctrl+P exit restores geometry so recoverable.
7. Edge case: user toggles always-on-top via Ctrl+T WHILE in PiP — does `m_prePipFlags` update correctly for exit restore? 5-min smoke test settles; one-line fix if broken.

---

## Agent 6 verdict

- [x] All P0 closed (n/a — none raised)
- [x] All P1 closed or justified (n/a — none raised)
- [x] Ready for commit (Rule 11)

**REVIEW PASSED — [Agent 3, VIDEO_PLAYER_FIX Phase 3]** 2026-04-16 first-read pass.

Audit P1 #3 closed. Mini-mode PiP is TODO-sanctioned; Batch 3.2's beyond-MVP D3D11 staging readback is a positive scope enhancement. Phase 1 remains pending (separate subtitle-Off scope); Phase 2 in-progress by Agent 3.
