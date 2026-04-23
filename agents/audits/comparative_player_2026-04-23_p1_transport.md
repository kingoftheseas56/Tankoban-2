# Comparative Player Audit - 2026-04-23 - Phase 1 Transport Re-run

By Agent 7 (Codex). For Agent 3 (Video Player).

Scope: Phase 1 only from `agents/audits/REQUEST_p1_rerun_2026-04-23.md`. This was a measurement-only Trigger D wake. No `src/` files were edited. Tankoban was measured against VLC, mpv, and PotPlayer on the current repo HEAD. Reference-player source cites are local-first.

## Executive Summary

- Verdict counts for this re-run: 5 CONVERGED, 2 DIVERGED-intentional, 4 DIVERGED, 0 WORSE, 2 BETTER, 9 DEFERRED.
- This re-run supersedes the 2026-04-20 Phase 1 pilot for current-state Phase 1 conclusions.
- Delta versus the 2026-04-20 pilot:
  - Cold-open is now log-backed on Tankoban instead of proxy-only. Three clean F1 re-opens landed at `wall_clock_delta_from_open_ms` 248 / 224 / 220, median 224 ms (`_player_debug.txt:39400`, `40217`, `40700`).
  - The pilot's "no keybind requiring fix" conclusion is stale. This re-run found three real keybind issues on Windows-layout input: `?`, `<`, and `>` all arrive with `Shift` and resolve to no action (`_player_debug.txt:40305-40306`, `40761-40762`, `40798-40799`).
  - The pilot's old speed-preset note is stale. Current Tankoban presets are `0.5 / 0.75 / 1.0 / 1.25 / 1.5 / 1.75 / 2.0` (`src/ui/player/VideoPlayer.cpp:141-143`), not the older `0.25`-inclusive set described in the pilot.
  - FC-5 bottom-chop did not repro on this wake's true 1920x1080 screen capture (`out/f1_fullscreen_fc5_1920x1080.png`) and the geometry logs stayed pixel-perfect in fullscreen (`_player_debug.txt:41349-41354`).
- Highest-signal findings:
  - Tankoban remains BETTER on loading-stage granularity and the explicit 30s "Taking longer than expected" watchdog.
  - Tankoban is CONVERGED on basic transport actions that were actually exercised live: pause/resume, mute, right-arrow seek, fullscreen entry geometry, and Backspace exit/back.
  - The main new fix-candidate class is keybinding hygiene, not render geometry.

## Section 1 - Reference-player version pin

- Tankoban: `0ce7ccc5adf28b795ba7896d22848f47f39f6952`
- VLC: `3.0.23`
  - Note: `vlc.exe --version` did not return a usable first-line string in this shell, so the PE file version was used instead.
- mpv: `mpv v0.41.0-461-gd20d108d9 Copyright (C) 2000-2026 mpv/MPlayer/mplayer2 projects`
- PotPlayer: `FileVersion 0, 0, 0, 0`
  - Note: PotPlayer's PE metadata is still incomplete here; this matches the request's warning that file metadata may be unreliable.

## Section 2 - Tankoban surface enumeration (scope fence)

- Batch 1.1: cold-open timing, LoadingOverlay stages, timeout watchdog
- Batch 1.2: play/pause, mute, speed surface, volume semantics, subtitle-delay controls
- Batch 1.3: arrow seek, click-to-seek, frame-step, chapter-nav
- Batch 1.4: HUD timeout, filename elision, time format, seek tooltip
- Batch 1.5: fullscreen geometry, FC-1 aspect preset, FC-2 aspect reset, autocrop, FC-5 screen-edge check
- Batch 1.6: keybind convergence and static/default-key audit
- Batch 1.7: library-integrated shell (`Esc`, `Backspace`, `?`)

## Section 3 - Batch 1.1 Cold-open

- Reference-player 3-run window-ready medians on F1:
  - VLC: 220 / 278 / 3917 ms, median 278 ms
  - mpv: 219 / 246 / 439 ms, median 246 ms
  - PotPlayer: 1873 / 1979 / 1997 ms, median 1979 ms
