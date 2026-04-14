# Assistant 1 Report — Groundwork FFmpeg Video Player Feature Map
# Generated: 2026-03-25

---

## FILE 1: ffmpeg_player_surface.py (FfmpegPlayerSurface)

### Constants
- `_HUD_AUTO_HIDE_MS = 3000` — HUD auto-hide delay in ms
- `_SUB_MARGIN_HUD_VISIBLE = 130` — subtitle bottom margin when HUD visible (hud_height ~90 + 40)
- `_SUB_MARGIN_HUD_HIDDEN = 40` — subtitle bottom margin when HUD hidden
- `_CURSOR_HIDE_MS = 2000` — cursor auto-hide delay in fullscreen
- `_SEEK_THROTTLE_MS = 80` — seek drag throttle: fires sidecar seek every 80ms during drag
- `_SPEED_PRESETS = (0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0)`

### Overall Widget Structure (Z-order, bottom to top)
Root widget: `FfmpegPlayerSurface(QWidget)`, stylesheet `background: black`.

Layout: `QVBoxLayout(margins=0, spacing=0)` with one child:
- `_stage_container` (QWidget, stretch=1). Has `_player_owner = self` set on it.
  - Its layout: `QVBoxLayout(margins=0, spacing=0)` with one child:
    - `_canvas` (FFmpegFrameCanvas / GLFrameCanvas, stretch=1). Paints frames via OpenGL. Has `_overlay_painter_cb` hooked to `_paint_canvas_overlay`.

  All overlays are children of `_stage_container`, positioned absolutely (NOT in layout):
  1. `_bottom_hud` (BottomHUD) — transport controls. Z above canvas.
  2. `_track_popover` (TrackPopover) — audio/subtitle track selector.
  3. `_filter_popover` (FilterPopover) — video/audio filter controls.
  4. `_playlist_drawer` (PlaylistDrawer) — right-side episode list.
  5. `_volume_hud` (VolumeHUD) — transient volume level indicator.
  6. `_toast_hud` (ToastHUD) — transient text notification, top-right.
  7. `_center_flash` (CenterFlash) — play/pause/seek icon at screen center.
  8. `_shortcuts_overlay` (ShortcutsOverlay) — full-screen shortcuts card.
  9. `_sub_label` (QLabel) — subtitle text, above bottom HUD.

### HUD Show/Hide Behavior

**Showing HUD:**
- Triggered by: any mouse move on `_canvas` (event filter) or `FfmpegPlayerSurface` itself
- `_on_mouse_activity()` calls `_set_controls_visible(True)` + `_arm_controls_autohide()`
- `_set_controls_visible(True)`:
  - Sets `_controls_visible = True`
  - `_bottom_hud.setVisible(True)` (no opacity animation — opacity effect breaks GL compositing)
  - Calls `_position_overlays()`, `_bottom_hud.raise_()`
  - Calls `_canvas.set_hud_rect(hud_rect)` — GL canvas paints HUD scrim directly
  - Refreshes scrubber and labels
  - Calls `_show_cursor()`

**Hiding HUD:**
- Timer fires after 3000ms → `_on_controls_hide_timeout()`
- Does NOT hide if: paused, scrubber dragging, pointer over HUD or popover, shortcuts overlay open
- `_set_controls_visible(False)`:
  - `_bottom_hud.setVisible(False)` directly
  - `_canvas.set_hud_rect(None)` — GL stops drawing scrim
  - Closes any open track popover or playlist drawer
  - Arms cursor auto-hide

**Auto-hide arming:**
- `_arm_controls_autohide()`: stops and restarts 3000ms timer
- Skipped if: not visible, paused, pointer over any overlay, scrubber dragging

**HUD canvas scrim (GL backdrop):**
- `_paint_canvas_overlay(painter, width, height)` — called from GLFrameCanvas.paintGL via callback
- Fills HUD rect with `QColor(10, 10, 10, 235)`
- Draws top edge line at `QColor(255, 255, 255, 20)` (1px height)

**HUD enter/leave (pointer over HUD):**
- `_on_hud_enter()`: sets `_pointer_over_hud = True`, stops auto-hide timer
- `_on_hud_leave()`: sets `_pointer_over_hud = False`, re-arms auto-hide if visible

