# Audit - Subtitle Quality - 2026-04-30

By Agent 7 (Codex). For Agent 3 (Video Player).
Reference comparison: Netflix subtitle rendering, Crunchyroll subtitle rendering, libass API, mpv subtitle options.
Scope: Current subtitle rendering in Tankoban's ffmpeg sidecar path and mpv path. This is a Trigger C text audit only. No source files were edited. No live Netflix or Crunchyroll playback capture was available in this Codex session, so the benchmark section uses official docs plus the visible behavior Hemanth and Agent 3 recorded in chat.md.

Answer: ffmpeg is the more complete Tankoban path today, but it is not yet a best-possible subtitle renderer. mpv is functional, but it currently runs close to mpv defaults and does not receive Tankoban's style settings. Phase 2 should raise both paths, not just make mpv match ffmpeg.

## §1. Current State - ffmpeg Backend Subtitle Rendering

Tankoban's ffmpeg backend uses the bundled native sidecar renderer at `native_sidecar/src/subtitle_renderer.cpp`. Text subtitles go through libass. Bitmap subtitles such as PGS/DVD are decoded with FFmpeg and blended as bitmaps. Output reaches the Qt player through the overlay SHM path: the sidecar emits `overlay_shm`, `SidecarProcess` forwards `overlayShm`, and `FrameCanvas` uploads that overlay texture (`native_sidecar/src/video_decoder.cpp:600-623`, `native_sidecar/src/video_decoder.cpp:734-743`, `src/ui/player/VideoPlayer.cpp:3739-3744`, `src/ui/player/FrameCanvas.cpp:2075-2081`).

Observations from source:

1. SRT-like text subtitles get an injected ASS style. The default is Arial, white text, bold enabled, black outline, black shadow, centered bottom alignment, and `MarginV=40` (`native_sidecar/src/subtitle_renderer.cpp:88-105`). The outline-off variant only changes `Outline=0` (`native_sidecar/src/subtitle_renderer.cpp:110-125`).