- Tankoban F1 re-open measurements:
  - `wall_clock_delta_from_open_ms` = 248 / 224 / 220 ms across three clean re-opens (`_player_debug.txt:39400`, `40217`, `40700`), median 224 ms
  - The internal `open -> first_frame` wire-up is visible in the current run:
    - `SEND open` at `_player_debug.txt:40677`
    - `RECV first_frame` at `_player_debug.txt:40698`
    - `onFirstFrame ... wall_clock_delta_from_open_ms:220` at `_player_debug.txt:40700`
- LoadingOverlay verdict:
  - BETTER. Tankoban still exposes stage-specific text for `Probing`, `OpeningDecoder`, `DecodingFirstFrame`, `Buffering`, and `Taking longer than expected` (`src/ui/player/LoadingOverlay.cpp:222-280`).
  - Evidence that the overlay was active and dismissed on first frame is in `_player_debug.txt:40698-40700`.
- Timeout watchdog verdict:
  - BETTER. Code-backed re-check only. `LoadingOverlay.cpp:280` still carries `Taking longer than expected - close to retry`.
- Notes:
  - The Tankoban numbers above are internal open-path measurements, not desktop-launch window creation. They are higher-signal than the pilot's click-proxy estimate, but not perfectly apples-to-apples with the launch-per-file reference harness.

## Section 4 - Batch 1.2 Core playback

- Play / pause:
  - CONVERGED. Live Space key presses resolved and emitted pause/resume IPC immediately:
    - pause: `_player_debug.txt:39618-39619`
    - resume: `_player_debug.txt:39696-39697`
- Mute:
  - CONVERGED. Live `M` press resolved and emitted `set_mute true` immediately (`_player_debug.txt:39743-39744`).
- Volume range:
  - DIVERGED-intentional. Tankoban is still `0..200` with an amp zone above 100 (`src/ui/player/VideoPlayer.cpp:2751-2761`).
- Speed surface:
  - DEFERRED for live menu parity across all four players.
  - Current Tankoban source state is:
    - presets `0.5 / 0.75 / 1.0 / 1.25 / 1.5 / 1.75 / 2.0` (`src/ui/player/VideoPlayer.cpp:141-143`)
    - chip menu at `src/ui/player/VideoPlayer.cpp:1591-1625`
    - runtime helpers at `src/ui/player/VideoPlayer.cpp:2254-2275`
- Subtitle-delay hotkeys:
  - DIVERGED. Live `Shift+,` and `Shift+.` produced `key=0x3c` and `key=0x3e` with `mods=0x2000000` and resolved to no action:
    - `_player_debug.txt:40761-40762`
    - `_player_debug.txt:40798-40799`
  - Tankoban currently binds `Qt::Key_Less` and `Qt::Key_Greater` with `Qt::NoModifier` (`src/ui/player/KeyBindings.cpp:48-49`), while the actual step implementation exists and is healthy in `VideoPlayer.cpp:1914-1919` and `3280-3283`.

## Section 5 - Batch 1.3 Seek behavior

- Right-arrow seek:
  - CONVERGED. Live Right-arrow resolved to `seek_fwd_10s` and emitted a seek IPC immediately (`_player_debug.txt:39956-39957`).
- Click-to-seek at 50 percent:
  - DEFERRED. MCP UI tree never exposed the player seek bar as an actionable element in this wake, and the visual HUD did not stabilize in screenshots strongly enough for a reliable midpoint click measurement.
- Exact seek parity on a short non-keyframe seek:
  - DEFERRED.
- Frame-step:
  - DEFERRED. No reliable punctuation-key synthesis path was available in this wake without conflating it with the shifted-punctuation keybinding issue.
