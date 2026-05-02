# Tankoban Player Surpass Audit - mpv + VLC Comparative
# 2026-04-25

## 0. Methodology + scope notes

### Bottom-line answer first

Tankoban cannot honestly surpass a best-configured mpv plus a mature VLC on pure playback-engine breadth today.

It can plausibly surpass them in three narrower ways:

1. Windows-specific presentation polish on the exact paths Tankoban chooses to own end-to-end.
2. Stream-aware playback behavior tied to its app context and sidecar diagnostics.
3. Library-aware playback persistence that general-purpose players do not structurally optimize for.

The weak point is audio. On pure audio-backend seriousness, Tankoban is currently behind mpv, behind VLC on Windows backend features, and far behind Roon / Audirvana / foobar2000 style "bit-perfect first" players. The current PortAudio path is shared-mode, stereo, and often fixed to 48 kHz, which disqualifies any honest "surpass" claim in A1-A4 and A7 until the architecture changes materially. [11][12][13][14][15][16][22][23][24]

### Scope retained after taxonomy review

I kept Hemanth's 17 buckets as-is:

- Video: V1-V7
- Audio: A1-A7
- Tankoban-only moat buckets: X1-X3

I considered splitting "HDR subtitle blending" out of V5, but in practice it belongs inside subtitle-rendering quality because the user-visible failure mode is subtitle readability and positioning, not core color science.

### Research passes completed

- Web research: official mpv manual, FFmpeg docs, libplacebo docs, VLC issue tracker/news, Roon/Audirvana/foobar official docs, plus a small corroboration pass on community reports for VRR/display-resample and Windows audio backend tradeoffs. [1][2][3][4][6][9][10][11][12][13][14][15][16][17][18]
- Reference codebase pass:
  - Local mpv source snapshot under `C:\Users\Suprabha\Downloads\Video player reference\mpv-master`
  - Local VLC snapshot under `C:\Users\Suprabha\Downloads\Video player reference\vlc-master`
  - Local QMPlay2 snapshot under `C:\Users\Suprabha\Downloads\Video player reference\QMPlay2-master`
  - Local IINA snapshot under `C:\Users\Suprabha\Downloads\Video player reference\iina-develop`
  - Shallow-cloned libplacebo under `C:\tools\libplacebo-source` at `8222476` [2][3]
- Tankoban current-state pass over `src/ui/player/` and `native_sidecar/src/`, using current HEAD `0386dfb` as authoritative. [22]-[32]
- Empirical spot-checks on local mpv standalone `C:\tools\mpv\mpv.com`:
  - `--version`
  - `--ao=help`
  - `--vo=help`
  - `--hwdec=help`
  - `--scale=help`
  - `--af=help` [5]

### What "surpass" means in this audit

A bucket counts as "surpassed" only if Tankoban can offer one of:

- objectively broader capability than both mpv and VLC, or
- objectively higher quality than both on the same workload, or
- materially better end-user outcome because Tankoban can use app/library/stream context they do not have.

"Different" does not count. "Good enough" does not count. "Could maybe if perfectly tuned" does not count.

### Confidence taxonomy

- Multi-source confirmed: primary doc/source plus corroborating issue, empirical check, or second primary source.
- Primary-only: official source or current code, but no second independent confirmation gathered in this wake.
- Empirical: local command/inspection on Hemanth's machine.
- Uncorroborated: interesting but only thinly supported; treat as a lead, not a planning anchor.
- Hypothesis: explicitly labeled. Domain master validates later.

### Important scope boundaries

- No Tankoban smoke testing was performed in this wake.
- No stream-engine audit beyond player-facing resilience was attempted; the stream-server pivot is a separate thread.
- Prior player audits were extended, not recopied. Where they are still useful I cite them directly. [19][20][21]

## 1. Video buckets V1-V7

### 1.V1 Decoder coverage + hardware acceleration

#### State of the field

- mpv on this box exposes D3D11VA, DXVA2, Vulkan, NVDEC, and VAAPI hardware decode paths, and the local build reports AV1 hardware-decode entries as well. [5]
- FFmpeg documents broad hardware acceleration families such as D3D11VA, QSV, VAAPI, and NVDEC; mpv inherits much of its decode breadth from that stack. [1][4][5]
- VLC's own release history shows ongoing work on AV1, HDR metadata handling, VP9 Profile 2 / 10-bit DXVA support, and 12-bit AV1 decoding. [7]

#### Tankoban current state

- Tankoban currently wires only D3D11VA on Windows. I found no NVDEC, QSV, AMF, or VAAPI path in the current sidecar. [26]
- The current video path still keeps a zero-copy D3D11 presentation goal alive, which is good, but it is a single-backend strategy, not a coverage strategy. [26][29]
- HDR-aware rendering exists, but only when the probe marks the source as HDR; the libplacebo renderer is not the universal render path today. [24][25]

#### Gap estimate

- Gap to mpv: Tankoban covers roughly 30-35 percent of mpv's practical Windows decode/backend flexibility in this bucket.
- Gap to VLC: Tankoban covers roughly 30 percent of VLC's practical decode/backend flexibility in this bucket.

#### Honest finding

Tankoban does not have a realistic short path to "surpass" both mpv and VLC on raw decoder/backend coverage. mpv and VLC both ride much deeper ecosystem surface area here.

#### Candidate surpass paths

- Narrow-path surpass candidate: make Tankoban the best Windows D3D11-first player for the exact codecs it chooses to support, with better diagnostics, fallback messaging, and presentation stability than either mpv or VLC.
  - Scope: 150-300 LOC, 1-2 summons.
  - Value: real user-facing win on the supported path, not broader backend parity.
