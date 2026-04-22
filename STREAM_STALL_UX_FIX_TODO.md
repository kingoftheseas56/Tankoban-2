# STREAM_STALL_UX_FIX TODO — **CLOSED 2026-04-22 20:44 (empirical smoke GREEN, all three batches shipped + validated)**

> **Closure note (Agent 4, 2026-04-22):** All three batches now live in the binary + validated on Invincible S01E03 Torrentio EZTV (hash ae017c71) via combined smoke this wake. Evidence: [out/_player_debug_204000_AV_SUB_SYNC_ITER2_COMBINED_SMOKE_GREEN.txt](out/_player_debug_204000_AV_SUB_SYNC_ITER2_COMBINED_SMOKE_GREEN.txt) + [out/sidecar_debug_204000_AV_SUB_SYNC_ITER2_COMBINED_SMOKE_GREEN.log](out/sidecar_debug_204000_AV_SUB_SYNC_ITER2_COMBINED_SMOKE_GREEN.log) — 8 distinct stall windows across ~90 s of forced piece-starvation.
>
> - **Batch 1 (HUD time-gating):** shipped at [VideoPlayer.cpp:1045](src/ui/player/VideoPlayer.cpp#L1045) — `const bool gateHud = m_streamMode && m_streamStalled` pins seekbar + timeLabel + m_lastKnownPosSec during stall. Git-blame traces to cb8a52b (swept under PLAYER_STREMIO_PARITY tag per Agent 0's interleave-sweep disclosure). Smoke confirmed HUD time does NOT advance during stall windows — time_update IPC keeps flowing at 1 Hz during stall but carries a FROZEN PTS value because AudioDecoder::pause transitively freezes AVSyncClock; downstream the onTimeUpdate gate suppresses the visual update anyway.
> - **Batch 2 (LoadingOverlay reappearance with "Buffering — waiting for piece N (K peers have it)" text):** shipped at c868e9c via edge-signal wiring at [StreamPage.cpp:1884-1946](src/ui/pages/StreamPage.cpp#L1884). 8/8 stall windows rendered correct text with live `TorrentEngine::peersWithPiece()` peer-counts (117, 124, 125, 217, 226, 202 observed). Dedup works: 28 `stall_detected` telemetry emissions → 8 debounced UI transitions → 8 overlay paints.
> - **Batch 3 (audio freeze alignment):** shipped via Agent 7 Trigger-D sidecar delivery STREAM_AV_SUB_SYNC_AFTER_STALL iter 2 — handle_stall_pause freezes AudioDecoder + VideoDecoder + AVSyncClock, handle_stall_resume runs flush_queue (Pa_AbortStream + Pa_StartStream to drop stale PortAudio samples) + seek_anchor clock to last_rendered_pts_us + clear_active_subs via ass_flush_events + PGS clear. 8/8 "AudioDecoder: flush_queue cleared PortAudio output buffer" log lines, zero PortAudio errors. time_update resumes avg 160 ms post-stall_resume (target was <1-2 s).
>
> Remaining Option B-full (video + subtitle packet-queue flush + decoder-ready wait) from [agents/audits/av_sub_sync_after_stall_2026-04-21.md](agents/audits/av_sub_sync_after_stall_2026-04-21.md) §130-133 is NOT adopted — current Option A+C+D+half-B is sufficient for the 15-37 s stall windows observed in this smoke. Option B-full is a conditional follow-up if post-recovery drift ever proves itself on longer stalls.

**Owner:** Agent 4 (Stream mode) primary. Touches VideoPlayer (Agent 3 domain) — coordinate on first batch.

**Created:** 2026-04-21 by Agent 4 after Hemanth observed the bug in-person during Wake 3 cold-open smoke. Agent 4 observed the same pattern in telemetry (stall_detected fired twice during that smoke on pieces 2 and 4) but flagged it as "separate scope" until Hemanth confirmed it's actively hurting the experience.

---

## Symptom (Hemanth's verbatim report 2026-04-21 ~09:00)

> "the screen froze but the time in hud keeps going... occasionally you could hear the voice. this is a massive massive glitch."

Concrete behavior:
1. **Video frame freezes on the last-decoded frame** — screen shows a single frame, no motion.
2. **HUD time keeps incrementing** — the timer at the bottom of the player advances as if playback is normal, which makes the user think something is broken on the RENDER side when actually it's a scheduler stall.
3. **Audio occasionally keeps playing in bursts** — snippets of voice come through during the freeze. This is the key tell: the **audio decoder has buffered ahead of the video decoder**, so when video stalls on a specific piece, audio can keep playing from its own prefetch until it too runs dry.
4. **No visible "Buffering..." overlay during the freeze** — the cold-open LoadingOverlay vanished once playback started and doesn't come back when a mid-playback stall fires.

Repro: random. Hemanth noted "it happens at random times." My telemetry from Invincible S01E03 Torrentio EZTV smoke (hash `07a38579` at 2026-04-21T03:23-03:25Z) caught two stalls inside a ~2 minute window:
- `stall_detected piece=2 wait_ms=5063 peer_have_count=20` → recovered after 13 s
- `stall_detected piece=4 wait_ms=5986 peer_have_count=19` → recovered after 32 s

Hemanth's frozen-screen observation happened during one of those 13-32 s windows.

---

## Why the HUD clock keeps ticking (root cause of bug 1)

The main-app `VideoPlayer::onTimeUpdate(positionSec, durationSec)` is called from a sidecar-emitted `timeUpdate` IPC event. The sidecar currently emits `timeUpdate` based on its own clock / last-decoded-PTS extrapolation — NOT based on actual new-frame-rendered-to-screen events. So during a stall:

- Sidecar's **demuxer** stalls (no new packets from the HTTP server because StreamEngine is waiting for piece 2 or 4 to arrive).
- Sidecar's **video decoder** runs out of buffered packets and stops producing new frames (FrameCanvas keeps painting the last frame).
- Sidecar's **audio decoder** has some buffered packets in its own lead time and keeps producing samples for ~3-10 s until it too runs dry.
- Sidecar's **clock / PTS extrapolator** keeps ticking forward regardless and reports advancing positionSec to main app every ~500 ms.
- Main app `VideoPlayer::onTimeUpdate` updates `m_timeLabel->setText(formatTime(posMs))` + `m_seekBar->setValue(posMs)` — HUD shows time advancing.

The fix path: gate the HUD time updates on stream-engine stall state. StreamEngineStats already has `stalled` + `stallElapsedMs` from P5 watchdog — it's an additive projection we just need to consume.

---

## Why the stall overlay doesn't appear (root cause of bug 2)

LoadingOverlay in VideoPlayer is designed for the cold-open window — it shows during sidecar states {Opening, Probing, OpeningDecoder, DecodingFirstFrame, Buffering, TakingLonger} and auto-hides on `firstFrame` event. Once playback starts, it's hidden and has no re-show path.

StreamEngine emits `statsUpdated(StreamEngineStats)` every ~5 s (StreamPage's telemetry tick). Agent 3's STATUS notes Phase 2 Batch 2.2 added a Qt cache_state consumer chain + LoadingOverlay "Buffering — N% (~Xs)" progress text — that's the cold-open side. For mid-playback stall, we need the LoadingOverlay (or a distinct StallOverlay widget) to re-appear when `statsUpdated.stalled == true` and auto-hide when it flips back to false.

Prior STATUS memory tagged this as "Agent 5 follow-up (~5-line consumer hook on bufferUpdate chain)" back in STREAM_ENGINE_REBUILD P5 ship. The wiring wasn't ever completed.

---

## Proposed fix — 2 batches, both small

### Batch 1 — HUD time-gating on StreamEngineStats.stalled

File: [src/ui/player/VideoPlayer.cpp](src/ui/player/VideoPlayer.cpp) (`onTimeUpdate` handler).

Behavior change:
- When stream mode is active AND the current stream's `stalled` flag is true, skip the `m_timeLabel->setText + m_seekBar->setValue` calls. Keep the saveProgress call (saveProgress on last known good position is fine, no harm).
- When `stalled` flag is false (playing), restore normal updates.

Implementation shape: `VideoPlayer` already has a reference to `StreamEngine` via the StreamPlayerController or similar. Add a `bool m_streamStalled = false` cached flag set from `statsUpdated` signal consumer. Gate the HUD update:
```cpp
if (!m_streamStalled) {
    m_timeLabel->setText(...);
    m_seekBar->blockSignals(true); m_seekBar->setValue(...); m_seekBar->blockSignals(false);
}
```

Smoke verification: trigger a stall (currently intermittent; retry Invincible S01E03 Torrentio EZTV a few times, or artificially — set a breakpoint / env flag to force a stall). During the stall, HUD time should freeze at the last-good position. When recovered, HUD should resume.

**Rule 14 call:** Keep `durationSec` update going even during stall (duration doesn't change mid-playback; no UX cost). Only gate `positionSec`.

### Batch 2 — Stall overlay reappearance during mid-playback stall

File: [src/ui/pages/stream/StreamPlayerController.cpp](src/ui/pages/stream/StreamPlayerController.cpp) + [src/ui/player/VideoPlayer.cpp](src/ui/player/VideoPlayer.cpp) + [src/ui/player/LoadingOverlay.*](src/ui/player/LoadingOverlay.h) (if it exists; else a new StallOverlay).

Behavior change:
- On `statsUpdated` signal consumed by StreamPlayerController, if `stats.stalled == true` and we're currently in "playing" state (past cold-open), forward an event to VideoPlayer that re-shows the LoadingOverlay with text `"Buffering — waiting for piece %1 (%2 peers have it)"` using `stats.stallPiece` + `stats.stallPeerHaveCount`.
- On `stats.stalled == false` flipping back, auto-hide the overlay.

Peer count surface gives the user honest "swarm is slow, not your fault" diagnostic: peer_have_count=0 → "stream source is incomplete in the swarm, may fail"; peer_have_count>0 → "peers have it, just slow — wait it out."

Edge cases:
- Stall triggered during seek — don't double-show (seek already triggers its own brief overlay).
- Stall triggered right at the moment user pauses — freeze the overlay in place; hide on unpause.

### Batch 3 (maybe, post-smoke) — audio freeze alignment

If Batch 1+2 don't fully address the "audio occasionally plays in bursts during freeze" side effect, we may need to signal the sidecar to pause its audio clock / freeze audio emit when main-app receives stall_detected. That's a sidecar protocol extension (new `pause_audio_during_stall` command or similar). Deferred unless Hemanth reports audio-bursts still feel wrong after Batch 1+2 ship.

---

## Files expected to touch

- `src/ui/player/VideoPlayer.cpp` — `onTimeUpdate` gating + new `setStreamStalled(bool)` accessor
- `src/ui/player/VideoPlayer.h` — new member + accessor decl
- `src/ui/pages/stream/StreamPlayerController.cpp` — wire statsUpdated → VideoPlayer::setStreamStalled + overlay show/hide
- `src/ui/pages/stream/StreamPlayerController.h` — signal/slot decls if new
- (maybe) `src/ui/player/LoadingOverlay.*` — new setStallMessage API for stall-specific text

Zero sidecar changes in Batch 1+2. Zero StreamEngine API changes (stalled + stallPiece + stallPeerHaveCount are already on StreamEngineStats). 12-method API freeze preserved.

---

## Rollback strategy

Batch 1 revert: remove the `m_streamStalled` check in `onTimeUpdate`. HUD reverts to always-ticking (the bug).
Batch 2 revert: remove the statsUpdated → overlay wire. Overlay reverts to cold-open-only behavior.
Either batch safe to revert independently; no schema / protocol changes.

---

## Exit criterion

On a fresh Tankoban launch, play any Torrentio EZTV Invincible episode. If a stall fires mid-playback:
1. HUD time freezes at the last-good position (Batch 1 verdict).
2. Buffering overlay appears with "waiting for piece X, N peers have it" text (Batch 2 verdict).
3. On stall recovery, HUD resumes and overlay disappears.

Optional: confirm audio burst behavior. If audio cuts out cleanly during freeze (no burst), Batch 3 unnecessary. If audio still has disruptive bursts, Batch 3 scope gets scheduled.
