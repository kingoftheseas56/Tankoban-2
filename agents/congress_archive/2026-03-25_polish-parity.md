# Congress 3 Archive — Polish & Parity (Comic Reader + Video Player)
**Opened:** 2026-03-25
**Closed:** 2026-03-25
**Result:** All 9 batches shipped across both tracks. Both tracks complete.

---

## Source
- Comic reader audit: `agents/assistant1_report.md` (Tankoban-Max comparison)
- Video player audit: `agents/assistant2_report.md` (TankobanQTGroundWork comparison)
- Missing modes (Auto Scroll, Auto Flip, Two-Page Scroll) intentionally excluded per Hemanth.

---

## Track A — Comic Reader (Agent 1, Batches A–F)

| Batch | Scope | What shipped |
|-------|-------|-------------|
| A | HUD auto-hide overhaul | `m_hudPinned` flag. Single/Double = pinned. Strip = 3s timer. Keypress resets timer. Scrub-drag freeze. |
| B | Rendering fidelity | No-upscale portrait (native-res ceiling). Unified pair scale (`min(scaleL, scaleR)`). Gutter gap `TWO_PAGE_GUTTER_PX=8`. Cover spine alignment. |
| C | Context menu completeness | Go-to-Page/Reveal/Copy in double-page mode. Bookmarks jump list. Image quality selector. Ctrl+0 reset. |
| D | Volume navigator polish | Progress metadata per row. Last-read time. Current volume marker. Volume count in title. Numeric search. |
| E | Y-axis pan + nav coalescing | Y-axis pan in double-page zoom. Navigation coalescing (`m_navBusy`/`m_navTarget`) — rapid keypresses coalesce. |
| F | Settings migration + overlay discipline | Series settings migration (seed from last-saved). `closeAllOverlays()` — single-open discipline enforced. |

---

## Track B — Video Player (Agents 2/4/5 parallel builds + Agent 3 sequential G–I)

### Parallel components

| Agent | Delivered |
|-------|-----------|
| Agent 2 | `SeekSlider.h/.cpp` — QSlider subclass, hover tracking, `hoverPositionChanged` + `hoverLeft` signals, self-contained groundwork QSS |
| Agent 4 | FilterPopover section headers → warm amber `rgba(214,194,164,240)`, border-radius 8px. `VideoContextMenu.h/.cpp` — full 6-section right-click context menu |
| Agent 5 | `TrackPopover::hoverChanged(bool)` signal via enterEvent/leaveEvent |

### Agent 3 sequential batches

| Batch | Scope | What shipped |
|-------|-------|-------------|
| G | Core bug fixes | Subtitle delay accumulator (`m_subDelayMs`). Shift guard on comma/period. Ctrl+Shift+Z reset. Enter/Return fullscreen. Speed chip "1.0x". 7-preset list (removed 0.25x). "Reset to 1.0x" in speed menu. showEvent/mousePressEvent focus. VolumeHUD position fix (dynamic bar height). |
| H | Polish + wiring | VolumeHUD flicker fix (guard re-fade). Center flash on scrub. TrackPopover style sync on open. TrackPopover `hoverChanged` wired to HUD timer. Toast resize reposition. ShortcutsOverlay warm amber title + hint label + Ctrl+Shift+Z/Backspace/Enter entries. |
| I | Wiring | SeekSlider wired (hover time bubble). VideoContextMenu wired (`contextMenuEvent` with all 13 action types). |

---

## Agent roles
- Agent 1: Comic reader domain master — all Track A batches
- Agent 2: Support — SeekSlider component
- Agent 3: Video player domain master — all Track B batches G/H/I
- Agent 4: Support — FilterPopover fix + VideoContextMenu component
- Agent 5: Support — TrackPopover hoverChanged signal