- Broad-coverage candidate: add explicit backend selection and at least one more decode family beyond D3D11VA.
  - Scope: 500-1200 LOC, 3+ summons.
  - Risk: FFmpeg interop and QA matrix explode immediately.

#### Risk / blockers

- FFmpeg backend complexity is not the hard part; the hard part is support burden across GPUs, drivers, HDR paths, and cross-adapter failure modes.

#### Hypotheses

- Hypothesis - Tankoban should not chase mpv/VLC on backend count. It should chase "fewer backends, much better observability" because that aligns with the sidecar architecture. Agent 4 to validate.

### 1.V2 Color pipeline + HDR tone-mapping

#### State of the field

- mpv plus libplacebo remain the quality reference tier here: ICC handling, BT.1886, soft gamut mapping, HDR tone mapping, dynamic peak detection, gamut expansion/compression, and Dolby Vision BL-only support are explicitly documented. [1][2][3]
- mpv exposes ICC auto-detection, subtitle blending before/after color management, and raw libplacebo overrides. [1]
- VLC has materially improved here over time, including HDR tone mapping through libplacebo and D3D11 HDR fixes, but community reports still show regressions and clip-switch edge cases. [7][9][10]

#### Tankoban current state

- Tankoban already ships a real libplacebo path, not a fake shader toy. It initializes Vulkan/libplacebo, uses `pl_renderer_create`, sets `ewa_lanczossharp` upscaling, wires peak detection, and supports `reinhard`, `bt2390`, `clip`, `mobius`, `linear`, and `hable`. [25]
- It auto-loads the system ICC profile when HDR is active and forwards source primaries/transfer metadata into the renderer. [24][25][30]
- The window path detects HDR-capable outputs via DXGI and uses a 16-bit float swap chain plus scRGB color space on HDR-capable displays. [29]
- The big limitation: the libplacebo renderer is currently HDR-only, and the minimalist HUD removed the user-facing tone-mapping control surface. [24][25][30]

#### Gap estimate

- Gap to mpv: Tankoban covers roughly 55-60 percent of mpv's underlying capability in this bucket, but much less of mpv's exposed and tunable capability.
- Gap to VLC: Tankoban covers roughly 55 percent of VLC's practical HDR/color capability, with stronger architecture in some spots but less breadth and less exposure.

#### Honest finding

This is Tankoban's strongest "engine quality" video bucket. It is the clearest evidence that Tankoban can become visually serious. It is not yet ahead of mpv because mpv uses libplacebo more completely, more universally, and with more user control.

#### Candidate surpass paths

- Extend libplacebo render path to SDR as well as HDR, so Tankoban gets one modern color/scaling pipeline instead of a split personality.
  - Scope: 300-700 LOC, 2-3 summons.
  - Value: biggest quality-per-LOC win in the whole video stack.
- Re-surface the existing tone-mapping and peak-detect controls in the new minimalist UI, but keep presets opinionated instead of mpv-style endless knobs.
  - Scope: 80-180 LOC, <1 summon.
  - Value: immediate visible win with code mostly already present. [25][30][31]
- Add a calibrated "reference HDR" preset set and a "safe SDR display" preset set.
  - Scope: 100-220 LOC, 1 summon.
  - Value: this is where Tankoban can beat VLC's defaults without pretending to out-configure mpv.

#### Risk / blockers

- Full Dolby Vision parity is not solved even at libplacebo level; BL-only support is not "full DV moat removal". [2]
- Exposing too many controls would copy mpv's culture without copying its audience.

#### Hypotheses

- Hypothesis - The fastest honest route to "better than VLC, competitive with mpv" video is not more decoder work. It is "libplacebo everywhere plus better defaults." Agent 4 to validate.

### 1.V3 Scaling quality

#### State of the field

- mpv exposes a deep scaler menu directly: `lanczos`, `spline36`, `ewa_lanczos`, `ewa_lanczossharp`, `ewa_lanczos4sharpest`, and more. [1][5]
- libplacebo's shader system explicitly calls out RAVU, FSRCNNX, and Anime4K style extensibility. [2]
- This is one of mpv's hardest moats because it is not only built-in algorithms; it is also the user shader ecosystem.

#### Tankoban current state

- Tankoban already defaults the libplacebo path to `ewa_lanczossharp`, which is a respectable serious-quality default. [25]
- But that path only runs on HDR content today, and Tankoban exposes none of the scaler choices in its current UI. [24][25][31][32]

#### Gap estimate

- Gap to mpv: roughly 25-30 percent.
- Gap to VLC: roughly 35-40 percent.

#### Honest finding

Tankoban cannot surpass mpv in this bucket without adopting much more of libplacebo and probably some custom-shader strategy. That is a P3 moat unless Tankoban narrows the claim to a tiny set of curated presets.

#### Candidate surpass paths

- Pragmatic path: offer 3 curated scaling presets for all content: Fast, Balanced, Reference.
  - Scope: 120-250 LOC, 1 summon.
  - Value: user-facing quality gain without exposing mpv's full surface.
- Stretch path: universal libplacebo render path plus optional shader preset packs.
  - Scope: 600-1400 LOC, 3+ summons.
  - Risk: turns Tankoban into a shader-hosting project.

#### Risk / blockers

- The mpv moat here is not just code. It is years of community iteration on what "good" looks like for anime, line art, grain retention, and ringing control.

