# Make MPV Solo

## Plan-specific rules (Hemanth-quoted, verbatim)

> Don't ask Hemanth questions or give him options unless it concerns the user-facing side of the app, meaning how it affects the experience of using the app.

> Talk to me in non-coder terms and simpler language so I can better follow instructions if the agent needs me to test something.

*Agent-facing translation: this means the executing agent picks the file path, refactor shape, IPC method, and any other technical choice without surfacing it to Hemanth. Only product/UX questions reach him. When asking him to smoke-test something, describe what to click and what to look for in plain English — no jargon, no menus.*

---

## General direction

Destination: mpv as the sole video player; the ffmpeg sidecar gone after a validation window proves nothing visibly regressed. Broad order: bench the current ffmpeg behavior so we have something to compare against → remove the small special override that locks streams to ffmpeg → wire the missing data bridges and fix the parity gaps the audit named → make mpv the daily default → wait + watch → delete ffmpeg. No phase labels, no clusters — flat numbered tasks below.

Audit anchor: `agents/audits/mpv_replacement_readiness_audit_2026-05-01.md` (Agent 7, READY WITH CONDITIONS verdict, §5 closure plan).

---

## Tasks

*Tasks 1-15 authored — the full arc from baseline through ffmpeg decommission. Tasks 16+ would only land if validation window (Task 12) or follow-on smoke surfaces new gaps; otherwise plan archives after Task 15.*

- [x] **1. Bench the current ffmpeg behavior on a test corpus.** ✅ closed 2026-05-01 — `agents/audits/baseline_ffmpeg_summary_2026-05-01.md` (6 YELLOW corpus + 6 patterns surfaced).
  - **What this involves (plain English):** Agent (via MCP) opens 5-10 video files Hemanth picks on the current ffmpeg player, lets each play long enough to settle, captures the technical logs to evidence files. No code changes — pure measurement so we have a baseline to compare future mpv changes against.
  - **What Hemanth does for the smoke test:** Pick the corpus — mix of subtitle types (anime ASS karaoke, simple SRT, PGS Blu-ray rip), HDR/SDR, common codecs. Watch each playback for ~30 seconds. Score each one GREEN (looks right, no glitches) / YELLOW (some issue but watchable) / RED (broken / unwatchable).
  - **Goal:** We measure how the current ffmpeg player behaves on a real set of files so later changes have a fair comparison point.
  - **What success looks like:** Hemanth picks 5-10 representative video files (mix of subtitle types, HDR/SDR, common codecs). Agent runs MCP-driven playback through ffmpeg on each, captures evidence files at `agents/audits/baseline_ffmpeg_<file>_<HHMMSS>.txt`. Hemanth gives a one-word verdict (GREEN / YELLOW / RED) per file based on what he sees on screen.
  - **Files in scope:** corpus paths picked in coordination with Hemanth; output evidence files in `agents/audits/`; existing `scripts/compare-mpv-tanko.ps1` log-parser harness if useful.
  - **Smoke owner:** Hemanth picks corpus + records subjective verdicts; Agent (MCP) runs the playback + captures evidence.

- [x] **2. Remove the stream-mode override that forces ffmpeg.** ✅ closed 2026-05-01 — `BackendFactory.cpp::chooseFor(std::optional<Type>)` signature change; saved-pref now wins on streams.
  - **What this involves (plain English):** One small code change to `BackendFactory.cpp` removes the hidden rule "if it's a stream, force ffmpeg." After this, the saved player choice (mpv or ffmpeg) wins on streams the same way it wins on library files.
  - **What Hemanth does for the smoke test:** Set the saved player to mpv (right-click a video → set as default, or whatever the existing UI is). Open a stream from the Sources tab. Confirm mpv is running it — the right-click "current backend" indicator should say mpv, and no ffmpeg sidecar process should be visible in Task Manager.
  - **Goal:** If your saved player choice is mpv, streams use mpv too. Today there's a hidden override that ignores your choice on streams.
  - **What success looks like:** With saved player set to mpv, Hemanth opens a stream from the Sources tab; the stream plays through mpv (verifiable by the right-click "current backend" indicator showing mpv, or by checking that no ffmpeg sidecar process spawned).
  - **Files in scope:** `src/ui/player/BackendFactory.cpp` (the §Q4 stream-mode lock at lines 28-38).
  - **Smoke owner:** Hemanth.

