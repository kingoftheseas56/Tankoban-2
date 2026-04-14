# Video Player Congress — Master Checklist
*Prepared by Agent 0 (2026-03-25)*
*Agenda: 1:1 parity with groundwork ffmpeg player*
*Sources: assistant1_report_player.md + assistant2_report_player.md vs current C++ implementation*

---

## How This Was Built

Assistants read the full groundwork player codebase:
- `app_qt/ui/player/ffmpeg_player_surface.py`, `player_surface.py`
- `player_qt/ui/bottom_hud.py`, `seek_slider.py`, `volume_hud.py`
- `player_qt/ui/center_flash.py`, `toast_hud.py`, `shortcuts_overlay.py`, `playlist_drawer.py`
- `player_qt/ui/filter_popover.py`, `track_popover.py`
- `app_qt/ui/player/ffmpeg_sidecar/protocol.py`, `transport.py`, `session.py`, `client.py`

Then current C++ was read in full:
- `VideoPlayer.cpp/h`, `FrameCanvas.cpp/h`, `SidecarProcess.cpp/h`
- `VolumeHud.cpp`, `CenterFlash.cpp`, `ShortcutsOverlay.cpp`, `PlaylistDrawer.cpp`

---

## Priority Scale
- **P0** — Fundamental behavior mismatch (wrong layout, missing core feature, visual regression)
- **P1** — Important UX gap (missing polish, wrong dimensions/colors)
- **P2** — Protocol/backend gap (missing sidecar commands/events)

---

## GAP 1: HUD Layout — Single Row vs Two Rows [P0]

### Groundwork
Two-row bottom HUD:
- **Row 1** (seek row): `[time_label 48px]` `[seek_back 28×28]` `[SeekSlider stretch]` `[seek_fwd 28×28]` `[duration 48px]`
- **Row 2** (controls row): `[back 30×30]` +8px gap `[prev 32×32]` `[play/pause 40×36]` `[next 32×32]` +8px gap `[title_label stretch]` `[HDR badge]` `[speed chip]` `[filters chip]` `[track chip 30×30]` `[playlist chip 30×30]`
- HUD scrim BG: `QColor(10,10,10,235)` fill + `QColor(255,255,255,20)` top-edge 1px line

### Current
Single row: `[back 42×30]` `[prev 32×30]` `[play 36×30]` `[next 32×30]` `[seekBar stretch]` `[timeLabel 110px]` `[speedLabel 38px]` `[volBtn 32×30]` `[fullscreen 32×30]`
Fixed height: 52px. No title. No chips. No row 1 seek buttons.

### Gaps
- Wrong layout (single row vs two rows)
- Back button: 42×30 → should be 30×30
- Play/pause: 36×30 → should be 40×36
- Seek-back and seek-fwd buttons missing from row 1 (28×28)
- Time label: combined "00:00 / 00:00" 110px → should be separate time (left, 48px) and duration (right, 48px)
- Title label missing (shows filename, stretch in row 2)
- HDR badge missing
- Speed label → should be a chip button (opens QMenu)
- Filters chip button missing (opens FilterPopover)
- Track chip button missing (opens TrackPopover)
- Volume button and fullscreen button not shown in groundwork row 2 at all — groundwork puts volume in VolumeHud only (scroll/Up/Down) and fullscreen as icon button in row 2
- HUD scrim: `rgba(8,8,8,0.85)` → `QColor(10,10,10,235)`; border-top → top-edge 1px `QColor(255,255,255,20)` painted

---

## GAP 2: SeekSlider [P0]

### Groundwork
- Range: 0–10000 (high precision, position = positionSec/durationSec * 10000)
- Groove: 5px tall
- Fill: warm amber `rgba(214,194,164,0.90)`
- Handle: 12px circle, warm gradient
- Live seek (throttled 80ms) while dragging
- **Time bubble**: when hovered/dragging, shows time preview above slider in `rgba(12,12,12,0.82)` pill

### Current
- Range: 0–durationSec (integer seconds, low precision for long content)
- Groove: 4px
- Fill: `rgba(255,255,255,0.55)` (plain white — wrong)
- Handle: 12px, white
- Seek only on release (no live preview)
- No time bubble

### Gaps
- Range precision: use 0–10000, scale position to durationSec
- Fill color: white → warm amber `rgba(214,194,164,0.90)`
- Groove height: 4px → 5px
- Throttled live seek: send seek every 80ms while dragging
- Time bubble widget: QPainter popup showing hovered time

---

## GAP 3: Button Styles [P1]