#### Hypotheses

- Hypothesis - Tankoban should aim for "three excellent presets" rather than "full shader hobbyist platform." Agent 4 to validate.

### 1.V4 Frame timing + presentation

#### State of the field

- mpv is still unusually serious about frame pacing: `display-resample`, interpolation, timing offsets, and display-sync modes are documented in detail. [1]
- mpv users still discuss VRR/display-resample interactions as an active edge area, which is evidence both of sophistication and of unsolved sharp corners. [6]
- VLC has improved D3D11/HDR output, but its public "presentation nerd" surface is much shallower than mpv's. [7][10]

#### Tankoban current state

- Tankoban uses a DXGI frame-latency waitable swap chain, D3D11 presentation, HDR-capable output detection, and HiDPI-aware physical-pixel swap-chain sizing. [29]
- It has zero-copy ambitions and retains a D3D11 presenter even for software decode fallback. [26][29]
- It does not currently expose mpv-grade interpolation, display-resample logic, or VRR policy choices. [31][32]

#### Gap estimate

- Gap to mpv: roughly 60 percent coverage.
- Gap to VLC: roughly 55-60 percent coverage.

#### Honest finding

Tankoban is closer here than many other buckets. Its Windows-specific rendering path is already opinionated in the right direction. This is one of the few buckets where a focused Tankoban could beat VLC and become genuinely competitive with mpv for the supported path.

#### Candidate surpass paths

- Surface a compact presentation diagnostics panel: present interval, late-frame count, refresh estimate, queue depth, HDR output state, and audio-device latency.
  - Scope: 80-180 LOC, <1 summon.
  - Value: Tankoban can beat both mpv and VLC on observability.
- Add a small set of presentation modes instead of dozens of knobs: Robust, Match display, Smooth motion.
  - Scope: 180-400 LOC, 1-2 summons.
  - Value: turns existing timing work into a user-visible advantage.
- Investigate VRR-aware policy for the waitable swap-chain path.
  - Scope: 300-700 LOC, 2-3 summons.
  - Risk: driver variability and multi-monitor weirdness.

#### Risk / blockers

- This bucket is very driver-sensitive. Superiority claims require empirical validation on real monitors, not code confidence.

### 1.V5 Subtitle rendering quality

#### State of the field

- mpv and VLC both rely on libass for text subtitles and have years of edge-case knowledge around storage size, margins, shaping, and position semantics. [1][20]
- FFmpeg itself also documents bitmap subtitle families and teletext-related support surfaces, which matter for "boring compatibility" more than glamorous feature lists. [4]

#### Tankoban current state

- Tankoban uses libass with DirectWrite on Windows, extracts fonts, uses a dedicated render thread, supports both libass text and PGS bitmap blending, and maps the subtitle-position slider using mpv's inversion semantics. [28]
- It correctly sets `ass_set_storage_size`, with comments explicitly citing mpv and VLC behavior. [28]
- It also fixes subtitle geometry against the real letterboxed video rectangle rather than the whole canvas, which is exactly the kind of app-specific polish general-purpose players often get wrong in custom UI overlays. [29]
- Missing evidence in current source slice: DVB subtitles, teletext subtitle handling, vertical-text corner cases, HDR-aware subtitle color strategy comparable to mpv's `--blend-subtitles`, and full ASS edge-case parity. [1][4][28][29]

#### Gap estimate

- Gap to mpv: roughly 55 percent.
- Gap to VLC: roughly 55-60 percent.

#### Honest finding

Tankoban is already better than a naive subtitle overlay. It is not yet ahead of mpv or VLC on breadth. It can, however, become better than both on "subtitle placement inside a branded app shell" because it controls HUD, bars, popovers, and video-rect geometry together.

#### Candidate surpass paths

- Add subtitle-safe-area presets that are aware of Tankoban's own HUD and popovers.
  - Scope: 80-140 LOC, <1 summon.
  - Value: true Tankoban-only advantage.
- Add explicit handling/reporting for subtitle family type and renderer mode in diagnostics.
  - Scope: 80-160 LOC, <1 summon.
  - Value: beats mpv/VLC on debuggability.
- Stretch: add DVB/teletext coverage and HDR subtitle blending modes.
  - Scope: 400-900 LOC, 2-3 summons.
  - Risk: hard compatibility work with low glamour.

### 1.V6 Container / format compatibility

#### State of the field

- mpv and VLC inherit years of FFmpeg/container hardening and still accumulate bugfixes around HLS, MP4, MKV, subtitle tracks, and adaptive edge cases. [4][7]
- VLC's NEWS file alone shows repeated fixes across HLS, MP4 PCM quirks, adaptive stack behavior, subtitle handling, and corrupt-stream tolerance. [7]

#### Tankoban current state

- Tankoban has substantial probe-escalation, reconnect, timeout, and broken-duration defensive logic in both demux and decode paths. [26][27]
- The demuxer comments show explicit handling for MKV duration contamination by subtitle/attachment streams and HTTP range-based recovery attempts. [27]
- This is good engineering, but it is still a narrow slice compared with mpv/VLC's accumulated format archaeology.

#### Gap estimate

- Gap to mpv: roughly 40-45 percent.
- Gap to VLC: roughly 40 percent.

#### Honest finding

Tankoban is stronger here than its youth would suggest, but this is still a moat bucket for mpv/VLC. General-purpose players win on sheer years of broken-file exposure.

#### Candidate surpass paths