- [x] **3. Bridge mpv's media info into the rest of the player.** ✅ closed 2026-05-01 — MpvBackend `FILE_LOADED` emit gets hdr / color_primaries / color_trc / chapters / audio_device / audio_host_api fields.
  - **What this involves (plain English):** mpv currently only tells the rest of the player two things — file duration and file path. Real ffmpeg files send a richer info bundle (HDR flag, color settings, chapter list, audio device). Code change makes mpv send the same richer bundle so the rest of the player has the data it needs to populate the HUD badges, chapter popover, and per-device audio-delay recall.
  - **What Hemanth does for the smoke test:** Open an HDR file on mpv. Look for the HDR badge in the player HUD (top-right corner area where it shows "HDR10" or similar). Open the chapter list popover — confirm chapters appear if the file has them. Agent verifies the technical fields populate via `tankoctl get-player` first.
  - **Goal:** mpv tells the rest of the player the same kind of file info ffmpeg already does — whether the file is HDR, what colors it uses, the chapter list, which audio device.
  - **What success looks like:** Agent MCP-plays an HDR file on mpv; runs `tankoctl get-player`; confirms HDR flag, color fields, chapter list, and audio-device fields are populated (non-empty). Hemanth then opens the same file through the UI and confirms the HDR badge appears in the HUD and the chapter list shows in the popover.
  - **Files in scope:** `src/ui/player/MpvBackend.cpp` (the `mediaInfo` payload at lines 304-320), possibly `src/ui/player/MpvBackend.h` for new property bridges.
  - **Smoke owner:** Agent (MCP) for `tankoctl get-player` field-population; Hemanth for visible HDR badge + chapter list verification.

- [x] **4. HDR + tone-mapping parity on mpv.** ✅ closed 2026-05-01 — `agents/audits/task4_close_2026-05-01.md`; both backends auto-pick bt.2446a; Hemanth verbatim "yeah it looks good as can be so green".
  - **What this involves (plain English):** This one's about visual quality, not feature plumbing. mpv has its own HDR rendering path; the task tunes it so HDR films look right on Hemanth's specific display. Heavy because there's no automatic test — Hemanth's eyes decide. Iterative: agent ships a tone-mapping setting, Hemanth scores, agent adjusts, repeat until GREEN.
  - **What Hemanth does for the smoke test:** Pick an HDR film known well (Sopranos S06E09, The Boys S03E06, or one of the go-tos). Right-click → Play with ffmpeg, watch a known-tricky scene (bright highlights, dark shadows, skin tones). Close. Right-click → Play with mpv, watch the same scene. Tell the agent GREEN / YELLOW / RED for mpv vs the ffmpeg memory. If RED or YELLOW, describe what looks off ("highlights blown out," "skin too red," "shadows crushed") — agent iterates tone-mapping settings.
  - **Goal:** HDR films play correctly on mpv on Hemanth's display, matching or improving over how ffmpeg renders them today.
  - **What success looks like:** Hemanth plays the same HDR file (e.g., Sopranos S06E09 or The Boys S03E06) on both backends back-to-back via right-click "Play with X"; gives a one-word verdict (GREEN / YELLOW / RED) on each based on subjective image quality. mpv must be GREEN. Per `feedback_subjective_over_trace.md`, Hemanth's eyes are the load-bearing arbiter — numeric metrics support but don't override.
  - **Files in scope:** `src/ui/player/MpvBackend.cpp` (`sendSetToneMapping` at lines 966-972), `src/ui/player/MpvVideoWidget.cpp` (HDR-related render context options).
  - **Smoke owner:** Hemanth.