2. libass is initialized with font extraction enabled (`ass_set_extract_fonts(library_, 1)`) and a renderer font provider. Windows uses `ASS_FONTPROVIDER_DIRECTWRITE`; non-Windows uses Fontconfig. The fallback family is Arial (`native_sidecar/src/subtitle_renderer.cpp:141-162`). I found no call to `ass_add_font` or `ass_set_fonts_dir`; container attachments are not explicitly loaded into libass by Tankoban code. libass supports extracted fonts and `ass_add_font`, and says an additional font directory can be set for lookup ([libass API header](https://github.com/libass/libass/blob/master/libass/ass.h), lines 2479-2517 and 3361-3371).

3. `ass_set_storage_size`, `ass_set_frame_size`, margins, use-margins, and pixel-aspect handling are present. The comments explicitly say this was added for correct aspect, blur, transforms, and VSFilter-compatible behavior (`native_sidecar/src/subtitle_renderer.cpp:335-365`). This matches libass' API guidance that renderers should be configured with storage size, frame size, and fonts, and that storage size affects transforms/aspect/blur scale ([libass API header](https://github.com/libass/libass/blob/master/libass/ass.h), lines 2599-2605 and 2655-2687).

4. The subtitle position slider is still implemented as a rendered-image Y-shift. The code calls `ass_set_line_position(renderer_, 0.0)`, renders the frame, computes the bounding box of the whole `ASS_Image` list, then shifts every image by `y_shift` before blending (`native_sidecar/src/subtitle_renderer.cpp:235-288`). That confirms the current implementation bypasses libass' line-position placement for the user slider.

5. This Y-shift was not accidental. The memory file `feedback_subtitle_position_yoffset_not_libass.md` records that Agent 3 intentionally shipped it after `ass_set_line_position` respected script margins and failed Hemanth's visible-position standard. The new best-possible bar changes the question: the implementation is good for the "push subs down" user intent, but it can deviate from authored ASS layout when one frame contains separate subtitle groups, signs, karaoke, or top/bottom events.

6. ASS/SSA embedded tracks use codec private data as the ASS header. SRT/SubRip/mov_text/text tracks get Tankoban's injected header. PGS/DVD subtitles use FFmpeg's bitmap subtitle decoder (`native_sidecar/src/subtitle_renderer.cpp:376-436`).

7. SRT-like packets are converted into ASS dialogue chunks by stripping XML-ish tags, converting newlines to `\N`, and using start time as `ReadOrder` (`native_sidecar/src/subtitle_renderer.cpp:544-580`). This is practical, but it means Tankoban's SRT fallback is a custom style path, not a full caption-authoring stack.

8. Runtime style override is narrow. It changes font scale through `ass_set_font_scale`, stores a margin lift, and flips the outline flag for future SRT-like track loads. It does not tune font family, text color, outline color, shadow color, shadow opacity, blur, background plate, or max-line behavior (`native_sidecar/src/subtitle_renderer.cpp:713-749`).

9. Qt-side persistence currently stores subtitle position under `videoPlayer/subtitlePosition` and sends it to the active backend on startup or adjustment (`src/ui/player/VideoPlayer.cpp:905-916`, `src/ui/player/VideoPlayer.cpp:2024-2030`). Subtitle delay is runtime-adjustable, but the style surface exposed through current code is mainly position and delay (`src/ui/player/SettingsPopover.cpp:120-193`).

10. There is an additional Qt-side overlay lift. When the HUD is visible, `FrameCanvas` shifts the whole subtitle overlay upward so controls do not cover it (`src/ui/player/VideoPlayer.cpp:2586-2594`, `src/ui/player/FrameCanvas.cpp:1198-1234`). This is a useful UI-protection layer, but it is another post-layout shift outside libass.

ffmpeg path summary: it renders real subtitles and has custom readability choices. It MATCHES STANDARD on having outline, shadow, margin/storage configuration, PGS support, and visible user position control. It DEVIATES FROM STANDARD on font robustness, embedded-font attachment handling, full style tuning, background plate support, and preserving complex authored layout when the user position slider shifts the whole rendered composition.

## §2. Current State - mpv Backend Subtitle Rendering

The mpv backend uses libmpv's native subtitle renderer. It does not use Tankoban's overlay SHM subtitle path. Subtitles are drawn by mpv inside the mpv render surface.

Observations from source:

1. On mpv creation, Tankoban sets only two subtitle options: `sub-ass-force-margins=yes` and `sub-visibility=yes` (`src/ui/player/MpvBackend.cpp:148-151`).

2. Tankoban observes subtitle track list, `sid`, `sub-visibility`, and `sub-delay` (`src/ui/player/MpvBackend.cpp:228-242`). It parses mpv's `track-list` into Tankoban's existing audio/subtitle track payloads (`src/ui/player/MpvBackend.cpp:430-474`). Visibility and delay signals are wired (`src/ui/player/MpvBackend.cpp:482-490`).

3. Track selection is wired through mpv `sid`. Off maps to `sid=no`; selecting a track sets `sid` to the cached mpv track id (`src/ui/player/MpvBackend.cpp:791-801`).

4. Subtitle visibility and subtitle delay are wired to mpv's `sub-visibility` and `sub-delay` (`src/ui/player/MpvBackend.cpp:806-817`).

5. `sendSetSubStyle` is a stub. It returns a sequence number, but ignores `fontSize`, `marginV`, and `outline`. The comment names `sub-font-size`, `sub-margin-y`, and `sub-border-size` as future wiring (`src/ui/player/MpvBackend.cpp:820-825`).

6. Subtitle position is wired to mpv `sub-pos` (`src/ui/player/MpvBackend.cpp:828-833`). Pixel offset and subtitle size are stub-like: pixel offset only emits `subtitleOffsetChanged`; size only emits `subtitleSizeChanged` and does not call a mpv property in the visible excerpt (`src/ui/player/MpvBackend.cpp:857-871`).

7. External subtitle loading is wired with mpv `sub-add` for local paths and URLs (`src/ui/player/MpvBackend.cpp:836-854`).

8. I found no Tankoban code touching these mpv subtitle style properties: `sub-font`, `sub-font-size`, `sub-color`, `sub-outline-color` / `sub-border-color`, `sub-shadow-color`, `sub-shadow-offset`, `sub-margin-y`, `sub-blur`, `sub-bold`, `sub-italic`, `sub-spacing`, or `sub-border-style`. mpv exposes these knobs: font, size, blur, bold, italic, outline color/size, border style, text color, margins, shadow offset, and spacing are all documented in mpv's subtitle options ([mpv options docs](https://github.com/mpv-player/mpv/blob/master/DOCS/man/options.rst), lines 1778-1896).

9. Agent 3's Phase 1 close note says mpv stream playback is now functional under `TANKOBAN_FORCE_MPV=1`, and it carries subtitle styling delta as the Phase 2 flag (`agents/chat.md:2162-2164`, `project_mpv_ffmpeg_parity_p1_complete.md`). Hemanth's 1.E re-smoke observation was "very low quality looking subtitles" under mpv (`agents/chat.md:2153`).

mpv path summary: mpv MATCHES STANDARD on using a mature upstream subtitle renderer and supporting native track selection, external subs, delay, and position. Tankoban's mpv integration DEVIATES FROM STANDARD because it does not yet apply Tankoban's style persistence or the Netflix/Crunchyroll-level visual target. Today it is effectively "mpv default plus force margins."

## §3. Netflix + Crunchyroll Benchmark Characterization

This is not a pixel-copy target. The useful standard is the set of dimensions polished streaming subtitles control.

Reference observations:

1. Netflix's user-facing help says subtitle/caption appearance can change font, size, shadow, and background color ([Netflix Help Center](https://help.netflix.com/en/node/100267), lines 7-10). On TV devices, Netflix exposes size and style in the playback settings when supported ([Netflix Help Center](https://help.netflix.com/en/node/100267), lines 27-40).

2. Netflix's partner timed-text guide uses white font, relative font size, two-line maximum, bottom-heavy line treatment, and bottom placement unless raised to avoid lower-third clashes ([Netflix Partner Help Center](https://partnerhelp.netflixstudios.com/hc/en-us/articles/219375728-Timed-Text-Style-Guide-Subtitle-Templates), lines 161-165, 175-204, and 234-238).

3. Crunchyroll's help says caption appearance can customize text font, background, and window color, and that app behavior follows device accessibility caption settings where applicable ([Crunchyroll Help](https://help.crunchyroll.com/hc/en-us/articles/38779221011092-How-to-set-custom-Closed-Captions-on-Crunchyroll), lines 34-56).

Quality dimensions extracted from the benchmark:

1. Text is high contrast by default: usually white or near-white glyphs with dark edge separation.

2. The edge treatment is deliberate. Netflix exposes shadow and background. mpv exposes outline, shadow, and background-box style. The viewer should not feel like text is pasted flat over video.

3. Font choice is clean and legible. The font should look like a streaming product font, not a bare system fallback. For anime, the renderer also needs to respect ASS authored fonts for signs, songs, and styled effects where present.

4. Size is balanced. It should be large enough for normal couch distance, but not so large that two-line dialogue takes over the frame.

5. Positioning is stable. Normal dialogue sits centered near the bottom. It moves only when needed to avoid credits, signs, faces, or important action. Sequences should not jump around unless the authored subtitle track requires it.

6. Line treatment matters. Netflix explicitly caps at two lines and favors readable bottom-heavy breaks. Tankoban cannot fully control this for authored subtitle files, but its fallback SRT path should not make wrapping feel arbitrary.

7. Background plates are optional but first-class. Netflix and Crunchyroll both expose background/window settings. Tankoban currently has no equivalent visible user setting.

8. The renderer must preserve authored ASS effects. Crunchyroll-style anime subtitles often depend on positioned signs, karaoke, alternate styles, and embedded fonts. A "force everything into one global look" policy would deviate from the anime subtitle standard.

## §4. STANDARD / DEVIATES Gap Analysis

ffmpeg path vs Netflix/Crunchyroll standard:

1. MATCHES STANDARD - high contrast default for basic text. Tankoban's injected SRT style is white Arial with black outline and shadow (`native_sidecar/src/subtitle_renderer.cpp:88-105`). Effort to keep: small.

2. DEVIATES FROM STANDARD - font identity and fallback are thin. The default family is Arial, and I found no bundled font directory or explicit attached-font loading (`native_sidecar/src/subtitle_renderer.cpp:156-162`; `native_sidecar/src/demuxer.cpp:630-637`). Netflix/Crunchyroll expose font control or rely on a coherent platform font surface. Effort: medium.

3. DEVIATES FROM STANDARD - MKV font attachments are not clearly handled. `ass_set_extract_fonts` is enabled, but Tankoban appears to pass only subtitle stream extradata to libass and does not call `ass_add_font` for attachment streams (`native_sidecar/src/subtitle_renderer.cpp:147-148`, `native_sidecar/src/demuxer.cpp:630-637`). For anime ASS, this matters because authored fonts are part of the intended look. Effort: medium.

4. DEVIATES FROM STANDARD - the user position slider shifts the rendered image list after libass layout. That solved Hemanth's "too high" complaint, but the best-possible standard also values authored placement for signs, karaoke, and simultaneous top/bottom events (`native_sidecar/src/subtitle_renderer.cpp:235-288`; `feedback_subtitle_position_yoffset_not_libass.md`). Effort: medium if policy split is added; heavy if per-event grouping is needed.

5. DEVIATES FROM STANDARD - style controls are incomplete. Tankoban can scale font, move position, adjust delay, and toggle outline for injected text. It does not expose shadow opacity, outline thickness, background/window plate, font family, text color, or max-line preference (`native_sidecar/src/subtitle_renderer.cpp:713-749`, `src/ui/player/SettingsPopover.cpp:120-193`). Effort: medium.

6. DEVIATES FROM STANDARD - SRT fallback strips XML-like tags and injects one default style. This is acceptable for simple dialogue, but it is not a rich caption treatment (`native_sidecar/src/subtitle_renderer.cpp:544-580`). Effort: small to medium.

7. MATCHES STANDARD - storage size, margins, use-margins, and pixel aspect are handled. This is a correct libass foundation (`native_sidecar/src/subtitle_renderer.cpp:335-365`; [libass API header](https://github.com/libass/libass/blob/master/libass/ass.h), lines 2655-2687 and 2703-2758). Effort to keep: small.

mpv path vs Netflix/Crunchyroll standard:

1. MATCHES STANDARD - uses mpv/libass native rendering, with `sub-ass-force-margins=yes` and standard track/delay/external-sub plumbing (`src/ui/player/MpvBackend.cpp:148-151`, `src/ui/player/MpvBackend.cpp:430-490`, `src/ui/player/MpvBackend.cpp:791-854`). Effort to keep: small.

2. DEVIATES FROM STANDARD - Tankoban does not wire its subtitle style persistence into mpv. `sendSetSubStyle` ignores the style payload (`src/ui/player/MpvBackend.cpp:820-825`). Effort: small to medium.

3. DEVIATES FROM STANDARD - mpv's rich `sub-*` style surface is unused. mpv documents font, size, outline color/size, shadow/back color, shadow offset, border style, margins, blur, bold, italic, and spacing ([mpv options docs](https://github.com/mpv-player/mpv/blob/master/DOCS/man/options.rst), lines 1778-1896). Tankoban currently uses almost none of that surface (`src/ui/player/MpvBackend.cpp:148-151`, `src/ui/player/MpvBackend.cpp:820-866`). Effort: medium.

4. DEVIATES FROM STANDARD - mpv position uses `sub-pos`, while ffmpeg position uses the rendered-image Y-shift. These are different semantics for ASS scripts. The TODO already flags this as Phase 2 scope (`MPV_FFMPEG_PARITY_FIX_TODO.md:200-216`). Effort: medium.

5. DEVIATES FROM STANDARD - the visible mpv path under FORCE_MPV was reported as basic white sans-serif, thin/no outline, and no clear shadow (`agents/chat.md:2153`). Source inspection supports that this is not yet Tankoban-tuned. Effort: medium.

Hypotheses - Agent 3 to validate:

- Hypothesis - mpv's visible "low quality" subtitle look comes from `sendSetSubStyle` being a no-op plus no startup `sub-*` tuning beyond `sub-ass-force-margins` and visibility. Agent 3 to validate.

- Hypothesis - ffmpeg's current Y-shift policy is correct for Hemanth's simple dialogue positioning target, but deviates from best-possible ASS rendering on files with simultaneous dialogue plus signs/karaoke/top events. Agent 3 to validate.

- Hypothesis - missing explicit MKV attachment handling is why some anime ASS tracks may fall back to Arial or a system substitute even when the file carries its intended font. Agent 3 to validate.

## §5. Recommendations - Advisory Scope Split

These are gap-close candidates, not directives.

### (a) Backend-agnostic / libass-level

1. Define one Tankoban subtitle visual spec before backend work starts. Baseline candidate: near-white text, clean sans-serif fallback, dark outline, subtle shadow, bottom-centered placement, no background plate by default, optional background plate available.

2. Keep authored ASS styles as first priority for ASS/SSA tracks. Apply Tankoban overrides mainly to unstyled text formats unless Hemanth explicitly wants a "force my style" mode.

3. Add a policy split for positioning:
   - "Standard position" uses renderer-native placement (`sub-pos` on mpv, libass placement on ffmpeg where viable).
   - "Force position" preserves the current Tankoban intent: push the visible composition to the user's chosen vertical slot.

4. Treat embedded fonts as required for the anime target. Verify that MKV attachments reach libass on ffmpeg. Verify mpv already handles them on the same files.

5. Use the same curated smoke corpus for both backends: simple SRT, ASS karaoke, ASS signs plus dialogue, two simultaneous events, PGS Blu-ray, external SRT, and a stream-server subtitle file.

### (b) mpv-specific

1. Wire `sendSetSubStyle` to mpv properties. Minimum bridge: `sub-font-size`, `sub-border-size` or `sub-outline-size`, `sub-outline-color` / `sub-border-color`, `sub-shadow-color`, `sub-shadow-offset`, `sub-margin-y`, and `sub-border-style`.

2. On backend startup, push the persisted Tankoban subtitle position and style, not only position when non-default.

3. Decide how Tankoban's current `fontSize`, `marginV`, and `outline` payload maps to mpv's scaled-pixel model. mpv documents font size and outline/margins in scaled pixels at a 720-height reference ([mpv options docs](https://github.com/mpv-player/mpv/blob/master/DOCS/man/options.rst), lines 1790-1818 and 1854-1874).

4. Validate whether `sub-pos` passes Hemanth's Saiki karaoke case. If it does not, keep mpv native behavior for authored ASS and only use global force-position for unstyled text, or add a clearly named force-position path.

5. Check whether `sub-ass-override` should stay default. mpv warns many style options are ignored for ASS unless ASS rendering is disabled or override modes are used ([mpv options docs](https://github.com/mpv-player/mpv/blob/master/DOCS/man/options.rst), lines 1778-1789). This is important: forcing style onto anime ASS can break signs and karaoke.

### (c) ffmpeg-specific

1. Verify MKV attachment font flow. If attachments do not reach libass, add explicit attachment extraction and `ass_add_font`, or route attachment font files through `ass_set_fonts_dir`.

2. Keep `ass_set_storage_size`, `ass_set_frame_size`, margins, and pixel-aspect handling. That foundation matches libass' required setup.

3. Revisit the position implementation under the new best-possible bar. The current Y-shift is an intentional user-position fix. The gap-close question is whether it should become a mode, not whether it was a mistake.

4. Tune the injected SRT ASS header toward the agreed visual spec. Current style is close on readability but fixed to Arial and has no user-facing background plate or shadow opacity control.

5. Confirm frame pacing for subtitle overlay updates. The sidecar has a dedicated render thread and overlay SHM path, but Phase 2 should smoke subtitle motion during fast dialogue, karaoke, seek, pause/resume, and stream stalls.

### (d) New persistence / UI surface

The current UI surface is not enough for the Netflix/Crunchyroll bar. Add only settings Hemanth can understand from the screen:

1. Subtitle size.

2. Subtitle position.

3. Outline thickness.

4. Shadow strength.

5. Background plate: off / behind text / full line box.

6. Font choice: default / readable sans / authored font.

7. Force authored styling: off by default, explicit toggle only if Hemanth asks for it.

8. Optional max-line behavior for unstyled text. This helps SRT, but should not rewrite authored ASS.

## §6. Proposed Phase 2 Sub-phase Breakdown

Suggested shape:

1. 2.A - Audit. This file is 2.A.

2. 2.B - Ratify the target visual spec. Agent 3 and Agent 0 should turn the benchmark into a short target: default font family, size, outline, shadow, background plate default, and whether "force position" is a separate mode.

3. 2.C - Evidence corpus. Build the subtitle smoke set before code work: simple SRT, ASS karaoke, ASS signs/dialogue, PGS, external SRT, stream subtitle. Include one bright scene and one busy lower-third scene.

4. 2.D - mpv style bridge. Wire Tankoban style persistence into mpv `sub-*` properties. Smoke under `TANKOBAN_FORCE_MPV=1`.

5. 2.E - ffmpeg font/attachment pass. Verify or add embedded-font attachment handling, then tune the injected SRT style.

6. 2.F - positioning policy pass. Compare ffmpeg Y-shift, libass/native placement, and mpv `sub-pos` on the corpus. Keep the current Y-shift only where it serves the user-position goal without damaging authored ASS layout.

7. 2.G - UI/persistence polish. Add only the small set of user-facing settings ratified in 2.B. Keep defaults close to Netflix/Crunchyroll readability.

8. 2.H - same-file backend smoke. Hemanth judges the same subtitle files on ffmpeg and mpv. Objective trace is secondary here; subjective subtitle feel is the product gate per `feedback_subjective_over_trace.md`.