- Do not chase universal container superiority.
- Do chase excellent failure explanation:
  - identify "bad duration from subtitle stream",
  - identify "HTTP probe exhausted",
  - identify "unsupported subtitle family",
  - identify "hwaccel fell back to software".
  - Scope: 150-250 LOC, 1 summon.
  - Value: beats both players on user comprehension even while staying behind on breadth.

#### Hypotheses

- Hypothesis - Tankoban's best move is not more demux edge-case heroics. It is better classification and surfacing of why playback fell off the happy path. Agent 4 to validate.

### 1.V7 Streaming + network resilience

#### State of the field

- FFmpeg exposes HLS, DASH, HTTP persistence, HTTP partial requests, background async wrappers, and retry-related knobs; VLC's adaptive stack also shows repeated maintenance in HLS/DASH/Smooth/live behavior. [4][7]
- mpv is robust for general streaming, but its core value is not "deeply integrated torrent-aware app semantics."

#### Tankoban current state

- Tankoban has three-tier HTTP probe escalation, reconnect options, 30s unified `rw_timeout`, a 64 MiB prefetch ring, and a dedicated prefetch thread between raw HTTP IO and demux. [26][27]
- This is one of Tankoban's strongest current technical stories.
- The limitation is scope: this audit excludes stream-server-level gaps, and current Tankoban does not yet fully surface stream/network state to the user in an advantageously rich way. [26][27]

#### Gap estimate

- Gap to mpv: roughly 60 percent on generic streaming mechanics, but potentially better on app-specific stream behavior if surfaced.
- Gap to VLC: roughly 65 percent.

#### Honest finding

This is a credible surpass bucket, but not by out-generic-playering generic players. Tankoban can win if it translates transport knowledge into playback behavior and UI affordances.

#### Candidate surpass paths

- Add seek-to-cached-region affordance and buffered-range visualization.
  - Scope: 150-300 LOC, 1-2 summons.
- Add stall reason classification: peer starvation vs probe timeout vs decoder backlog vs audio-device startup.
  - Scope: 120-260 LOC, 1 summon.
- Add chapter-end predictive prebuffering for episodic playback.
  - Scope: 200-400 LOC, 1-2 summons.

#### Risk / blockers

- The biggest risk is stepping into the stream-engine domain boundary. Keep the player-side UX and diagnostics separate from server internals.

## 2. Audio buckets A1-A7

### 2.A1 Output backend + latency

#### State of the field

- mpv exposes `wasapi` and supports exclusive mode where the backend supports it. [1][5]
- VLC's WASAPI module explicitly supports shared vs exclusive mode and documents the usual latency / OS-mixer tradeoff. [8]
- Roon and Audirvana both frame exclusive control as a baseline requirement for high-end playback, not an optional nicety. [11][12][13]

#### Tankoban current state

- Tankoban prefers WASAPI shared via PortAudio, prewarms a shared stream, and uses `hostApiSpecificStreamInfo = nullptr`; I found no exclusive WASAPI, ASIO, or Kernel Streaming path. [22][24]
- Tankoban does have one genuinely good user-facing feature here: per-device offset recall, including Bluetooth-specific default compensation and persistence. [30]

#### Gap estimate

- Gap to mpv: roughly 25 percent.
- Gap to VLC: roughly 20 percent.

#### Honest finding

Tankoban is behind in backend seriousness but ahead in one area mpv/VLC mostly leave to the user: device-specific latency heuristics. The backend is weak; the UX layer is promising.

#### Candidate surpass paths

- Add a first-run latency calibration flow per output device and keep the current offset persistence.
  - Scope: 120-220 LOC, 1 summon.
  - Value: could beat both mpv and VLC on practical Bluetooth usability.
- Add explicit backend/latency/device diagnostics in AudioPopover.
  - Scope: 60-120 LOC, <1 summon.
- True backend catch-up path: WASAPI exclusive and optional ASIO.
  - Scope: 500-1200 LOC, 3+ summons.
  - Risk: architecture and QA burden.

### 2.A2 Bit-perfect output

#### State of the field

- Roon, Audirvana, foobar WASAPI exclusive, and VLC exclusive/passthrough all explicitly center the "bypass the OS mixer" story. [8][11][12][13][15]

#### Tankoban current state

- Tankoban currently resamples many files into a prewarmed 48 kHz stereo float path, especially when the prewarmed stream is used. [22][24]
- That is excellent for startup smoothness and bad for bit-perfect claims.

#### Gap estimate

- Gap to mpv: roughly 10 percent.
- Gap to VLC: roughly 15 percent.

#### Honest finding

Tankoban does not have bit-perfect playback today in any serious audiophile sense on the main Windows path.

#### Candidate surpass paths

- None as a quick win.
- Real path: separate "fast-start shared" and "reference exclusive" output modes.
  - Scope: 500-1000 LOC, 3+ summons.
- Alternative path: stay honest and do not market this bucket until architecture changes.
  - Scope: 0 LOC.
  - Value: avoids false claims.

### 2.A3 Resampling quality

#### State of the field

- SoXR is explicitly positioned as a high-quality resampling library with configurable phase response, aliasing, rejection, and bandwidth tradeoffs. [17]
- mpv exposes meaningful resampling controls and, via lavfi, can reach far beyond a fixed internal path. [1][5]
- foobar's shared-output component changelog still talks about playback stalling with extreme-latency resamplers, which is a reminder that this bucket matters in real systems, not only on paper. [16]

#### Tankoban current state