- [x] **5. Surface clear error messages when mpv playback fails.** ✅ closed 2026-05-01 — `MPV_EVENT_END_FILE` reason-aware emit + `onError` dismisses LoadingOverlay + stops firstFrameWatchdog.
  - **What this involves (plain English):** Right now if mpv hits a problem (file missing, network drop, can't decode), the player either goes silent or shows a generic error. Code change makes it surface a sensible, plain-English message Hemanth can read. Foundational for every future smoke — without this, when mpv fails Hemanth has nothing to report back beyond "it didn't work."
  - **What Hemanth does for the smoke test:** Agent injects the failure cases first via MCP and confirms each fires a message. Hemanth's only part: agent sends one screenshot of a failure message; Hemanth reads it like a non-developer would and tells the agent if it makes sense ("does this tell me what went wrong without needing logs?").
  - **Goal:** When something breaks on mpv (file missing, broken stream URL, codec unsupported, network drop), the player says clearly what went wrong instead of going silent or showing a generic error.
  - **What success looks like:** Agent MCP-tries each failure mode in turn (nonexistent file path, broken stream URL, file with unsupported codec) and confirms each surfaces a clear UI message via `tankoctl logs` ring buffer + visible toast/dialog. Hemanth screenshots one to confirm the visible message reads sensibly to a non-developer.
  - **Files in scope:** `src/ui/player/MpvBackend.cpp` (error handling around lines 304-355), possibly `src/ui/player/VideoPlayer.cpp` if UI handling needs updating to render mpv errors the same way it renders sidecar errors.
  - **Smoke owner:** Agent (MCP) for failure injection; Hemanth for one screenshot to verify visible message shape.

- [x] **6. Subtitle residuals — Force-position toggle, pixel offset, URL subtitle offset/delay.** ✅ closed 2026-05-01 — `agents/audits/task6_close_2026-05-01.md`; 3 stubs filled + Pattern A track-id stringify. **6.B** in-session carry-id extension also ✅ same wake (m_carrySubId/m_carryAudioId fields).
  - **What this involves (plain English):** Three subtitle features that don't work on mpv yet. The Force-position toggle (today logs a warning and does nothing on mpv); the pixel-offset slider (emits the change but doesn't move the subtitle on mpv); and external URL subtitles loaded with offset/delay parameters (mpv loads the URL but ignores the timing args). Code change wires all three to mpv's native subtitle property surface.
  - **What Hemanth does for the smoke test:** Open an mpv-backed file with subtitles. Toggle the Force-position toggle ON; drag the position slider; see subtitles move on screen. Then load an external URL subtitle (drag-drop or "Load subtitle" entry) with a delay value applied; confirm the subtitle timing matches the dialogue.
  - **Goal:** Force-position toggle + pixel-offset slider + URL subtitle offset/delay all work on mpv the same way they work on ffmpeg.
  - **What success looks like:** Hemanth toggles Force-position on mpv, slides position, sees subtitles move. Loads URL subtitle with delay, sees timing match the spoken dialogue.
  - **Files in scope:** `src/ui/player/MpvBackend.cpp` (the stubs at lines 866-911 — `sendSetSubtitlePositionMode` + `sendSetSubtitlePixelOffset` + `sendSetSubtitleUrl`).
  - **Smoke owner:** Hemanth.

- [x] **7. HUD, focus, mouse, and keyboard parity under MpvVideoWidget.** ✅ closed 2026-05-01 — `agents/audits/task7_close_2026-05-01.md`; Pattern C seek-accumulator + Esc QShortcut scope fix + cursor on m_mpvWidget + setMouseTracking. (Note: post-MAKE_MPV_BEAT_FFMPEG arc, MpvVideoWidget has been replaced by MpvVulkanWidget — fixes carried via Task 3.5 swap.)
  - **What this involves (plain English):** Phase 1 left a known edge — HUD reveal on mouse hover, popover focus, and keyboard shortcuts (space / arrows / F / Esc) don't all behave the same on mpv as on ffmpeg. Code change closes the gap so the player chrome feels identical regardless of which backend is rendering.
  - **What Hemanth does for the smoke test:** Open an mpv-backed file. Hover the mouse over the player area — HUD should reveal smoothly. Press space (pauses), arrow keys (seeks 5s/10s), F (fullscreen toggles), Esc (closes player). All shortcuts should behave the same as ffmpeg. Open a popover (subtitle / settings / audio); confirm it opens, takes focus, and the keyboard shortcuts inside the popover work.
  - **Goal:** HUD reveal on hover, popover toggles, fullscreen, close button, and keyboard shortcuts all behave the same on mpv as on ffmpeg.
  - **What success looks like:** Hemanth runs through the full HUD + keyboard shortcut matrix on an mpv-backed file; everything matches ffmpeg behavior with no dead clicks, missing reveals, or frozen popovers.
  - **Files in scope:** `src/ui/player/MpvVideoWidget.cpp`, `src/ui/player/MpvVideoWidget.h`, `src/ui/player/VideoPlayer.cpp` (event routing).
  - **Smoke owner:** Hemanth.

