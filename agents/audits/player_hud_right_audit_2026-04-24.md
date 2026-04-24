# Player HUD right-side audit — 2026-04-24

**Scope.** The five chips on the right side of the bottom HUD — `1.0x` / `Filters` / `EQ` / `Tracks` / `List` — plus their popovers. Goal: enumerate what's unrefined and propose specific fixes ranked by severity. Written on Hemanth's request: *"let's make some UI tweaks, I want you to use mcp to audit the HUD items on the right. They are unrefined and I'd appreciate if you could do a comprehensive audit to identify areas of improvement."*

**Method.**
- MCP smoke against a fresh Tankoban session (Saiki Kusuo no Psi-nan Episode 11, post-`build_and_run.bat` with all today's Agent 3 ships applied — includes the three video-player items shipped evening of 2026-04-24).
- UIA-enumerated the chip widgets for exact geometries (via `automation_elements list`).
- Clicked each chip sequentially (1.0x → Filters → EQ → Tracks), captured popover screenshots.
- Cross-referenced findings against `src/ui/player/VideoPlayer.cpp:1370-1743` (chipStyle + ctrlRow layout) and the four popover files.
- NOT covered: live hover-state testing (synthetic `SetCursorPos` via pywinauto-mcp doesn't fire `WM_MOUSEMOVE` on the native HWND canvas, so hover isn't testable via the current MCP toolchain — code inspected instead).

**Verdict scale.** Each finding tagged `SEV-1` (user-facing, visibly wrong or inconsistent), `SEV-2` (refinement, feels unpolished), `SEV-3` (nice-to-have, matches reference-player convention).

---

## 1. Chip-row findings (the five chips as a strip)

### 1.1 Gap spacing is irregular [SEV-1]

**Measured from UIA** (window 1920 wide, chips y=756-793):

```
Chip        x-range       width   gap after
1.0x        1513 → 1576   63 px
                                  18 px
Filters     1594 → 1675   81 px
                                   5 px  ← tight pair
EQ          1680 → 1735   55 px
                                  18 px
Tracks      1753 → 1834   81 px
                                   5 px  ← tight pair
List        1839 → 1899   60 px
```

**Source-side intent** at `src/ui/player/VideoPlayer.cpp:1734-1743`:
- speed → filters: `addSpacing(12)`
- filters ↔ eq: `addSpacing(3)` (intra-group)
- eq → tracks: `addSpacing(12)`
- tracks ↔ list: `addSpacing(3)` (intra-group)

So the intent is three groups: `{1.0x} {Filters, EQ} {Tracks, List}`. A real intention is there — Filters+EQ are both video/audio adjustments, Tracks+List are both content selectors.

**What makes it feel unrefined.** The 12→3 ratio is too subtle — visually it reads as "uneven" not "grouped." Reference players either (a) use a single consistent gap (mpv OSC: 6px between every button, IINA: 8px) or (b) use a clear separator (VLC: ~12px + thin vertical divider between groups).

**Proposal.** Pick one:
- **(a)** Uniform 6px between all five chips (drop the grouping cue entirely).
- **(b)** Keep the grouping but make it deliberate: 4px intra-group + 16px inter-group + a 1px vertical divider between groups (ethereal — `rgba(255,255,255,0.08)`). Makes the *intent* visible.

I'd go with (b): the grouping is semantically useful and Hemanth is a taste-first user — a visible divider is cheap (`QFrame::VLine` with 12px height + alpha stroke) and reads correctly at a glance.

### 1.2 Chip widths are uneven, driven by label length — and it shows [SEV-2]

Chip widths: 63 / 81 / 55 / 81 / 60 — range 26 px (41% spread). `EQ` (55 px) and `List` (60 px) are visually short; `Filters` and `Tracks` (81 px) dominate. Padding is `4px 10px` per `chipStyle` (VideoPlayer.cpp:1379) so label length is load-bearing.

**Reference-player convention.** mpv OSC uses uniform-width icon-buttons (24×24 each). IINA uses a mix but aligns them to a 32px baseline grid. When labels vary, a common trick is min-width so short labels get padding:

```css
QPushButton { min-width: 56px; text-align: center; }
```

**Proposal.** Set `min-width: 64px` on `chipStyle` so `EQ`/`List` pad up to match `Filters`/`Tracks`. Range drops from 26px to ~17px, chips feel more like a strip and less like a sentence. `1.0x` stays naturally centered.

### 1.3 Only two of five chips signal their "active" state [SEV-1]

`chipStyle` defines `QPushButton[active="true"]` as a left-border accent (`VideoPlayer.cpp:1397-1400`) — 3px of `rgba(245,245,245,0.75)` on the left edge. Grep:

```
src/ui/player/VideoPlayer.cpp:2025  m_eqChip->setProperty("active", eqActive);
src/ui/player/VideoPlayer.cpp:2059  m_filtersChip->setProperty("active", count > 0);
```

Only `EQ` and `Filters` wire it. `Tracks` / `List` / `1.0x` never set `active`.

**What the user sees right now:**
- `1.0x` — the label itself carries state (`0.75x` / `1.25x` etc). Fine.
- `Filters` — shows a left accent when any non-default filter is on. Fine.
- `EQ` — shows a left accent when preset ≠ Flat. Fine.
- `Tracks` — no indicator, ever. User can't tell which audio/sub track is active without opening the popover.
- `List` — no indicator. User can't tell episode position without opening the drawer.

**Proposal** (both SEV-1 because they're visible regressions vs reference players):
- **Tracks:** show a subtle suffix on the chip label — `Tracks · ES` when subtitle language is Spanish, `Tracks · 5.1` when 5.1 audio is selected. Keep label short; truncate at ~12 chars. IINA uses exactly this pattern.
- **List:** show episode position inline — `List 11/28`. Very common convention (VLC, PotPlayer both do this when a playlist is loaded).

Both are drop-in label rewrites in the existing chip — no new widgets.

### 1.4 Chip hover/pressed contrast is weak [SEV-2]

From `chipStyle` gradients (VideoPlayer.cpp:1371-1391):
- Normal: 68/44/24 gradient on dark.
- Hover: 80/56/36 — only ~12 shade brighter.
- Checked (popover open): 30/20/12 — barely darker.

Side-by-side on a dark background (the HUD overlay over black-ish letterbox) the three states are near-indistinguishable. mpv/VLC bump hover contrast noticeably (~25-30%). The `:checked` state should be MORE obvious, not less — when a user has a popover open, the parent chip should look "pressed in."

**Proposal.**
- Hover: bump to 92/68/44 (~35% brighter than rest).
- Checked: keep the dark gradient but boost border to `rgba(245,245,245,0.55)` (from 0.38) so the "selected" state is ring-accented instead of fill-darkened. Matches most modern flat-UI conventions.

### 1.5 No keyboard shortcut hints / tooltips [SEV-3]

None of the chips have `setToolTip()`. A hover tooltip showing the keyboard shortcut (e.g. `Tracks (T)`, `List (L)`) would be useful discoverability. Deferred as SEV-3 because it's additive, not fixing a visible wrong.

---

## 2. Popover findings (per chip)

### 2.1 `1.0x` speed chip → native QMenu [SEV-1]

The speed chip opens `new QMenu(this)` (VideoPlayer.cpp:1599) — that's a **native-styled QMenu**, not a custom popover class. All four other chips open custom `QFrame` popovers (`FilterPopover` / `EqualizerPopover` / `TrackPopover` / `PlaylistDrawer`). That's structurally inconsistent — one chip behaves differently from the other four.

Visual symptoms:
- Menu item padding, font, separator thickness, corner radius all driven by Qt's native-menu rendering + a thin stylesheet override (VideoPlayer.cpp:1600-1605). The other popovers use hand-tuned padding/sizing in their own .cpp.
- Menu position drop-up from chip's top-left (`menu->exec(m_speedChip->mapToGlobal(m_speedChip->rect().topLeft()))`). Adequate but not anchor-aware like the other popovers.
- **"Reset to 1.0x" is literally redundant** — `1.0x` is already the third menu item and picking it does the same thing. If you want a "reset," you'd do it differently (middle-click on the chip, double-click header of a slider popover, etc.).

**Proposal.** Port this to a real `SpeedPopover` class matching the others — small vertical list of speeds, checkmark on current, maybe a small slider for fine-tune (0.05 steps between presets). Delete the "Reset to 1.0x" row entirely. Even simpler: the chip itself could be a combo-box style presser that cycles through presets on each click (and a right-click menu for direct jump) — but that's a product decision not a refinement. The QMenu → QFrame port alone is the SEV-1 fix.

### 2.2 Filters popover has no title + no reset [SEV-1]

Screenshot: Filters popover opens with `Video` header → [Deinterlace dropdown, Motion smoothing checkbox, Brightness/Contrast/Saturation sliders] → `Audio` header → [Volume Normalization checkbox].

Missing — compare to EQ popover (screenshot §2.3):
- **No popover title.** EQ says "Equalizer"; Filters has no banner. Feels half-dressed.
- **No Reset button.** EQ has a "Reset" button at the bottom. Filters has no way to quickly undo changes. If a user ruins brightness, they have to drag each slider back to 0 by eye.
- **No preset combo.** EQ has presets (Flat / Rock / Pop / etc.). Filters could have {Default / Bright / High-contrast / Anime} but doesn't.

The inconsistency is the most unrefined thing here — both popovers show the same DOM shape (sliders + toggles) but one has proper chrome and one doesn't.

**Proposal.**
- Add `QLabel("Filters")` at top with the same styling as `Equalizer` title.
- Add a `Reset` button at the bottom mirroring EQ's reset.
- Consider adding a "Preset" combo (Default / Auto / Custom) even if "Custom" is the only working choice for now — it creates structural parity with EQ so the two popovers read as siblings.

### 2.3 Filters popover: label/terminology drift [SEV-2]

Labels: `Deinterlace` / `Motion smoothing` / `Brightness` / `Contrast` / `Saturation` / `Volume Normalization`.

- "Motion smoothing" is a streaming-TV marketing term for frame-interpolation. In video-player land the standard term is "Interpolate" or "Frame doubling." Users coming from mpv/VLC/PotPlayer won't recognize "Motion smoothing."
- "Volume Normalization" is correct but verbose. mpv calls it "loudnorm" internally; user-facing it's usually just "Normalize."
- `Deinterlace` dropdown shows "Off" as the only visible value — presumably has Yadif/Bwdif/etc. on click but cramped font makes it read as just-off state.

**Proposal.** Rename: `Motion smoothing` → `Interpolate` (matches existing `FilterPopover.h:28` `interpolate()` accessor). `Volume Normalization` → `Normalize`. Bump Deinterlace dropdown font to 12 px so the mode is readable at rest.

### 2.4 Filters popover: slider value semantics inconsistent [SEV-2]

Brightness shows `0.0`, Contrast shows `1.0`, Saturation shows `1.0`. These are ON DIFFERENT SCALES — brightness is additive (0 = no change), contrast and saturation are multiplicative (1.0 = no change). Showing them as raw numbers without units asks the user to remember that "0.0 on brightness" and "1.0 on contrast" both mean "leave alone."

**Proposal.** Present as percent-deltas: `0%` for all three when at default. User reads "all three at 0%" = all three untouched. Makes a lot easier to answer "what did I change?" on a busy popover.

### 2.5 EQ popover: label overflow on DRC [SEV-2]

`Dynamic Range Compression` checkbox label stretches to the full width of the popover. On a 320-ish px wide panel, one checkbox label consuming the entire bottom row looks unbalanced.

**Proposal.** Shorten to `Compressor` or `DRC` (keep a tooltip with the full explanation). Frees horizontal space; visually matches the 10 short band labels above.

### 2.6 EQ popover: preset combo + `Save as...` button crowded [SEV-3]

Top row: `Flat` combo + `Save as...` button inline. `Save as...` has ellipsis indicating it opens a dialog — fine pattern, but visually busy when a preset is all most users ever touch. Native combo styling (rendered by Qt) sits awkwardly on the dark background — its arrow icon is blocky.

**Proposal.** SEV-3 — polish: restyle combo with a custom down-arrow, move `Save as...` to a small "+" icon-button on the right of the combo, matching the chip-row vocabulary elsewhere.

### 2.7 Tracks popover is a four-section monster [SEV-1]

Screenshot: Tracks popover shows Audio list → Subtitle list → Subtitle Delay row → Style section (6 controls: Size, Margin, Position, Outline, Color, BG). Total height ~420 px, wider than the other popovers.

**What's unrefined:**
- Too many concerns in one popover. Audio track selection + subtitle track selection + subtitle delay + subtitle style are four distinct user intents, gathered under one chip for convenience.
- Subtitle Delay row is a weird mini-control cluster: `[−] 0ms [+] [Reset]` — four buttons to manage one scalar. Compare to the slider pattern used everywhere else in the popover. Inconsistent.
- Style section is 6 tightly-packed rows. Six controls tall on a popover that also has three other sections = cognitive overload.
- No clear visual selection state in the Audio/Subtitle lists — just "Stereo · Stereo · 48kHz · Default" as a flat text row. User can't tell which track is currently playing without reading carefully.
- Top edge of the popover is **within 5-10 px of the window title bar** when open (y ~50). Feels precariously positioned.

**Proposal (two paths, pick one):**

**Path A (minimal — reshape):**
- Move Subtitle Delay into the Style section as another slider row.
- Add selection chevrons / checkmarks to the Audio and Subtitle list rows so the active track is unmistakably marked.
- Move `Position` slider (the one I shipped evening 2026-04-24) to immediately after `Margin` in Style (currently: Size / Margin / Position / Outline / Color / BG — Position is between Margin and Outline which reads OK). Actually it's already there — leave it.
- Raise popover top by ~40 px so it doesn't hug the title bar.

**Path B (decompose):**
- Split Tracks popover into `Audio` chip + `Subs` chip. Audio popover: audio track list only. Subs popover: subtitle track + delay + style. Reduces the Tracks popover to one concern at a time.
- The chip strip becomes `1.0x / Filters / EQ / Audio / Subs / List` — six chips, reads better than five-with-one-overloaded.

I lean Path B — it's a small chip-count bump but it dissolves the "monster popover" problem. Hemanth's call.

### 2.8 Tracks popover: "BG" / "Size" / "Margin" / "Position" label widths inconsistent [SEV-2]

Looking at the Style section: label column has `Size` (4 chars), `Margin` (6), `Position` (8), `Color` (5), `BG` (2). `BG` is an opaque abbreviation for "Background opacity." All these should be the same width of column AND consistently spell-or-abbreviate.

**Proposal.** Unify the label column to fixed-width ~52 px and use consistent full words: `Size`, `Margin`, `Position`, `Outline`, `Color`, `Background`. Or if space is at a premium, abbreviate symmetrically: `Size`, `Marg`, `Pos`, `Outl`, `Clr`, `BG`. Full words are friendlier and we have the room — prefer that.

### 2.9 List (playlist drawer) findings [SEV-2 overall, from the prior screenshot during the `L`-key reveal]

- Drawer has its own title ("Playlist") + shuffle/loop/sort/refresh icon toolbar + `Save`/`Load` buttons + scrollable episode list + `Auto-advance` checkbox at the bottom. More structure than the chip popovers — that's fine, drawers can be richer.
- The icon toolbar uses unicode glyph icons (🔀 ↻ etc.) at ~16px — fine at a distance but the glyphs feel dated vs SVG icons used elsewhere in the app.
- The episode list rows are uniform-height, lightly padded — visually calm. No complaint.
- `Auto-advance` checkbox at the bottom is a sensible setting — but also has unclear authority (per-show? global?).

**Proposal.** SEV-3 — swap the glyph icons for SVG matching the player's existing icon language. Leave the rest.

---

## 3. Summary of proposed changes, ranked

**SEV-1 (ship first — visibly wrong / missing):**
1. **Fix chip gap spacing** — either uniform 6px OR 4/16 + VLine divider between groups. Target: `VideoPlayer.cpp:1734-1743`.
2. **Surface "active" state on Tracks + List chips** — `Tracks · ES` / `List 11/28` style suffixes. Target: `VideoPlayer.cpp:2025-2059` area (add parallel setProperty/setText calls).
3. **Port `1.0x` QMenu to a real Popover class** matching the other four. Delete `Reset to 1.0x` row. Target: new `SpeedPopover.{h,cpp}` + `VideoPlayer.cpp:1598-1630` rewrite.
4. **Add title + Reset + (optional preset combo) to Filters popover** to match EQ structurally. Target: `FilterPopover.cpp` constructor.
5. **Pick Path A vs B for Tracks popover** (reshape vs decompose) and execute.

**SEV-2 (refinement — feels unpolished):**
6. `min-width: 64px` on chipStyle so EQ/List don't look short.
7. Bump hover contrast + switch `:checked` to ring-accent border.
8. Rename `Motion smoothing` → `Interpolate`, `Volume Normalization` → `Normalize`.
9. Present Filters slider values as `0%` at default (unified semantics).
10. Shorten EQ's `Dynamic Range Compression` → `Compressor` or `DRC`.
11. Unify Tracks Style label column widths + spelling.

**SEV-3 (nice-to-have — matches reference-player convention):**
12. Tooltips on chips with keyboard shortcuts.
13. Restyle EQ preset combo + Save-as button.
14. Swap playlist drawer glyph icons for SVG.

---

## 4. What a minimal first fix-TODO would look like

If we pick the top 3 SEV-1s (gap spacing + chip state indicators + 1.0x QMenu port) plus the Filters title/reset parity, that's **4 batches** at ~1-2 wakes each:

1. **Batch 1** — chip gap spacing: 12/3 → 4/16 + VLine divider between groups. ~1 hour, `VideoPlayer.cpp` only, no new files.
2. **Batch 2** — `Tracks` + `List` chip state indicators: add `updateTracksChipLabel()` / `updatePlaylistChipLabel()` that writes suffix on chip text when active track / current episode changes. Target `VideoPlayer.cpp:2025-2059` vicinity.
3. **Batch 3** — port `1.0x` QMenu to `SpeedPopover`. New `src/ui/player/SpeedPopover.{h,cpp}` mirroring `EqualizerPopover` shape. Delete `Reset to 1.0x`. Rewrite click handler.
4. **Batch 4** — Filters popover parity: add title + Reset button. ~1 hour in `FilterPopover.cpp`.

Each batch is its own RTC; each builds-and-smokes independently (all compile-verified via `build_check.bat`, visually verified by Hemanth).

The Tracks popover Path A-vs-B decision is deliberately carved off from Batch 1-4 — it's a product decision that needs Hemanth to pick a direction before I scope the TODO.

---

## 5. Evidence + references

**UIA enumeration:** saved at `C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/3e0fb8b5-b0de-4be9-ace2-e266bc15ea13/tool-results/mcp-pywinauto-mcp-automation_elements-1777053929827.txt` (won't persist past this wake; chip measurements extracted inline in §1.1).

**Source touch-points:**
- `src/ui/player/VideoPlayer.cpp:1370-1400` — chipStyle QSS.
- `src/ui/player/VideoPlayer.cpp:1594-1686` — five chip constructors + click handlers.
- `src/ui/player/VideoPlayer.cpp:1727-1743` — ctrlRow layout + spacing.
- `src/ui/player/VideoPlayer.cpp:2025, 2059` — active-property call sites.
- `src/ui/player/FilterPopover.cpp` — Filters popover layout.
- `src/ui/player/EqualizerPopover.cpp` — EQ popover layout (has title + reset).
- `src/ui/player/TrackPopover.cpp` — Tracks popover (4-section monster).
- `src/ui/player/PlaylistDrawer.cpp` — List drawer.

**Reference players for convention cites:**
- mpv OSC — minimal icon-only row, uniform 6px gaps, no chip labels (icon+tooltip).
- IINA — chip-with-suffix-state pattern (`Track · EN`, `List 3/8`).
- VLC — explicit group dividers, wider gaps.
- PotPlayer — rich popovers with reset buttons + presets uniformly on every popover (our EQ matches this, our Filters diverges).

**Rule compliance:**
- Rule 15 (self-service): drove MCP + screenshots + source inspection + audit writing myself; Hemanth provided the one-line summon + nothing else.
- Rule 17 (cleanup): NOT done yet — current Tankoban session (PID 15424) is still alive because this was an agent-driven launch, and I'll stop-tankoban before releasing the MCP lock.
- Rule 19 (MCP LANE LOCK): LOCK posted at start of audit, RELEASED after stop-tankoban.
- No src/ touched. Audit-only deliverable.
