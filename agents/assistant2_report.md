# Assistant 2 — Video Player Parity Audit
Date: 2026-03-25

## Summary

The C++ VideoPlayer is approximately 65-70% at parity with the Groundwork FfmpegPlayerSurface. The core playback loop, all basic keyboard shortcuts, seek bar, volume HUD, center flash, shortcuts overlay, playlist drawer, track popover, filter popover, and toast HUD are all present and structurally correct. The biggest functional gaps are: (1) the right-click context menu is entirely absent in C++ — Groundwork has a rich 8-submenu context menu covering playback, video, audio, subtitles, filters, hardware acceleration, HDR/color, and aspect ratio; (2) the HUD auto-hide logic does not prevent hiding while cursor is over TrackPopover; (3) subtitle delay has no accumulator and the TrackPopover delay label is never updated; (4) drag-and-drop for external subtitle files is missing; (5) the seek bar only shows the time bubble during drag, not on hover; (6) the HDR badge is absent; (7) the Enter/Return fullscreen shortcut is missing; (8) the speed chip shows "1x" at startup instead of matching the label format; (9) TrackPopover style sliders are never synced from current state before opening; (10) VolumeHUD re-fades from zero on every volume scroll instead of holding at full opacity while visible.

---

## P0 — Critical (wrong behavior, broken features)

| # | Component | Groundwork behavior | Our behavior | Gap |
|---|-----------|-------------------|-------------|-----|
| 1 | Right-click context menu | Full 8-submenu menu: Playback (play/pause, mute, speed submenu with "Reset to 1.0x"), Video (Aspect Ratio submenu with 8 presets, fullscreen toggle), Audio (track list), Subtitles (track list + Off + Load External...), Filters (deinterlace checkbox, normalize checkbox), Hardware Acceleration (Auto/Disabled), HDR & Color (5 tone-map algos + color management toggle), separator + Tracks / Playlist / Back to library. All styled with CONTEXT_MENU_SS. | No right-click context menu at all. mousePressEvent is not overridden in VideoPlayer. | Complete feature absent. |
| 2 | HUD auto-hide: pointer-over-overlay prevention | _arm_controls_autohide() calls _controls_overlay_under_pointer() which returns True if any of: bottom HUD, track popover, playlist drawer, or shortcuts overlay is under mouse. HUD timer is not armed if any of these are hovered. TrackPopover and FilterPopover enterEvent/leaveEvent call _on_hud_enter() / _on_hud_leave() on the player owner via the "_player_owner" attribute. | PlaylistDrawer enterEvent calls QMetaObject::invokeMethod(parent(), "showControls"). TrackPopover enterEvent and leaveEvent are empty (call base class only). FilterPopover enterEvent emits hoverChanged(true) which stops the hide timer — that part is connected. But TrackPopover has no such signal. HUD can hide while user is interacting with TrackPopover. | HUD auto-hides while TrackPopover is open and cursor is over it. |
| 3 | Subtitle delay: cumulative tracking + popover sync | _sub_delay_ms accumulator tracks total delay. Keyboard < and > add/subtract 100ms. _on_popover_sub_delay() also updates the same accumulator. After each change, m_trackPopover.set_delay(self._sub_delay_ms) syncs the display label. Ctrl+Shift+Z resets to 0. | sendSetSubDelay(-100.0) and sendSetSubDelay(+100.0) are sent as absolute deltas, no accumulator. m_trackPopover->setDelay() is never called from keyboard shortcuts or from VideoPlayer at all. TrackPopover delay label always reads "0ms". No reset shortcut. | Sub delay state is not tracked. Popover shows wrong value. Reset missing. |
| 4 | Center flash on scrub | _flush_pending_seek() fires _flash_center("seek-fwd") or _flash_center("seek-back") when the scrub drag ends, comparing target vs origin position. | No center flash fires on scrub drag release. Flash only fires on seek-step buttons and Left/Right arrow keys. | Missing seek feedback on scrub. |
| 5 | Drag-and-drop external subtitle loading | dragEnterEvent / dragMoveEvent / dropEvent accept dropped .srt .ass .ssa .sub .vtt files and call _load_external_subtitle(). acceptDrops is True. | setAcceptDrops not called. No drag event overrides. | Feature absent. |
| 6 | HDR badge | _on_media_info() shows a gold QLabel "HDR" badge in the bottom HUD for 5 seconds when hdr=True in the media info. _on_hud_enter() re-shows it if HDR is active. Badge style: color #FFD700, bg rgba(255,215,0,0.15), border-radius 3px, padding 1px 4px. | No HDR label widget exists in the C++ control bar. | Feature absent. |
| 7 | Speed chip startup label | Speed chip shows "1.0x" from set_speed_chip_text(f"{speed:g}x") where speed=1.0. | Speed chip built with hardcoded text "1x". m_speedChip = new QPushButton("1x"). | Wrong startup label. |
| 8 | Enter / Return fullscreen | Key_Enter and Key_Return toggle fullscreen. Listed in shortcuts overlay as "F / F11 / Enter". | Not in keyPressEvent switch. No shortcut handles these keys. | Shortcut absent. |
| 9 | Ctrl+Shift+Z reset subtitle delay | _reset_sub_delay() called when Ctrl+Shift+Z pressed. _sub_delay_ms set to 0, sidecar updated, toast shown. Also listed in shortcuts overlay reference card. | Not in keyPressEvent. No reset method exists. | Shortcut and behavior absent. |
| 10 | Key_Comma and Key_Period: Shift guard for sub delay | Groundwork: fires sub delay adjust for Key_Less or (Key_Comma AND Shift). Similarly Key_Greater or (Key_Period AND Shift). This prevents plain comma/period press (e.g., in filenames or text) from triggering sub delay. | C++ fires sub delay on Key_Less OR Key_Comma (no shift check), and Key_Greater OR Key_Period (no shift check). Plain comma/period keypress triggers sub delay incorrectly. | Modifier check missing — accidental triggering. |

