# Video Player Fix TODO — IINA-identity UX parity

**Owner:** Agent 3 (Video Player). Agent 0 coordinates phase gates and commit sweeps. Agent 6 reviews each phase against this doc + `agents/audits/video_player_2026-04-15.md` as co-objective. No cross-agent touches.

**Created:** 2026-04-15 by Agent 0 after Agent 7's video player audit.

## Context

Player Polish Phases 1-6 shipped (vsync timing, shader pipeline, HDR color processing, audio drift/amp/DRC, subtitle rendering via sidecar, frame-pacing smoothness). The engine is correct. What's left is **feature-presence and UX polish** at the Qt shell layer — what a user of QMPlay2 / IINA / mpv would notice missing or feeling clunky in our player.

Agent 7's audit at `agents/audits/video_player_2026-04-15.md` identified 0 P0s, 8 P1s, 5 P2s. The zero-P0 count is truthful, not safe-playing — the six polish phases legitimately closed the broken-engine class of problem. What remains is the Qt shell lagging behind modern desktop video players on: seekbar richness, queue modes, window utilities (PiP/always-on-top/snapshot), opening/loading UX, keyboard discoverability, video transforms, subtitle surface cohesion, stats visibility.

The audit surfaced one genuine potential bug: the subtitle Off path may have a `std::stoi("off")` throw on the sidecar side if the SubtitleMenu Off branch fires before the VideoPlayer visibility-false branch. This gets validated in Phase 1 before anything else — it might upgrade to a P0.

**Identity direction locked by Hemanth:** IINA-identity. Polished desktop UX with visual polish emphasis. Priority order favors seekbar chapter markers + thumbnail hover previews, PiP, always-on-top, snapshot export, Open-URL/recent/drag-drop, coherent subtitle menu organization. Matches the established gray-scale polished-desktop aesthetic. Rejected alternatives: mpv-identity (keyboard power + deep stats) and QMPlay2-identity (dense Qt controls + InfoDock panel).

**Scope:** 6 required phases + 1 stretch. ~15 batches. Phase 1 is validation-first (subtitle Off path) before any fix work. Phases 2-6 are IINA-aligned feature polish. Phase 7 is a stretch badge that can ship independently.

## Objective

After this plan ships, a video player user can:
1. Use the subtitle Off control from every entry point (context menu, SubtitleMenu, TrackPopover) without a silent failure or crash.
2. See chapter markers on the seek bar and get a chapter-title tooltip on hover for files with chapter metadata.
3. See a thumbnail preview at the cursor position while hovering/dragging the seek bar.
4. Toggle picture-in-picture, always-on-top, and export a single-frame snapshot from the context menu or keyboard shortcut.
5. Open remote URLs, see a Recent Files submenu, drag-drop single files to open OR multiple files to enqueue.
6. Shuffle, repeat-one, repeat-all, loop-file from the PlaylistDrawer. Save the current queue to `.m3u` or load one back.
7. See every shipped keyboard shortcut in a live keybinding editor (replacing the static stale overlay), and rebind any of them.
8. (Stretch) Toggle a compact codec/resolution/FPS/dropped-frames badge for diagnostics without breaking the polished HUD aesthetic.

## Non-Goals (explicitly out of scope for this plan)