### Groundwork
**Icon buttons** (back, prev, play, next, fullscreen):
- `background: transparent`
- hover: `rgba(255,255,255,0.08)`
- pressed: `rgba(255,255,255,0.04)`
- icon color: `#cccccc`
- no border

**Chip buttons** (speed, filters, track, playlist):
- 3-stop vertical gradient: `rgba(68,68,68,0.95)` → `rgba(44,44,44,0.98)` → `rgba(24,24,24,0.98)`
- border: `rgba(255,255,255,0.18)` outer + inner subtle
- font: 11px weight 600
- border-radius: 6px

### Current
Icon buttons have: `background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.10)`; hover `rgba(255,255,255,0.14)`. Icons are white (#ffffff). No chip buttons exist.

### Gaps
- Icon button default bg: remove `rgba(255,255,255,0.06)` → transparent
- Icon button border: remove border entirely
- Icon button hover: `0.14` → `0.08`
- Add pressed state: `rgba(255,255,255,0.04)`
- Icon color: `#ffffff` SVG fill → `#cccccc`
- Chip buttons need to be created (speed chip, filters chip, track chip, playlist chip)

---

## GAP 4: Speed — Cycling vs QMenu [P0]

### Groundwork
Speed chip button opens a **QMenu** anchored at `btn.mapToGlobal(btn.rect().bottomLeft())`.
Presets: 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0.
Chip label shows `f"{speed:g}x"` (e.g. "1x", "1.25x").
Speed change shows a ToastHUD message.

### Current
Speed is cycled via C/]/X/[ keyboard shortcuts. `m_speedLabel` is a plain QLabel (no click). No menu. No toast on change.

### Gaps
- Speed label → chip button that opens QMenu
- Add 0.25 preset (currently starts at 0.5)
- Toast on speed change (blocked by GAP 5 — ToastHUD)
- Keyboard shortcuts C/]/X/[ still cycle speed — that's fine (groundwork also has them)

---

## GAP 5: ToastHUD — Completely Missing [P0]

### Groundwork
- 280px max-width pill, top-right corner, 12px right/top margin
- Font: 12px weight 600
- BG: `rgba(10,10,10,0.85)`
- Hold: 2000ms, fade-out: 200ms
- No stacking — replaces current toast

### Current
Errors shown in `m_timeLabel` text. No transient toast widget exists.

### Gaps
- Create `ToastHud` class (similar pattern to VolumeHud)
- Wire toast for: speed change, mute toggle, track changes, sub visibility, error messages
- Remove error display from m_timeLabel

---

## GAP 6: FilterPopover — Completely Missing [P0]

### Groundwork
QFrame, min 220px / max 320px, anchored 8px above filter chip button (top-right of chip).

**Video section:**
- Deinterlace checkbox (default unchecked)
- Brightness slider: -100 → 100 (default 0)
- Contrast slider: 0 → 200 (default 100)
- Saturation slider: 0 → 200 (default 100)

**Audio section:**
- Volume Normalization checkbox (default unchecked)

300ms debounce before sending `set_filters` command.
Global event filter for click-outside dismiss.
`enterEvent`/`leaveEvent` → prevent HUD auto-hide while hovering.

### Current
Nonexistent.

### Gaps
- Create `FilterPopover` class
- Create chip button in HUD row 2 labeled "Filters" (or icon)
- Wire `SidecarProcess::sendSetFilters()` command (add to SidecarProcess.h/cpp)
- Handle `filters_changed` event from sidecar
- Prevent HUD hide on hover

---

## GAP 7: TrackPopover — Completely Missing [P0]

### Groundwork
QFrame, min 200px / max 320px, same anchor logic as FilterPopover.

**Audio section:**
- QListWidget, max 4 rows visible (30px each), current highlighted

**Subtitle section:**
- QListWidget with "Off" at index 0, rest from `tracks_changed` event
- Sub delay: `+100ms` / `-100ms` / `Reset` buttons
- **Sub style sub-section**: font size spinbox (16–48, default 24), margin spinbox (0–100, default 40), Outline checkbox (default checked)

300ms debounce. Same dismiss behavior as FilterPopover.

### Current
Tracks cycled via A/S keyboard only. No visual track picker. No sub delay UI. No sub style.

### Gaps
- Create `TrackPopover` class
- Create track chip button in HUD row 2
- Add `SidecarProcess::sendSetSubDelay()` command
- Add `SidecarProcess::sendSetSubStyle()` command
- Handle `sub_delay_changed` event

---

## GAP 8: Subtitle Rendering — Completely Missing [P0]

### Groundwork
`subtitle_text` event → renders text over video at bottom with configurable font size, margin, and outline.
Separate `sub_label` widget floated above canvas, always topmost in z-order.

### Current
`subtitle_text` event not connected in SidecarProcess at all. No subtitle display.

### Gaps
- Handle `subtitle_text` event in SidecarProcess (add signal)
- Create subtitle overlay widget (QLabel or QPainter custom widget)
- Apply sub style from TrackPopover settings

---

## GAP 9: CenterFlash Appearance [P1]

### Groundwork
- Shape: **full circle** (no radius arg — `addEllipse`)
- BG: `rgba(0,0,0,0.55)` = `QColor(0,0,0,140)`
- Fade-in: 120ms (currently instant)
- Fade-out: 350ms (matches)
- Hold: 300ms (matches)

### Current
- Shape: rounded rect, radius 16 (`addRoundedRect(..., 16, 16)`)
- BG: `QColor(10,10,10,180)` (slightly different shade)
- Fade-in: none (jumps to 1.0 instantly)

### Gaps
- Shape: `addRoundedRect` → `addEllipse`
- BG: `QColor(10,10,10,180)` → `QColor(0,0,0,140)`
- Fade-in: add 120ms QPropertyAnimation before hold

---

## GAP 10: VolumeHud Appearance [P1]

### Groundwork
- Size: 160×32px (ours: 164×36)
- Fade-in: 120ms (ours: instant jump to 1.0)
- Hold: 1200ms (matches)
- Fade-out: 200ms (matches)
- Position: center-bottom with specific padding
- Symbols: ⊘︎/◔︎/◑︎/◕︎ (groundwork uses unicode symbols, not SVG icons)

### Current
- Size: 164×36
- No fade-in
- Uses SVG icons (functionally equivalent but not 1:1)

### Gaps
- Size: 164×36 → 160×32
- Add 120ms fade-in animation on showVolume()
- Symbols: minor (SVG icons acceptable if visible result matches)

---

## GAP 11: Missing Keyboard Shortcuts [P1]

### Groundwork keyboard shortcuts not in our implementation:
- `<` / `>` — subtitle delay ±100ms
- `D` — deinterlace toggle (calls set_filters with deinterlace flag)
- `Shift+A` — audio normalization toggle (calls set_filters with normalization flag)

### Current
These three are absent from `keyPressEvent` AND from ShortcutsOverlay list.

### Gaps
- Add `Qt::Key_Less` / `Qt::Key_Greater` → `m_sidecar->sendSetSubDelay(±0.1)`
- Add `Qt::Key_D` → toggle deinterlace (requires FilterPopover state or standalone bool)
- Add `Qt::Key_A` with `Shift` → toggle audio normalization
- Add all three to ShortcutsOverlay SHORTCUTS table

---

## GAP 12: HUD Auto-Hide Conditions [P1]

### Groundwork
HUD hides after 3000ms UNLESS:
1. Player is paused
2. User is actively scrubbing (slider pressed)
3. Pointer is over an overlay widget (FilterPopover, TrackPopover, PlaylistDrawer)
4. ShortcutsOverlay is open

Cursor separately hides after `_CURSOR_HIDE_MS=2000` (independent timer).

### Current
- Checks: paused ✓, playlistDrawer open ✓
- Missing: m_seeking check, FilterPopover/TrackPopover hover check
- Cursor hide: tied to HUD hide (hides simultaneously at 3000ms)

### Gaps
- `hideControls()`: add `if (m_seeking) return;`
- Separate cursor hide timer at 2000ms (independent of HUD)
- When FilterPopover/TrackPopover added, wire their enter/leave events to show/stop HUD timer

---

## GAP 13: SidecarProcess Protocol Gaps [P2]

### Missing commands (groundwork protocol has these, C++ doesn't):
- `set_sub_delay` (payload: `{delayMs: float}`)
- `set_sub_style` (payload: `{fontSize, marginPercent, outline}`)
- `load_external_sub` (payload: `{path}`)
- `set_filters` (payload: `{video: {deinterlace, brightness, contrast, saturation}, audio: {normalize}}`)
- `resize` (payload: `{width, height}`) — for proper sidecar surface sizing
- `set_surface_size` (payload: `{width, height}`)
- `set_fullscreen` (payload: `{active}`) — explicit fullscreen command (not just UI toggle)
- `set_tone_mapping`, `set_icc_profile`, `set_hwaccel`, `set_color_management` (advanced — lower priority)

### Missing event handling:
- `subtitle_text` (payload: `{text}`) — display subtitle
- `sub_delay_changed` (payload: `{delayMs}`)
- `sub_visibility_changed` (payload: `{visible}`)
- `filters_changed` (payload: filter state)
- `media_info` (payload: includes HDR metadata for HDR badge)
- `closed` (process clean close)
- `external_sub_loaded`

### Missing sendOpen fields:
- Groundwork sends `videoId` and `showId` in open payload; ours only sends `path`, `startSeconds`

---

## GAP 14: Auto-Advance — Checkbox Not Consulted [P1]

### Groundwork
`onEndOfFile` checks `m_autoAdvance.isChecked()` before advancing.

### Current
`onEndOfFile` always calls `nextEpisode()` without checking the checkbox state.

### Gap
- `onEndOfFile`: guard with `if (m_playlistDrawer && !m_playlistDrawer->isAutoAdvance()) return;`
- Need `isAutoAdvance()` accessor on PlaylistDrawer

---

## Summary Table

| # | Component | Gap | Priority | New/Fix |
|---|-----------|-----|----------|---------|
| 1 | VideoPlayer HUD | Two-row layout, button sizes, title, chips | P0 | Fix |
| 2 | SeekSlider | Range precision, fill color, groove height, live seek, time bubble | P0 | Fix |
| 3 | Button styles | Transparent bg, hover/pressed states, icon color, chip gradient style | P1 | Fix |
| 4 | Speed chip | QMenu instead of cycle, additional presets, toast | P0 | Fix |
| 5 | ToastHUD | Entire component missing | P0 | New |
| 6 | FilterPopover | Entire component missing + sidecar command | P0 | New |
| 7 | TrackPopover | Entire component missing + sidecar commands | P0 | New |
| 8 | Subtitle rendering | subtitle_text event + overlay widget | P0 | New |
| 9 | CenterFlash | Shape (circle), bg color, fade-in | P1 | Fix |
| 10 | VolumeHud | Size, fade-in | P1 | Fix |
| 11 | Keyboard shortcuts | <>, D, Shift+A missing | P1 | Fix |
| 12 | HUD auto-hide | m_seeking guard, cursor timer, popover hover | P1 | Fix |
| 13 | SidecarProcess | 8+ missing commands, 7+ missing events | P2 | Fix |
| 14 | Auto-advance | Ignores checkbox state | P1 | Fix |

---

## Groundwork Theme Constants (Agent 3 reference)

```
BG_PANEL       = rgba(16,16,16,0.94)         # QColor(16,16,16,240)
BG_HUD         = QColor(10,10,10,235)         # HUD background
BG_TOAST       = rgba(10,10,10,0.85)          # QColor(10,10,10,217)
ACCENT_WARM    = rgba(214,194,164,0.95)       # QColor(214,194,164,242)
BORDER_DEFAULT = rgba(255,255,255,0.12)       # QColor(255,255,255,31)
TEXT_PRIMARY   = rgba(245,245,245,0.98)       # QColor(245,245,245,250)
TEXT_MUTED     = rgba(255,255,255,0.55)       # QColor(255,255,255,140)
HUD_AUTO_HIDE  = 3000ms
CURSOR_HIDE    = 2000ms
SEEK_THROTTLE  = 80ms
```

---

## Proposed Congress Batch Plan

Agent 3 should read this document in full, then file their position in CONGRESS.md. Agent 0 will assign batches in order from P0 to P1. P2 items (protocol gaps) can accompany whichever batch needs them.

**Suggested batch order:**
- **Batch A**: SidecarProcess protocol additions (P2 gap 13) — needed first, all other batches depend on new commands/events
- **Batch B**: HUD two-row layout restructure (P0 gap 1) — structural foundation
- **Batch C**: SeekSlider full rewrite (P0 gap 2) — seek range, fill, time bubble, live seek
- **Batch D**: ToastHUD new component (P0 gap 5) — toast needed by speed chip and other features
- **Batch E**: Speed chip → QMenu (P0 gap 4) — needs ToastHUD from D
- **Batch F**: FilterPopover new component (P0 gap 6) — needs Batch A sidecar command
- **Batch G**: TrackPopover new component (P0 gap 7) — needs Batch A sidecar command
- **Batch H**: Subtitle rendering (P0 gap 8) — needs Batch A subtitle_text event
- **Batch I**: CenterFlash + VolumeHud polish, button styles (P1 gaps 9, 10, 3)
- **Batch J**: Keyboard shortcut additions + auto-hide + auto-advance fixes (P1 gaps 11, 12, 14)