---

## P1 — Polish (visual differences, minor wrong behavior)

| # | Component | Groundwork behavior | Our behavior | Gap |
|---|-----------|-------------------|-------------|-----|
| 1 | VolumeHUD position | _position_overlays() places it: horizontally centered, y = height - bottom_hud_height - volume_height - 18. Recalculated on every overlay reposition including resize. | Hardcoded: py = parentWidget()->height() - height() - 80. The -80 is not tied to actual control bar height and is not recalculated on resize. | Misaligns when HUD is hidden or window is resized. |
| 2 | VolumeHUD: fade-in on re-trigger | When already visible, Groundwork sets opacity=1.0 directly (no re-fade). Only fade-in animates from 0 when hidden. | showVolume() always calls setOpacity(0.0) then starts fade-in regardless of whether widget is already visible. Causes flicker on rapid scroll. | Flicker on repeated volume scroll. |
| 3 | ToastHud: position recalculation on resize | _position_overlays() recalculates toast position on every resize, show, and after every _show_toast() call. | Position is set once inside showToast() at x = p->width() - width() - 12. Not updated on resize. | Toast drifts to wrong position if window is resized while toast is visible. |
| 4 | ToastHud: fade-in animation | Groundwork sets opacity=1.0 immediately — no fade-in. Only fade-out is animated (200ms). | C++ animates a 120ms fade-in before the toast reaches full opacity. | Extra animation not present in Groundwork. |
| 5 | Seek bar: hover time bubble | SeekSlider.mouseMoveEvent shows the time bubble on any mouse hover (dragging or not). leaveEvent hides it. | C++ time bubble only appears inside sliderMoved (active drag only). No hover-only bubble. | Hover time preview missing. |
| 6 | Speed menu: "Reset to 1.0x" | Speed chip menu has addSeparator() then "Reset to 1.0x" action at the bottom. | Speed chip menu only lists the 8 speed presets. No separator, no reset action. | Missing reset action. |
| 7 | Speed presets list | (0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0) — 7 items, starts at 0.5x. | (0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0) — 8 items, adds 0.25x. | Groundwork does not have 0.25x preset. |
| 8 | ShortcutsOverlay: title color | Title "Keyboard Shortcuts" rendered in ACCENT_WARM = rgba(214, 194, 164, 0.95) — warm amber. | Title rendered in rgba(245, 245, 245, 250) — white. | Wrong color. |
| 9 | ShortcutsOverlay: hint label | Card has a hint label at the bottom: "Press ? or click to close" in TEXT_MUTED color. | No hint label. | Missing text. |
| 10 | ShortcutsOverlay: shortcut entries | 22 entries. Includes: "Shift+Z" -> "Subtitle delay -100ms", "Shift+X" -> "Subtitle delay +100ms", "Ctrl+Shift+Z" -> "Reset subtitle delay", "Backspace" -> "Back to library". | C++ has 21 entries. Uses "< / >" for sub delay (matches actual key binding). Missing: Ctrl+Shift+Z reset, Backspace entry. Also shows "F / F11" without "/ Enter". | Minor content divergence. |
| 11 | FilterPopover: section header color | Section headers "Video" and "Audio" use ACCENT_WARM = rgba(214, 194, 164, 0.95). | Section headers use rgba(255,255,255,140) — muted white. | Wrong color. |
| 12 | FilterPopover: border-radius | border-radius: 8px. | border-radius: 10px. | Minor visual difference. |
| 13 | TrackPopover style state: not synced on open | Before calling toggle(), _track_popover.set_style() is called with the current font_size/margin/outline values from the adapter or popover, so sliders reflect last applied values. | m_trackPopover->setStyle() is never called before toggle(). Sliders always show defaults (24, 40, checked) even after user has changed them. | Style state lost between open/close cycles. |
| 14 | PlaylistDrawer hover: HUD auto-hide prevention | enterEvent notifies player owner via _player_owner attribute (_on_hud_enter). leaveEvent calls _on_hud_leave. Drawer is a QFrame, not QWidget. | PlaylistDrawer enterEvent calls QMetaObject::invokeMethod(parent(), "showControls") — correct for keeping HUD up but restarts the timer instead of freezing it. leaveEvent does not restart the timer. | Minor divergence — timer restarts on enter instead of pausing. |
| 15 | Control bar background | BG_SCRIM = rgba(12, 12, 12, 0.45) — very transparent, relies on GL canvas painting opaque backdrop via _paint_canvas_overlay. | rgba(10, 10, 10, 0.92) — nearly opaque. Correct for a Qt widget renderer that does not have a GL overlay painting pass. | Not wrong for C++ Qt, but more opaque than reference. |
| 16 | showEvent: focus grab | showEvent calls self.setFocus(Qt.FocusReason.OtherFocusReason) to ensure keyboard events land on the player immediately on show. | No showEvent override in VideoPlayer. Player may not have keyboard focus when first shown without a click. | Keyboard shortcuts may not work immediately after player is opened. |
| 17 | mousePressEvent: focus grab | mousePressEvent calls setFocus(MouseFocusReason) to re-acquire focus after clicking inside the player. | No mousePressEvent in VideoPlayer. | Click-to-refocus missing. |
| 18 | Seek flash: direction from scrub origin | _flush_pending_seek() compares target vs _pending_seek_origin (position at start of drag) to decide seek-fwd vs seek-back flash direction. | No flash on scrub at all (covered in P0-4). | — |