- Engine/render work already shipped in Player Polish Phases 1-6 (D3D11, HDR, audio drift/amp/DRC, subtitle renderer internals, frame pacing)
- Audio passthrough (IEC 61937) — deferred per prior plan-mode investigation
- Wider speed range (down to 0.03125x or up to 32x) — audit P2, not IINA-signature
- Equalizer presets + FFmpeg audio-filter text path — QMPlay2-signature, not IINA
- A-B loop, reverse playback, ordered chapters — mpv-signature, not IINA-first priority
- Scripting/hooks, Lua config, mpv.conf compatibility — mpv-identity only
- Full stats overlay with deep metrics — mpv/QMPlay2; our version is a compact badge only (Phase 7 stretch)
- Online subtitle search (IINA has it, but requires integrating OpenSubtitles-equivalent — out of scope for this pass; Stream mode's subtitles aggregator covers the addon-backed path)
- Explicit in-video bookmarks (product-specific, not reference parity per audit)
- Subtitle text search / "jump to phrase" (product-specific per audit)
- Stream-mode-specific features — covered by Stream UX TODO
- Videos library page UX — Agent 5's day-to-day, Agent 3's secondary scope only
- Error retry / open-containing-folder affordances (audit P2, polish)
- Cloud sync / Trakt / multi-device (same as original plan)
- Chromecast, debrid backends — same as original plan
- Any work outside `src/core/stream/`, `src/ui/pages/stream*`, `src/ui/pages/StreamPage.*`, `src/ui/player/VideoPlayer.*` (minimal additions), plus the already-existing `src/ui/player/SubtitleMenu.*`

## Agent Ownership

All batches are **Agent 3's domain** (Video Player). Primary files: `src/ui/player/*`. The sidecar at `TankobanQTGroundWork/native_sidecar/src/` is Agent 3's territory too (pending migration per the Option 1 ratification). No cross-agent touches in this plan — Agent 3 owns every file modified. Agent 0 coordinates phase gates and commit sweeps. Agent 6 reviews each phase against this plan + the audit as co-objective.

---

## Phase 1 — Subtitle Off path validation + fix

**Why:** Audit P1 #7 flagged contradicting subtitle Off code paths. Three entry points route Off differently:
- `VideoPlayer.cpp:1865-1886` context-menu Off → sends visibility=false
- `SubtitleMenu.cpp:247-263` menu Off → calls `sendSetSubtitleTrack(-1)`
- `SidecarProcess.cpp:472-493` helper → emits `set_tracks` with sub id literal `"off"`
- Sidecar `main.cpp:802-884` parses nonempty subtitle ids with `std::stoi` → `std::stoi("off")` **throws**.

If both client code paths fire at runtime, one of them could be crashing or silently failing the Off transition. This MIGHT upgrade to a P0 during validation. Validate before anything else; fix as a unified Off flow so later subtitle UI work doesn't build on a broken foundation.

### Batch 1.1 — Validate the Off path (no code change)

- Build an EPUB-equivalent test: open a video with at least one embedded subtitle track. Toggle Off via each entry point in sequence (context menu → SubtitleMenu → TrackPopover). Capture sidecar stderr/log between toggles.
- Determine: (a) Does `sendSetSubtitleTrack(-1)` actually emit `set_tracks` with id `"off"` at runtime, or does an earlier branch intercept? (b) Does `std::stoi("off")` throw at the sidecar? If yes, does the sidecar catch it or does playback die? (c) Is the visibility-false path always winning over the set_tracks path in practice?
- Post findings in chat.md as observation-only (Agent 2's validation pattern). Format: `Subtitle-Off validation — [CONFIRMED BROKEN / CONFIRMED SAFE / PARTIAL]: <evidence>`.
- Files: zero modified. Observation only.
- **Success:** chat.md has an authoritative verdict. Phase 1 Batch 1.2 scope is decided by what Batch 1.1 found.

### Batch 1.2 — Unify the Off path *(shipped 2026-04-15, awaiting verify)*

Batch 1.1 verdict: **PARTIAL** — Paths 4 (TrackPopover) + 5 (SubtitleMenu) both latent-crash on first user Off via `std::stoi("off")` at sidecar main.cpp:850. Path 1 (context-menu) was fixed in Phase 5 P0 fix-batch but used a local inline pattern; consolidated here. Design ratified: **visibility-only Off semantics, no set_tracks payload ever carries `"off"`** — eliminates the crash vector at the wire rather than guarding it sidecar-side. No sidecar rebuild required.

- Added `VideoPlayer::setSubtitleOff()` canonical helper: `m_subsVisible=false; sendSetSubVisibility(false); toast`. Track selection preserved (picking a numeric track later resumes the previously-selected stream).
- Rerouted Path 1 (context-menu Off) to call `setSubtitleOff()`. Supersedes Phase 5 inline fix.
- Rerouted Path 4 (TrackPopover Off id==0) to call `setSubtitleOff()`.
- Rewrote `SidecarProcess::sendSetSubtitleTrack(-1)`: drops `sendSetTracks(QString(), "off")`, substitutes `sendSetSubVisibility(false)`. Path 5 (SubtitleMenu) rides this transitively — zero file-local edit, same crash-free outcome.
- Extended VideoPlayer's `subVisibilityChanged` handler to update `m_subsVisible = visible` on sidecar emission. Pre-fix, SubtitleMenu-driven Off left VideoPlayer's state stale-true → next `T` toggle did the wrong flip. Now sidecar is authoritative.

Sidecar std::stoi try/catch hardening: deferred. All known callers now numeric-only; worth adding as defense-in-depth in a future hardening sweep.

- Files: `src/ui/player/VideoPlayer.h/.cpp`, `src/ui/player/SidecarProcess.cpp`. SubtitleMenu.cpp + TrackPopover.cpp untouched.
- **Success:** Zero `"off"` string sub_id payloads main-app-side. `std::stoi("off")` unreachable. Off works from every entry point. `m_subsVisible` coherent across paths.

### Phase 1 exit criteria
- Subtitle Off path validated and made consistent across entry points.
- If broken: sidecar hardened against future parsing misuse.
- Agent 6 review: validation report + unification match the audit P1 #7 citation chain.
- `READY FOR REVIEW — [Agent 3, Phase 1]: Video Player UX | Objective: Phase 1 per plan file radiant-foraging-flask.md + agents/audits/video_player_2026-04-15.md. Files: ...`

---

## Phase 2 — Seekbar richness

**Why:** Audit P1 #1 flagged our seekbar as "below modern desktop-player expectations" — time bubble + drag-to-seek exist, but no chapter markers (despite `m_chapters` being populated), no hover-thumbnail previews (IINA's signature), no buffered/seekable range band. Seekbar feel is the single biggest visible-polish gap.

### Batch 2.1 — Chapter markers on SeekSlider *(shipped 2026-04-15, awaiting verify)*

- `SeekSlider::setChapterMarkers(QList<qint64> markersMs)` stores the tick positions; `paintEvent` override calls `QSlider::paintEvent` then renders 2px near-white ticks (slightly taller than the 5 px groove) at each position via QPainter, mapping ms → fraction via `m_durationSec`. Empty list hides ticks.
- `VideoPlayer` mediaInfo handler extracts chapter start times (seconds → ms via `* 1000`), calls `m_seekBar->setChapterMarkers(...)`. Fresh files with no chapter metadata pass an empty list → ticks cleared.
- `hoverPositionChanged` lambda prepends chapter title to the time bubble when the cursor fraction is within `8.0 / sliderGeoWidth` (~8 px) of any chapter's start fraction. Format: `"Chapter Title · 12:34"`. Empty title falls back to `"Chapter"`. Drag path (`sliderMoved`) not extended — hover-only per TODO spec.
- Files: `src/ui/player/SeekSlider.h/.cpp`, `src/ui/player/VideoPlayer.cpp`.
- **Success:** open a file with chapter metadata → seekbar shows tick marks at chapter boundaries. Hover a tick → tooltip shows chapter title + time. No-chapter files unchanged (empty marker list suppresses paint).

### Batch 2.2 — Buffered/seekable range band

Audit citation: mpv OSC seek-range rendering at `player/lua/osc.lua:897-920`. IINA equivalent at `MainWindowController.swift:1128-1154` thumbnail peek context.

- Sidecar: if `ffmpeg_sidecar`'s demuxer exposes a "buffered-to" or "seekable-range" concept (check `AVFormatContext->io->...` or equivalent; HTTP streams expose this via Range-response-cache), plumb it through a new `seekableRangeChanged(rangeStartMs, rangeEndMs)` sidecar event at a low cadence (1-2 Hz, not per-frame).
- `SeekSlider`: render a subtle gray band underneath the position indicator showing the buffered range. Hide when range == full duration (local files).
- Files: `TankobanQTGroundWork/native_sidecar/src/main.cpp` + demuxer surface (+ migration path per the ratified sidecar vendoring — if sidecar has moved to in-repo by the time this phase runs, path becomes `Tankoban 2/native_sidecar/src/main.cpp`). `src/ui/player/SidecarProcess.h/.cpp` (new event). `src/ui/player/SeekSlider.h/.cpp`.
- **Success:** play an HTTP stream → seekbar shows a subtle band representing "how far can I seek without re-buffering." Local files show no band (full-file seekable). Band updates as playback progresses without stutter.

### Batch 2.3 — Thumbnail hover previews (the signature batch)

Audit citation: IINA `ThumbnailPeekView` invoked at `MainWindowController.swift:1128-1154`. This is the headline IINA seekbar feature.

- On file open, kick off a background thumbnail extraction: sample N positions (e.g., every 5% of duration, capped at 40 thumbnails) via `ffmpeg_sidecar`'s existing decode pipeline or a secondary short-lived ffmpeg process spawned just for thumbnails. Each thumbnail: 160x90px JPEG, cached to `{AppData}/Tankoban/data/video_thumbnails/{file_sha1}_{pct}.jpg`.
- Background extraction budget: ~50-100ms per thumbnail, batched so UI is never blocked. Skip entirely for files < 2 minutes (no payoff). Skip for network streams until Batch 2.2's seekable-range work tells us where seeks are valid.
- `SeekSlider` hover handler: on mouse-move, compute nearest cached thumbnail percent, display a floating preview bubble (160x90px with thin border, positioned above slider) showing the thumbnail. If not yet cached, show the existing time bubble instead (graceful degradation).
- Purge cache on library remove / file delete. LRU eviction at a reasonable total size threshold (e.g. 500MB).
- Files: NEW `src/ui/player/ThumbnailExtractor.h/.cpp` (background worker), `src/ui/player/SeekSlider.h/.cpp` (hover preview rendering), `src/ui/player/VideoPlayer.cpp` (kick extraction on openFile).
- **Success:** open a local 45-minute file → within ~5 seconds, seekbar hover shows thumbnail previews at cursor position. Drag the slider → thumbnails update at ~10 fps.
- **Budget note:** this is the biggest batch in this phase. If the extraction infrastructure feels like a 3-week job instead of 3-day, stop and replan — might split into "extraction infra" + "hover UI" as two batches.

### Phase 2 exit criteria
- Chapter markers + tooltip on files with chapter metadata.
- Buffered range band on HTTP streams.
- Thumbnail hover previews on local files.
- Agent 6 review against audit P1 #1 citation chain.

---

## Phase 3 — Window utilities (PiP, always-on-top, snapshot)

**Why:** Audit P1 #3 flagged desktop window features as thin. IINA, QMPlay2, and mpv all expose PiP/always-on-top/snapshot as core player utilities. We have fullscreen + media keys only.

### Batch 3.1 — Always-on-top toggle *(shipped 2026-04-15, awaiting verify)*

- `VideoPlayer::m_alwaysOnTop` member + `toggleAlwaysOnTop()` method. Applies `Qt::WindowStaysOnTopHint` to the top-level window via `window()->setWindowFlag + show()` (Qt requires both for runtime flag changes). Persists to `QSettings("player/alwaysOnTop")`; reads in constructor, applies first in `showEvent` via a one-shot guard so the top-level native handle exists.
- Keyboard shortcut: **Ctrl+T**. Plain `T` is already bound to `open_subtitle_menu` — conflict confirmed by KeyBindings.cpp inspection, used Ctrl+T instead. Added to DEFAULTS so the settings-editable keybinding system picks it up.
- Context menu: new checkable "Always on Top\tCtrl+T" entry below Fullscreen. Check mark reflects current state via `data.alwaysOnTop` populated from `m_alwaysOnTop`.
- Files: `src/ui/player/VideoPlayer.h/.cpp`, `src/ui/player/VideoContextMenu.h/.cpp`, `src/ui/player/KeyBindings.cpp`.
- **Success:** toggle works from both context menu and Ctrl+T. Top-level window stays above other apps when enabled. Check mark updates immediately. State persists across app restart (`QSettings("player/alwaysOnTop")`).

### Batch 3.2 — Snapshot export *(shipped 2026-04-15, awaiting verify)*

- `FrameCanvas::captureCurrentFrame()` MVP reads `m_reader->readLatest()` (SHM ring, BGRA) and returns a deep-copied QImage with `Format_ARGB32` (bytes-in-memory matches BGRA on little-endian). `isZeroCopyActive()` exposes the D3D11 zero-copy state so the caller can surface a specific "snapshot unavailable" toast rather than generic failure.
- `VideoPlayer::takeSnapshot()` checks zero-copy first (toast + bail), grabs the QImage, computes save path `{Pictures}/Tankoban Snapshots/{baseName}_{HH-MM-SS}_{ptsSec}s.png`, ensures dir exists, saves PNG, toasts the filename. Uses `QStandardPaths::PicturesLocation`.
- Context menu: "Take Snapshot\tCtrl+S" entry after Always-on-Top.
- Keybinding: **Ctrl+S**. Plain `S` is `cycle_subtitle`; Ctrl+S is the conventional Save shortcut, mnemonic fit.
- **Scope:** includes burned-in subs when sidecar-rendered into the BGRA frame. Both code paths now supported — D3D11 zero-copy readback added via `CreateTexture2D(USAGE_STAGING + CPU_ACCESS_READ) → CopyResource → Map/memcpy-rows/Unmap`, SHM path unchanged. `captureCurrentFrame` dispatches on `m_d3dActive` transparently to the caller.
- Files: `src/ui/player/FrameCanvas.h/.cpp`, `src/ui/player/VideoPlayer.h/.cpp`, `src/ui/player/VideoContextMenu.h/.cpp`, `src/ui/player/KeyBindings.cpp`.
- **Success:** Ctrl+S on a playing video (SHM mode) → PNG in Pictures/Tankoban Snapshots at native resolution with burned-in subs. Toast confirms filename. Zero-copy mode → specific toast, no silent fail.

### Batch 3.3 — Picture-in-picture *(shipped 2026-04-15 as mini-mode fallback; awaiting verify)*

**Chose the mini-mode fallback path** per the TODO's explicit scope clause. Rationale: Agent 3 can't build-verify locally (CONTRACTS.md Build Verification Rule), and second-FrameCanvas has multiple unverified unknowns (swap-chain-per-HWND, shared D3D11 cross-device, SHM double-consumption). Mini-mode delivers identical user outcome at ~100 LOC with zero D3D11 churn; second-window upgrade path stays clean.

- `VideoPlayer::togglePictureInPicture()` on `m_inPip` toggles between normal and mini-PiP.
- Enter: exits fullscreen if active, stashes `{geometry, windowFlags, fullscreen}`, sets `FramelessWindowHint | WindowStaysOnTopHint` on `window()`, resizes to 320x180, parks at `screen->availableGeometry().bottomRight() - 24 px` (multi-monitor aware via `window()->screen()`), hides HUD, toast.
- Exit: restores stashed flags + geometry; if pre-PiP fullscreen was true, re-calls `toggleFullscreen()` to re-enter via the normal path; re-shows HUD.
- Window drag: mouse-press records `globalPos - window()->frameGeometry().topLeft()`; mouse-move-with-left-held calls `window()->move()`. Lets user reposition the frameless mini-window.
- Escape in PiP exits PiP (preempts `back_to_library` binding via guard at top of `keyPressEvent`).
- Keybinding: **Ctrl+P** (plain `P` is `prev_episode`).
- Context menu entry label swaps between "Picture-in-Picture" / "Exit Picture-in-Picture" based on `data.inPip`.
- Double-click-to-fullscreen suppressed in PiP (fullscreen + PiP don't compose).
- Files: `src/ui/player/VideoPlayer.h/.cpp`, `src/ui/player/VideoContextMenu.h/.cpp`, `src/ui/player/KeyBindings.cpp`. No new files.
- **Success:** Ctrl+P on playing video → top-level window shrinks to 320x180 bottom-right, frameless, always-on-top, HUD hidden, playback continues uninterrupted. Drag moves the window. Ctrl+P or Esc restores exactly (including fullscreen if active before).
- **Deferred:** auto-PiP-on-minimize (IINA-style); separate-window PiP with second FrameCanvas; in-PiP play/pause button overlay. All optional upgrades.

Original second-FrameCanvas spec (retained for upgrade reference):

Audit citation: IINA `VideoPIPViewController.swift:11-59` + `MainWindowController.swift:516-525`. Flagged as the most architecturally complex batch in this phase.

- Design: detaching the video surface into a secondary small always-on-top window (~320x180 default, resizable, bottom-right corner default position). Primary player window hides during PiP; restoring exits PiP and re-embeds in primary window.
- Implementation approach — two options:
  1. **Separate `QWidget` window** hosting a second `FrameCanvas` that subscribes to the same `ShmFrameReader`. Main player's FrameCanvas pauses rendering during PiP. Cleaner but duplicates the render surface.
  2. **Reparent the existing FrameCanvas** into a new top-level window. Single render surface, no duplication. Qt reparenting with D3D11 is fragile — the swap chain may need recreation on the new HWND.
- **Recommendation: option 1** (separate window + second FrameCanvas). Less fragile; the tradeoff is a second D3D11 pipeline running, which is acceptable on modern hardware.
- Minimal control overlay on the PiP window: play/pause button + close button (exits PiP back to main window). No seek bar, no context menu, no extras — keep PiP aesthetic minimal per IINA.
- Expose via: (a) context menu "Picture-in-Picture" entry, (b) keyboard shortcut — suggest `P` if unconflicted, (c) automatic on minimize if user enables "Auto-PiP on minimize" in QSettings (IINA-style optional).
- Files: NEW `src/ui/player/PipWindow.h/.cpp`, `src/ui/player/VideoPlayer.h/.cpp` (pip lifecycle), `src/ui/player/VideoContextMenu.cpp`, `src/ui/player/KeyBindings.cpp`.
- **Success:** enter PiP → main window hides, small PiP window appears bottom-right, always-on-top, with play/pause overlay. Click PiP close → main window restores, PiP window closes. Playback uninterrupted through transition.
- **Budget/risk:** this is the highest-risk batch in the plan due to D3D11 + second render surface interaction. If the second-FrameCanvas approach has resource-sharing issues (swap-chain-per-HWND constraints), fall back to a simpler "mini-mode" — hide all controls, shrink main window to PiP size, set always-on-top — same user outcome, simpler engineering. Agent 3's call on execution.

### Phase 3 exit criteria
- Always-on-top toggle functional + persistent.
- Snapshot export to PNG works at native resolution.
- PiP works (or minimal mini-mode fallback ships if PiP proves too fragile).
- Agent 6 review against audit P1 #3 citation chain.

---

## Phase 4 — Opening UX (URL / recent / drag-drop)

**Why:** Audit P1 #4 flagged our opening/loading surface as lagging. Sidecar already accepts URLs + startSeconds (`SidecarProcess.cpp:114-122`), but the Qt shell exposes local-file opening only. IINA + QMPlay2 both expose URL/open-address + recent files + drag-drop multi-file enqueue. This phase surfaces what the sidecar already supports.

### Batch 4.1 — Open URL dialog *(shipped 2026-04-15, awaiting verify)*

- New `OpenUrlDialog` (QDialog, 450x120) with QLineEdit + Open/Cancel buttons. Accept-time guard via `looksLikeUrl(s)` anon-namespace helper (valid `QUrl` + scheme in `{http, https, rtsp, rtmp, file}`); empty or malformed input stays open with text selected rather than accepting garbage.
- Clipboard auto-populate: on construction, reads `QGuiApplication::clipboard()->text()`, inserts if it looks like a URL. Otherwise blank with placeholder `https://example.com/stream.m3u8`.
- `VideoPlayer::showOpenUrlDialog()` `exec()`s the modal, routes accepted URL through existing `openFile()` path. Sidecar's `sendOpen` already preserves `http*` URLs via the `startsWith("http", CaseInsensitive)` guard at SidecarProcess.cpp:118-120 — no new transport logic.
- Context menu entry "Open URL...\tCtrl+U" after PiP entry. Keybinding Ctrl+U (QMPlay2/IINA/VLC convention).
- Files: NEW `src/ui/player/OpenUrlDialog.h/.cpp`, `src/ui/player/VideoPlayer.h/.cpp` (dialog plumbing), `src/ui/player/VideoContextMenu.h/.cpp` (OpenUrl enum + entry), `src/ui/player/KeyBindings.cpp` (Ctrl+U default), `CMakeLists.txt` (add OpenUrlDialog.cpp).
- **Success:** Ctrl+U → dialog opens, clipboard URL auto-filled if present → Open routes through openFile → sidecar plays. Malformed URL stays in dialog rather than silently failing. Cancel closes without side effect.

### Batch 4.2 — Recent files list *(shipped 2026-04-15, awaiting verify)*

- QSettings `player/recentFiles` (QStringList). Dedupe-on-insert, cap 20, most-recent-first. New `VideoPlayer::pushRecentFile(path)` called from the top of `openFile()` (pre-stopPlayback) — crash-recovery bypasses `openFile`, so recents aren't duplicated on sidecar respawn.
- Context menu Recent submenu inserted between Open URL and the Aspect Ratio group in [VideoContextMenu.cpp](src/ui/player/VideoContextMenu.cpp). Shows `QFileInfo::fileName()` for local paths (URL tail for http/rtsp/rtmp), full path in `QAction::toolTip`. Greys out local paths when `QFileInfo::exists()` is false; URL entries always enabled (reachability not probed). `(No recent files)` disabled placeholder when empty. "Clear Recent" separator entry at the bottom.
- `ActionType::OpenRecent` carries `QString` payload; callback prunes-on-missing for unreachable local paths + toast "File no longer exists — removed from recent list". `ClearRecent` removes the QSettings key + toast.

### Batch 4.3 — Drag-drop open + enqueue *(shipped 2026-04-15, awaiting verify)*

- `setAcceptDrops(true)` in VideoPlayer constructor; `dragEnterEvent` / `dropEvent` overrides on VideoPlayer classify and dispatch.
- Drop classification — URLs first (`mime->urls()`), text URL fallback (browser address-bar drag). Local files partition into videos vs subtitles via `player_utils::isSubtitleFile` (extensions `srt/vtt/ass/ssa/sub`).
- Actions per plan:
  - Subtitles + playback active: `sendSetSubtitleUrl(QUrl::fromLocalFile(p), 0, 0)` per entry + toast.
  - Subtitles + no playback: toast "Start a video first to load subtitles", no-op.
  - Single video + no playback: `openFile(path)`.
  - Single video + active: `appendToQueue` (VideoPlayer-side: mutates `m_playlist`, rebuilds `m_playlistDrawer->populate`, updates episode buttons).
  - Multi-video + no playback: `openFile(first, all, 0)` via existing playlist-param path; extras beyond first get "Queued N files" toast.
  - Multi-video + active: batch `appendToQueue` + toast.
  - URL text drop: routed through the same open/queue branches as video files (drag implies intent; no dialog interruption).
- New [PlayerUtils.h](src/ui/player/PlayerUtils.h) header-only deduplicates `looksLikeUrl` (now consumed by OpenUrlDialog + dropEvent) and `isSubtitleFile`.
- Playback-active test: `!m_currentFile.isEmpty()`.
- No change to `PlaylistDrawer` API (rebuild via `populate` with the updated `m_playlist` — existing pattern).
- Files: `src/ui/player/VideoPlayer.h/.cpp`, `src/ui/player/VideoContextMenu.h/.cpp`, `src/ui/player/OpenUrlDialog.cpp` (migrated to `player_utils::looksLikeUrl`), NEW `src/ui/player/PlayerUtils.h`. No CMake change (PlayerUtils.h is header-only).

### Phase 4 exit criteria
- Open URL dialog works from context menu + keyboard.
- Recent Files submenu persists and works.
- Drag-drop handles single, multi, URL, and subtitle cases.
- Agent 6 review against audit P1 #4.

---

## Phase 5 — Queue modes

**Why:** Audit P1 #2 framed PlaylistDrawer as "an episode list with auto-advance, not a queue manager." Users coming from QMPlay2/IINA/mpv expect shuffle, repeat-one, repeat-all, loop-file, and playlist save/load. Currently we have one Auto-advance checkbox.

### Batch 5.1 — Queue mode toggles (shuffle, repeat modes, loop-file) *(shipped 2026-04-15, awaiting sidecar rebuild + verify)*

- `PlaylistDrawer` toolbar row between the header divider and the episode list. Four checkable `QToolButton`s — glyphs ⇄ (Shuffle), ∞ (Repeat All), "1" (Repeat One), ⟲ (Loop File). Unicode characters, not emojis; stay gray per `feedback_no_color_no_emoji`. Tooltips spell out the names. State persisted via `QSettings("player/queueMode/shuffle|repeatAll|repeatOne|loopFile")`; loaded at construction.
- Precedence at EOF (behavior-side, not UI-exclusive — user may leave all four checked): `loopFile > repeatOne > (atEnd + repeatAll) > shuffle > auto-advance`. Implemented in `VideoPlayer::onEndOfFile`.
- `shuffle`: picks a random other index in the playlist (bounded retry + fallback to `(idx+1) % N`). Carries audio/sub lang preferences per existing nextEpisode pattern.
- `repeatAll`: wraps to index 0 when at end-of-queue and more than one item exists.
- `repeatOne`: `sendSeek(0.0)` on EOF. Client-side — zero sidecar cost.
- `loopFile`: sidecar-side. Main-app sends `set_loop_file` command; sidecar's `VideoDecoder::loop_file_` atomic gates an EOF seek-to-0 + codec flush + clock anchor reset, skipping the terminal `eof` event entirely. Avoids probe+open roundtrip per loop. `onSidecarReady` re-pushes the persisted state on each sidecar respawn so crash recovery honors it. Pre-5.1 sidecar binaries don't know the command → return NOT_IMPLEMENTED → swallowed to debug log by SidecarProcess; client-side `onEndOfFile` still handles loopFile via `sendSeek(0.0)` as a fallback.
- Files: `src/ui/player/PlaylistDrawer.h/.cpp` (toolbar + state + signal), `src/ui/player/VideoPlayer.h/.cpp` (onEndOfFile precedence + onSidecarReady state push + loopFileChanged connect), `src/ui/player/SidecarProcess.h/.cpp` (`sendSetLoopFile`), `native_sidecar/src/video_decoder.h/.cpp` (loop_file_ atomic + EOF seek-to-0 branch), `native_sidecar/src/main.cpp` (set_loop_file command dispatch).
- No CMake change — all files pre-existing.
- Sidecar rebuild required for the fast-path loop (no probe+open per iteration). Main-app-side fallback keeps Loop File functional on pre-5.1 sidecars via client-driven seek.
- **Success:** enable each mode in isolation and verify the expected EOF behavior. Combined modes follow the documented precedence.

### Batch 5.2 — Playlist save/load (.m3u) *(shipped 2026-04-15, awaiting verify)*

- PlaylistDrawer toolbar gains "Save" + "Load" text buttons after the queue-mode toggles (non-checkable, right-aligned post-stretch, 24 px tall, subtle hover, stays gray per `feedback_no_color_no_emoji`).
- `PlaylistDrawer::saveRequested()` + `loadRequested()` signals — UI surface only. VideoPlayer owns state + dialogs + format parse.
- **Save** (`VideoPlayer::saveQueue`): QFileDialog to `{Music}/playlist.m3u` default. Writes `#EXTM3U` header + `#EXTINF:-1,{basename}` + path per entry, UTF-8 (`QStringConverter::Utf8`). Saves `m_playlist` when non-empty, else the lone `m_currentFile`. Toast confirms count + filename.
- **Load** (`VideoPlayer::loadQueue`): QFileDialog for `.m3u/.m3u8`. Parse skips empty lines + `#` comments; remaining lines are paths. Relative paths resolved against the .m3u's directory (standard player behavior). URL-shaped lines (`player_utils::looksLikeUrl`) kept as-is. Empty result → toast + bail.
- **Replace vs Append** when current playlist/file non-empty: QMessageBox prompts Replace / Append / Cancel. Append calls `appendToQueue` per entry; Replace calls `openFile(first, all, 0)`.
- `.pls` support deferred — `.m3u` covers the 90% case and is universally supported.
- Files: `src/ui/player/PlaylistDrawer.h/.cpp` (toolbar + signals), `src/ui/player/VideoPlayer.h/.cpp` (save/load impl + dialog + format).
- No CMake change.
- **Success:** build a queue of 5 files, save → .m3u opens in VLC with all 5. Load the same .m3u back → Tankoban restores the queue.

### Phase 5 exit criteria
- Shuffle, Repeat All, Repeat One, Loop File all work from the PlaylistDrawer toolbar.
- Playlist save/load round-trips correctly.
- Agent 6 review against audit P1 #2 citation chain.

---

## Phase 6 — Shortcuts editor (replace static ShortcutsOverlay)

**Why:** Audit P1 #5 flagged the current ShortcutsOverlay as stale and incomplete. It's missing period/comma frame step, PageUp/PageDown chapter navigation, Ctrl audio delay keys, and lists bracket variants for speed that aren't in the default KeyBindings table. Meanwhile `KeyBindings.cpp` already persists bindings via QSettings — the data layer is ready; only the editor UI is missing. QMPlay2's `KeyBindingsDialog.cpp` is the reference.

### Batch 6.1 — Keybinding editor dialog *(shipped 2026-04-15, awaiting verify)*

- NEW [`KeybindingEditor`](src/ui/player/KeybindingEditor.h) modal — 4-column QTableWidget: Action, Shortcut, Default, Reset (per-row button). Populated from `KeyBindings::allActions()`. Cell click on "Shortcut" enters capture mode; next non-modifier keyPress sets the binding via `KeyBindings::setBinding()` which persists to QSettings immediately. Escape during capture cancels. Modifier-only presses ignored (wait for real key).
- Duplicate detection: when captured sequence matches another action's current binding, QMessageBox prompts "Unbind & Reassign" vs Cancel. Unbind sets the other action to empty sequence; editor redraws with "(unbound)" label.
- "Reset All" button at bottom of dialog: calls `KeyBindings::resetDefaults()` (wipes QSettings key, repopulates from DEFAULTS). Per-row "Reset" button calls new `KeyBindings::resetAction()` which is a targeted counterpart.
- New `KeyBindings::defaultKeyForAction(action)` static helper exposes the factory binding for the Default column.
- `ShortcutsOverlay` deleted (class files removed; VideoPlayer member + includes + resize/raise/keyPressEvent intercept + buildUI construct + action dispatch all stripped). `show_shortcuts` action (`?` key) + context menu "Keyboard Shortcuts...\t?" entry now open the new editor.
- Files: NEW `src/ui/player/KeybindingEditor.h/.cpp`; DELETED `src/ui/player/ShortcutsOverlay.h/.cpp`; `src/ui/player/KeyBindings.h/.cpp` (two helpers); `src/ui/player/VideoPlayer.h/.cpp` (editor plumbing + ShortcutsOverlay removal); `src/ui/player/VideoContextMenu.h/.cpp` (OpenKeybindings enum + entry); `CMakeLists.txt` (-ShortcutsOverlay.cpp, +KeybindingEditor.cpp).
- **Success:** press `?` → editor opens. Rebind any action → binding persists across app restart. Reset All restores defaults. Duplicate binding triggers prompt rather than silent conflict.

### Phase 6 exit criteria
- Every shipped shortcut appears in the editor with its current + default binding.
- Rebinding works + persists.
- Duplicate detection prevents silent conflicts.
- Agent 6 review against audit P1 #5 citation chain.

---

## Phase 7 — Stats badge (stretch, optional)

**Why:** Audit P1 #6 flagged performance visibility as diagnostic-only (Ctrl+Shift+V writes CSV, no in-player surface). IINA-identity wants polish, not a full mpv-style stats overlay — but a compact codec/resolution/FPS/dropped-frames badge gives enough for power users without cluttering the HUD. Stretch because the polish/flash phases closed the actual performance problems; this is visibility, not correctness.

### Batch 7.1 — Compact stats badge (toggle) *(shipped 2026-04-15, awaiting sidecar rebuild + verify)*

- NEW [`StatsBadge`](src/ui/player/StatsBadge.h) widget — single-line label in a translucent dark pill, monospace text. Format: `{codec} · {W}×{H} · {fps} fps · {drops} drops`. "—" fallback for unknown fps/drops so a pre-7.1 sidecar binary still renders a usable badge (codec + resolution arrive in firstFrame pre-7.1 already).
- Top-right positioning: `width - sh.width() - 12, y=52` — below the toast strip so both can coexist without overlap.
- Sidecar-side addition (rebuild required): `ProbeResult::fps` sourced from `AVStream::avg_frame_rate` (fallback to `r_frame_rate`), emitted in `first_frame` event payload.
- Client-side: `VideoPlayer::onFirstFrame` parses fps + codec + dims into `m_statsFps/codec/Width/Height`. Stats ticker runs at 1 Hz when badge visible, polling `FrameCanvas::framesSkipped()` for the drops field.
- Toggle via `toggle_stats` action (default `I`) or context menu "Show Stats" (checkable, reflects `m_showStats`). State persists via QSettings `player/showStats`; applied at startup from saved value.
- `FrameCanvas::framesSkipped()` accessor exposes the existing `m_framesSkippedTotal` counter (Batch 1.2 lag-skip guard).
- Files: NEW `src/ui/player/StatsBadge.h/.cpp`; `src/ui/player/VideoPlayer.h/.cpp` (state + ticker + toggle + positioning); `src/ui/player/FrameCanvas.h` (accessor); `src/ui/player/VideoContextMenu.h/.cpp` (ToggleStats enum + entry); `src/ui/player/KeyBindings.cpp` (toggle_stats default); `CMakeLists.txt` (+StatsBadge.cpp); sidecar `demuxer.h/.cpp` + `main.cpp` (fps propagation).
- **Success:** press I → compact badge top-right shows `h264 · 1920×1080 · 23.976 fps · 0 drops`. Press I again → hides. Persists. Pre-7.1 sidecar shows "— fps" until rebuild.

### Phase 7 exit criteria (stretch)
- Stats badge toggleable + populates correctly from shipped sidecar events.
- Aesthetic matches existing HUD (no color accents, consistent typography).
- Agent 6 review against audit P1 #6.

**Ship-independently note:** Phase 7 is a stretch — it can land after Phase 6 OR be deferred indefinitely. Don't gate Phase 6 commit on Phase 7 readiness. If Agent 3's time is better spent elsewhere (player polish regressions, Stream-mode integration issues), drop Phase 7 without reopening this plan.

---

## Cross-Cutting Concerns

### Ordering
Phase 1 is the critical path — validation-first. If Batch 1.1 confirms the subtitle Off bug is broken, Phase 1 becomes retroactively P0 and must ship in isolation before any other phase. If safe, Phase 1 is still worth shipping first as a light cleanup.

After Phase 1: Phase 2 → 3 → 4 → 5 → 6 sequentially. Each is mostly independent but ordering reflects user-visible impact ranking (seekbar richness is highest-visible, stats badge is lowest). Phase 7 is stretch — land after 6 or drop.

One exception: Phase 3 Batch 3.3 (PiP) is the highest-risk batch in the plan due to D3D11 + second render surface interaction. If Agent 3 sees it might stall, split into "PiP infrastructure" + "PiP UX polish" as two batches, or fall back to mini-mode (shrunk window + always-on-top) for simpler shipping.

### Agent roles per phase
All phases are Agent 3's domain. No cross-agent touches. Sidecar edits (Batch 2.2 seekable range, Batch 5.1 loop-file) either happen in `TankobanQTGroundWork/native_sidecar/` OR in the migrated in-repo `Tankoban 2/native_sidecar/` depending on whether the Option 1 sidecar migration has shipped by then. Either location stays within Agent 3's territory.

### Agent 6 review gates
At each phase exit: `READY FOR REVIEW — [Agent 3, Phase X]: Video Player UX | Objective: Phase X per plan file radiant-foraging-flask.md + agents/audits/video_player_2026-04-15.md. Files: ...`. Agent 6 reviews against this plan + the audit as co-objective. No phase moves to commit (Rule 11) until `REVIEW PASSED`.

### Rule 11 commit gates
Per-batch `READY TO COMMIT` in chat.md. Agent 0 batches commits at phase boundaries, not per-batch, unless a batch is particularly risky. Isolate-commit candidates: Phase 1 Batch 1.2 (if subtitle fix lands — cross-sidecar change), Phase 3 Batch 3.3 (PiP — architectural risk), Phase 2 Batch 2.3 (thumbnail extractor — new infrastructure with potential for unbounded disk use if gates wrong).

### What's retained from prior plans (do NOT re-open)
- Player Polish Phases 1-6 shipped work: D3D11 pipeline, shader pipeline, HDR color processing, audio drift/amp/DRC, subtitle renderer internals, frame-pacing smoothness. Not touched by this plan.
- PLAYER_POLISH_TODO.md tracks any remaining polish debt (Phase 8+ not yet planned) — not conflated with this UX plan.
- Sidecar migration (vendor native_sidecar into Tankoban 2, Option 1 ratified) is orthogonal to this plan. Agent 3 can land the migration before OR after this plan's phases — no dependency either way.

---

## Critical Files Modified

**New (to be created):**
- `src/ui/player/ThumbnailExtractor.h/.cpp` (Phase 2 Batch 2.3 — background thumbnail worker)
- `src/ui/player/PipWindow.h/.cpp` (Phase 3 Batch 3.3 — PiP window + second FrameCanvas)
- `src/ui/player/OpenUrlDialog.h/.cpp` (Phase 4 Batch 4.1)
- `src/ui/player/KeybindingEditor.h/.cpp` (Phase 6 Batch 6.1)
- `src/ui/player/StatsBadge.h/.cpp` (Phase 7 Batch 7.1, stretch)

**Modified (by phase):**
- Phase 1: `src/ui/player/VideoPlayer.h/.cpp`, `src/ui/player/SubtitleMenu.cpp`, `src/ui/player/TrackPopover.cpp`, `src/ui/player/SidecarProcess.h/.cpp`. If subtitle Off validation confirms broken: sidecar `main.cpp:802-884` (std::stoi guard).
- Phase 2: `src/ui/player/SeekSlider.h/.cpp` (chapter markers, thumbnail hover, range band), `src/ui/player/VideoPlayer.h/.cpp` (chapter push, thumbnail extractor kickoff), `src/ui/player/SidecarProcess.h/.cpp` (seekable range event). Sidecar-side: `main.cpp` + demuxer surface for seekable-range event.
- Phase 3: `src/ui/player/VideoPlayer.h/.cpp` (always-on-top + snapshot + PiP wiring), `src/ui/player/FrameCanvas.h/.cpp` (capture method), `src/ui/player/VideoContextMenu.cpp`, `src/ui/player/KeyBindings.cpp`.
- Phase 4: `src/ui/player/VideoPlayer.h/.cpp` (drag-drop + recent files + Ctrl+U), `src/ui/player/VideoContextMenu.cpp` (Recent submenu), `src/ui/player/PlaylistDrawer.h/.cpp` (append API if not present).
- Phase 5: `src/ui/player/PlaylistDrawer.h/.cpp` (toolbar + queue modes + save/load), `src/ui/player/VideoPlayer.h/.cpp` (EOF reads queue mode), `src/ui/player/SidecarProcess.h/.cpp` (setLoopFile). Sidecar-side: `main.cpp` + `video_decoder.cpp` (loop-seek).
- Phase 6: `src/ui/player/ShortcutsOverlay.h/.cpp` (strip or delete), `src/ui/player/KeyBindings.h/.cpp` (list-all-actions API), `src/ui/player/VideoContextMenu.cpp` (editor entry), `src/ui/player/VideoPlayer.cpp` (route `?` to editor).
- Phase 7: `src/ui/player/VideoPlayer.h/.cpp` (stats data + toggle).

**Reference (read-only during implementation):**
- `agents/audits/video_player_2026-04-15.md` — the audit. Phase-by-phase objective source.
- `C:/Users/Suprabha/Downloads/Video player reference/iina-develop/` — UX reference. Key files already cited per-phase. Read for UX patterns, NOT for Swift-specific implementation.
- `C:/Users/Suprabha/Downloads/Video player reference/QMPlay2-master/` — Qt/FFmpeg architectural cousin. Read for implementation patterns.
- `C:/Users/Suprabha/Downloads/Video player reference/mpv-master/` — `etc/input.conf` for keybinding conventions, `player/lua/osc.lua` for OSC design ideas (selectively — IINA-identity, not mpv).
- `C:/Users/Suprabha/Downloads/Video player reference/secondary reference/vlc-master/` — consult only, not primary reference.
- Existing `PLAYER_POLISH_TODO.md` at repo root — what the polish phases already closed. Phase boundaries there are independent of this plan's phases.

---

## Risks & Mitigations

1. **Phase 1 subtitle Off fix is sidecar-cross-cutting if Batch 1.1 validates broken.** Mitigation: sidecar `main.cpp:802-884` needs a try/catch around the `std::stoi` call OR an explicit integer check before stoi. Either way, it's a 3-5 line sidecar change. Validate direction during Batch 1.1 before coding. If migration to in-repo sidecar has shipped by this point, the path is local; if not, Agent 3 edits the groundwork location per existing workflow.

2. **Phase 2 Batch 2.3 (thumbnail extractor) is the plan's biggest batch.** Mitigation: split into two sub-batches if budget looks tight — (a) extractor infrastructure with cache + on-disk writes but no UI, then (b) SeekSlider hover rendering using cached thumbnails. Each sub-batch builds + ships + smoke-tests independently. Also: strict disk cap (500MB LRU) prevents runaway disk use.

3. **Phase 3 Batch 3.3 (PiP) is the plan's highest architectural risk.** Mitigation: fall-back path defined upfront — if second-FrameCanvas approach has D3D11 swap-chain-per-HWND issues, ship "mini-mode" instead (shrink main window, always-on-top, hide controls, same user outcome). Don't block Phase 4 on PiP — if Batch 3.3 stalls, ship Batches 3.1 + 3.2 alone and reopen 3.3 later.

4. **Phase 5 Batch 5.1 Loop File requires a sidecar command.** Mitigation: sidecar-side implementation is an AVDemuxer seek-to-0 on EOF — same primitive already used for the existing finished-at-90% detection. Additive, non-regressive.

5. **Phase 6 keybinding editor could conflict with persisted QSettings bindings from earlier versions.** Mitigation: versioned settings key (`player/keybindings_v2` if schema changes); Reset to Defaults button lets users escape any mismatch. Migration code in `KeyBindings::load` handles v1 → v2 if needed.

6. **Scope creep.** Explicitly enumerated non-goals above. mpv-identity features (scripting hooks, wider speed range, deep stats overlay, A-B loop) and QMPlay2-identity features (InfoDock panel, equalizer presets, FFmpeg filter text path) are all deliberately deferred. Agent 6 will flag any batch that drifts toward those.

7. **Sidecar migration timing.** If Option 1 sidecar vendoring hasn't landed by the time Agent 3 starts this plan, Batch 2.2 + Batch 5.1 sidecar edits still happen in `TankobanQTGroundWork/native_sidecar/`. If it has landed, they happen in `Tankoban 2/native_sidecar/`. Either location is within Agent 3's scope; no coordination hit.

---

## Verification

End-to-end test plan after each phase:

**Phase 1:** (1) Validation findings posted. (2) If broken: toggle subtitles Off from all three entry points → each one cleanly disables subtitles without crash or console error. (3) If safe: same test, just confirming the cleanup didn't regress anything.

**Phase 2:** (1) Open file with chapters → tick marks on seekbar, tooltip on hover. (2) Play HTTP stream → subtle buffered-range band visible. (3) Open local 45-min file → within 5 seconds, hover seekbar shows thumbnail at cursor position.

**Phase 3:** (1) Toggle always-on-top from context menu → window stays on top. Restart app → state persists. (2) Snapshot → PNG in Pictures folder at native resolution, reveal-in-explorer toast appears. (3) Enter PiP → small window bottom-right, main hides. Exit PiP → main restores. Playback uninterrupted.

**Phase 4:** (1) Ctrl+U → Open URL dialog, paste valid stream URL → plays. (2) Open several files, restart, context menu → Recent submenu shows them in order. (3) Drag one file in → opens. Drag two → queued. Drag .srt during playback → subtitle loads.

**Phase 5:** (1) Enable Shuffle → next file picks randomly. (2) Enable Repeat All → queue loops. (3) Enable Repeat One → current file replays on EOF without re-probe. (4) Enable Loop File → single file loops silently. (5) Save queue to .m3u, load in VLC → same queue loads.

**Phase 6:** Press `?` → keybinding editor opens. Rebind Play/Pause to `K`. Restart app → `K` still works. Assign a duplicate key → warning appears. Reset to Defaults → original map restored.

**Phase 7 (stretch):** Press `I` → compact stats badge appears top-right. Plays a known HDR file → codec + resolution + fps + drops all populate correctly. Press `I` again → hides. Restart → state persists.

**Build verification per Rule 6:** every batch ends with `build_and_run.bat` → exit 0 → feature smoke-tested in the running app → Agent 3 pastes build exit code + one-line smoke result in chat.md before posting `READY TO COMMIT`. No batch declared done on compile alone. Phase 1 Batch 1.1 is the one exception — pure observation, no code change, no build required.