- Chapter navigation:
  - DIVERGED by static/default mapping. Tankoban binds `PageDown = chapter_next` and `PageUp = chapter_prev` (`src/ui/player/KeyBindings.cpp:56-57`).
  - mpv's stock map is the opposite: `PGUP add chapter 1` and `PGDWN add chapter -1` (`C:/Users/Suprabha/Downloads/Video player reference/mpv-master/mpv-master/etc/input.conf:75-76`).
  - Live content jump was not measurable on F1 because F1 is not a chaptered fixture.
- Buffered-range gray bar:
  - DEFERRED / Tankoban-exclusive. No cross-player verdict.

## Section 6 - Batch 1.4 HUD / overlay

- Auto-hide timeout:
  - DEFERRED. The HUD controls did not become reliably inspectable in current MCP snapshots even while the player responded to keyboard actions.
- Filename elision:
  - DEFERRED. F2 was not reachable from the indexed Tankoban library in this wake, so the long-filename Tankoban side of the comparison could not be exercised honestly.
- Time format:
  - DEFERRED for live re-verification. The pilot's adaptive-format conclusion remains plausible but was not re-proven in this wake.
- Hover tooltip content:
  - DEFERRED for live re-verification.

## Section 7 - Batch 1.5 Fullscreen + aspect

- Fullscreen entry geometry:
  - CONVERGED. Live `F` press resolved and produced pixel-perfect fullscreen geometry:
    - keypress at `_player_debug.txt:41349`
    - aspect/scissor at `_player_debug.txt:41350-41351`
    - `set_canvas_size 1920x1080` at `_player_debug.txt:41353-41354`
- Backspace out-of-fullscreen/back:
  - CONVERGED for Tankoban's intended shell path. Live Backspace resolved to `back_fullscreen` in this wake:
    - `_player_debug.txt:40137-40143`
    - `_player_debug.txt:41194`
- Autocrop on F1:
  - CONVERGED / no false positive. On F1 the current autocrop path stayed latched with `applied_any=0` and `applied_top=0`:
    - `_player_debug.txt:41269-41273`
- FC-5 bottom-chop side-axis:
  - NOT REPRODUCED in this wake.
  - Full-res evidence: `out/f1_fullscreen_fc5_1920x1080.png`
  - Geometry logs stayed pixel-perfect in fullscreen (`_player_debug.txt:41349-41354`).
- FC-1 2.39 preset live re-check:
  - DEFERRED live. Code-backed only:
    - menu entries exist at `src/ui/player/VideoContextMenu.cpp:102-103`
    - token parsing exists at `src/ui/player/VideoPlayer.cpp:3086-3087`
- FC-2 D-2 aspect reset live re-check:
  - DEFERRED live because F3 was not reachable from the indexed Tankoban library in this wake.
  - Follow-up pass evidence:
    - Videos search for `Chainsaw` rendered `No results for "Chainsaw"` in the live MCP snapshot.
    - Direct File Explorer drag/drop of the selected F3 MKV onto the live Tankoban player surface did not produce any subsequent `openFile` log; the newest open remained the sports fixture reopen at `_player_debug.txt:42331-42339`.
  - Code is still present at `src/ui/player/VideoPlayer.cpp:1006-1011`.

## Section 8 - Batch 1.6 Keybind convergence

- Live-converged keys:
  - `Space` pause/resume
  - `M` mute
  - `Right` seek +10s
  - `F` fullscreen
  - `Backspace` exit-fullscreen/back
- Static/default issues found in current Tankoban map:
  - `show_shortcuts` is bound as `Qt::Key_Question` with `Qt::NoModifier` (`src/ui/player/KeyBindings.cpp:67`)
  - subtitle delay is bound as `Qt::Key_Less` / `Qt::Key_Greater` with `Qt::NoModifier` (`src/ui/player/KeyBindings.cpp:48-49`)
  - speed step keys are not first-class keybinding defaults; only reset is registered (`src/ui/player/KeyBindings.cpp:13-15`)
  - speed up/down still live in a hardcoded fallback (`src/ui/player/VideoPlayer.cpp:3255-3259`)