- Tankoban uses `swresample` and hard-converts to stereo float, often at 48 kHz. [22]
- I found no SoXR, no quality preset surface, and no user-selectable resampler behavior. [22]

#### Gap estimate

- Gap to mpv: roughly 20-25 percent.
- Gap to VLC: roughly 20-25 percent.

#### Honest finding

Tankoban is currently in the "functional" tier, not the "serious configurable resampling" tier.

#### Candidate surpass paths

- Add a compact resampling policy switch: Auto, Match device, Quality-first.
  - Scope: 150-300 LOC, 1-2 summons.
- Stretch: SoXR-backed reference mode for non-exclusive playback.
  - Scope: 250-500 LOC, 2 summons.
  - Risk: extra dependency and tuning burden.

### 2.A4 Channel mapping

#### State of the field

- VLC's WASAPI path and release notes explicitly cover passthrough and multichannel-related capability. [7][8]
- Audirvana on Windows explicitly supports WASAPI, ASIO, and Kernel Streaming and recommends ASIO for full DSD capability. [13][14]

#### Tankoban current state

- Tankoban always outputs stereo. The source comments are explicit. [22]
- I found no multichannel retention, Atmos passthrough, HDMI passthrough, HRTF, or headphone virtualizer path. [22][24]

#### Gap estimate

- Gap to mpv: roughly 10 percent.
- Gap to VLC: roughly 10-15 percent.

#### Honest finding

Tankoban cannot claim superiority in this bucket today. It is not even at parity.

#### Candidate surpass paths

- Mid-tier path: preserve native channel count to output instead of forced stereo.
  - Scope: 300-700 LOC, 2-3 summons.
- Stretch path: passthrough and spatial rendering modes.
  - Scope: 800-1800 LOC, 3+ summons plus hardware QA.

### 2.A5 A/V sync precision

#### State of the field

- mpv exposes explicit video-sync policies, timing offsets, and sync compensation modes. [1]
- VLC and other mature players also treat audio clocking as a first-class concern even if they expose fewer knobs publicly.

#### Tankoban current state

- Tankoban already logs and tracks latency carefully, persists per-device offset, and threads audio-device information back into the UI layer. [22][24][30]
- The current fixed shared output architecture hurts fidelity buckets, but the instrumentation and compensation mindset are strong.

#### Gap estimate

- Gap to mpv: roughly 50-55 percent.
- Gap to VLC: roughly 50 percent.

#### Honest finding

Tankoban is not ahead on raw sync algorithm sophistication, but it is one of the few buckets where it has the beginnings of a product-level advantage: it remembers the user's actual output device reality.

#### Candidate surpass paths

- Add automatic offset suggestions driven by observed startup latency deltas.
  - Scope: 120-220 LOC, 1 summon.
- Add confidence-scored sync diagnostics in the UI instead of burying everything in logs.
  - Scope: 100-180 LOC, <1 summon.
- Keep the current manual +/- 50 ms control path; it is already practical. [30][32]

### 2.A6 Audio effects

#### State of the field

- mpv's current local build exposes `lavfi`, `rubberband`, compressors, equalizers, denoisers, limiters, FFT tools, and many more through its audio-filter surface. [5]
- Roon/Audirvana/foobar-class products also compete on curated DSP, but the real takeaway here is that Tankoban is not yet in that conversation. [11][12][13][15][16]

#### Tankoban current state

- Tankoban has dormant capability for `loudnorm`, EQ, deinterlace/interpolation filter strings on the sidecar command surface, and a simple DRC path in the audio decoder header comments, but the new minimalist player removed the corresponding UI affordances. [23][30][31][32]

#### Gap estimate

- Gap to mpv: roughly 20-25 percent.
- Gap to VLC: primary-only estimate roughly 25-30 percent.

#### Honest finding

Tankoban is currently weaker than its own codebase suggests because the engine still has pieces the UI no longer surfaces.

#### Candidate surpass paths

- Re-surface a sharply limited audio-effects surface: Normalize, Dialogue boost, Night mode.
  - Scope: 80-180 LOC, <1 summon.
  - Value: can beat both mpv and VLC on clarity of intent even while losing on raw DSP breadth.
- Do not rebuild a giant effects laboratory.
  - Value: preserves product focus.

### 2.A7 Format coverage + lossless handling

#### State of the field

- mpv and VLC both get broad decode coverage from FFmpeg and adjacent platform support. [1][4][7]
- Audirvana explicitly supports native DSD only via ASIO on Windows, and distinguishes DoP vs native paths. [13][14]
- foobar's output ecosystem still reflects an enthusiast player culture around exclusive mode and alternate outputs. [15][16]

#### Tankoban current state

- Tankoban decodes mainstream FFmpeg-backed formats, but the output path is float PCM through PortAudio and I found no evidence of native DSD, DXD-specialized output, MQA-specific handling, or cue-sheet-centric playback behavior. [22][24]
- Gapless behavior may be acceptable in some cases, but I did not find enough evidence to claim a product-level edge here. Treat gapless superiority as uncorroborated.

#### Gap estimate

- Gap to mpv: roughly 20 percent.
- Gap to VLC: roughly 15-20 percent.

#### Honest finding

This is another bucket where Tankoban should avoid grand claims. The path to superiority would pull the product toward "audiophile local music player" territory, which is probably the wrong species.

#### Candidate surpass paths

- No recommended pure-format arms race.
- If audiobook support matters, pursue book-aware playback context, bookmarks, and resume semantics instead of chasing DSD bragging rights.
  - Scope: 150-350 LOC, 1-2 summons.

