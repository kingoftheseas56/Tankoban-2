# Tankoban player — gap audit vs mpv + VLC (v2)
# 2026-04-25

## 0. Methodology + scope

### Why v2

The 2026-04-25 morning audit at `agents/audits/player_surpass_mpv_vlc_2026-04-25.md` covered the right buckets but graded Tankoban against Roon / Audirvana / foobar2000 (audiophile-tier players we don't compete with), authored a "don't bother" verdicts section, and used framing the brotherhood asked be struck from canon ("backend seriousness", "split personality", "wrong species", "honestly", "feels unfinished"). Hemanth + Agent 3 reviewed and identified the salvageable signal — P0.1, P0.2, P0.3, P0.4, P1.1, Theme 3, §7 — and the parts to discard. This v2 retains and extends the salvageable parts, drops the rest, and adds fresh gaps surfaced by re-reading the codebase post-today's-ships.

### Reference set lock

This audit compares Tankoban against **mpv + VLC only**. Not Roon. Not Audirvana. Not foobar2000. Not JRiver / HQPlayer. Not Kodi / Plex / Jellyfin. Not MadVR / SVP / AviSynth chains. If a feature only exists in audiophile-tier or external-post-processing-chain territory, it does not appear in this audit at all.

### Role lock

Gap-spotter, not roadmap-author. Findings have:

1. What mpv does (option flag, file:line, or doc URL).
2. What VLC does (similar).
3. What Tankoban does (cite into `src/` or `native_sidecar/src/`).
4. One-sentence neutral gap framing.
5. One scope tag: `[easy]` / `[moderate]` / `[scope-heavy]` / `[strategic call]`.

No P0/P1/P2/P3 ranking. No "ship X first." No "don't bother with Y" — the brotherhood decides the roadmap. No LOC-and-summon scoping. No verdicts.

### Today's in-flight state

Audit baseline is **HEAD `0386dfb`** (2026-04-25). Player-domain ships since 2026-04-24 already addressed several gaps the prior audit flagged:

- `71988a3` Path A: silent MME → WASAPI shared host-API swap. PortAudio defaulted to MME on Windows; today's swap selects WASAPI shared as default, with measured 30× device-latency improvement on Hemanth's setup. Closes part of the prior audit's A1 "WASAPI shared via PortAudio — but you weren't actually getting WASAPI" subgap.
- `b8cdbf2`, `3f193e4`, `4ca8ecf` VIDEO_HUD_MINIMALIST: 4 popovers (TrackPopover / SubtitleMenu / FilterPopover / EqualizerPopover) deleted, 3 new popovers (SubtitlePopover / AudioPopover / SettingsPopover) added. Bottom HUD now icon-only chips. SettingsPopover exposes audio delay (±50 ms) + subtitle delay (±100 ms) and **nothing else** — no tone-map, no scaler, no normalize, no deinterlace, no equalizer. The engine's filter / tone-map / EQ surfaces are still wired in `SidecarProcess`; the new HUD does not surface them. This **widens** the prior audit's V2 / V3 / A6 "engine-surface vs UI-surface" gap.
- `f15bde3` VIDEO_CONTEXT_MENU_MINIMALIST Phase 2: right-click menu collapsed 13 top-level + 14-item More-tree → 8 flat items. Same widening effect for the same gap.
- `2a59f61` `scripts/compare-mpv-tanko.ps1` (mpv-vs-Tankoban regression harness v1, log-parser).
- IPC round-trip latency tracker (runtime instrumentation in `SidecarProcess`, dumps `out/ipc_latency.log` per session per CLAUDE.md "Build Quick Reference" → "IPC round-trip latency tracker" entry).
- `3d4d6aa` Subtitle vertical-position slider — note: the slider was in TrackPopover, which got deleted in today's HUD redesign. The sidecar still has `sendSetSubPos` (`src/ui/player/SidecarProcess.h` declares it; `src/ui/player/SidecarProcess.cpp` invokes it) but no UI surface drives it post-redesign. Captured as a fresh dimension under V5.

Where today's ships closed a prior-audit gap I mark "**closed by today's ship**" inline.

### Banned-pattern policy

The following framings were used in the v1 audit and are explicitly out for v2: any "seriousness" framing about Tankoban; "split personality"; "feels unfinished"; "honestly" / "honest" hedges that imply a counterfactual lie; "Tankoban cannot honestly surpass"; "is probably the wrong species"; "avoids false claims" (defensive framing against marketing we never made); senior-reviewer-grading-junior register in any form; `Honest "don't bother" paths` headings or content; LOC-and-summon scoping numbers; `Gap to mpv: X percent` quantification.

OK to use: "mpv supports X via [option/file:line]; Tankoban currently Y. Closing this would require Z." (factual). "VLC offers X. Tankoban offers Y. Different shape, both valid." (acknowledge our valid choices). "Today's HUD redesign closed [prior-audit-finding-N]; no longer applies." (in-flight acknowledgment).

### Reference codebases consulted

- mpv source at `C:\Users\Suprabha\Downloads\Video player reference\mpv-master\`.
- VLC source at `C:\Users\Suprabha\Downloads\Video player reference\vlc-master\`.
- libplacebo at `C:\tools\libplacebo-source\`.
- Local mpv standalone empirical checks (re-used from prior audit's [5]):
  - `C:\tools\mpv\mpv.com --ao=help` → `wasapi`, `openal`, `pcm`, `null`.
  - `C:\tools\mpv\mpv.com --vo=help` → `gpu-next`, `gpu`, `direct3d`, `vulkan`, `dmabuf-wayland`, etc.
  - `C:\tools\mpv\mpv.com --hwdec=help` → `d3d11va`, `d3d11va-copy`, `dxva2`, `nvdec`, `vulkan`, `vaapi`, `vulkan-copy`.
  - `C:\tools\mpv\mpv.com --scale=help` → `lanczos`, `spline36`, `ewa_lanczos`, `ewa_lanczossharp`, `ewa_lanczos4sharpest`, ravu/FSRCNNX shader hooks.
  - `C:\tools\mpv\mpv.com --af=help` → `lavfi`, `rubberband`, `equalizer`, `loudnorm` (via lavfi), DRC, etc.

### What this audit is not

- Not a stream-engine audit. The 2026-04-24 stream-server pivot (`agents/_archive/audits/...` plus `agents/audits/STREAM_SERVER_PIVOT_TODO.md`) replaced the legacy libtorrent stream engine with Stremio's stream-server subprocess. Player-side gaps that intersect stream-mode UX (e.g. buffered-region surfacing) are noted in the relevant V/A bucket; engine-internal gaps are out of scope.
- Not a smoke pass. No Tankoban or mpv runtime smoke was performed for this re-audit; the `compare-mpv-tanko.ps1` harness can be invoked separately when there's a regression hypothesis to test.

---

## 1. Video buckets V1-V7

### V1 — Decoder coverage + hardware acceleration

**mpv**: D3D11VA, DXVA2, NVDEC, QSV, VAAPI, Vulkan-decode, plus copy variants. Backend chosen via `--hwdec=<name>` or `--hwdec=auto-safe`; falls through to software decode on failure. Codec breadth inherits from FFmpeg (HEVC, AV1, VP9 Profile 2, 10-bit, 12-bit). [mpv manual; empirical `--hwdec=help`]

**VLC**: D3D11VA + DXVA2 on Windows; OS-specific VA-API/VideoToolbox elsewhere. Backend chosen via Preferences → Input/Codecs → Hardware-accelerated decoding. NEWS shows ongoing AV1 / VP9 Profile 2 / 12-bit AV1 hardware-decode work over recent releases. [`vlc-master/NEWS:447-450`, `:521-523`, `:1049-1075`]

**Tankoban**: D3D11VA only on Windows. `native_sidecar/src/video_decoder.cpp:523-593` initializes the FFmpeg HW context with `AV_HWDEVICE_TYPE_D3D11VA`; no NVDEC, QSV, AMF, or VAAPI fallback. Software decode is the only fallback path.

**Gap**: When D3D11VA fails — older driver, specific codec/level mismatch, GPU-context loss after sleep, multi-adapter fork — Tankoban has only software decode to fall back to, while mpv and VLC try a second hardware family before software. The fallback is silent in current logs (no `hwdec_failed:reason=…` event published from sidecar).

**Tag**: `[scope-heavy]` for adding a second hardware family. `[easy]` for surfacing the failure reason whenever the existing software fallback fires.

---

### V2 — Color pipeline + HDR tone-mapping

**mpv**: `--vo=gpu-next` runs the full libplacebo render path for SDR and HDR content. Tone-mapping function selectable via `--tone-mapping=<bt2390|hable|reinhard|mobius|spline|...>`; ICC profile loadable via `--icc-profile=...` or `--icc-profile-auto`; subtitle blending mode controllable via `--blend-subtitles=<no|yes|video>`. [mpv manual `--vo=gpu-next` and `--tone-mapping`]

**VLC**: D3D11 video output supports HDR tone mapping through libplacebo on Windows; VLC has tracked PQ→SDR mapping, scRGB output, and HDR curve switch issues over recent releases. [`vlc-master/NEWS` HDR entries; VLC issues 26631, 27694]

**Tankoban**: real libplacebo path with `pl_renderer_create`, `ewa_lanczossharp` upscaler, hermite downscaler, peak-detect wired (`native_sidecar/src/gpu_renderer.cpp:107-115`); tone-mapping function selector covers `reinhard` / `bt2390` / `clip` / `mobius` / `linear` / `hable` (`native_sidecar/src/gpu_renderer.cpp:217-237`); HDR metadata input wired (`gpu_renderer.cpp:239-269`); ICC profile auto-load on Windows via `GetICMProfileA` (`gpu_renderer.cpp:272-321`). Window path detects HDR-capable outputs via DXGI and uses an scRGB 16-bit-float swap chain on capable displays (`src/ui/player/FrameCanvas.cpp:150-280`).

**Gaps**:
1. The libplacebo renderer is **only constructed when `probe->hdr` is true** (`native_sidecar/src/main.cpp:792-807`). SDR content takes the non-libplacebo path. The same source code that ships ewa_lanczossharp + tone-mapping for HDR is unused for the 95%+ of files that are SDR. Carries forward the prior audit's P1.1 ([19], "libplacebo SDR + HDR same path"). Today's HUD redesign did not change this. **Tag**: `[strategic call]`.
2. Today's `SettingsPopover` exposes only audio delay + subtitle delay; the HUD redesign removed the prior tone-mapping / peak-detect controls from the user-visible surface (`src/ui/player/SettingsPopover.h:24-67`). The setter still exists at `FrameCanvas.cpp` (search for `set_tone_mapping`) and at `SidecarProcess.cpp` (the sidecar command surface still accepts the command), but nothing in the new HUD invokes it. Carries forward prior audit's P0.1 ([19], "re-surface tone-mapping / scaler presets"). Today's redesign **widens** this gap rather than closes it. **Tag**: `[easy]` for re-surfacing presets in the new HUD; `[moderate]` for designing the picker shape consistent with the new minimalist chip vocabulary.

**Closed by today's ship**: none in V2.

---

### V3 — Scaling quality

**mpv**: `--scale=<lanczos|spline36|ewa_lanczos|ewa_lanczossharp|ewa_lanczos4sharpest|...>` plus `--cscale`, `--dscale` for chroma and downscale. User-shader ecosystem (RAVU, FSRCNNX, Anime4K) hooked via `--glsl-shader=<path>`. [mpv manual; empirical `--scale=help`]

**VLC**: D3D11 output uses platform-default scalers; recent libplacebo integration brings serious upscaling on Windows but VLC does not expose mpv-style scaler choice in its preferences UI.

**Tankoban**: defaults to `pl_filter_ewa_lanczossharp` upscaler when the libplacebo path runs (`native_sidecar/src/gpu_renderer.cpp:109`). No user-facing scaler picker. No user-shader path.

**Gaps**:
1. Same root as V2 — the good scaler default is gated to HDR content because the libplacebo renderer is HDR-gated. SDR content gets whatever the non-libplacebo path applies. **Tag**: `[strategic call]` (resolves with V2 §1 strategic call).
2. No user-facing scaler choice. mpv exposes ~10 scalers; Tankoban exposes 0. The prior audit's note that Tankoban could ship "3 curated presets" rather than a full mpv-style picker is a brotherhood roadmap call, not an audit finding. **Tag**: `[strategic call]`.
3. No user-shader hook. mpv's shader ecosystem is years of community iteration; matching it head-on is a large strategic commitment rather than a code task. Tankoban could choose to never have one. **Tag**: `[strategic call]`.

---

### V4 — Frame timing + presentation

**mpv**: `--video-sync=display-resample` resamples audio to match the display refresh; `--interpolation=yes` plus `--tscale=<filter>` interpolates frames; `--vsync-policy`, `--display-fps-override`, `--frame-drop-mode`. mpv's frame-pacing surface is one of its longest-iterated areas. VRR + display-resample interaction has known edge cases tracked in mpv issues. [mpv manual; mpv issue 12005 for VRR]

**VLC**: D3D11 presenter improved with HDR fixes; less granular frame-pacing surface than mpv.

**Tankoban**: DXGI frame-latency waitable swap chain (`src/ui/player/FrameCanvas.cpp:150-280`); HiDPI-aware physical-pixel swap-chain sizing; HDR-aware swap-chain configuration on capable displays; D3D11 presenter retained even on software-decode fallback (`native_sidecar/src/video_decoder.cpp:219-287`). VsyncTimingLogger present (`src/ui/player/VsyncTimingLogger.cpp`).

**Gaps**:
1. No interpolation / display-resample mode. mpv's `--video-sync=display-resample` is a meaningful smoothness feature for non-multiple-of-source-fps displays (e.g. 24fps content on a 60Hz panel without 24p mode). **Tag**: `[scope-heavy]` — touches both audio resampler timing and the present loop.
2. No user-facing frame-pacing diagnostic surface. The `VsyncTimingLogger` writes a log, the IPC latency tracker writes a log, but the user sees nothing at runtime (no late-frame counter, no present-interval estimate, no queue-depth indicator). The prior audit framed this as "observability is a real product advantage" (Theme 3 [19]); that framing is still accurate post-redesign because the new HUD also doesn't expose this. **Tag**: `[easy]` for a stats badge; `[moderate]` for a richer panel.
3. No VRR-aware policy on the waitable-swapchain path. The current path is timing-conservative, which is fine; behavior on a VRR-capable monitor with a frame-rate-mismatched source is uncharacterized. **Tag**: `[scope-heavy]` (driver and monitor variability dominates).

---

### V5 — Subtitle rendering

**mpv**: libass + DirectWrite/CoreText/FontConfig; PGS / DVB / teletext bitmap families via FFmpeg lavc decoders; `--blend-subtitles=<no|yes|video>` controls subtitle/HDR blend ordering; `--sub-pos=<0..150>` for vertical position; `--sub-margin-y` / `--sub-margin-x`; `--sub-ass-override` for style overrides. Long-iterated edge-case surface around storage size, pixel aspect, line-height defaults. [mpv manual]

**VLC**: libass + freetype on most platforms; PGS + DVB + teletext via dedicated demux + decode modules; subtitle position + margin controls in Subtitle/OSD preferences; long history of subtitle-related fixes. [`vlc-master/NEWS` subtitle entries]

**Tankoban**: libass + DirectWrite on Windows; dedicated render thread; PGS bitmap + libass text both supported (`native_sidecar/src/subtitle_renderer.cpp:138-167`, `:219-257`); `ass_set_storage_size` correctly invoked with comments citing mpv and VLC behavior (`subtitle_renderer.cpp:304-320`); subtitle geometry rescaled against the actual letterboxed video rectangle rather than the whole canvas (`src/ui/player/FrameCanvas.cpp:1180-1209`); cinemascope subtitle architectural fix shipped 2026-04-16 at `ade3241`; subtitle vertical-position slider shipped 2026-04-24 at `3d4d6aa`.

**Gaps**:
1. **Today's HUD redesign removed the user surface for vertical-position adjustment.** The `3d4d6aa` slider lived in `TrackPopover`, which was deleted in today's `b8cdbf2`. The sidecar's `sendSetSubPos` command (`src/ui/player/SidecarProcess.h` and `.cpp`) is still wired but no element of `SubtitlePopover` / `AudioPopover` / `SettingsPopover` invokes it post-redesign (verified with grep across `src/ui/player/` — only `VideoPlayer.cpp` + `SidecarProcess.{h,cpp}` reference `setSubtitlePos`/`sub_pos`). This is a fresh dimension specific to today's churn. **Tag**: `[easy]` to re-surface as a Settings row or as a small inline control on `SubtitlePopover`.
2. No DVB / teletext subtitle coverage. The current sidecar handles libass + PGS only (`subtitle_renderer.cpp` paths). DVB/teletext are uncommon outside live-broadcast capture, so this is a category-relevance question, not a quality-tier question. **Tag**: `[scope-heavy]`.
3. No `--blend-subtitles=video` analog. Subtitle blending happens after color management on Tankoban's path; for HDR content with bright subtitles over dim scenes this can cause subtitle-clipping or halo artifacts mpv's `--blend-subtitles=video` mode addresses. Verifying whether Tankoban exhibits the artifact requires a smoke; the gap is that no control is offered either way. **Tag**: `[moderate]`.

**Already strong**: storage-size handling, video-rect-aware geometry, libass + DirectWrite combination — these are documented in the prior audit's §7 [19] and remain accurate.

---

### V6 — Container + format compatibility

**mpv**, **VLC**: both inherit broad coverage from FFmpeg + lavf, with years of accumulated edge-case patches across HLS, DASH, MP4 fragments, multi-program TS, EBML weirdness, and broken-but-recoverable streams. [mpv changelog; `vlc-master/NEWS` adaptive entries]

**Tankoban**: substantial probe-escalation, reconnect, and broken-duration defensive logic (`native_sidecar/src/demuxer.cpp:176-320`); explicit MKV duration-contamination handling for files where subtitle/attachment streams pollute the format-level duration; HTTP range-based recovery attempts (`native_sidecar/src/video_decoder.cpp:219-287`).

**Gap**: General-purpose players accumulate broken-file exposure across decades; Tankoban's defensive logic is real and competent for the cases it has hit, not a substitute for that breadth. The actionable gap mpv and VLC also illustrate is the **failure-classification surface** — when a file fails to play, what does the user see?
- mpv prints structured demuxer / decoder messages to console.
- VLC surfaces an error dialog with a coarse reason category.
- Tankoban currently has structured failure events at the sidecar layer (`AUDIO_DECODE_INIT_FAILED:<reason>`, `AUDIO_DEVICE_STARTUP_FAILED:<reason>`, etc.) but the player UI converts most of these into generic "playback failed" toasts.

**Tag**: `[easy]` for surfacing the existing structured reason strings into the HUD/toast path; `[scope-heavy]` to chase universal container compatibility.

---

### V7 — Streaming + network resilience

Out of scope per Hemanth's audit prompt §0 — the 2026-04-24 STREAM_SERVER_PIVOT moved stream mode onto Stremio's reference stream-server subprocess (per `agents/audits/STREAM_SERVER_PIVOT_TODO.md` and the stream-server-pivot project memory). Stream-engine internals are not a player-domain audit concern. Player-side gaps that intersect stream UX (buffered-region visualization, stall-reason classification) appear under V4 (frame timing diagnostics) and the prior audit's P0.3 [19] which the brotherhood may carry forward independent of this re-audit.

---

## 2. Audio buckets A1-A7

### A1 — Output backend + latency

**mpv**: `wasapi` available on Windows with shared/exclusive mode (`--audio-exclusive=yes`); selectable via `--ao=wasapi`. [mpv manual; empirical `--ao=help`]

**VLC**: WASAPI module supports both shared and exclusive mode via the audio-output preferences. [`vlc-master/modules/audio_output/wasapi.c:623-695`, `:809-968`]

**Tankoban**: PortAudio on the audio path, with today's `71988a3` Path A swap explicitly preferring WASAPI shared via `paWASAPI` host-API lookup (`native_sidecar/src/audio_decoder.cpp:446-477`); Pa_Initialize prewarm path also WASAPI shared per the matching code in `native_sidecar/src/main.cpp` (the prewarm block in the same wake). Per-device offset recall + Bluetooth-specific default compensation on the Qt side (`src/ui/player/VideoPlayer.cpp` audio-delay region — line numbers shifted with today's HUD redesign churn; the per-device persistence path is intact).

**Gaps**:
1. No WASAPI-exclusive path. mpv and VLC both offer it. PortAudio supports `paWasapiExclusive` via `hostApiSpecificStreamInfo`, currently set to `nullptr` (`audio_decoder.cpp:477`). This carries forward the prior audit's "second serious Windows audio output mode" finding ([19] P2.1) — keeping the existing fast-start shared path AND adding an exclusive path is two backends, not a swap. Whether this is worth chasing is a Hemanth + brotherhood call. **Tag**: `[strategic call]`.
2. AudioPopover does not surface backend / device / latency diagnostics. Today's `AudioPopover.cpp:73-106` is track-list-only — no row for "Output: WASAPI shared @ 48000 Hz, latency 3 ms, device: <name>". The information all exists (sidecar logs `actual_latency`, `clock_->set_output_latency`, host-API selection) but never reaches the user. This carries forward the prior audit's P0.2 [19] and remains as accurate post-redesign as it was pre-redesign. **Tag**: `[easy]`.

**Closed by today's ship**: the prior audit's "Tankoban prefers WASAPI shared via PortAudio" was correct in intent; today's `71988a3` made the explicit host-API selection match the intent, with measured 30× device-latency improvement. The *backend-not-actually-WASAPI-by-default* subgap is closed.

---

### A2 — Bit-perfect output

**mpv**: with `--audio-exclusive=yes` and `--audio-resample-disable` (or matching device sample rate), bypasses the OS mixer and outputs sample-for-sample. [mpv manual]

**VLC**: WASAPI exclusive mode bypasses the OS mixer; sample-rate matching depends on settings.

**Tankoban**: shared-mode WASAPI; the prewarmed PortAudio stream is fixed at 48 kHz stereo float, with `swresample` converting any source rate (44.1 / 96 / 192) into that path (`native_sidecar/src/audio_decoder.cpp:370-395`).

**Gap**: Tankoban is not bit-perfect on the prewarmed path. The prewarm is the architectural choice that makes Bluetooth cold-start fast (the prior audit's Theme 2 [19] articulates this trade — startup smoothness vs bit-perfect fidelity); they cannot both be the default with a single output stream. mpv and VLC default to "open the device fresh per file" and pay the cold-start cost in exchange for bit-perfect-when-rate-matches. **Tag**: `[strategic call]` — picks the default user wants, plus possibly a second mode for the other.

---

### A3 — Resampling quality

**mpv**: `--audio-resample-filter-size`, `--audio-resample-cutoff`, `--audio-resample-phase-shift`, `--audio-resample-linear`, plus a SoXR backend selectable via `--af=lavfi=[aresample=resampler=soxr]`. [mpv manual; empirical `--af=help`]

**VLC**: SoXR resampler module available; quality preset selectable in Audio preferences.

**Tankoban**: `swresample` (FFmpeg's default) with library-default settings; converts to float32 interleaved stereo at the prewarmed rate (`audio_decoder.cpp:386-407`). No SoXR. No user-facing quality preset.

**Gap**: For source rates that don't divide evenly into the prewarm rate (44.1 → 48 kHz is the common case for most music files and many anime BGM tracks), `swresample`'s default filter size produces audible aliasing artifacts at high frequencies on revealing content. mpv and VLC let the user choose a higher-quality resampler. **Tag**: `[moderate]` for a quality-preset switch; `[scope-heavy]` if a SoXR dependency gets introduced.

---

### A4 — Channel mapping

**mpv**: preserves source channel layout to output by default; `--audio-channels=auto` does the right thing on multichannel devices; HRTF / surround-virtualization filters via `--af=lavfi`. [mpv manual]

**VLC**: preserves channel layout; supports passthrough (HDMI bitstream) for AC3 / DTS / E-AC3 / Atmos source content via the WASAPI passthrough path. [`vlc-master/modules/audio_output/wasapi.c` passthrough region]

**Tankoban**: hard-stereo. `audio_decoder.cpp:373-374` comment is explicit: `// Always output stereo — resampler handles mono→stereo upmix`; `out_channels = 2`; `AV_CHANNEL_LAYOUT_STEREO` is the resampler target (`audio_decoder.cpp:387`).

**Gap**: 5.1 / 7.1 source content is downmixed to stereo unconditionally. Atmos / TrueHD / DTS-MA passthrough is not available. Whether this matters in Tankoban's category (anime / IPL / general video on a Windows laptop or desktop) depends on user setup — most users on stereo speakers or stereo headphones will never notice; users with a 5.1 / 7.1 receiver or HDMI-passthrough soundbar will. **Tag**: `[scope-heavy]` for native-channel preservation; `[scope-heavy]` plus device-QA matrix for passthrough.

---

### A5 — A/V sync precision

**mpv**: audio-clocked by default; `--video-sync=<audio|display-resample|...>`; explicit `--audio-pts-correction-threshold`; per-file timing offset via `--audio-delay`. [mpv manual]

**VLC**: audio-clocked by default; per-file delay via `Tools → Track Synchronization`.

**Tankoban**: audio-clocked (sidecar `g_clock` anchored from audio decoder); per-device audio-delay offset persisted across sessions via QSettings (the audio-delay region in `src/ui/player/VideoPlayer.cpp` — line numbers shifted with today's HUD churn but the persistence is intact); Bluetooth-specific default offset; Ctrl+= / Ctrl+- / Ctrl+0 keybinds plus today's `SettingsPopover` ±50 ms button pair. The IPC round-trip latency tracker (CLAUDE.md "Build Quick Reference") complements the audio-clock instrumentation.

**Gap**: A/V sync algorithmic sophistication is on par with VLC and behind mpv's `--video-sync=display-resample` mode; per-device persistence is a Tankoban-only convenience that mpv and VLC don't ship. The narrow gap surfacable today is "automatic offset suggestion based on observed startup latency delta" — Tankoban already collects the data (`actual_latency`, `set_output_latency`) but does not propose an offset to the user. **Tag**: `[moderate]`.

---

### A6 — Audio effects + filters

**mpv**: `--af=lavfi=[<chain>]` exposes the full FFmpeg lavfi audio-filter graph: `loudnorm`, `dynaudnorm`, `acompressor`, `equalizer`, `firequalizer`, `rubberband`, `volume`, `aecho`, `lowpass`, `highpass`, plus 200+ more. [mpv manual; empirical `--af=help`]

**VLC**: `Audio → Effects` panel exposes equalizer, spatializer, compressor, headphone surround, bass redirection — narrower set than mpv's lavfi but more discoverable than mpv's CLI surface.

**Tankoban**: the sidecar's filter command surface accepts `set_filters <spec>` (`src/ui/player/SidecarProcess.cpp` filter-command region) which constructs FFmpeg lavfi audio + video filter graphs (`native_sidecar/src/main.cpp` filter-graph region; pre-redesign cite at `audio_decoder.h:67-75` for the DRC intent); the engine has dormant capability for `loudnorm`, EQ, compressor through this surface.

Today's HUD redesign removed the equalizer and filter popovers (`EqualizerPopover.{h,cpp}` deleted in `b8cdbf2`; `FilterPopover.{h,cpp}` deleted in same commit) and the keybindings for normalize/deinterlace (`src/ui/player/KeyBindings.cpp:24-55` was tightened in today's `4ca8ecf`). The new `SettingsPopover` exposes audio delay + subtitle delay only.

**Gap**: The engine has the surface; the UI no longer exposes any of it. Whether to re-expose, and if yes in what curated shape (the prior audit's "Normalize / Dialogue boost / Night mode" suggestion is one possibility; nothing forces that specific shape), is a brotherhood + Hemanth call — the audit notes the gap exists rather than picking the answer. Carries forward prior audit's P1.3 [19]. **Tag**: `[easy]` for re-exposing existing engine capability via curated presets; `[strategic call]` for what the HUD surface should look like.

---

### A7 — Format coverage + lossless handling

**mpv**, **VLC**: FLAC, ALAC, Opus, WavPack, AIFF, embedded-cuesheet handling, gapless playback for sequenced tracks; mpv supports DSD via FFmpeg.

**Tankoban**: FFmpeg-backed decode covers FLAC / ALAC / Opus / etc. for the formats users hit in normal video files (audiobook content, embedded soundtracks, music in ambient sources). Gapless playback for adjacent tracks in a playlist is uncharacterized; cuesheet support is uncharacterized.

**Gap**: Tankoban's category is "library + stream + read app." Pure-music-format edge cases (DSD, MQA, cuesheet-driven playback) sit outside that category — a Tankoban that pursued those would be a different product. The category-relevant subset of A7 is "audiobook gapless + chapter-boundary playback feel," which the brotherhood already has on its radar via the audiobook paired-reading work (`AUDIOBOOK_PAIRED_READING_FIX_TODO.md`, per memory `project_audiobook_paired_reading.md`). No fresh dimension for this audit. **Tag**: out-of-category.

---

## 3. What Tankoban already does well

This section extends the prior audit's §7 [19] with today's ships. Inclusion here is factual — no claim about "outdoing" mpv or VLC, just an honest record of what's solid in the current build so the brotherhood doesn't accidentally undo it during gap-closing work.

- Real Windows rendering stack: D3D11 + DXGI waitable swap chain (`src/ui/player/FrameCanvas.cpp:150-280`).
- HDR-capable output detection, scRGB 16-bit-float swap chain on capable displays (same).
- Genuine libplacebo path with serious tone-mapping choices and ICC auto-detection (`native_sidecar/src/gpu_renderer.cpp:90-321`). Currently HDR-only; see V2 §1 strategic call.
- ewa_lanczossharp upscaler default on the libplacebo path (`gpu_renderer.cpp:109`).
- libass + DirectWrite + dedicated render thread + correct `ass_set_storage_size` + PGS handling + video-rect-aware overlay positioning (`native_sidecar/src/subtitle_renderer.cpp:138-320`, `src/ui/player/FrameCanvas.cpp:1180-1209`).
- Three-tier HTTP probe escalation, reconnect policy, 30 s `rw_timeout`, 64 MiB prefetch ring, dedicated prefetch thread (`native_sidecar/src/video_decoder.cpp:219-287`, `native_sidecar/src/demuxer.cpp:176-320`). Carries through stream-mode use even after the stream-server pivot, since the player still consumes from a localhost HTTP source.
- Per-device audio-delay offset recall + Bluetooth default compensation (`src/ui/player/VideoPlayer.cpp` audio-delay region) — neither mpv nor VLC ship this convenience.
- Per-show preference persistence: aspect override, crop override, audio language, subtitle language, exact track IDs, subtitle visibility (`src/ui/player/VideoPlayer.cpp` per-show pref region) — a stronger media-library mental model than mpv or VLC ship natively.
- Cinemascope subtitle architectural fix (shipped 2026-04-16 at `ade3241`, archived audit at `agents/_archive/audits/player/cinemascope_aspect_2026-04-16.md`).
- Subtitle vertical-position slider mechanism (shipped 2026-04-24 at `3d4d6aa` — note: UI surface for it removed in today's HUD redesign per V5 §1; the `sendSetSubPos` engine command is intact).
- **Today's WASAPI Path A swap** (`71988a3`) — moved PortAudio off the MME default onto WASAPI shared with measured 30× device-latency improvement.
- **Today's HUD redesign** (`b8cdbf2`, `3f193e4`, `4ca8ecf`, `f15bde3`) — bottom HUD is icon-only chips; SubtitlePopover unifies embedded + addon + local-file sources; `dismissed()` signal keeps chip `:checked` state in lockstep with popover visibility; CenterFlash is a bare icon now (no black blob).
- **Today's auto-hide guard** — popovers prevent HUD auto-hide while open (`src/ui/player/VideoPlayer.cpp` `isAnyPopoverOpen()` helper inside `hideControls`).
- **2026-04-24 `compare-mpv-tanko.ps1` regression harness** — log-parser comparing mpv standalone vs Tankoban sidecar dropped-frame and stall-cycle counts (`scripts/compare-mpv-tanko.ps1`).
- **2026-04-24 IPC round-trip latency tracker** — runtime instrumentation in `SidecarProcess` per-command p50/p99/max, dumped to `out/ipc_latency.log` per session.
- **2026-04-24 cursor auto-hide on canvas** (`b59888d`) and **popover wheelEvent acceptance** (`7af5aef`).

---

## 4. Open questions for Hemanth

These are clarifying questions, not roadmap pronouncements. They surface where a finding's resolution depends on a strategic call the audit isn't entitled to make.

1. **V2 / V3 strategic call**: should the libplacebo render path run for SDR content as well as HDR (resolves the V2 §1 + V3 §1 root cause in one move), or is the HDR-only gating the right architectural shape for now?
2. **V2 §2 + A6 strategic call**: the new minimalist HUD intentionally surfaces less than the engine offers. Should re-exposed controls (tone-map preset, scaler preset, normalize, EQ) re-appear in the new HUD vocabulary, or remain engine-only / context-menu-only?
3. **V5 §1 question**: the subtitle vertical-position slider that shipped 2026-04-24 lost its UI surface in today's HUD redesign — re-surface (Settings row? small chip on SubtitlePopover? context-menu item?), or leave engine-only?
4. **A1 §1 + A2 strategic call**: dual audio-output story (fast-start shared default + reference exclusive opt-in), or stay single-mode shared as today?
5. **A4 strategic call**: preserve native channel count to output (5.1 / 7.1), or stay forced-stereo? Different answer for Hemanth's daily setup (likely stereo) vs a future user who plugs in a receiver.
6. **V1 §1 question**: when D3D11VA fails and software fallback fires, surface the reason in the HUD or stay log-only?
7. **V6 question**: surface the existing sidecar structured failure-reason strings (`AUDIO_DECODE_INIT_FAILED:<reason>`, etc.) in user-visible toasts, or keep the user-visible toast generic?
8. **Theme question carry-forward** ([19] Theme 3): observability is a real product surface (IPC latency, mpv-vs-Tankoban harness, VsyncTimingLogger, sidecar PERF logs, structured failure events all already exist) — invest in a user-visible diagnostics panel at some point, or keep observability developer-facing?

---

## 5. References

### External

1. mpv manual (stable): https://mpv.io/manual/stable/
2. mpv VRR / display-resample issue: https://github.com/mpv-player/mpv/issues/12005
3. VLC NEWS file (release notes): `C:\Users\Suprabha\Downloads\Video player reference\vlc-master\NEWS`
4. VLC WASAPI module: `C:\Users\Suprabha\Downloads\Video player reference\vlc-master\modules\audio_output\wasapi.c:623-695,809-968`
5. VLC HDR issue 26631: https://code.videolan.org/videolan/vlc/-/issues/26631
6. VLC HDR issue 27694: https://code.videolan.org/videolan/vlc/-/issues/27694
7. libplacebo README: https://github.com/haasn/libplacebo
8. Empirical mpv local checks 2026-04-25 on `C:\tools\mpv\mpv.com`: `--version`, `--ao=help`, `--vo=help`, `--hwdec=help`, `--scale=help`, `--af=help`.

### Tankoban current state (HEAD `0386dfb`)

9. Audio output backend + Path A WASAPI: `native_sidecar/src/audio_decoder.cpp:370-395,446-477`
10. Sidecar open-flow + libplacebo HDR-only init: `native_sidecar/src/main.cpp:780-870`
11. GPU renderer libplacebo init + scaler defaults: `native_sidecar/src/gpu_renderer.cpp:85-115`
12. GPU renderer tone-mapping + HDR metadata + ICC load: `native_sidecar/src/gpu_renderer.cpp:217-321`
13. Video decoder D3D11VA hwaccel + HTTP prefetch: `native_sidecar/src/video_decoder.cpp:219-287,523-593`
14. Demuxer probe + retry + duration recovery: `native_sidecar/src/demuxer.cpp:176-320`
15. Subtitle renderer libass + DirectWrite + PGS + storage size: `native_sidecar/src/subtitle_renderer.cpp:138-167,219-257,304-320`
16. FrameCanvas HDR detection + waitable swap chain + subtitle geometry: `src/ui/player/FrameCanvas.cpp:150-280,1180-1209`
17. New popovers post HUD redesign:
    - `src/ui/player/AudioPopover.h:1-49` + `AudioPopover.cpp:73-106`
    - `src/ui/player/SubtitlePopover.h:1-156`
    - `src/ui/player/SettingsPopover.h:1-67`
18. Sidecar IPC + filter command surface + sub-pos: `src/ui/player/SidecarProcess.h`, `src/ui/player/SidecarProcess.cpp` (file-level — line numbers shifted in today's churn)

### Prior internal audit (salvageable signal cited where applicable)

19. `agents/audits/player_surpass_mpv_vlc_2026-04-25.md` — prior v1 audit; the salvageable findings extended in this v2 are P0.1 (re-surface tone-mapping / scaler presets), P0.2 (audio diagnostics in AudioPopover), P0.3 (buffered-range + stall-reason), P0.4 (per-show preference inheritance — already shipping, mentioned only in §3), P1.1 (libplacebo SDR + HDR same path), P1.3 (audio effects re-surface), Theme 3 (observability is a real product surface), and §7 (what already does well).

### Archived prior audits referenced in v1 [19] (not re-derived in v2)

20. `agents/_archive/audits/player/comparative_player_2026-04-20_p3_hdr_filters.md`
21. `agents/_archive/audits/player/comparative_player_2026-04-20_p2_subtitles.md`
22. `agents/_archive/audits/player/video_quality_dip_2026-04-24.md`

### HEAD anchor

23. `git rev-parse --short HEAD` → `0386dfb` (2026-04-25, post chat.md sweep `0386dfb`).

---

## 6. TODO checklist summary

Same findings as §1 + §2, condensed to scannable checkbox form. No ranking, no "ship X first" — each item retains its scope tag from the prose body. `[x]` = closed by today's ship; `[ ]` = open. Bucket §-numbers cross-reference back to the prose body for full mpv-does / VLC-does / Tankoban-does context.

### Video

- [ ] **V1 §1** When D3D11VA fails, software fallback fires silently — surface fail-reason in HUD/toast. Cite: `native_sidecar/src/video_decoder.cpp:523-593`. [easy]
- [ ] **V1 §2** No NVDEC / QSV / AMF / VAAPI second-hardware-family fallback. mpv + VLC both fall through one hw family before software. [scope-heavy]
- [ ] **V2 §1** Libplacebo render path is gated on `probe->hdr` only — SDR content takes a different code path. Closing this collapses V2/V3/V4 root cause. Cite: `native_sidecar/src/main.cpp:792-807`. [strategic call] (carries forward prior P1.1 [19])
- [ ] **V2 §2** Tone-map / peak-detect UI removed from new minimalist HUD; engine setter at `native_sidecar/src/gpu_renderer.cpp:217-237` still wired but unreachable from `SettingsPopover` (audio + sub delay only). Re-surface in new chip vocabulary. [easy] (carries forward prior P0.1 [19])
- [ ] **V3 §1** Same root as V2 §1 — `ewa_lanczossharp` upscaler default at `gpu_renderer.cpp:109` runs only on HDR path. [strategic call]
- [ ] **V3 §2** No user-facing scaler choice (mpv exposes ~10 via `--scale=...`). Curated preset row possible without copying mpv's full surface. [strategic call]
- [ ] **V3 §3** No user-shader hook analog (mpv `--glsl-shader=`). [strategic call]
- [ ] **V4 §1** No interpolation / display-resample analog (mpv `--video-sync=display-resample` + `--interpolation=yes`). [scope-heavy]
- [ ] **V4 §2** `VsyncTimingLogger` + IPC round-trip tracker write logs only — no user-visible runtime diagnostic surface (late-frame counter, present-interval estimate, queue depth, HDR output state, audio-device latency). [easy] for stats badge / [moderate] for full panel. (carries forward prior Theme 3 [19])
- [ ] **V4 §3** No VRR-aware policy on waitable-swapchain path; behavior on VRR display with mismatched-fps source uncharacterized. [scope-heavy]
- [ ] **V5 §1** Subtitle vertical-position slider UI surface was deleted with `TrackPopover` in today's `b8cdbf2`; sidecar `sendSetSubPos` still wired but unreachable. Re-surface as `SettingsPopover` row, `SubtitlePopover` inline control, or context-menu item. [easy]
- [ ] **V5 §2** No DVB / teletext subtitle decode (mpv + VLC both have it). Category-relevance question. [scope-heavy]
- [ ] **V5 §3** No `--blend-subtitles=video` analog for HDR subtitle blending order. [moderate]
- [ ] **V6** Sidecar emits structured `AUDIO_DECODE_INIT_FAILED:<reason>` / `AUDIO_DEVICE_STARTUP_FAILED:<reason>` events; UI collapses these into generic "playback failed" toasts. Surface the reason strings. [easy]
- ~~**V7** Streaming + network resilience~~ — out of scope per 2026-04-24 STREAM_SERVER_PIVOT.

### Audio

- [x] **A1 §0** ~~PortAudio default was MME on Windows despite "WASAPI shared" intent~~ — **closed by today's `71988a3`** (Path A host-API swap, measured 30× device-latency improvement, `native_sidecar/src/audio_decoder.cpp:446-477`).
- [ ] **A1 §1** No WASAPI-exclusive path. PortAudio's `paWasapiExclusive` capability sits unused at `audio_decoder.cpp:477` (`hostApiSpecificStreamInfo = nullptr`). mpv + VLC both expose exclusive. [strategic call]
- [ ] **A1 §2** `AudioPopover` is track-list-only (`src/ui/player/AudioPopover.cpp:73-106`); no row for backend / device / measured latency / sample-rate. Data exists at `clock_->set_output_latency` + sidecar host-API logs. [easy] (carries forward prior P0.2 [19])
- [ ] **A2** Prewarmed PortAudio stream is fixed 48 kHz stereo float (`audio_decoder.cpp:380`) — not bit-perfect. Architecturally trades startup smoothness vs fidelity. [strategic call]
- [ ] **A3** `swresample` library defaults; no SoXR; no quality preset switch. Audible aliasing risk on 44.1 ↔ 48 kHz revealing content. [moderate] for preset / [scope-heavy] if SoXR dependency.
- [ ] **A4** Hard-stereo output (`audio_decoder.cpp:373-374` `// Always output stereo`); no native channel-count preservation; no Atmos / TrueHD / DTS-MA passthrough. [scope-heavy]
- [ ] **A5** Per-device offset persistence is solid (Tankoban-only convenience neither mpv nor VLC ship). Gap surfacable: automatic offset suggestion from observed `actual_latency` deltas. [moderate]
- [ ] **A6** Engine `set_filters` lavfi surface lives at `src/ui/player/SidecarProcess.cpp` filter-command region; `audio_decoder.h:67-75` documents DRC intent; today's HUD deleted `EqualizerPopover.{h,cpp}` + `FilterPopover.{h,cpp}` and tightened `KeyBindings.cpp` removing normalize/deinterlace binds. Re-expose curated subset. [easy] mechanical / [strategic call] for HUD shape. (carries forward prior P1.3 [19])
- ~~**A7** Format coverage + lossless~~ — out-of-category (DSD / MQA / cuesheet pull toward a different product).

### Don't break (load-bearing strengths from §3)

Gap-closing work should not accidentally undo any of these:

- Libplacebo HDR path with ewa_lanczossharp + tone-map + ICC auto-load (`gpu_renderer.cpp:90-321`)
- DXGI waitable swap chain + scRGB 16-bit-float on HDR-capable displays (`FrameCanvas.cpp:150-280`)
- libass + DirectWrite + correct `ass_set_storage_size` + video-rect-aware overlay (`subtitle_renderer.cpp:138-320`, `FrameCanvas.cpp:1180-1209`)
- 3-tier HTTP probe escalation + reconnect + 64 MiB prefetch ring (`video_decoder.cpp:219-287`, `demuxer.cpp:176-320`)
- Per-device + Bluetooth-default audio-delay persistence
- Per-show preference persistence (aspect / crop / audio lang / sub lang / track IDs / sub visibility)
- Today's `71988a3` WASAPI swap, `b8cdbf2`/`3f193e4`/`4ca8ecf` HUD chip redesign + auto-hide guard, `f15bde3` context menu collapse, `2a59f61` mpv-vs-Tankoban harness, IPC round-trip latency tracker, `b59888d` cursor auto-hide, `7af5aef` popover wheelEvent acceptance

### Upstream blockers (questions that gate clusters of TODOs)

The §4 questions each gate one or more checklist items. The most upstream:

- **Q1 (libplacebo for SDR or stay HDR-only)** gates V2 §1, V3 §1, V4 (some), and any future "scaler preset" or "tone-map preset" UI work — answering this first lets the brotherhood collapse multiple checklist items into a single architectural move.
- **Q2 (re-expose tone-map / scaler / normalize / EQ in new HUD?)** gates V2 §2, V3 §2, A6.
- **Q4 (dual audio output story?)** gates A1 §1, A2.
- **Q5 (preserve native channels?)** gates A4.
- **Q8 (user-visible diagnostics panel?)** gates V1 §1, V4 §2, V6, A1 §2.

Items not gated by an open question (no upstream block, scope tag is sufficient context): V1 §2, V3 §3, V4 §1, V4 §3, V5 §1, V5 §2, V5 §3, A3, A5.