- Live keybind failures:
  - `?` failed to resolve:
    - `_player_debug.txt:40305-40306`
  - `<` failed to resolve:
    - `_player_debug.txt:40761-40762`
  - `>` failed to resolve:
    - `_player_debug.txt:40798-40799`
- Reference-player source anchors used for candidate generation:
  - mpv chapter defaults: `etc/input.conf:75-76`
  - mpv speed defaults: `etc/input.conf:79-83`
  - mpv subtitle-delay defaults: `etc/input.conf:107-109`

## Section 9 - Batch 1.7 Library-integrated shell

- Backspace:
  - Verified. Tankoban returns cleanly out of player/fullscreen via `back_fullscreen` (`_player_debug.txt:40137-40143`, `41194`).
  - Verdict: DIVERGED-intentional versus the reference-player exit conventions, but correct for Tankoban's embedded-library shell.
- Esc:
  - Source-backed only in this wake. Binding still exists at `src/ui/player/KeyBindings.cpp:68`.
  - Live verification is DEFERRED because repeated Windows-MCP `esc` injection attempts still did not generate any Tankoban `keyPress` log line in this session.
- `?`:
  - Intended shell affordance exists in source (`src/ui/player/KeyBindings.cpp:67`) but is DIVERGED in live behavior on this Windows keyboard path (`_player_debug.txt:40305-40306`).

## Section 10 - Deferred-Measurement Ledger

- A3 direct tile-click-to-first-frame delta: deferred. Internal open-path timing is available, but the exact MCP click timestamp was not captured tightly enough to claim a precise click-to-first-frame number.
- B4 live speed-menu inspection across VLC / mpv / PotPlayer: deferred.
- C2 exact-seek parity on a non-keyframe offset: deferred.
- C3 frame-step: deferred.
- C4 click-to-seek midpoint latency: deferred.
- C5 live chapter navigation on F3: deferred because F3 was not reachable from the indexed Tankoban library. Follow-up pass confirmed `Chainsaw` returned `No results` in Videos search, and a direct File Explorer drag/drop fallback did not generate a new `openFile` after `_player_debug.txt:42331-42339`.
- D1 filename elision on F2: deferred because F2 was not reachable from the indexed Tankoban library.
- D2 / D4 / D7 HUD time-format, auto-hide timeout, and hover-tooltip live re-check: deferred because the player HUD did not become reliably inspectable in current MCP snapshots.
- J3 FC-1 live menu verification and FC-2 D-2 live re-check on F3: deferred for lack of reachable F3 in the indexed Tankoban library. Follow-up pass confirmed `Chainsaw` returned `No results` in Videos search, and a direct File Explorer drag/drop fallback did not generate a new `openFile` after `_player_debug.txt:42331-42339`.
- L1 Esc live spot-check: deferred because the current Windows-MCP `esc` injection did not emit a Tankoban key event in this wake.

## Section 11 - Fix Candidates Ratification-Request Block

Only ranked candidates with defensible reference-player source cites are included below. The `?` bug is real, but it is excluded from the ranked list because the direct keybind-editor shortcut is Tankoban-specific and I do not have a clean reference-player source analogue for that exact affordance.