---

## P2 — Missing features (not implemented at all)

| # | Feature | Groundwork has it | We have it? | Notes |
|---|---------|------------------|------------|-------|
| 1 | Right-click context menu | Yes — full 8-submenu | No | Entire feature absent |
| 2 | Drag-and-drop external subtitle loading | Yes | No | No event overrides, no setAcceptDrops |
| 3 | HDR badge in control bar | Yes | No | Widget not built |
| 4 | Aspect ratio override | Yes — 8 presets via context menu, canvas set_aspect_override() | No | Not in VideoPlayer or FrameCanvas |
| 5 | Hardware acceleration toggle | Yes — auto/disabled via context menu, QSettings persisted | No | No UI, sendSetHwaccel not wired |
| 6 | HDR tone mapping selection | Yes — auto/hable/reinhard/mobius/clip via context menu | No | No UI, sendSetToneMapping not wired |
| 7 | Color management toggle | Yes — via context menu, QSettings persisted | No | No UI, sendSetColorManagement not wired |
| 8 | External subtitle load dialog | Yes — QFileDialog launched from context menu and keyboard shortcut | No | sendLoadExternalSub exists in SidecarProcess but is never called |
| 9 | Speed menu "Reset to 1.0x" | Yes | No | Menu has no reset action |
| 10 | Subtitle delay accumulator | Yes — self._sub_delay_ms | No | No m_subDelayMs in VideoPlayer |
| 11 | Ctrl+Shift+Z sub delay reset | Yes | No | No shortcut, no method |
| 12 | TrackPopover style sync on open | Yes — set_style() called before toggle() | No | setStyle() never called from VideoPlayer |
| 13 | _show_confirmed_track_toast() pattern | Yes — toast fires only after sidecar confirms track change | No | C++ toasts immediately on user action without waiting for confirmation |
| 14 | showEvent / mousePressEvent focus management | Yes | No | No overrides |
| 15 | _on_hud_enter / _on_hud_leave on TrackPopover | Yes | No | TrackPopover enterEvent/leaveEvent are empty |