## 3. Tankoban-specific surpass paths X1-X3

### 3.X1 Library-aware playback

#### Observations

- Tankoban already persists show-level aspect override, crop override, audio language, subtitle language, track IDs, and subtitle visibility. [30]
- It also resolves preference priority sanely: carry-forward, per-file, per-show, then global fallback. [30]
- This is already a stronger media-library mental model than mpv or VLC ship natively.

#### Honest finding

Tankoban can surpass both mpv and VLC here today with more surface polish, because the underlying data model is already real.

#### Candidate surpass paths

- Expose the existing persistence model transparently in the player UI.
  - Scope: 80-180 LOC, <1 summon.
- Add per-show "prefer dub/sub / prefer signs-only / inherit previous episode" policy labels.
  - Scope: 120-250 LOC, 1 summon.
- Add sibling-episode inheritance preview so users understand what will carry forward.
  - Scope: 100-220 LOC, 1 summon.

### 3.X2 Stream-mode-aware UX

#### Observations

- Tankoban already knows much more about open/probe/prefetch behavior than mpv or VLC typically surface to normal users. [26][27]
- It does not yet fully cash that knowledge out into stream-aware controls and explanations.

#### Honest finding

This is a real moat. mpv and VLC can play a URL well; Tankoban can become better at explaining whether the problem is cache, swarm, decoder, or device.

#### Candidate surpass paths

- Buffered-region seek guardrail.
  - Scope: 120-220 LOC, 1 summon.
- Stall-recovery banner with reason and next action.
  - Scope: 100-220 LOC, 1 summon.
- Predictive prebuffering around chapter/episode boundaries.
  - Scope: 200-400 LOC, 1-2 summons.

### 3.X3 Integrated-app context

#### Observations

- General-purpose players do not natively understand Tankoban's comic/book/video mode boundaries or brotherhood-level diagnostics.
- Tankoban can route resume, diagnostics, and preference intelligence across app modes in a way mpv/VLC structurally do not try to do.

#### Honest finding

This is the highest-ceiling "surpass" territory in the whole audit because it avoids direct engine-arms-race framing entirely.

#### Candidate surpass paths

- Unified resume card across book/comic/video modes.
  - Scope: 150-300 LOC, 1-2 summons.
- "Continue with preferred audio/sub pair from this series" affordance.
  - Scope: 100-200 LOC, 1 summon.
- Built-in playback diagnostic export from the same app shell.
  - Scope: 120-250 LOC, 1 summon.

## 4. Cross-bucket themes

### Theme 1 - Tankoban already has part of a modern renderer, but only part

Tankoban's current code is not "just a basic FFmpeg blit." It has D3D11, DXGI waitable swap chains, HDR display detection, scRGB output, and a real libplacebo renderer. [24][25][26][29]

The problem is split coverage:

- libplacebo path: serious, but HDR-only.
- SDR path: not yet the same class of modern pipeline.
- UI exposure: reduced just as the underlying engine became more capable. [24][30][31][32]

This split is why Tankoban feels simultaneously advanced and unfinished.

### Theme 2 - Audio startup UX and audio fidelity currently pull in opposite directions

The prewarmed shared 48 kHz stereo path is a clever answer to Bluetooth startup pain. [22][24][30]

It is also a direct blocker to:

- bit-perfect playback,
- native-rate output,
- multichannel seriousness,
- high-end backend claims. [11][12][13][14][15][16]

That is not a bug. It is an architecture trade. Tankoban should treat it as such.

### Theme 3 - Observability is Tankoban's cleanest path to beating both players

mpv is more configurable.
VLC is more compatibility-broad.

Tankoban can still beat both by being more explicit about:

- what backend opened,
- what latency was measured,
- what buffer/cache state exists,
- why fallback happened,
- what track/subtitle policy is being applied,
- what HDR/scaler path is active. [24][26][27][29][30]

That is a real product advantage, not a consolation prize.

### Theme 4 - The real moat is app context, not general-purpose feature count

Every bucket that asks Tankoban to become "mpv but more" or "VLC but more" trends toward P3.

Every bucket that asks Tankoban to become:

- the best player for this series,
- the clearest player for this device,
- the smartest player for this stream,
- the most coherent player inside this app,

looks achievable.

## 5. Prioritized recommendation

### P0 - Quick wins

#### P0.1 Re-surface tone-mapping / scaler presets already supported by the engine

- Buckets: V2, V3
- Scope: 80-180 LOC, <1 summon
- Risk: low
- Rationale: the code already has meaningful libplacebo controls; the UI simply stopped exposing them. [25][30][31][32]

#### P0.2 Add audio-device / backend / latency diagnostics to the current AudioPopover

- Buckets: A1, A5, X2
- Scope: 60-120 LOC, <1 summon
- Risk: low
- Rationale: turns an existing hidden strength into visible product value. [24][30]

#### P0.3 Add buffered-range and stall-reason surface for stream playback

- Buckets: V7, X2
- Scope: 120-220 LOC, 1 summon
- Risk: low to moderate
- Rationale: this is where Tankoban can beat both reference players without pretending to out-demux them. [26][27]

#### P0.4 Expose per-show playback preference inheritance explicitly

- Buckets: X1
- Scope: 80-180 LOC, <1 summon
- Risk: low
- Rationale: Tankoban already stores the right data; the win is surfacing it clearly. [30]

### P1 - Strategic

#### P1.1 Make libplacebo the render path for SDR as well as HDR

