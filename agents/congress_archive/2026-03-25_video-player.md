# Congress 2 Archive — Video Player Parity
**Opened**: 2026-03-25
**Closed**: 2026-03-25
**Motion**: Bring the C++ video player to 1:1 parity with the groundwork ffmpeg player
**Prep doc**: `agents/congress_prep_player.md`
**Domain master**: Agent 3 (Video Player)
**Outcome**: COMPLETE — all 10 batches A–J shipped, build clean

---

## What Was Built

Starting from a functional but structurally incomplete video player (single-row HUD, no popovers, no subtitle rendering, minimal protocol), Congress 2 brought it to full groundwork parity.

| Batch | Scope | Owner | Outcome |
|-------|-------|-------|---------|
| A | SidecarProcess: 5 commands + 6 events | Agent 3 | DONE — snake_case wire format per groundwork protocol |
| B | HUD two-row layout restructure | Agent 3 | DONE — seek row + controls row, chip buttons, title label |
| C | SeekSlider overhaul (range/color/throttle/bubble) | Agent 3 | DONE — warm amber, 0-10000 range, 80ms throttle |
| D | ToastHud wiring | Agent 3 | DONE — 6 call sites, error off timeLabel |
| E | Speed chip QMenu (8 presets) | Agent 3 | DONE — checkmark, toast, keyboard cycling preserved |
| F | FilterPopover wiring | Agent 3 | DONE — filtersChanged → sendSetFilters, HUD hover guard |
| G | TrackPopover wiring | Agent 3 | DONE — audio/sub selection, delay, style forwarding |
| H | SubtitleOverlay wiring | Agent 3 | DONE — sibling of FrameCanvas, HUD offset sync |
| I | CenterFlash + VolumeHud polish + button styles | Agent 3 | DONE — circle shape, 160x32, #cccccc icons, fade-ins |
| J | Keyboard shortcuts + HUD guards + auto-advance | Agent 3 | DONE — <> DA, cursor timer, auto-advance guard |

## Parallel Component Builds

| Agent | Component | Status |
|-------|-----------|--------|
| Agent 1 | ToastHud.h/.cpp | Built and handed off. Wired in Batch D. |
| Agent 2 | SubtitleOverlay.h/.cpp | Built and handed off. Wired in Batch H. |
| Agent 4 | FilterPopover.h/.cpp | Built and handed off. Wired in Batch F. |
| Agent 5 | TrackPopover.h/.cpp | Built and handed off. Wired in Batch G. |

## Agent 3 Position (filed 2026-03-25)

Accepted all 10 batches. Four technical concerns raised and resolved:
1. **Sidecar binary ownership** — confirmed ffmpeg_sidecar.exe is Python-built groundwork binary that already implements all protocol commands. C++ client side only.
2. **QMenu anchor** — corrected from `bottomLeft()` to `topLeft()`. Qt auto-flips upward above the HUD.
3. **Event filter safety** — always remove in `hide()`, call `hide()` in destructor.
4. **QRhiWidget subtitle z-order** — SubtitleOverlay must be sibling of FrameCanvas, not child. `raise()` after every resizeEvent.

## Batch Assignments (Agent 0)

Full specs preserved in this archive for reference.

### BATCH A — SidecarProcess protocol additions

New commands: `sendSetSubDelay`, `sendSetSubStyle`, `sendLoadExternalSub`, `sendSetFilters`, `sendResize`
New signals: `subtitleText`, `subVisibilityChanged`, `subDelayChanged`, `filtersChanged`, `mediaInfo`, `processClosed`
Wire format: snake_case fields per groundwork `protocol.py`. `set_filters` takes ffmpeg filter strings constructed internally.

### BATCH B — HUD two-row layout

Row 1: `[m_timeLabel 48px]` `[m_seekBackBtn 28×28]` `[m_seekBar stretch]` `[m_seekFwdBtn 28×28]` `[m_durLabel 48px]`
Row 2: `[m_backBtn 30×30]` 8px `[m_prevEpisodeBtn 32×32]` `[m_playPauseBtn 40×36]` `[m_nextEpisodeBtn 32×32]` 8px `[m_titleLabel stretch]` `[m_speedChip]` `[m_filtersChip]` `[m_trackChip]` `[m_playlistChip]`
Removed `m_volBtn`, `m_fullscreenBtn`. Dynamic HUD height via sizeHint().

### BATCH C — SeekSlider overhaul

Range 0-10000. Warm amber fill `rgba(214,194,164,0.90)`. 5px groove. 80ms live seek throttle. Time bubble `QColor(12,12,12,209)` pill above handle.

### BATCH D — ToastHud wiring

6 call sites: speed change, mute toggle, track cycle, sub visibility, error display.

### BATCH E — Speed chip QMenu

8 presets: 0.25x–2.0x. Checkmark on current. Exec at topLeft(). Keyboard cycling preserved.

### BATCH F — FilterPopover wiring

Chip → toggle popover. `filtersChanged` → `sendSetFilters`. Chip label shows "Filters (N)". HUD hover guard.

### BATCH G — TrackPopover wiring

Chip → toggle popover. `audioTrackSelected` → `sendSetTracks`. `subtitleTrackSelected` → `sendSetTracks`. `subDelayAdjusted` → `sendSetSubDelay`. `subStyleChanged` → `sendSetSubStyle`.

### BATCH H — Subtitle rendering wiring

SubtitleOverlay as sibling of FrameCanvas. `subtitleText` → `setText`. `subVisibilityChanged` → clears text. `subStyleChanged` → `setStyle`. `setControlsVisible` on HUD show/hide. `reposition()+raise()` in resizeEvent.

### BATCH I — CenterFlash + VolumeHud polish + button styles

CenterFlash: `addEllipse(QRectF(0,0,80,80))`, `QColor(0,0,0,140)`, 120ms fade-in.
VolumeHud: `setFixedSize(160,32)`, 120ms fade-in.
Icons: `fill='#cccccc'`, `stroke='#cccccc'`.

### BATCH J — Keyboard shortcuts + HUD guards + auto-advance

Keys: `< ,` → sub delay -100ms, `> .` → +100ms, `D` → deinterlace toggle, `Shift+A` → normalize toggle.
HUD: `if (m_seeking) return;` guard. Separate `m_cursorTimer` (2000ms) for cursor hide.
PlaylistDrawer: `isAutoAdvance()`. `onEndOfFile()`: `if (!m_playlistDrawer->isAutoAdvance()) return;`
ShortcutsOverlay: added `< / >`, `D`, `Shift+A` entries.