- [x] **8. Audio polish — speed-sync, DRC, per-device delay recall.** ✅ closed 2026-05-03 (8.B sub-item closed this wake). Audio-speed → no-op; DRC → af-add/af-remove with @drc label (`agents/audits/task8_close_2026-05-01.md`). **8.B per-device delay recall** ✅ shipped via housekeeping commit `c9b365a` 2026-05-02 — `src/ui/player/AudioDeviceWatcher.{h,cpp}` (Windows IMMNotificationClient watcher + non-Windows stub) + `VideoPlayer.cpp:237-239` connect + `VideoPlayer.cpp:2277-2333 onAudioDeviceChanged` slot mirroring file-open recall path. Compile-verified by `c9b365a` landing; functional smoke (Bluetooth-headphones plug/unplug while playing) is Hemanth's lane and queued for next opportunistic test.
  - **What this involves (plain English):** Three audio features partially working on mpv. Audio-speed sync (currently maps to mpv `speed` which changes playback speed instead of just audio sync); DRC dynamic-range compression (overwrites the audio filter chain which can collide with other filters); per-device audio-delay recall (depends on Task 3 mediaInfo bridge sending audio-device fields). Code change fixes all three.
  - **What Hemanth does for the smoke test:** Open an mpv-backed file. Toggle DRC on/off mid-playback; hear the loudness even out then snap back. Then switch your audio output device (Bluetooth headphones, speakers, HDMI) and confirm the saved per-device delay applies automatically — you shouldn't need to re-tune the delay slider after the device switch.
  - **Goal:** Audio-speed sync, DRC (loudness compression), and per-device audio-delay recall all work on mpv.
  - **What success looks like:** Hemanth toggles DRC on/off and hears the loudness change; switches audio device and the saved delay applies without manual adjustment.
  - **Files in scope:** `src/ui/player/MpvBackend.cpp` (audio surface at lines 740-789), `src/ui/player/VideoPlayer.cpp` (per-device recall logic). Depends on Task 3 mediaInfo bridge for audio-device fields.
  - **Smoke owner:** Hemanth.

- [x] **9. Structured filters and EQ wiring.** ✅ closed 2026-05-01 — Hemanth-narrowed scope to brightness-only (contrast/saturation cut per VIDEO_HUD_MINIMALIST). `BrightnessPopover.{h,cpp}` + chip + brightness.svg + MpvBackend::sendSetFilters wired via mpv `brightness` property. Hemanth verbatim "the brightness feature works".
  - **What this involves (plain English):** When you open the filter or EQ popover today on mpv, the controls don't actually change the picture — they're no-op stubs in the mpv backend. Code change wires them through to mpv's video filter graph so the controls actually work.
  - **What Hemanth does for the smoke test:** Open an mpv-backed file. Open the filter popover. Slide brightness, contrast, saturation. Confirm the picture changes live as you slide each one.
  - **Goal:** When you pull up the filter or EQ popover, the controls actually affect the picture on mpv.
  - **What success looks like:** Hemanth slides brightness/contrast/saturation on an mpv-backed file and sees the picture change in real time.
  - **Files in scope:** `src/ui/player/MpvBackend.cpp` (the `sendSetFilters` stub at lines 948-964).
  - **Smoke owner:** Hemanth.