### Cursor Hide Logic
- Only in fullscreen (`_embedded_fullscreen == True`)
- `_arm_cursor_autohide()`: starts 2000ms timer only when fullscreen AND controls hidden AND not paused
- `_maybe_hide_cursor()`: if fullscreen and controls hidden → `setCursor(BlankCursor)`, else `ArrowCursor`
- `_show_cursor()`: restores `ArrowCursor` if currently blank
- Cursor shown immediately when: HUD shown, paused, exit fullscreen

### Keyboard Shortcuts (complete list)
All handled in `_dispatch_shortcut(key, mods)`:

| Key | Action |
|-----|--------|
| Space | Toggle pause; flash "pause" or "play" center icon |
| Left | Seek relative -10s; flash "seek-back" |
| Right | Seek relative +10s; flash "seek-fwd" |
| F | Toggle fullscreen |
| F11 | Toggle fullscreen |
| Enter / Return | Toggle fullscreen |
| Escape | If shortcuts overlay open: close it. Elif fullscreen: exit fullscreen. Else: emit backRequested |
| Backspace | Emit backRequested (always) |
| M | Toggle mute; show volume HUD |
| Up | Volume +5; show volume HUD |
| Down | Volume -5; show volume HUD |
| C or ] (no ctrl/alt/meta) | Cycle speed up (next preset) |
| X or [ (no Shift, no ctrl/alt/meta) | Cycle speed down (previous preset) |
| Z or \ (no Shift, no ctrl/alt/meta) | Reset speed to 1.0x |
| A (no Shift, no ctrl/alt/meta) | Cycle audio track |
| S (no Shift, no ctrl/alt/meta) | Cycle subtitle track |
| Shift+S | Toggle subtitle visibility (Visible/Hidden) |
| < or Shift+, | Subtitle delay -100ms |
| > or Shift+. | Subtitle delay +100ms |
| D (no ctrl/alt/meta) | Toggle deinterlace |
| Shift+A | Toggle audio normalization |
| N (no Shift) | Emit nextRequested |
| P (no Shift) | Emit prevRequested |
| ? or Shift+/ | Toggle shortcuts overlay |

Additional QShortcuts installed via `_install_shortcuts()`:
- F, F11, Return, Enter: toggle fullscreen (widget-with-children context)
- Escape, Backspace: dispatch to `_dispatch_shortcut`

### Mouse Behavior
- **Mouse move on canvas**: routed via event filter → `_on_mouse_activity()` → show HUD + arm timer
  - Duplicate detection: global position checked against `_last_mouse_global_pos`; ignores synthetic events
- **Left double-click on canvas**: `_toggle_fullscreen()`
- **Right-click on canvas**: `_show_context_menu(global_pos)` — shows context menu, forces controls visible
- **Context menu event on canvas**: also triggers `_show_context_menu`
- **Wheel on canvas or player**: volume adjust ±5 per tick; show volume HUD
- **Mouse press on player widget**: `setFocus(MouseFocusReason)`
- No single-click play/pause on canvas

### Volume Control
- Range: 0-100 (integer)
- Wheel step: ±5 per tick (flat, both directions)
- Wheel: if currently muted and scroll up, unmutes first
- M key: `_toggle_mute()` — flips `_muted` flag, sends to adapter
- Volume above 0 clears muted state on wheel up
- After any volume change: `_show_volume_feedback()` → positions overlays + `_volume_hud.show_volume(vol, muted)` + raise

### Speed Control
- Presets: 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0
- Chip label format: `f"{speed:g}x"` (e.g., "0.5x", "1x", "1.5x", "2x")
- Speed chip click → `_on_speed_chip_clicked()`:
  - Stops auto-hide timer
  - Shows `QMenu` at `btn.mapToGlobal(btn.rect().bottomLeft())`
  - Menu has checkable actions for each preset, then separator + "Reset to 1.0x"
  - Checked action = current speed (abs diff < 1e-6)
  - After menu closes: re-arms auto-hide
- Cycle up (C/]): finds next preset above current, clamps at max
- Cycle down (X/[): finds next preset below current, clamps at min
- `_set_speed()` also shows toast: `f"Playback speed {speed:g}x"`

### Track Selection
- Audio tracks: cycled with A key, or selected from track popover / context menu
- Subtitle tracks: cycled with S key (includes Off state at end of cycle)
- Track chip (CC icon button): opens `TrackPopover` anchored to chip button
  - Popover populated with: tracks list, current audio ID, current sub ID, sub visibility
  - Also shows sub delay in popover
- On audio select: sends to adapter, shows toast with track label
- On subtitle select (id <= 0): disables subs; (id > 0): enables and sets track
- Subtitle visibility toggle (Shift+S): saves `_last_sid` when hiding, restores it when showing

### Progress Saving
- Progress timer: starts at 1000ms interval, calls `_write_progress("periodic")`
  - NOTE: `_write_progress` in ffmpeg surface delegates to adapter
- Per-app preferences saved to `QSettings`:
  - `"player/audio_normalization"` → bool
  - `"player/tone_mapping"` → str (default "auto")
  - `"player/color_management"` → bool (default True)
  - `"player/hwaccel_mode"` → str (default "auto")
- Per-show progress (position) is delegated to adapter/bridge — not directly stored in QSettings here

### Resume
- Delegated to adapter; `_pending_initial_seek` field used for initial position seek on open
- Wait for time-pos > 0.5 or 4 attempts before applying initial seek

### Fullscreen
- `_toggle_fullscreen(active=None)`:
  - Computes target as `not _fullscreen_is_effective()` if active is None
  - Sets `_fullscreen_pending_target = target`
  - Calls `_shell_fullscreen_callback(target)` if set, else emits `fullscreenRequested`
- `set_embedded_fullscreen(active)`: called by shell to confirm state
  - On exit: stops cursor timer, shows cursor, re-positions overlays
  - On enter: arms cursor auto-hide
- In fullscreen: cursor hides after 2000ms of no HUD

### Playlist/Episode Navigation
- `nextRequested` / `prevRequested` signals emitted on N/P keys or HUD buttons
- `set_playlist(paths, index)`: populates drawer + enables/disables prev/next buttons
- Prev/next enable state: `prev_enabled = playlist_index > 0`, `next_enabled = index < len - 1`

### Overlay Positioning (_position_overlays)
Called on resize and whenever any overlay visibility changes.
- Bottom HUD: `setGeometry(0, height - hud_height, width, hud_height)` where `hud_height = max(80, sizeHint().height())`
- Playlist drawer: `setGeometry(max(0, width - drawer_w - 12), 10, drawer_w, max(180, height - hud_height - 20))`
- Volume HUD: `move(max(12, (width - vol_w) // 2), max(12, height - hud_height - vol_h - 18))`
- Toast HUD: `move(max(12, width - toast_w - 14), 14)`
- Center flash: `move(max(0, (width-80)//2), max(0, (height-80)//2))`
- Shortcuts overlay: `setGeometry(0, 0, width, height)`
- Subtitle label: positioned above HUD with `margin_x = max(40, width//8)`; y = `height - label_h - (hud_h + 10 if HUD visible else 20)`

### Context Menu (right-click)
Styled with `CONTEXT_MENU_SS`. Submenus:
- **Playback**: Play/Pause action, Speed submenu (presets + Reset), (no Mute in ffmpeg version)
- **Video**: Aspect Ratio submenu (Default/-1, 16:9, 4:3, 21:9/2.33:1, 2.35:1, 1:1, 9:16, 3:2), Enter/Exit Fullscreen
- **Audio**: checkable track rows (lang+title+codec format)
- **Subtitles**: checkable track rows + Off option + "Load External Subtitle..."
- **Filters**: Deinterlace (checkable), Audio Normalization (checkable)
- **Hardware Acceleration**: Auto / Disabled (CPU only)
- **HDR & Color**: tone mapping presets (Auto, Hable/Filmic, Reinhard, Mobius, Clip), separator, Use System Color Profile (checkable)
- Separator
- Tracks, Playlist, Back to library

### Subtitle Overlay
- QLabel child of `_stage_container`
- Style: color white, font 24px bold, font-family Arial, background rgba(0,0,0,140), radius 4px, padding 6px 14px
- Positioned above HUD with side margins = max(40, width//8)
- WA_TransparentForMouseEvents
- Shown when subtitle_text_received signal fires; hidden when duration expires (QTimer)

---

## FILE 2: player_surface.py (PlayerSurface — mpv version)

### Constants
- `_HUD_AUTO_HIDE_MS = 3000`
- `_SPEED_PRESETS = (0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0)`
- `_CURSOR_HIDE_MS = 2000` (from `_arm_cursor_autohide` inline start)
- `_bottom_bar_height = 90` (initial default, updated on layout)

### Widget Structure
Same pattern as ffmpeg version:
- `QVBoxLayout(margins=0, spacing=0)` → `_stage_container`
  - `QVBoxLayout(margins=0, spacing=0)` → `render_host` (MpvRenderHost, stretch=1)
  - Overlays (NOT in layout):
    - `bottom_hud` (BottomHUD)
    - `_track_popover` (TrackPopover)
    - `_playlist_drawer` (PlaylistDrawer)
    - `_volume_hud` (VolumeHUD)
    - `_toast_hud` (ToastHUD)
    - `_shortcuts_overlay` (ShortcutsOverlay)
    - (no CenterFlash in mpv version)

### HUD Show/Hide Behavior
- Same overall logic as ffmpeg version
- `_on_mouse_activity()` is called by `render_host.wheel_volume_signal` and `right_click_signal` routing, plus key press
- `_set_controls_visible(visible)`:
  - `bottom_hud.setVisible(visible)`
  - On show: `_position_overlays()`, `bottom_hud.raise_()`; close popovers/drawer on hide
  - Subtitle safe margin adjusted via mpv property
- mpv subtitle margin:
  - HUD visible: `sub_margin_y = bottom_bar_height + 40`, `sub_pos = max(55, min(98, 100 - margin/h*100))`
  - HUD hidden: `sub_margin_y = 28`, `sub_pos = 95`

### Overlay Positioning (_position_overlays)
- Bottom HUD: `setGeometry(0, h - bottom_height, w, bottom_height)` where `bottom_height = max(80, sizeHint().height())`
- Playlist drawer: `setGeometry(max(0, w - drawer_w - right_margin), top_margin, drawer_w, available_h)` where right_margin=12, top_margin=10, available_h=max(180, h-bottom_height-(top_margin*2))
- Volume HUD: `move(max(12, int((w - volume_w)/2)), max(12, h - bottom_height - volume_h - 18))`
- Toast HUD: `move(max(12, w - toast_w - 14), 14)`
- Shortcuts overlay: `setGeometry(0, 0, w, h)`

### Keyboard Shortcuts (mpv version)
Same as ffmpeg version EXCEPT:
- No Shift+S (subtitle visibility toggle not in shortcut handler)
- No subtitle delay shortcuts (< > not handled)
- No D (deinterlace) shortcut
- No Shift+A (audio normalization) shortcut
- All other shortcuts identical (Space, Left/Right, F/F11/Enter, Escape, Backspace, M, Up/Down, C/]/X/[/Z/\, A, S, N, P, ?)

### Wheel Volume
- `_on_wheel_volume(delta)`:
  - Step: 5 if `abs(delta) > 60`, else 2
  - If muted and scrolling, unmutes first
  - Clamps 0-100
  - Calls `_show_volume_feedback(include_toast=False)`

### Progress Saving
- `_progress_timer` interval 1000ms calls `_write_progress("periodic")`
- `_write_progress` is a stub: `pass  # Wired by EmbeddedPlayerHost via progress bridge`
- UI update timer: 250ms, calls `_update_ui()` → updates scrubber and time labels, updates play icon

### Context Menu (mpv version)
Submenus:
- **Playback**: Play/Pause, Unmute/Mute, Playback speed submenu + Reset to 1.0x
- **Video**: Aspect Ratio (same presets), Enter/Exit Fullscreen
- **Audio**: checkable track list
- **Subtitles**: checkable track list + Off (include_off=True), no external subtitle option here

---

## FILE 3: bottom_hud.py (BottomHUD)

### Overall Structure
- `QWidget` subclass, `WA_StyledBackground=True`
- Background (mpv version): `BG_SCRIM` = `rgba(12,12,12,0.45)`, `border-top: 1px solid BORDER_SUBTLE`
- Background (ffmpeg version): overridden to `background: transparent; border: none;` — GL paints scrim directly
- `QVBoxLayout(margins=(16, 6, 16, 8), spacing=4)` with two rows

### Row 1 — Seek Row
`QHBoxLayout(spacing=6)`, left to right:
1. **Time label** (`_time_label`): fixed width 48, font 11px, color TEXT_PRIMARY, right+vcenter aligned
2. **Seek back button** (`_seek_back_btn`): icon 18px (arrow counterclockwise), button 28x28, tooltip "Back 10s", emits `seek_step_requested(-10.0)`
3. **SeekSlider** (`_scrub`): expanding, stretch=1
4. **Seek forward button** (`_seek_fwd_btn`): icon 18px (arrow clockwise), button 28x28, tooltip "Forward 10s", emits `seek_step_requested(+10.0)`
5. **Duration label** (`_duration_label`): fixed width 48, font 11px, color TEXT_MUTED, left+vcenter aligned

### Row 2 — Transport Row
`QHBoxLayout(spacing=4)`, left to right:
1. **Back button** (`_back_btn`): icon 20px (left arrow), button 30x30, tooltip "Back to library"
2. Spacing: 8px
3. **Prev button** (`_prev_btn`): icon 22px (skip-prev), button 32x32, tooltip "Previous"
4. **Play/Pause button** (`_play_pause_btn`): icon 28px, button 40x36 (taller than wide), tooltip "Play / Pause"
5. **Next button** (`_next_btn`): icon 22px (skip-next), button 32x32, tooltip "Next"
6. Spacing: 8px
7. **Title label** (`_title_label`): expanding (stretch=1), font 12px, color TEXT_MUTED; shows current file name
8. **HDR badge** (`_hdr_badge`): hidden by default; text "HDR", color #FFD700, bg rgba(255,215,0,0.15), radius 3px, padding 1px 4px, font 10px 800; shown for 5s when HDR content detected
9. **Speed chip** (`_speed_chip`): text "1.0x", CHIP_BTN_SS style, fixed height 26, min width 42
10. **Filters chip** (`_filters_chip`): text "Filters" or "Filters (N)", CHIP_BTN_SS style
11. **Track chip** (`_track_chip`): icon button, CC/subtitle SVG icon 20px, button 30x30, tooltip "Tracks"
12. **Playlist chip** (`_playlist_chip`): icon button, playlist SVG icon 20px, button 30x30, tooltip "Playlist"

### Button Styles
**Icon button** (`_ICON_BTN_SS`):
- Normal: `background: transparent; border: none; padding: 4px; border-radius: 4px`
- Hover: `background: rgba(255,255,255,0.08)`
- Pressed: `background: rgba(255,255,255,0.04)`
- Icons: all drawn in `#cccccc` (SVG fill)

**Chip button** (`CHIP_BTN_SS`):
- Normal: vertical gradient `rgba(68,68,68,0.95) → rgba(44,44,44,0.98) → rgba(24,24,24,0.98)`, border `rgba(0,0,0,0.70)`, border-top `rgba(110,110,110,0.60)`, border-bottom `rgba(0,0,0,0.80)`, radius 4px, padding `4px 10px`, color `rgba(240,240,240,0.98)`, font 11px weight 600
- Hover: gradient `rgba(90,90,90,0.98) → rgba(56,56,56,0.98) → rgba(30,30,30,0.98)`
- Pressed: reversed gradient `rgba(22,22,22,0.98) → rgba(42,42,42,0.98) → rgba(68,68,68,0.98)`

### Filters Chip Label
- `set_filters_label(active_count)`:
  - 0 active: `"Filters"`
  - N active: `"Filters (N)"`

### Track Chip
- Uses CC/subtitle SVG icon (not text)
- Tooltip updated to show count: `"Tracks (N audio / M subs)"`
- Click: opens TrackPopover, closes other popovers

### Disabled State (Prev/Next)
- `setEnabled(False)` + appended stylesheet `"QPushButton { opacity: 0.3; }"`

### HUD Mouse Tracking
- `enterEvent` / `leaveEvent`: call `owner._on_hud_enter()` / `owner._on_hud_leave()` via `_player_owner` attribute chain

---

## FILE 4: seek_slider.py (SeekSlider)

### Widget Type
- Inherits `QSlider(Qt.Orientation.Horizontal)`
- Range: 0 to 10000 (`_RANGE = 10000`)
- Stylesheet: `SEEK_SLIDER_SS` (see theme section)
- Cursor: `PointingHandCursor`

### Track Appearance (SEEK_SLIDER_SS)
- Groove height: **5px**
- Groove background: vertical gradient `rgba(80,80,80,0.9) → rgba(30,30,30,0.95)`
- Groove border: `1px solid rgba(0,0,0,0.65)`, radius 2px
- Fill (sub-page): warm amber gradient `rgba(214,194,164,0.90) → rgba(160,140,110,0.90)`, radius 2px
- Add-page (unfilled): `rgba(20,20,20,0.9)`, radius 2px

### Handle (Thumb)
- Width: **12px**
- Margin: `-5px 0` (extends 5px above and below groove)
- Normal: gradient `rgba(240,230,210,0.98) → rgba(170,150,120,0.98)` (warm light)
- Hover: gradient `rgba(255,245,225,0.98) → rgba(200,180,145,0.98)` (brighter)
- Border: `1px solid rgba(0,0,0,0.65)`, radius 3px

### Click-to-Seek
- `mousePressEvent`: sets `_dragging = True`, calls `_seek_from_x(x)`
- `_seek_from_x(x)`: computes fraction from groove rect, sets slider value, emits `seek_fraction_requested(frac)`
- Position computation: `(x - groove.x()) / groove.width()`, clamped 0-1

### Drag Behavior
- `mouseMoveEvent`: if dragging, calls `_seek_from_x(x)`, always calls `_show_bubble(x)`
- `mouseReleaseEvent`: clears `_dragging`, hides bubble
- `dragging` property: read by player to prevent HUD auto-hide during drag

### Time Bubble (Hover Tooltip)
- Widget: `QLabel`, parented to slider's parent (so it can float above slider)
- Style: `background: BG_BUBBLE` (`rgba(12,12,12,0.82)`), color TEXT_PRIMARY (`rgba(245,245,245,0.98)`), font 11px, padding `2px 6px`, radius 3px
- Content: formatted time string `fmt_time(fraction * duration)`
- Position: above slider, centered on cursor x: `bx = slider_x + cursor_x - bubble_w//2`, `by = slider_y - bubble_h - 4`
- Shown during: any mouse move over slider (hover or drag)
- Hidden on: mouse release, mouse leave, duration <= 0

### Chapter Markers
- Painted in `paintEvent` (after base class paint)
- Color: `QColor(255, 255, 255, 80)` (semi-transparent white)
- Width: 1px vertical line across groove height
- Position: computed from `ch_time / duration * groove.width()`
- Skips markers at fraction 0 or 1

---

## FILE 5: volume_hud.py (VolumeHUD)

### Trigger
- `show_volume(volume, muted)` called from player on: wheel scroll, mute toggle

### Dimensions
- Total size: **160 x 32px** (fixed)
- Position: centered horizontally, `height - hud_height - 32 - 18` from top

### Background / Styling
- `background: BG_TOAST` (`rgba(10,10,10,0.85)`)
- `border: 1px solid BORDER_MEDIUM` (`rgba(255,255,255,0.18)`)
- `border-radius: 6px`
- `WA_TransparentForMouseEvents = True`

### Internal Layout
`QHBoxLayout(margins=(10,0,10,0), spacing=8)`:
1. **Speaker symbol** (`_sym`): QLabel, font 14px, TEXT_PRIMARY, center-aligned
   - Muted or volume 0: `⊘︎` (U+2298 + VS15)
   - volume 1-32: `◔︎` (U+25D4 + VS15)
   - volume 33-65: `◑︎` (U+25D1 + VS15)
   - volume 66-100: `◕︎` (U+25D5 + VS15)
2. **Volume bar** (`_bar`, `_VolumeBar`): **90 x 8px**
   - Groove: `rgba(20,20,20,230)`, rounded 3px
   - Fill: `rgba(214,194,164,240)` (warm amber), rounded 3px, width = 90 * level/100
3. **Percentage label** (`_pct`): fixed width **32**, font 11px, TEXT_PRIMARY, center-aligned
   - Muted: `"MUTE"`
   - Otherwise: `f"{vol}%"`

### Animation Timing
- Fade in: **120ms** (opacity 0→1), via `QPropertyAnimation` on `QGraphicsOpacityEffect`
- Hold: **1200ms** (`_HIDE_MS`)
- Fade out: **200ms** (opacity 1→0), then `hide()`
- On repeated calls while visible: skips fade-in, resets hold timer

---

## FILE 6: center_flash.py (CenterFlash)

### Trigger Conditions
- Called via `_flash_center(icon_name)` from player:
  - Play/pause toggle: `"play"` or `"pause"`
  - Seek step (keyboard Left/Right, seek buttons): `"seek-back"` or `"seek-fwd"`
  - Seek drag flush (after scrubber drag): `"seek-fwd"` or `"seek-back"` based on direction

### Icons
- Container: 80x80px QLabel, `background: rgba(0,0,0,0.55)`, `border-radius: 40px` (circle)
- Icon inside: 40x40px SVG pixmap, color `#e0e0e0`
- SVG icons (all same paths as bottom HUD transport, but `#e0e0e0` fill):
  - `"play"`: triangle right
  - `"pause"`: two vertical bars
  - `"seek-back"`: counterclockwise arrow
  - `"seek-fwd"`: clockwise arrow

### Animation Timing
- Fade in: **120ms** (opacity 0→1)
- Hold: **300ms**
- Fade out: **350ms** (opacity 1→0), then `hide()`

### Positioning
- Exact center of stage: `move((width-80)//2, (height-80)//2)`
- `WA_TransparentForMouseEvents = True`

---

## FILE 7: toast_hud.py (ToastHUD)

### Trigger
- `show_toast(text, duration_ms=2000)` called by player for: speed changes, track changes, subtitle state, subtitle delay, buffer state, HW accel notices, normalization/deinterlace toggles

### Appearance
- Outer widget: transparent, `WA_TransparentForMouseEvents`, `WA_TranslucentBackground`, `WA_NoSystemBackground`
- Inner label (`_label`) style:
  - `background: BG_TOAST` (`rgba(10,10,10,0.85)`)
  - `color: TEXT_PRIMARY` (`rgba(245,245,245,0.98)`)
  - `border: 1px solid BORDER_MEDIUM` (`rgba(255,255,255,0.18)`)
  - `border-radius: 6px`
  - `padding: 8px 14px`
  - `font-size: 12px; font-weight: 600`

### Sizing
- Max width: **280px**
- Width: `min(280, parent_width - 28)`
- Text: elided with `Qt.ElideRight` if too long

### Position
- Top-right: `move(max(12, width - toast_w - 14), 14)`

### Timing
- No fade in: opacity set to 1.0 immediately on each call
- Hold: **2000ms** default (`_HIDE_MS`)
- Special durations: 30000ms for buffering (manually cleared when buffering stops)
- Fade out: **200ms**, then `hide()`
- Repeated calls replace current toast immediately (no stacking)

### Stacking
- Single toast only. New `show_toast` call cancels the previous one and replaces it.

---

## FILE 8: shortcuts_overlay.py (ShortcutsOverlay)

### Trigger
- `?` key or `Shift+/` (hardcoded in `_dispatch_shortcut`)
- `toggle()` method: fade in if hidden, fade out if visible

### Appearance
- Full-screen overlay: `background: rgba(0,0,0,0.75)` (75% black scrim)
- Center card: fixed width **420px**
  - Background: `BG_PANEL` (`rgba(16,16,16,0.94)`)
  - Border: `1px solid BORDER_DEFAULT` (`rgba(255,255,255,0.12)`)
  - Border-radius: 12px
  - Padding: `QVBoxLayout(margins=(24,20,24,20), spacing=12)`
- Title: "Keyboard Shortcuts", color ACCENT_WARM (`rgba(214,194,164,0.95)`), 16px, weight 700, centered
- Grid: `QGridLayout(spacing=6)`, column 0 min width 120
  - Column 0: key label — TEXT_PRIMARY, 12px, weight 700
  - Column 1: description — TEXT_MUTED, 12px
- Hint: "Press ? or click to close" — TEXT_MUTED, 11px, centered

### Complete Shortcut List (hardcoded in file)
```
Space           Play / Pause
Left            Seek back 10s
Right           Seek forward 10s
Up / Down       Volume up / down
M               Mute / Unmute
C / ]           Speed up one step
X / [           Speed down one step
Z / \           Reset speed to 1.0x
A               Cycle audio track
S               Cycle subtitle track
Shift+S         Toggle subtitle visibility
Shift+Z         Subtitle delay −100ms
Shift+X         Subtitle delay +100ms
Ctrl+Shift+Z    Reset subtitle delay
D               Toggle deinterlace
Shift+A         Toggle audio normalization
N               Next episode
P               Previous episode
F / F11 / Enter Toggle fullscreen
Esc             Exit fullscreen / Cancel / Back
Backspace       Back to library
?               Toggle this help
```

### Animation
- Fade in: **150ms**
- Fade out: **150ms**, then `hide()`

### Dismiss
- `?` key again (calls `toggle()`)
- Click anywhere on overlay (`mousePressEvent` → `toggle()`)

---

## FILE 9: playlist_drawer.py (PlaylistDrawer)

### Trigger
- Playlist chip button click (or context menu "Playlist")
- `toggle()` method: `show()`+raise+install filter if hidden, `_dismiss()` if visible

### Dimensions and Position
- Width: **320px** (fixed, `_DRAWER_W = 320`)
- Side: **right**
- Position: `setGeometry(max(0, width - 320 - 12), 10, 320, max(180, height - hud_height - 20))`
- Right margin from edge: 12px
- Top margin: 10px

### Background
- `QFrame#PlaylistDrawer` scoped:
  - `background: BG_PANEL` (`rgba(16,16,16,0.94)`)
  - `border: 1px solid BORDER_DEFAULT` (`rgba(255,255,255,0.12)`)
  - `border-radius: 12px`

### Internal Layout
`QVBoxLayout(margins=(14,12,14,12), spacing=8)`:

1. **Header row** (QHBoxLayout):
   - Title "Playlist": ACCENT_WARM (`rgba(214,194,164,0.95)`), 14px, weight 700
   - Stretch
   - Close button (`✕︎`): 24x24, TEXT_MUTED (hover: TEXT_PRIMARY), transparent bg, no border, 14px

2. **Divider**: QFrame HLine, BORDER_SUBTLE color (`rgba(255,255,255,0.08)`), height 1px

3. **Episode list** (`_list`, QListWidget, stretch=1):
   - Styled with `TRACK_LIST_SS`:
     - Background: transparent, no border, no outline
     - Item: TEXT_SECONDARY, padding `5px 10px`, radius 4px, font 12px
     - Hover: `rgba(255,255,255,0.08)`
     - Selected: color ACCENT_WARM, bg `rgba(255,255,255,0.06)`
   - No horizontal scrollbar
   - Each item: `▶︎ {stem}` for current, `   {stem}` for others (3-space prefix with play marker U+25B6+VS15)
   - Current item: `setSelected(True)` on populate
   - Item data: index stored in `Qt.ItemDataRole.UserRole`

4. **Auto-advance checkbox**:
   - Text: "Auto-advance", checked by default
   - Style: TEXT_SECONDARY, font 12px, indicator 14x14

### Click Behavior
- Item click: emits `episode_selected(index)` → `_dismiss()`

### Dismiss Behavior
- Close button X click: `_dismiss()`
- Click outside drawer (any mouse press): detected via `QApplication.installEventFilter(self)` when open; if global pos not inside drawer rect → `_dismiss()`; filter removed on dismiss
- Player chip click (toggle): calls `_dismiss()` if open
- HUD auto-hide hide-all: player calls `toggle()` to close when hiding HUD

### HUD Auto-Hide Prevention
- `enterEvent` → `owner._on_hud_enter()`: stops auto-hide timer
- `leaveEvent` → `owner._on_hud_leave()`: re-arms auto-hide

---

## THEME VALUES (player_qt/ui/theme.py)

```
BG_DEEP         rgba(10, 10, 10, 0.92)
BG_PANEL        rgba(16, 16, 16, 0.94)
BG_SCRIM        rgba(12, 12, 12, 0.45)       # Original BottomHUD bg (mpv version)
BG_TOAST        rgba(10, 10, 10, 0.85)       # VolumeHUD + ToastHUD bg
BG_BUBBLE       rgba(12, 12, 12, 0.82)       # Seek slider time bubble

BORDER_SUBTLE   rgba(255, 255, 255, 0.08)    # HUD top border, dividers
BORDER_DEFAULT  rgba(255, 255, 255, 0.12)    # Drawer, popover borders
BORDER_MEDIUM   rgba(255, 255, 255, 0.18)    # VolumeHUD, ToastHUD borders

TEXT_PRIMARY    rgba(245, 245, 245, 0.98)    # Main readable text
TEXT_SECONDARY  rgba(255, 255, 255, 0.92)    # Playlist items, checkboxes
TEXT_MUTED      rgba(255, 255, 255, 0.55)    # Duration, title, descriptions

ACCENT_WARM     rgba(214, 194, 164, 0.95)    # Tankoban brand amber — drawer/overlay titles, seek fill, selected items
ACCENT_HIGHLIGHT rgba(255, 170, 92, 0.90)   # (unused in these files)
```

**Context menu** (`CONTEXT_MENU_SS`):
- Background: `rgba(12,12,12,0.92)`, TEXT_SECONDARY text, BORDER_DEFAULT border, radius 8px, padding `6px 0`
- Item padding: `6px 24px 6px 16px`
- Item selected: `rgba(255,255,255,0.10)`
- Separator: height 1px, BORDER_SUBTLE, margin `4px 8px`

**Transport icon button SVG fill:** `#cccccc`
**CenterFlash icon color:** `#e0e0e0`

---

ASSISTANT 1 COMPLETE