- Buckets: V2, V3, V4
- Scope: 300-700 LOC, 2-3 summons
- Risk: moderate
- Rationale: highest quality-per-effort strategic move in the video stack. [2][3][25][29]

#### P1.2 Add compact presentation modes plus diagnostics

- Buckets: V4, A5
- Scope: 180-400 LOC, 1-2 summons
- Risk: moderate
- Rationale: Tankoban already has waitable-swapchain and D3D11 presentation groundwork; packaging it well could surpass VLC and challenge mpv for the supported Windows path. [1][6][29]

#### P1.3 Turn hidden audio features into intentional product features

- Buckets: A6, X1
- Scope: 100-220 LOC, 1 summon
- Risk: low to moderate
- Rationale: Normalize / Dialogue boost / Night mode are easier to beat users' expectations with than a giant filter playground. [23][31][32]

#### P1.4 Add smarter stream-aware resume and predictive prebuffering

- Buckets: V7, X2, X3
- Scope: 200-400 LOC, 1-2 summons
- Risk: moderate
- Rationale: this plays directly to Tankoban's app-context moat. [26][27][30]

### P2 - Stretch

#### P2.1 Add a second serious Windows audio output mode: WASAPI exclusive

- Buckets: A1, A2, A3, A5
- Scope: 500-1000 LOC, 3+ summons
- Risk: high
- Rationale: necessary for honest high-fidelity audio claims. [8][11][12][13]

#### P2.2 Preserve native channel count and add passthrough groundwork

- Buckets: A4, A7
- Scope: 300-900 LOC, 2-3 summons
- Risk: high
- Rationale: current forced stereo is a hard ceiling. [22]

#### P2.3 Add subtitle family breadth beyond current libass + PGS focus

- Buckets: V5
- Scope: 400-900 LOC, 2-3 summons
- Risk: high
- Rationale: valuable but compatibility-heavy, with low glamour-to-effort ratio. [4][28]

### P3 - Honest "don't bother" paths

#### P3.1 Beating mpv's scaler/shader ecosystem head-on

- Buckets: V3
- Rationale: libplacebo plus user-shader ecosystem is a community moat. Curated presets are feasible; ecosystem superiority is not. [2][5]

#### P3.2 Beating mpv/VLC on universal container weird-file resilience

- Buckets: V6
- Rationale: years of ecosystem exposure matter more than isolated code heroics. [4][7]

#### P3.3 Claiming audiophile-player superiority without backend redesign

- Buckets: A1-A4, A7
- Rationale: current shared stereo 48 kHz-oriented path makes the claim false on its face. [11][12][13][14][15][16][22][24]

#### P3.4 Chasing full Dolby Vision moat language

- Buckets: V2
- Rationale: even the underlying libraries still have explicit limits; Tankoban should not anchor a strategy on this. [2]

## 6. Open questions for Hemanth

1. Does Hemanth want Tankoban to be "best practical Windows video player for this app" or "best pure playback engine"? The audit strongly supports the first and warns against the second.
2. Is shipping more libplacebo surface area acceptable if it stays hidden behind curated presets rather than mpv-style knob density?
3. Does Hemanth want a dual-path audio story:
   - Fast start / shared / Bluetooth-friendly
   - Reference / exclusive / slower but cleaner
4. Should Tankoban invest in multichannel and passthrough at all, or stay focused on stereo-first video consumption?
5. Is "better diagnostics than mpv/VLC" a first-class product goal? If yes, several P0/P1 items become obvious.
6. Should stream-aware playback remain separate from stream-engine work, or is Hemanth willing to let the player surface server-side state more directly?

## 7. What Tankoban already does well

This matters because the right next step is not "replace everything with mpv thinking."

- Tankoban already has a real Windows rendering stack with D3D11 and a DXGI waitable swap chain. [29]
- It already detects HDR-capable outputs and uses an HDR-aware swap-chain configuration on capable displays. [29]
- It already ships a genuine libplacebo path with serious tone-mapping choices and ICC auto-detection, even if it is currently HDR-only. [24][25]
- It already defaults that libplacebo path to a respectable upscaler (`ewa_lanczossharp`). [25]
- It already has better-than-average subtitle geometry awareness for a branded app shell: PGS + libass, correct storage size, DirectWrite fonts, and video-rect-aware overlay positioning. [28][29]
- It already does more stream-resilience engineering than many young players: tiered probing, reconnect policy, long `rw_timeout`, prefetch thread, and large ring buffer. [26][27]
- It already handles per-device audio delay better than mpv/VLC defaults do for Bluetooth-style pain points. [24][30]
- It already persists show-level aspect, crop, subtitle language, audio language, exact track IDs, and subtitle visibility. That is genuine product leverage. [30]
- It already retains hidden hooks for normalization / EQ / DRC style behavior even though the current minimalist UI no longer surfaces them. [23][31][32]

The strategic mistake would be to throw these strengths away and start a generic-player parity chase from zero.

## 8. References (bibliography)

### Core docs / external references

1. mpv manual (stable): https://mpv.io/manual/stable/
2. libplacebo README: https://github.com/haasn/libplacebo
3. libplacebo options docs: `C:\tools\libplacebo-source\docs\options.md:337-380,452-466`
4. FFmpeg docs:
   - https://ffmpeg.org/ffmpeg-all.html
   - https://www.ffmpeg.org/ffmpeg-protocols.html