- [x] **10. Frame-drop and performance telemetry from mpv.** ✅ closed (writer at `MpvBackend.cpp:630` writing append-only session blocks to `out/mpv_telemetry.log`; ~105KB on disk last written 2026-05-03 16:24, used as evidence in MAKE_MPV_BEAT_FFMPEG Tasks 4 + 9 closes — drops/sec rate per-session + cmd p50/p99 + decoder state).
  - **What this involves (plain English):** Today we can't tell from logs how mpv playback performed — no frame-drop counter, no decoder state record, no render timing. Code change adds a telemetry log file (equivalent to the `out/ipc_latency.log` the ffmpeg side already has) so future regressions are diagnosable from logs alone without needing eyes on the screen.
  - **What Hemanth does for the smoke test:** Mostly an agent task. Agent runs an mpv-backed playback for ~5 minutes; checks the new mpv telemetry log; confirms frame-drop counter, decoder state, render-timing entries are populated and meaningful. Hemanth's part: optional — only if the agent reports an unexpected number, agent may ask "did you notice any stuttering during the 5-minute test?" to confirm or contradict the numbers.
  - **Goal:** We can tell from logs how mpv playback performed (dropped frames, decoder state, render timing) so regressions are diagnosable from telemetry alone.
  - **What success looks like:** Agent runs a 5-minute mpv playback; reads the new mpv telemetry log; confirms frame-drop counter, decoder state, render-timing entries present and meaningful. Hemanth optionally cross-checks with subjective "did it feel smooth" verdict.
  - **Files in scope:** `src/ui/player/MpvBackend.cpp`, `src/ui/player/MpvVideoWidget.cpp`, possibly a new `out/mpv_telemetry.log` equivalent to `out/ipc_latency.log`.
  - **Smoke owner:** Agent (MCP); Hemanth optional cross-check.

---

## Pattern re-test gates (added 2026-05-01 after Task 1 baseline)

Task 1's ffmpeg baseline surfaced 4 user-facing patterns. None were investigated per Hemanth's strategic call ("we're replacing this player anyway, are we spending time fixing it?"). They're tracked here so they fire as automatic re-test triggers on the right later task instead of getting lost. Full evidence: `agents/audits/baseline_ffmpeg_summary_2026-05-01.md`.

- **Pattern A — Anime ASS subtitle dropout** (3/3 anime files: Vinland S02E01, Saiki Ep 11, Apothecary S02E01).
  - ✅ **CLOSED 2026-05-01** by Task 6: root cause was mpv emitting track-list `id` as int while the ffmpeg sidecar emits as string; VideoPlayer's `mergeTrackList` skipped every track via `if (id.isEmpty()) continue;`. Stringify fix in MpvBackend.cpp's track-list parser. Hemanth verbatim verdict: "subttiles appear and they work." Note: file's "default" sub track on most anime is Signs/Songs (not Dialogue) — user manually picks Dialogue once via the now-functional Subtitles popover or right-click menu. Task 6.B queued for in-session carry-forward extension to also remember track ID across playlist (today carry-forward only persists language).
- **Pattern B — Sub-position slider non-functional** on rendered subs (Sopranos PGS + Community embedded SRT, 2/2 where subs render).
  - ✅ **CLOSED 2026-05-01** by Task 6: PGS sub-position slider moves subs on mpv per Hemanth verdict ("yes the slider moves them"). Sopranos S06E01 used as PGS test fixture. Underlying mechanism: existing `sendSetSubtitlePosition` already wired sub-pos correctly on mpv path; Pattern B was a Task 1 baseline observation on ffmpeg, mpv path was clean from day one.
- **Pattern C — Seek-accumulation bug, both directions** (Apothecary + Boys S03E01 + Community, 3/3). Rapid double-tap of arrow keys does not accumulate; second press shows seek-flash visual but position stays.
  - ✅ **CLOSED 2026-05-01** by Task 7. Root cause: `seekBy` lambda in VideoPlayer.cpp:3146 read `m_seekBar->value()` for the seek base on each press — but the seekbar lags ~200ms behind real position because it echoes the BACKEND-confirmed position. Rapid double-tap landed both presses on the same stale base → second press visual-flashed but went to same target. UI-layer bug, identical to ffmpeg path. Fix: `m_pendingSeekTargetSec` accumulator (VideoPlayer.h field + read in seekBy + clear in onTimeUpdate when position lands within ±1s of target). MCP-verified: paused at 999.29s, rapid right-arrow ×2 with ~50ms gap → position 1019.23s (Δ +19.94s ≈ +20s expected). Both presses accumulated.