---

## Confirmed parity (things that correctly match)

- CenterFlash: size 80x80, icon size 40x40, circle bg rgba(0,0,0,140), fade-in 120ms, hold 300ms, fade-out 350ms. All four icons present (play, pause, seek-back, seek-fwd). Correct centering in parent.
- ShortcutsOverlay: fade in/out 150ms, black scrim, card width 420, border-radius 12, mousePressEvent closes on click, toggle() with fade both directions.
- PlaylistDrawer: width 320, "Playlist" header in warm amber, close button (x), auto-advance checkbox checked by default, click-outside dismiss via event filter, item format with play marker for current index, _dismiss() wired to close button.
- TrackPopover: Audio/Subtitle sections with correct list styles, delay row (-/+/Reset), Style section (Size slider 16-48, Margin slider 0-100, Outline checkbox), debounce timer 300ms, max 4 visible rows, ROW_HEIGHT 30, "Off" prepended to subtitle list, click-outside dismiss, anchor-above positioning.
- FilterPopover: Deinterlace, Brightness (-100 to 100, default 0), Contrast (0-200, default 100), Saturation (0-200, default 100), Normalize. Debounce 300ms. buildVideoFilter() with yadif and eq. buildAudioFilter() with loudnorm. activeFilterCount(). Click-outside dismiss. Filters chip badge "Filters (N)".
- VolumeHUD: size 160x32, warm amber bar fill rgba(214,194,164,240), bar bg rgba(20,20,20,230), fade-in 120ms, hide delay 1200ms, fade-out 200ms. Four volume levels. MUTE text. WA_TransparentForMouseEvents set.
- Seek bar: range 0-10000, warm amber sub-page fill, gradient groove (5px height), warm gradient handle. Time bubble on drag. blockSignals pattern during programmatic updates.
- All basic keyboard shortcuts: Space, Left/Right, Up/Down, M, C/], X/[, Z/\, A, S, Shift+S, D, Shift+A, N, P, L, ?, Esc, Backspace, F, F11, <, >.
- HUD auto-hide: 3000ms timer, stops while paused, stops during seek drag.
- Cursor auto-hide: 2000ms timer, only in fullscreen, only when not paused and HUD hidden.
- Episode prev/next buttons: hidden for single-item playlist, disabled at boundaries.
- onEndOfFile: auto-advance if playlist has more items and auto-advance is enabled.
- Progress save/restore: SHA1-based video ID, positionSec/durationSec stored, resume if > 2s and < 95% of duration.
- onTracksChanged: merges audio + subtitle arrays with type field added, populates TrackPopover.
- Speed keyboard cycling (C/X/Z shortcuts step through presets), chip text updates, toast fires.
- SidecarProcess: all required commands wired — open, pause, resume, seek, stop, setVolume, setMute, setRate, setTracks, setSubVisibility, setSubDelay, setSubStyle, loadExternalSub, setFilters, resize, shutdown.
- SubtitleOverlay present and repositions above HUD when controls are visible.
- Seek throttle: 80ms QTimer matching Groundwork's _SEEK_THROTTLE_MS = 80.

---

## Recommended fix order

1. **Right-click context menu** (P0-1) — Highest impact missing feature. Add mousePressEvent right-button detection. Build full context menu: Playback submenu (play/pause, mute, speed submenu with reset), Video submenu (aspect ratio submenu with 8 presets, fullscreen), Audio submenu (track list from m_audioTracks), Subtitles submenu (track list + Off + Load External...), Filters submenu (deinterlace, normalize), separator, Tracks, Playlist, Back to library. Style with CONTEXT_MENU_SS. Requires adding aspect ratio support to FrameCanvas as a follow-up.

2. **TrackPopover hover: HUD auto-hide prevention** (P0-2 / P2-15) — Add a signal or invokeMethod call in TrackPopover enterEvent/leaveEvent that tells VideoPlayer to pause/resume the hide timer. One-line fix per event handler.

3. **Subtitle delay accumulator + popover sync + reset shortcut** (P0-3 / P0-9) — Add int m_subDelayMs = 0 to VideoPlayer. Key_Less/Key_Greater must increment/decrement and call m_trackPopover->setDelay(m_subDelayMs). Add Ctrl+Shift+Z to reset to 0. Fix Shift modifier guards for Key_Comma/Key_Period (P0-10).

4. **VolumeHUD fade-in flicker** (P1-2) — In showVolume(), guard the fade-in: if already visible and opacity >= 1.0, skip the setOpacity(0.0) and just restart the hide timer.

5. **Hover time bubble on seek bar** (P1-5) — Install a mouseMoveEvent on m_seekBar (or subclass it) that shows the time bubble on hover even without a drag. leaveEvent should hide it.

6. **showEvent and mousePressEvent focus** (P1-16/P1-17) — Override showEvent to call setFocus(). Override mousePressEvent to call setFocus(MouseFocusReason).

7. **Speed chip: startup label, presets, and reset action** (P0-7 / P1-6) — Change "1x" to "1.0x" in buildUI(). Add separator + "Reset to 1.0x" action at bottom of speed menu popup. Consider removing 0.25x to match Groundwork's 7-preset list.

8. **Enter/Return fullscreen shortcut** (P0-8) — Add Key_Return and Key_Enter to the fullscreen case in keyPressEvent.

9. **TrackPopover style sync on open** (P1-13 / P2-12) — Before m_trackPopover->toggle(), call m_trackPopover->setStyle() with the current values (track popover exposes subFontSize(), subMargin(), subOutline() accessors that can be read back).

10. **ShortcutsOverlay + FilterPopover polish** (P1-8/P1-9/P1-11) — Change ShortcutsOverlay title color to rgba(214,194,164,240), add hint label. Change FilterPopover section header colors to rgba(214,194,164,240). These are pure paint-code/stylesheet changes.

---

### Notes for Agent 1 (batch planning)

- The HDR badge (P2-3), aspect ratio (P2-4), hardware acceleration (P2-5), tone mapping (P2-6), and color management (P2-7) are all context-menu-dependent. They can be bundled with Fix #1 as a single "context menu + advanced settings" batch.
- The confirmed-track-toast pattern (P2-13) is a refinement, not a blocker. Leave for a later polish batch.
- sendLoadExternalSub already exists in SidecarProcess — wiring up the QFileDialog launch is straightforward and can be bundled with the subtitle submenu in the context menu batch.
- ToastHud position-on-resize (P1-3) is a one-liner: call a reposition helper from resizeEvent.