5. Empirical mpv local checks on 2026-04-25:
   - `C:\tools\mpv\mpv.com --version`
   - `C:\tools\mpv\mpv.com --ao=help`
   - `C:\tools\mpv\mpv.com --vo=help`
   - `C:\tools\mpv\mpv.com --hwdec=help`
   - `C:\tools\mpv\mpv.com --scale=help`
   - `C:\tools\mpv\mpv.com --af=help`
6. mpv VRR / display-resample discussion: https://github.com/mpv-player/mpv/issues/12005
7. VLC NEWS snapshot:
   - `C:\Users\Suprabha\Downloads\Video player reference\vlc-master\NEWS:447-450`
   - `C:\Users\Suprabha\Downloads\Video player reference\vlc-master\NEWS:521-523`
   - `C:\Users\Suprabha\Downloads\Video player reference\vlc-master\NEWS:1049-1075`
8. VLC WASAPI source:
   - `C:\Users\Suprabha\Downloads\Video player reference\vlc-master\modules\audio_output\wasapi.c:623-695`
   - `C:\Users\Suprabha\Downloads\Video player reference\vlc-master\modules\audio_output\wasapi.c:809-968`
9. VLC issue - no HDR tone mapping on SDR display: https://code.videolan.org/videolan/vlc/-/issues/26631
10. VLC issue - HDR curve setup switching issue: https://code.videolan.org/videolan/vlc/-/issues/27694
11. Roon Exclusive Mode: https://help.roonlabs.com/portal/en/kb/articles/exclusive-mode
12. Audirvana exclusive access doc: https://help.audirvana.com/en/support/solutions/articles/202000072992-how-to-unlock-the-exclusive-access-audio-mode-
13. Audirvana device connection doc: https://help.audirvana.com/en/support/solutions/articles/202000059411-how-can-i-connect-my-device-to-audirv%C4%81na-
14. Audirvana DSD native playback doc: https://help.audirvana.com/en/support/solutions/articles/202000075482-how-to-play-dsd-natively-
15. foobar2000 archived WASAPI output support page: https://www.foobar2000.org/components/view/foo_out_wasapi/release/3.0
16. foobar2000 WASAPI shared component page: https://www.foobar2000.org/components/view/foo_out_wasapis?6scBfgBs=wFBOT
17. SoXR wiki / home: https://sourceforge.net/p/soxr/wiki/Home/
18. Head-Fi Windows backend tradeoff discussion: https://www.head-fi.org/threads/wasapi-asio4all-ks-in-general-causes-lag.812128/

### Prior internal audit thread

19. Prior HDR/filter comparative audit: `../_archive/audits/player/comparative_player_2026-04-20_p3_hdr_filters.md`
20. Prior subtitle comparative audit: `../_archive/audits/player/comparative_player_2026-04-20_p2_subtitles.md`
21. Prior video-quality dip audit: `../_archive/audits/player/video_quality_dip_2026-04-24.md`

### Tankoban current-state citations

22. Audio decode / resample / stereo output:
   - `native_sidecar/src/audio_decoder.cpp:370-395`
   - `native_sidecar/src/audio_decoder.cpp:427-523`
23. DRC path comment / intent: `native_sidecar/src/audio_decoder.h:67-75`
24. Sidecar main:
   - HDR-only libplacebo init: `native_sidecar/src/main.cpp:792-807`
   - WASAPI-shared prewarm path: `native_sidecar/src/main.cpp:1534-1591`
25. GPU renderer:
   - libplacebo init + scaler defaults: `native_sidecar/src/gpu_renderer.cpp:90-115`
   - tone-mapping choices: `native_sidecar/src/gpu_renderer.cpp:217-233`
   - HDR metadata + ICC load entry points: `native_sidecar/src/gpu_renderer.cpp:239-280`
26. Video decoder:
   - HTTP prefetch / ring buffer setup: `native_sidecar/src/video_decoder.cpp:219-287`
   - D3D11VA-only hwaccel path: `native_sidecar/src/video_decoder.cpp:523-593`
27. Demuxer probe / retry / duration recovery:
   - `native_sidecar/src/demuxer.cpp:176-320`
28. Subtitle renderer:
   - libass + DirectWrite + render thread: `native_sidecar/src/subtitle_renderer.cpp:138-167`
   - PGS + libass text path + line position: `native_sidecar/src/subtitle_renderer.cpp:219-257`
   - storage size + margins: `native_sidecar/src/subtitle_renderer.cpp:304-320`
29. FrameCanvas:
   - HDR output detection + waitable swap chain + HiDPI sizing: `src/ui/player/FrameCanvas.cpp:150-280`
   - autocrop disabled by default: `src/ui/player/FrameCanvas.cpp:961-983`
   - subtitle overlay/video-rect handling: `src/ui/player/FrameCanvas.cpp:1180-1209`
   - tone-map setter surface: `src/ui/player/FrameCanvas.cpp:2239-2260`
30. VideoPlayer:
   - minimalist HUD removed filter / tone-map UI: `src/ui/player/VideoPlayer.cpp:1951-2005`
   - per-device audio delay recall and Bluetooth default: `src/ui/player/VideoPlayer.cpp:2021-2075`
   - per-show preference persistence: `src/ui/player/VideoPlayer.cpp:2912-3107`
31. Sidecar filter command surface:
   - `src/ui/player/SidecarProcess.cpp:367-386`
32. Current keybinding surface:
   - removed normalize/deinterlace bindings, kept audio-delay controls:
   - `src/ui/player/KeyBindings.cpp:24-55`
33. Tankoban HEAD at audit time: `0386dfb` (`git rev-parse --short HEAD`, 2026-04-25)