- **Pattern D — Top-edge clipping** (Vinland windowed video crop + Boys top-of-screen sub clipping, 2/2).
  - **Re-test gate:** Cutover validation window (when mpv is default, before ffmpeg deletion — Task 11+ territory). If repros → file targeted widget-geometry fix. If doesn't repro → free win.

Two more patterns from the baseline are comparison bars rather than bugs:
- **Pattern E — ffmpeg frame stats excellent today** (60fps clean, 0 dropped, draw <0.25ms p50). ✅ **CLOSED 2026-05-01** by Task 4 Phase B: mpv frame-pacing on Boys S03E01 = 24fps stable cadence (source frame rate), zero dropped frames per `[MPV-RENDER]` traces in Task 3 Smoke A + Task 4 Phase B. mpv matches the bar.
- **Pattern F — ffmpeg HDR rendering acceptable on The Boys S03E01** (Hemanth no-complaint = pass). ✅ **CLOSED 2026-05-01** by Task 4 Phase B: Hemanth verbatim verdict on mpv HDR same file = "yeah it looks good as can be so green". Both backends were already running auto-picked tone-mapping (mpv `tone-mapping=auto` → bt.2446a; libplacebo `pl_tone_map_auto` → also bt.2446a) — equivalent subjective quality confirmed. Evidence: `agents/audits/baseline_mpv_hdr_boys_s03e01_141626.txt`.

---

## Tasks (continued)

- [ ] **11. Flip the saved-default-pref to mpv.**
  - **What this involves (plain English):** Small code change — for new installs (or anywhere no saved pref exists yet), the player defaults to mpv. Existing users keep their saved pref unchanged. Hemanth manually flips his own saved pref to mpv to kick off the validation window in Task 12. The right-click "Play with ffmpeg" entry stays visible during this task as an emergency revert path.
  - **What Hemanth does for the smoke test:** Open the app post-update. Manually flip your own saved pref to mpv via the existing right-click set-default mechanism. Open 5 different videos (mix of library + stream); confirm all play through mpv. Then on one specific file, right-click → "Play with ffmpeg" — confirm that single file opens via ffmpeg as expected (the per-file emergency revert still works).
  - **Goal:** For new installs (or first launch with no saved pref), mpv is the default player. Existing users opt-in by flipping their pref.
  - **What success looks like:** Hemanth flips his saved pref to mpv; opens 5 different videos (library + stream mix); all play through mpv. Right-click "Play with ffmpeg" on one specific file opens via ffmpeg (per-file revert path preserved).
  - **Files in scope:** `src/ui/player/BackendFactory.cpp` (default-pref logic when QSettings key absent); settings persistence layer (likely a QSettings key in the existing player namespace). No UI changes — right-click menu unchanged this task.
  - **Dependencies:** Tasks 7-10 must close GREEN before firing (Pattern C in Task 7 + telemetry in Task 10 are gates — flipping default before those close exposes Hemanth to known-unresolved gaps daily).
  - **Smoke owner:** Hemanth.