**FC-6 / closes speed-hotkey convergence (~20 LOC):**
- Tankoban surface: speed reset is the only first-class speed binding in `src/ui/player/KeyBindings.cpp:13-15`; speed up/down still live in the hidden fallback at `src/ui/player/VideoPlayer.cpp:3255-3259`.
- Gap: reference players expose speed step/reset as first-class defaults; Tankoban currently hides step hotkeys behind unlisted legacy fallback behavior.
- Reference source: mpv at `C:/Users/Suprabha/Downloads/Video player reference/mpv-master/mpv-master/etc/input.conf:79-83` - stock defaults for `[` / `]` speed step and `BS` reset.
- Hemanth-testable: yes.
- Risk: low. Preserve `C` / `X` / `\` as compatibility aliases if muscle-memory churn is a concern.
- Proposed scope: `src/ui/player/KeyBindings.cpp`, `src/ui/player/VideoPlayer.cpp`, and if needed the editor labels path for discoverability. Roughly 15-25 LOC.

**FC-7 / closes subtitle-delay hotkey reliability on Windows layouts (~10-20 LOC):**
- Tankoban surface: `src/ui/player/KeyBindings.cpp:48-49` binds `<` and `>` with `Qt::NoModifier`, but the live Windows event path arrives as shifted punctuation and resolves to no action (`_player_debug.txt:40761-40762`, `40798-40799`). The actual subtitle-delay implementation itself is healthy at `src/ui/player/VideoPlayer.cpp:1914-1919` and `3280-3283`.
- Gap: reference players use layout-robust subtitle-delay defaults rather than shifted punctuation that fails under this path.
- Reference source: mpv at `C:/Users/Suprabha/Downloads/Video player reference/mpv-master/mpv-master/etc/input.conf:107-109` - `z` / `Z` / `x` subtitle-delay defaults.
- Hemanth-testable: yes.
- Risk: low. Can be shipped as added alternates without removing the current bindings immediately.
- Proposed scope: `src/ui/player/KeyBindings.cpp` only if solved by default remap/aliasing; `KeyBindings` match logic too if Tankoban wants true shift-aware punctuation support. Roughly 10-20 LOC.

**FC-8 / closes chapter-nav polarity drift (~2-6 LOC):**
- Tankoban surface: `src/ui/player/KeyBindings.cpp:56-57` binds `PageDown = next chapter` and `PageUp = previous chapter`.
- Gap: mpv's stock desktop convention is the opposite polarity: `PGUP = next chapter`, `PGDWN = previous chapter`.
- Reference source: mpv at `C:/Users/Suprabha/Downloads/Video player reference/mpv-master/mpv-master/etc/input.conf:75-76` - stock chapter mapping.
- Hemanth-testable: yes, once a chaptered fixture is reachable in the indexed library or via a future direct-open test harness.
- Risk: moderate only because of existing Tankoban muscle memory; lowest-risk variant is adding aliases before swapping defaults.
- Proposed scope: `src/ui/player/KeyBindings.cpp` only. Roughly 2-6 LOC.

Research-needed but intentionally excluded from the ranked list:

- `?` keybind editor launch is broken in the live Windows key path (`_player_debug.txt:40305-40306`) because `Key_Question` arrives with `Shift`. This is a real bug, but I do not have a clean reference-player source analogue for a direct `?` "open shortcuts editor" affordance, so it should be handled as a Tankoban-local fix rather than a ranked port-seed.

## Section 12 - Rule 17 cleanup trace

- Cleanup command: `powershell -NoProfile -File scripts/stop-tankoban.ps1`
- Cleanup timestamp: `2026-04-23T19:44:06.9199149+05:30`
- Cleanup output:
  - `Tankoban.exe` PID 7384 killed, uptime `00:18:18`
  - `ffmpeg_sidecar.exe` PID 20740 killed, uptime `00:04:24`
  - `2 process(es) killed. Wake can end.`
- Post-cleanup process check: no `Tankoban.exe` or `ffmpeg_sidecar.exe` remained.

## Section 13 - Phase 1 Exit Criteria Status

- Deliverable landed at `agents/audits/comparative_player_2026-04-23_p1_transport.md`: PASS
- Version pin recorded: PASS
- Reference-player startup medians captured: PASS
- Tankoban claims backed by logs, source cites, and saved screenshots: PASS
- Section 11 ranked candidate block present with 3 reference-cited candidates: PASS
- Rule 17 cleanup completed: PASS
- Rule 19 lock released + wake-summary + RTC appended in `agents/chat.md`: PASS

## Evidence Files

- `out/_player_debug_pre_p1_rerun.txt`
- `out/f1_fullscreen_fc5_1920x1080.png`
- `out/vlc_f1_transport.png`
- `out/mpv_f1_transport.png`
- `out/potplayer_f1_transport.png`