- [ ] **12. Run the validation window of daily mpv use.**
  - **What this involves (plain English):** No code. Operational soak task. Hemanth uses mpv as his primary player for an agreed period (Hemanth-paced — typically 1-2 weeks of daily use, but he sets the duration). Any regressions get logged in a dedicated `## MPV Validation Window log` section in `agents/chat.md` so the brotherhood can triage. Agent's part: respond to logged regressions with one-task-at-a-time fixes via the Tasks 1-10 pattern. At the end of the window, Hemanth declares "validation closed" — that gates Task 13. **Pattern D from the re-test gates section above (top-edge clipping on Vinland windowed video + Boys top-of-screen sub clipping) is a known-pending regression — explicitly verify whether it repros on mpv as default during this window; file a targeted widget-geometry fix if so.**
  - **What Hemanth does for the smoke test:** Just use the app normally as your daily player. When you notice something off (a stutter, a wrong-feeling subtitle position, a bad HDR moment, a popover not opening, a stream that won't start, anything), drop a one-line entry in the validation log: file name + brief description ("Severance S2E5, subtitles misaligned by 2 lines"). Specifically watch for top-edge clipping on Vinland and Boys (Pattern D). Agent works fixes between regressions. When you've gone N days without a new regression and feel confident, declare validation closed.
  - **Goal:** Daily-use exposure on mpv as the primary player surfaces real-world regressions before we delete the ffmpeg fallback. Pattern D resolution gets confirmed-or-fixed during this window.
  - **What success looks like:** Hemanth declares "validation closed — mpv is solid" after his agreed soak period. Validation log captures the regressions found + fixed during the window (zero open at close). Pattern D explicitly resolved (either "didn't repro on mpv-default = free win" or "fixed via targeted widget-geometry change").
  - **Files in scope:** `agents/chat.md` (NEW `## MPV Validation Window log` section); any mpv backend / widget files for triage fixes between regressions.
  - **Smoke owner:** Hemanth (daily use + log entries + close declaration); Agent (triage fixes between regressions).

- [ ] **13. Remove the right-click "Play with ffmpeg" + saved-pref-ffmpeg UI surface.**
  - **What this involves (plain English):** Post-validation UI cleanup. The dual-backend choice surfaces — right-click "Play with ffmpeg" entry, "Use ffmpeg player" tile menu entry, saved-pref-ffmpeg setting — all come out. Hemanth's saved pref forcibly migrates to mpv (no ffmpeg-only setting valid anymore). Right-click "Play with X" entries collapse to a single "Play" entry. ffmpeg stays buildable + runnable via a `TANKOBAN_FORCE_FFMPEG=1` env var as emergency revert during the final transition (Tasks 14 + 15 finish removal).
  - **What Hemanth does for the smoke test:** Open the app. Right-click any video. Confirm only "Play" (or equivalent single entry) shows — no "Play with ffmpeg" / "Play with mpv" choice. Open settings; confirm no player-backend-pick UI. Then close the app, set `TANKOBAN_FORCE_FFMPEG=1` in environment (or use a wrapper batch file the agent provides), launch; confirm the app plays through ffmpeg if the env is set (revert path preserved one more step).
  - **Goal:** The dual-backend choice UI comes out — single "Play" entry, single saved player. ffmpeg env-var fallback preserved one more task.
  - **What success looks like:** Hemanth right-clicks a video → sees a single "Play" entry; settings shows no player-pick UI; `TANKOBAN_FORCE_FFMPEG=1` env-var fallback still produces ffmpeg playback.
  - **Files in scope:** `src/ui/pages/VideosPage.cpp` + `src/ui/pages/ShowView.cpp` + `src/ui/pages/StreamPage.cpp` (right-click menus + tile menus); `src/ui/player/SettingsPopover.cpp` (settings UI); `src/ui/player/BackendFactory.cpp` (env-var fallback path preserved one step longer).
  - **Dependencies:** Task 12 closed ("validation closed — mpv is solid").
  - **Smoke owner:** Hemanth.

- [ ] **14. Move ffmpeg sidecar sources to `agents/_archive/native_sidecar/` per §Q7.**
  - **What this involves (plain English):** Per the §Q7 ratification ("PERMANENT ARCHIVE — never delete"), the entire `native_sidecar/` source tree git-moves to `agents/_archive/native_sidecar/`. Same for any other ffmpeg-only main-app files (`SidecarProcess.cpp/h`, etc.). `git mv` preserves history so future reverts are clean. The build wiring updates in Task 15; this task is the source relocation only — main app build will break expectedly until Task 15 lands.
  - **What Hemanth does for the smoke test:** Mostly an agent task — git mv operations. Hemanth's part: after the moves land, do a quick sanity check that the agent didn't accidentally move something used by mpv (e.g., `IPlayerBackend.h` is the shared interface — that header stays at its current path). Try to build the app — confirm the build BREAKS expectedly with "missing native_sidecar" or similar, which proves the move was thorough.
  - **Goal:** ffmpeg sidecar sources move into archive per §Q7; main app no longer compiles against them once Task 15 drops the wiring.
  - **What success looks like:** `git status` shows `R` (renamed) entries for `native_sidecar/` → `agents/_archive/native_sidecar/` + `SidecarProcess.{cpp,h}` moves; `git log --follow` on archived files preserves authoring history; main app build BREAKS expectedly with "missing native_sidecar" until Task 15 drops the wiring.
  - **Files in scope:** entire `native_sidecar/` tree → `agents/_archive/native_sidecar/`; `src/ui/player/SidecarProcess.{cpp,h}` → `agents/_archive/sidecar_process/` (or wherever fits the archive convention). `src/ui/player/IPlayerBackend.h` STAYS at current path (shared interface used by `MpvBackend`).
  - **Dependencies:** Task 13 closed.
  - **Smoke owner:** Agent (git operations); Hemanth (sanity-check the move was thorough by trying to launch and confirming the expected build break).

- [ ] **15. Drop ffmpeg from build wiring + packaging + docs.**
  - **What this involves (plain English):** Final cleanup. `CMakeLists.txt` drops the ffmpeg sidecar build target. `build_and_run.bat` + `setup.bat` drop the ffmpeg-related steps. NSIS installer (`installer/tankoban.nsi`) drops the ffmpeg sidecar bundling. README + BUILD + ARCHITECTURE docs update to reflect single-backend reality. After this task, the build is clean + smaller, the installer doesn't ship the ~50MB sidecar binary, the docs read honest, and Tankoban is mpv-solo.
  - **What Hemanth does for the smoke test:** Run `setup.bat` fresh on a clean machine OR run `build_and_run.bat`. Confirm the build succeeds without the sidecar. Confirm the app launches and plays a video through mpv. If you cut a release tag, confirm the new installer .exe is meaningfully smaller than before. Skim the updated README + BUILD + ARCHITECTURE docs and confirm they read sensibly without ffmpeg references.
  - **Goal:** Build wiring + installer + docs all reflect single-backend reality. ffmpeg references gone from the live build path; archived sources stay in repo per §Q7.
  - **What success looks like:** `build_and_run.bat` builds clean without ffmpeg sidecar; installer cuts a smaller `.exe`; docs (README + BUILD + ARCHITECTURE) updated to single-player reality; Hemanth opens the app, plays a video, confirms mpv-solo state.
  - **Files in scope:** `CMakeLists.txt`, `build_and_run.bat`, `setup.bat`, `installer/tankoban.nsi`, `README.md`, `BUILD.md`, `ARCHITECTURE.md`, possibly `CONTRIBUTING.md` if it mentions ffmpeg.
  - **Dependencies:** Task 14 closed.
  - **Smoke owner:** Agent (build verification); Hemanth (clean-machine setup.bat run + installer + docs review).

---

_Plan archive-ready after Task 15 closes. Tasks 16+ would only land if validation (Task 12) or follow-on smoke surfaces gaps not anticipated in Tasks 1-15._

---

## Tracking summary (added 2026-05-03)

- **Closed (10 + 2 sub-items):** Tasks 1-10 + Task 6.B + Task 8.B.
- **Cutover queue (5 tasks, all gated on Task 12 Hemanth soak):** Tasks 11-15.
- **Cross-arc note:** The `MAKE_MPV_BEAT_FFMPEG` 9-task arc (re-platforming the mpv renderer onto Vulkan + libplacebo + RGBA16F + HDR metadata bridge) closed 2026-05-03 ~16:25; that close empirically validated mpv quality matches ffmpeg per Hemanth's Task 5 verdict, satisfying SOLO's prerequisite for Task 11+ default-flip.
- **Pattern D status:** still pinned to Task 12 validation re-test gate per `## Pattern re-test gates` section above.
- **Carry-forward from MAKE_MPV_BEAT_FFMPEG (7 items, NON-blocking for SOLO cutover):** HUD bleed-through architectural fix (Plan-Mode-worthy); MainWindow top-bar flashing regression; aspect-ratio composite (target-crop math); sendSetToneMapping runtime override; MPV_EVENT_START_FILE stale-metadata clear; sub-position/size slider explicit drag verification; fullscreen flag get-player mismatch.
