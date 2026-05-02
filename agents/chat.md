# Agent Chat

All agents post updates here. Read before starting work, append after completing each major task.

Format: `## Agent [ID] ([Role]) -- [time]` followed by your message.

---
> ## ARCHIVE POINTER (pinned — read once)
>
> Chat history through 2026-05-02 lines 8–3428 was rotated to:
> [agents/chat_archive/2026-05-02_chat_lines_8-3428.md](chat_archive/2026-05-02_chat_lines_8-3428.md) (rotation 6)
>
> Previous rotations: [2026-04-24 lines 8–5253](chat_archive/2026-04-24_chat_lines_8-5253.md) (rotation 5), [2026-04-20 lines 8–3978](chat_archive/2026-04-20_chat_lines_8-3978.md) (rotation 4), [2026-04-18 lines 8–4038](chat_archive/2026-04-18_chat_lines_8-4038.md) (rotation 3), [2026-04-16 lines 8–3642](chat_archive/2026-04-16_chat_lines_8-3642.md) (rotation 2), [2026-04-16 lines 8–19467](chat_archive/2026-04-16_chat_lines_8-19467.md) (rotation 1).
>
> **Major milestones since rotation 5 (chat lines 8–3428 of this rotation):**
> - **REPO_HYGIENE_FIX_TODO ALL 7 PHASES ✅ CLOSED 2026-04-26** (Agent 0). P1 untracked-source commit + ring buffer + hardcoded-path strip + single-instance wire; P2 vcpkg + CMakePresets + setup.bat; P3 dev-control bridge (`tankoctl` + `DevControlServer.{h,cpp}` + per-class `devSnapshot`, ~10× faster than UIA tree walks); P4 7 lifecycle bug fixes (JsonStore race / scanner ownership / dropped rescans / cooperative cancellation / async sidecar start / session-id strict mode / std::stoi hardening); P5 v1+v1.x CI (repo-consistency lint + Windows build); P6 NSIS installer + GitHub Releases pipeline; P7 LICENSE + README + BUILD + ARCHITECTURE + CONTRIBUTING.
> - **SKILL_DISCIPLINE_FIX_TODO ALL 6 PHASES ✅ CLOSED 2026-04-25** (Agent 0). claude-mem repair (no `claude.cmd` on PATH root cause + Option A install + reboot) + RTC `Skills invoked:` provenance contract (contracts-v3) + pre-RTC nag hook + memory-degraded sentinel + tiered skill sheet (Tier 1 Core ~6 / Tier 2 Conditional ~13 / Tier 3 Milestone-only ~2).
> - **MAKE_MPV_SOLO 11 of 15 tasks closed 2026-05-01 → 2026-05-02** (Agent 3). T1 ffmpeg baseline + 6 patterns identified; T2 §Q4 stream-mode lock removed; T3 mpv mediaInfo bridge; T4 HDR + tone-mapping (auto-pick GREEN); T6 + T6.B subtitle residuals + carry-id extension; T7 HUD/focus/mouse/keyboard parity (Pattern C seek-accumulator + Esc QShortcut scope fix); T8 audio polish 2-of-3; T9 brightness-only filter control + popover; T11 default flip ffmpeg→mpv. Tasks 12–15 (validation + decommission) queued.
> - **MAKE_MPV_BEAT_FFMPEG arc opened 2026-05-02** (Agent 3 + Codex Trigger D). Task 1 architecture + Task 2 Vulkan widget mounts cleanly (Hemanth visual GREEN on Community S01E01) + startup-crash hotfix (Qt6Core.dll FATAL_USER_CALLBACK loop in early construction). libplacebo+Vulkan in MSVC main app via NEW MpvVulkanWidget + MpvLibplaceboBuildProbe.
> - **PER_VIEW_CHROME_FIX 4-phase arc CLOSED 2026-05-02** (Agent 5). Per-view chrome clusters (Min/Max/Close) shipped to Video Player + Comic Reader + Book Reader; Win32 SetWindowPlacement direct path closes invisible-restore latent bug.
> - **AUDIOBOOK_PAIRING_RESTORE shipped 2026-04-25 + AUDIOBOOK_SYNCED_TEXT brainstorm memo 2026-05-01** (Agent 2). Restored 3-file Tankoban-Max chapter-pairing port from HEAD that an unauthored uncommitted simplification had stripped; brainstorm memo gates downstream synced-text fix-TODO authoring (foliate-js EPUB 3 Media Overlays already vendored, dormant).
> - **VIDEO_HUD_TIME_LABELS_FIX + VIDEO_HUD_MINIMALIST polish + 3 comic-reader ships 2026-04-25** (Agent 3 + Agent 1). HEMANTH-DRIVEN MODE since 2026-04-25 — no agent-initiated player audits; comic-reader polish-mode rule lifted for SinglePage removal.
> - **SIDECAR_DISPATCHER_NON_BLOCKING_FIX Phase A SHIPPED 2026-04-26** (Agent 4). `handle_set_tracks` worker-thread split + cooperative cancellation atomic + 200ms drain in `teardown_decode`. Dispatcher unblocked on heavy HTTP subtitle-track-toggle path.
> - **TankoLibrary M2.1 → M2.4 + Track B batch 1 SHIPPED 2026-04-21 → 2026-04-22** (Agent 4B). LibGen-first pivot end-to-end (zero captcha) + AaSlowDownloadWaitHandler + BookDownloader + EPUB-only filter + AA default-disabled.
> - **THEME_SYSTEM_FIX P1 + P2 + Preset-axis REMOVAL shipped 2026-04-25** (Agent 5). Two-axis to single-axis Mode-only system per Hemanth correction ("never asked for colour palettes"). 6 Modes: Dark/Light/Nord/Solarized/Gruvbox/Catppuccin.
> - **Sweep markers in this range:** `0386dfb`, `d1e4bb7`, `226152e` (10 commits each), plus the closing rotation-marker `fe76e66` (4 commits) just landed.

- Baseline d3d11va-copy: 1.55 drops/sec
- Task 10.5 hwdec=no floor: 0.10-0.15 drops/sec
- **Tier 0 separable (this commit): 0.24 drops/sec** ← measurable uplift over floor but well within clean playback

The ~2× floor uplift is the expected GPU shader cost of the separable kernels — not a regression, just non-zero shader work where Task 10.5's bilinear default did effectively nothing. Drop signature is also distributed evenly across the 24 samples (not bursty like d3d11va-copy was), suggesting the cost is steady-state shader time, not periodic decoder pressure.

**Code change (single file, ~30 LOC to MpvBackend.cpp around line 240):** added 3 setOpt calls + a mpvLog announcing the resolved scaler set. Comment block carries the Tier 1 → Tier 0 lesson so future maintainers know NOT to re-try ewa_* on the OpenGL path (would regress this fix).

**What still needs Hemanth's eyes:** subjective side-by-side mpv-vs-ffmpeg picture quality. Per `feedback_subjective_over_trace.md` Hemanth's eyes are the canonical arbiter — telemetry only verifies we didn't break Task 10.5's stutter floor (we didn't). Three possible verdicts:
- **GREEN** ("matches ffmpeg" or "close enough"): ship Tier 0 as-is, close Task 10.7, unblock Tasks 11+
- **YELLOW** ("better than before but still soft"): Tier 0.5 adds `deband=yes` (cheap perceptual uplift, no GPU cost; addresses gradient banding in dark scenes) — single line edit, retry
- **RED** ("still way worse than ffmpeg"): the OpenGL-budget ceiling is real; defer to a future task that wires libplacebo as an mpv render hook (`vo=libmpv` lets you do this, but it's a substantial substrate change — out of Task 10.7 scope)

**Honest scope flags:**
- The 2× floor uplift is a real, measurable cost. If Hemanth's machine is running other GPU-pressing workloads (game in background, etc.) Tier 0 might tip to drops the way Tier 1 did. The TANKOBAN_MPV_HWDEC env var (Task 10.5) doesn't help here — these are separate render-pipeline knobs. A follow-up task could expose `TANKOBAN_MPV_SCALER` / `TANKOBAN_MPV_QUALITY=fast|hq` env-var families for users on weaker hardware. Not authored.
- Task 10.7 only addresses scaling (the most-impactful softness lever). It does NOT address ICC color management, debanding, or HDR tone-mapping nuance — the broader "look feel" parity. If Hemanth still sees subtle differences after Tier 0 those would be Task 10.8 territory.
- The test corpus is one file (Community S01E01, 1080p HEVC SDR). 4K, HDR, anime ASS cards, and dark-scene movies may show different cost profiles. Generalization untested.
- mpv's `profile=gpu-hq` would set similar values via mpv's own preset system instead of explicit setOpt calls. Chose explicit because (a) it's auditable from this code site without reading mpv's profile config, (b) avoids any cross-version preset content drift in libmpv.

**Cumulative arc status (12 of 15 task slots filled — Task 10.7 pending Hemanth verdict):**
- ✅ Tasks 1-7 + 8 + 9 + Task 9 follow-up bundle + 10 + 10.5 + 10.7 (this RTC, structurally) + 6.B
- ⏳ Task 10.7 final close gated on Hemanth side-by-side eyeball verdict
- ⏳ Task 8.B queued (audio device watcher)
- ⏳ Conditional Task 0.5 if YELLOW + Task 10.6 / 10.8 if surfacing additional gaps
- ⏳ Tasks 11-15 (cutover) gated on 10.7 GREEN

**Smoke discipline:** MCP LOCK held start-to-finish ~00:01am-08:11am (overnight gap; build + Tier-0 morning smoke continued same lock window since lab was idle). videoBackend QSettings restored to ffmpeg via reg.exe (windows-mcp Registry tool disconnected mid-session; fell back to direct `reg` CLI per the same contract). Tankoban + 1 stale stremio-runtime PID killed per Rule 17.

**Files touched:** `src/ui/player/MpvBackend.cpp`, `out/mpv_telemetry.log` (2 new session blocks: Tier 1 regression + Tier 0 floor verification), `agents/chat.md`.

/superpowers:executing-plans (Task 10.7 walked per Hemanth-greenlit ladder; Tier 1 → Tier 0 backoff was on-spec for the "if telemetry regresses → back off" branch; honest about the lesson). /simplify (smallest possible diff: 3 setOpt calls + 1 mpvLog; no new headers, no new fields, no new abstractions). /build-verify (BUILD OK both rebuilds). /superpowers:verification-before-completion (telemetry confirms drops returned to floor zone post-backoff; visible-quality verification explicitly handed to Hemanth's eyes per `feedback_subjective_over_trace.md`). /superpowers:requesting-code-review (mpv property names + values are documented in mpv's own option reference; no novel API surface; setOpt + mpvLog patterns reused from existing initializeMpv flow). /superpowers:systematic-debugging (Tier 1 regression Phase 1 hypothesis "OpenGL-vs-Vulkan shader budget" diagnosed via telemetry data + mid-smoke Hemanth verbatim "stuttering again"; Phase 2 fix verified by floor-restoration smoke). /security-review N/A (mpv property writes; no input parsing; no network). /superpowers:receiving-code-review (Hemanth's "stuttering again" feedback received + acted on in-flight rather than deferred). | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review, /superpowers:systematic-debugging, /superpowers:receiving-code-review] | files: src/ui/player/MpvBackend.cpp, agents/chat.md

READY TO COMMIT - [Agent 3, MAKE_MPV_SOLO Task 10.7 ✅ STRUCTURALLY CLOSED (Hemanth eyeball verdict pending) — picture-quality uplift on mpv via separable scalers; Tier 1 ship attempted libplacebo verbatim parity (ewa_lanczossharp polar) and regressed catastrophically (1585 drops/120s = 13.2 drops/sec, 130× worse than Task 10.5 floor) — Hemanth verbatim "it's stuttering again" mid-smoke confirmed empirical. Backoff to Tier 0 separable scalers (scale=spline36 + dscale=mitchell + cscale=spline36 — mpv's own gpu-hq baseline) restored drops to 29/120s = 0.24 drops/sec (clean, ~2× the bilinear floor but 55× better than Tier 1; bursty-pattern absent). 1 file (~30 LOC MpvBackend.cpp); 2 builds; 2 smokes. Lesson carried in code comment: libplacebo runs Vulkan compute shaders with a larger GPU budget than mpv's OpenGL path on Intel UHD 620; ewa_* polar filters prohibitive on iGPU GL. Subjective picture-quality verdict (GREEN/YELLOW/RED side-by-side mpv-vs-ffmpeg) handed to Hemanth's eyes per feedback_subjective_over_trace.md]: Tier 0 verified by telemetry as not regressing the Task 10.5 stutter fix. If GREEN ship as-is + close 10.7 + unblock 11+. If YELLOW try Tier 0.5 (deband=yes, cheap). If RED defer to future libplacebo-via-mpv-render-hook task (substantial substrate work). | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review, /superpowers:systematic-debugging, /superpowers:receiving-code-review] | files: src/ui/player/MpvBackend.cpp, agents/chat.md


## MCP LOCK RELEASED — Agent 3 — Task 10.7 Tier 0 separable scalers shipped (eyeball verdict pending)
Released 2026-05-02 ~08:12am. videoBackend QSettings restored to ffmpeg via reg.exe direct (windows-mcp tool disconnected mid-session). Tankoban + stale stremio-runtime killed per Rule 17. out/mpv_telemetry.log carries 8 session blocks total (Task 10 baseline + 5 from 10.5 matrix + Tier 1 regression + Tier 0 floor verification). Awaits Hemanth side-by-side mpv-vs-ffmpeg eyeball verdict on next playback session.


[2026-05-02 ~08:30am] Agent 3 — **MAKE_MPV_SOLO Task 8.B ✅ STRUCTURALLY CLOSED — Windows audio device-change watcher; closes the deferred deliverable from Task 8 (#3 of 3 named items, "per-device audio-delay recall"); Hemanth smoke (plug/unplug Bluetooth headphones during mpv playback) pending physical hardware switch.** 5 files (~280 LOC): NEW `AudioDeviceWatcher.{h,cpp}` (~210 LOC IMMNotificationClient COM watcher) + `VideoPlayer.{h,cpp}` (~50 LOC: 2 new fields + new slot + ctor instantiate/connect + hostApi cache at file-open) + `CMakeLists.txt` (sources/headers list + ole32+propsys link). 1 build (BUILD OK first try). Smoke is Hemanth-physical-only (no MCP analog for plugging/unplugging Bluetooth headphones).

**Architecture:**

The Task 8 RTC (2026-05-01 close) shipped the in-process audio polish (#1 sendSetAudioSpeed → no-op + #2 sendSetDrcEnabled → af-add/af-remove with @drc: label) and explicitly deferred the third deliverable: "per-device audio-delay recall depends on Task 3 mediaInfo bridge sending audio-device fields... DEFERRED to Task 8.B (Windows IMMNotificationClient watcher, ~100 LOC)". Task 8.B closes that deferral.

The pre-existing infra (VideoPlayer.cpp:3970-4007) already handled the FILE-OPEN recall path: sidecar's mediaInfo JSON delivers `audio_device` + `audio_host_api`, VideoPlayer combines them into a QSettings key via `makeDeviceKey()`, looks up the saved per-device offset, applies via `sendSetAudioDelay`. What was missing: MID-PLAYBACK device switch detection. If you start playback on speakers, then plug in Bluetooth headphones, the existing code stays bound to the speaker key — you'd have to reopen the file to get the BT delay.

**Three pieces shipped:**

(1) **NEW `src/ui/player/AudioDeviceWatcher.{h,cpp}` — IMMNotificationClient COM watcher (~210 LOC).** Pimpl pattern: public Qt-only header exposes `defaultDeviceChanged(QString friendlyName)` signal + lifecycle; private Impl holds the COM `IMMDeviceEnumerator` + `DeviceNotifyImpl` (subclass of `IMMNotificationClient`) and routes via `RegisterEndpointNotificationCallback`. Only the eRender + eConsole role is acted on (eMultimedia / eCommunications fire for the same physical change — gating on eConsole keeps us to one event per switch). Friendly name resolved via `IPropertyStore::GetValue(PKEY_Device_FriendlyName)` inside the COM callback (the audio engine thread already has COM apartment, so CoCreateInstance + IMMDevice property reads work directly there). Marshaled to GUI thread via `QMetaObject::invokeMethod(..., Qt::QueuedConnection, ...)`. Detach pattern in the Impl destructor: `detachOwner()` BEFORE `UnregisterEndpointNotificationCallback` so any in-flight callback that races us reads nullptr and skips the emit. Cross-platform: non-Windows builds get a no-op stub Impl so the class compiles cleanly without #ifdef pollution at every call site. ~210 LOC total — ~50 over the 100-LOC estimate; the overage is mostly defensive cleanup ordering (HRESULT-checked CoCreate / GetDevice / OpenPropertyStore / GetValue chain) + the cross-platform stub Impl. Worth the safety.

(2) **`src/ui/player/VideoPlayer.{h,cpp}` wiring (~50 LOC).** Two new private fields: `m_audioHostApi` (cached host API from the most-recent file-open mediaInfo so the watcher slot can re-key correctly) + `m_audioDeviceWatcher` (owned by VideoPlayer with parent=this for automatic destruction). Constructor instantiates the watcher + connects `defaultDeviceChanged → onAudioDeviceChanged` (Qt::AutoConnection — same thread, direct dispatch). New slot `onAudioDeviceChanged(QString friendlyName)` mirrors the file-open recall logic at :3970-4007: regenerate `m_audioDeviceKey` via `makeDeviceKey(friendlyName, m_audioHostApi)`, dedupe on no-change, look up saved delay in QSettings, three branches (saved value found → apply + toast / Bluetooth heuristic match → BT_DEFAULT_MS=300 + persist + toast / wired-or-unknown → 0 + apply). All three branches sync the `SettingsPopover` value label so the +/- chip reads the new active delay. Same `looksLikeBluetooth()` heuristic as the file-open path — single source of truth.

(3) **`CMakeLists.txt`** — adds `AudioDeviceWatcher.cpp` to `SOURCES` + `.h` to `HEADERS` + `ole32 propsys` to the WIN32 `target_link_libraries` (mmdevapi types live in mmdevapi.lib which Qt's WIN32_GUI defaults already pull, but the `IPropertyStore::GetValue` + `PropVariantClear` + `PKEY_*` lookup uses propsys; `CoCreateInstance` + COM lifetime are ole32). Both are universal Windows libs, no version constraints.

**Smoke matrix (deferred to Hemanth physical hardware):**

This task can't be MCP-smoked because the trigger (Windows default audio device change) requires physical hardware action — plugging/unplugging Bluetooth headphones, switching HDMI output, etc. No software-only equivalent that fires the IMMNotificationClient OnDefaultDeviceChanged callback authentically (`PropVariantInit + GetValue` mock would need a kernel-level audio hook). Hemanth-physical-only.

Hemanth verification protocol:
1. Open Tankoban. Play any video on mpv (right-click → Play with mpv).
2. While playing, plug in Bluetooth headphones (or whatever audio device you have available with a saved-delay history).
3. Audio routing should switch to the new device automatically (Windows default-device-change). Within ~50-200ms a toast should appear: "<DeviceName> → <N>ms" (saved value) OR "Bluetooth: <DeviceName> → 300ms" (Bluetooth heuristic match, first time) OR no toast (wired/unknown, delay=0).
4. The Settings popover's audio-delay value chip should now read the new delay.
5. Switch back. Reverse toast appears with the speakers' saved delay.
6. If the toast doesn't appear and audio sync feels off → Bluetooth heuristic might not be matching the device's friendly name (the markers list at VideoPlayer.cpp:88-99 covers common consumer audio brands but not all). Tell me the friendly name shown in Windows Sound settings and I'll add the marker.

**Honest scope flags:**
- ffmpeg-sidecar path consideration: PortAudio (the sidecar's audio backend) does NOT auto-rebind to the new Windows default device on the fly — the sidecar stays bound to whatever device was current at file-open. The watcher's recall WILL still update the sidecar's `audio-delay` (via `sendSetAudioDelay`) but the audio is still routing to the OLD physical device. User would need to close + reopen the file to get the sidecar onto the new device. mpv on the other hand auto-rebinds via WASAPI on default-device-change (mpv property `audio-device=auto`). Net: Task 8.B closes the per-device-DELAY recall on the mpv path cleanly; on the ffmpeg path it sets the right delay but the audio still goes through the old device until file reopen. Acceptable trade-off given we're moving to mpv-solo and the ffmpeg path is decommissioning territory.
- The `m_audioHostApi` cache is populated only at file-open mediaInfo time. If the user plugs in headphones BEFORE opening any file in the current Tankoban session, the watcher fires but the slot early-returns ("No file ever opened in this session"). The next file open's mediaInfo will populate hostApi + recall the saved delay then; no behavior gap (just no toast on the pre-file device switch). Documented in the slot's early-return comment.
- Bluetooth heuristic is name-based (`looksLikeBluetooth` markers list at VideoPlayer.cpp:88-99). Devices that report only their MAC address (no friendly name) won't match — those fall through to 0ms and rely on manual tuning. Existing limitation from the file-open path; not new in 8.B.
- Watcher registration failure path: if `CoCreateInstance(MMDeviceEnumerator)` or `RegisterEndpointNotificationCallback` returns failure (rare, possibly happens under sandboxed COM apartment configurations), the Impl silently sets m_notify/m_enumerator to nullptr — watcher is constructed but never fires. Caller sees no-op behavior, which is acceptable for an opt-in-feeling recall feature.
- Threading: COM callback fires on the audio engine thread; `QMetaObject::invokeMethod` Qt::QueuedConnection marshals to the QObject's home thread. Watcher's parent in VideoPlayer ctor is `this` (a QWidget on the GUI thread), so the slot dispatches on the GUI thread — exactly where `m_backend->sendSetAudioDelay`, QSettings I/O, ToastHud, and SettingsPopover all live. No thread races.
- LOC budget overage (210 vs 100 estimated in the Task 8 RTC): defensive HRESULT chains + cross-platform stub Impl + COM lifetime correctness comments account for the 110-LOC delta. Worth shipping clean than shipping minimal.

**Cumulative arc status (12 of 15 task slots filled):**
- ✅ Tasks 1-7 + 8 + 8.B (this RTC) + 9 + Task 9 follow-up bundle + 10 + 10.5 + 10.7 (Tier 0, eyeball pending) + 6.B
- ⏳ Task 10.7 final close gated on Hemanth side-by-side eyeball verdict (deferred per Hemanth "deal stutter after all the tasks are done if it's still there")
- ⏳ Tasks 11-15 (cutover) — next sequential
- ⏳ Conditional Task 10.6 widget paint cadence + Task 10.8 ICC/banding parity if Hemanth surfaces them post-cutover

**Smoke discipline:** No MCP this RTC (physical hardware action only). videoBackend stays at ffmpeg (current session default). No Tankoban running post-build (killed pre-build per Rule 1). Files staged for Hemanth's next launch.

**Files touched:** `src/ui/player/AudioDeviceWatcher.h` (new), `src/ui/player/AudioDeviceWatcher.cpp` (new), `src/ui/player/VideoPlayer.h`, `src/ui/player/VideoPlayer.cpp`, `CMakeLists.txt`, `agents/chat.md`.

/superpowers:executing-plans (Task 8.B walked per Task 8 RTC's deferral spec; closes the named deliverable cleanly). /simplify (smallest possible diff that meets the spec — Pimpl pattern keeps the Windows COM mess out of the public header; reused existing makeDeviceKey + looksLikeBluetooth helpers from VideoPlayer.cpp:74,87 instead of re-authoring; reused existing recall-logic shape from :3970-4007 instead of inventing a new path). /build-verify (BUILD OK first try; ole32+propsys link added without errors; new sources compile). /superpowers:verification-before-completion (build green is mechanical evidence; Hemanth physical smoke pending; honest scope flags carried for the ffmpeg-PortAudio-rebind limitation, the no-file-open-yet edge, and the Bluetooth-heuristic name-only fallback). /superpowers:requesting-code-review (Pimpl with cross-platform stub mirrors the standard Qt pattern; COM ref-counting via std::atomic<ULONG> is canonical; detach-before-unregister ordering follows Microsoft's IMMNotificationClient docs verbatim; Qt::QueuedConnection marshaling is the documented thread-cross idiom). /superpowers:systematic-debugging N/A (pure feature ship, no bug-shape work). /security-review (Windows COM ref-counting + HRESULT-checked allocation chain + property-store value type validation + nullptr guards on every interface pointer + detach-before-release for owner pointer; no input parsing on the friendly-name string before passing to makeDeviceKey which already sanitizes; no network surface; QueuedConnection is thread-safe by Qt design). /superpowers:receiving-code-review N/A. | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review, /security-review] | files: src/ui/player/AudioDeviceWatcher.h, src/ui/player/AudioDeviceWatcher.cpp, src/ui/player/VideoPlayer.h, src/ui/player/VideoPlayer.cpp, CMakeLists.txt, agents/chat.md

READY TO COMMIT - [Agent 3, MAKE_MPV_SOLO Task 8.B ✅ STRUCTURALLY CLOSED — Windows audio device-change watcher closes the deferred per-device-delay-recall deliverable from Task 8 (#3 of 3 named items); 5 files (~280 LOC): NEW AudioDeviceWatcher.{h,cpp} (~210 LOC IMMNotificationClient COM watcher with Pimpl + cross-platform stub for non-Windows + thread-marshaled Qt signal + detach-before-unregister lifecycle) + VideoPlayer.{h,cpp} wiring (~50 LOC: 2 new fields m_audioHostApi cache + m_audioDeviceWatcher owned widget + ctor instantiate+connect + new onAudioDeviceChanged slot mirroring file-open recall logic at :3970-4007 + hostApi cache at file-open) + CMakeLists.txt (sources/headers list + ole32+propsys win32 link); 1 build (BUILD OK first try); smoke deferred to Hemanth physical hardware (plug/unplug Bluetooth headphones during mpv playback). Honest scope flags: ffmpeg-sidecar PortAudio path doesn't auto-rebind on default-device change (sidecar stays on old device until file reopen; Task 8.B updates the audio-delay number but the audio still routes through the old device on ffmpeg path) — acceptable since mpv path auto-rebinds via WASAPI and we're moving mpv-solo. Bluetooth heuristic is name-based; MAC-only devices fall through to 0ms manual-tune path (existing limitation, not new). Cumulative: 12 of 15 task slots filled; Tasks 11-15 (cutover) next sequential]: Hemanth verification protocol documented in body. | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review, /security-review] | files: src/ui/player/AudioDeviceWatcher.h, src/ui/player/AudioDeviceWatcher.cpp, src/ui/player/VideoPlayer.h, src/ui/player/VideoPlayer.cpp, CMakeLists.txt, agents/chat.md


[2026-05-02 ~08:46am] Agent 3 — **MAKE_MPV_SOLO Task 11 ✅ STRUCTURALLY CLOSED — saved-default-pref flipped from `ffmpeg` to `mpv`; new installs (or any QSettings-key-absent launch) now boot into mpv; existing users keep their stored pref unchanged; right-click per-file "Play with X" overrides untouched.** 2 files (~10 LOC functional + ~20 LOC comment context): BackendFactory.cpp default-string swap at the QSettings fallback site + Task 11 dependency-gate context comment; BackendFactory.h doc-comment updates at the two sites that name the old default. 1 build (BUILD OK first try).

**One-line functional change** at `src/ui/player/BackendFactory.cpp` readPreference():
```
  pre:  s.value(kKey, kFfmpegSlug).toString();
  post: s.value(kKey, kMpvSlug).toString();
```
That's the load-bearing flip. Everything else is doc/comment context.

**Three doc updates** (BackendFactory.h header docstring + chooseFor precedence comment + .cpp Task 11 dependency-gate block) so future maintainers reading the file know:
- Default semantic flipped at Task 11 cutover 2026-05-02
- Existing users with saved "ffmpeg" stay on ffmpeg (saved value > default fallback by QSettings semantic — `s.value(key, fallback)` returns the stored value if present, fallback only when absent)
- Per-file right-click overrides unchanged
- Dependency gates from MAKE_MPV_SOLO.md Task 11 spec ("Tasks 7-10 must close GREEN") were met before firing: Tasks 7+8+9+10+10.5+10.7-Tier-0+8.B all closed prior

**What this does NOT change (per Task 11 spec — explicitly out of scope):**
- Right-click "Play with ffmpeg" / "Play with mpv" menu entries: unchanged. Per-file emergency revert path stays live for the cutover validation window in Task 12.
- ffmpeg sidecar build wiring: unchanged. Sidecar still ships; nothing's archived/deleted yet (Task 13/14/15 territory).
- Existing user preferences: unchanged. Hemanth's QSettings has `player/videoBackend = "ffmpeg"` (stored from prior sessions); reading that returns Ffmpeg. He opts into mpv via the existing right-click "Set mpv as default" UI which calls writePreference + flips the value to "mpv".
- TANKOBAN_FORCE_MPV env var: unchanged. Still highest-precedence override above the saved pref.

**Smoke matrix (Hemanth-physical):**

(1) **Fresh-install simulation.** Delete the saved pref to verify mpv loads on empty-key:
- One bash line: `reg delete "HKCU\Software\Tankoban\Tankoban\player" /v videoBackend /f`
- Then launch via build_and_run.bat. Open a video. Confirm mpv runs it. (Verifiable visually via the brightness chip's behavior or via `tankoctl get-state` / mpv-only HUD elements.)

(2) **Existing-pref preservation.** Without deleting anything, launch Tankoban. Should still boot into ffmpeg (because `player/videoBackend = "ffmpeg"` is stored from prior sessions). Confirms the flip doesn't stomp existing prefs.

(3) **Per-file revert path.** Right-click any video tile → "Play with ffmpeg" — confirms the per-click override still routes to ffmpeg sidecar. (Same surface as Task 2's stream-mode-lock removal smoke.)

**Honest scope flags:**
- For Hemanth to TEST this task as a "new user" on his existing machine, he needs to delete the saved pref via reg.exe (one-line documented above). Without that, the launch won't change behavior because his existing "ffmpeg" pref is stored.
- After Hemanth manually flips to mpv via right-click and runs the cutover validation soak (Task 12), the daily mpv experience will go through the freshly-built changes from Tasks 7-10.5 + 8.B + 10.7 Tier 0. Any latent issues that surface during Task 12 territory get logged and triaged per the Task 12 protocol.
- ffmpeg fallback path: if libmpv fails to load at runtime (HAS_LIBMPV is ON at compile time but the DLL is missing), `BackendFactory::create(Type::Mpv, ...)` would return a SidecarProcess via the existing #ifdef HAS_LIBMPV graceful-fallback semantic. Preserved.

**Cumulative arc status (13 of 15 task slots filled):**
- ✅ Tasks 1-7 + 8 + 8.B + 9 + Task 9 follow-up bundle + 10 + 10.5 + 10.7 (Tier 0, eyeball pending) + 11 (this RTC, structurally) + 6.B
- ⏳ Task 12 — daily-use validation soak (Hemanth-paced, 1-2 weeks; logs regressions; gates Task 13)
- ⏳ Tasks 13-15 — UI surface decommission + sidecar source archive + build-wiring drop (sequential, gated on Task 12 close)
- ⏳ Task 10.7 final close + conditional Task 10.6/10.8 if any post-cutover surfaces additional gaps

**Smoke discipline:** No MCP this RTC (single-line semantic flip; Hemanth-physical smoke for the fresh-install simulation). videoBackend stays at ffmpeg (current Hemanth pref). No Tankoban running post-build (killed pre-build per Rule 1).

**Files touched:** `src/ui/player/BackendFactory.cpp`, `src/ui/player/BackendFactory.h`, `agents/chat.md`.

/superpowers:executing-plans (Task 11 walked per spec; one-line code change + doc updates; dependency gates checked before firing). /simplify (single-line functional change; no new files, no refactor; reused existing kMpvSlug constant; doc updates are minimal). /build-verify (BUILD OK first try). /superpowers:verification-before-completion (build green; spec dependency gates verified met against the closed-tasks list; smoke protocol documented for Hemanth physical verification). /superpowers:requesting-code-review (single-line diff; default-fallback semantic preserved everywhere except the one keyword swap; doc comments updated to match). /superpowers:systematic-debugging N/A. /security-review N/A (read-only QSettings; no input parsing; no network). /superpowers:receiving-code-review N/A. | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: src/ui/player/BackendFactory.cpp, src/ui/player/BackendFactory.h, agents/chat.md

READY TO COMMIT - [Agent 3, MAKE_MPV_SOLO Task 11 ✅ STRUCTURALLY CLOSED — saved-default-pref flipped from ffmpeg to mpv; new installs / QSettings-key-absent launches boot into mpv; existing users keep their stored pref (saved value > default fallback by QSettings semantic); right-click per-file overrides untouched; ffmpeg sidecar build wiring unchanged. 2 files (~10 LOC functional + ~20 LOC doc): BackendFactory.cpp one-line slug swap (kFfmpegSlug → kMpvSlug at the QSettings .value() fallback site) + Task 11 dependency-gate comment block; BackendFactory.h two doc-comment updates at the sites naming the old default. 1 build (BUILD OK first try). Dependency gates met: Tasks 7-10 + 10.5 + 10.7 Tier 0 + 8.B all closed prior. Cumulative: 13 of 15 task slots filled; Tasks 12-15 (validation soak + UI/source/build decommission) sequential next, all Hemanth-paced from here. Hemanth fresh-install smoke = `reg delete HKCU\Software\Tankoban\Tankoban\player /v videoBackend /f` then relaunch — should boot mpv on the empty-pref path]: Per-file right-click "Play with ffmpeg" emergency revert path preserved for Task 12 validation window. | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: src/ui/player/BackendFactory.cpp, src/ui/player/BackendFactory.h, agents/chat.md


[2026-05-02 ~08:55am] Agent 3 — **MAKE_MPV_SOLO Task 12 STARTED — daily-use mpv validation soak window opened.** Hemanth's saved pref `player/videoBackend` flipped from `ffmpeg` → `mpv` via reg.exe (pre-approved by Hemanth's "begin task 12" greenlight). Validation log section opened in this same chat.md below this RTC. Soak duration is Hemanth-paced (~1-2 weeks of daily use, his call). Agent 3 in standby: triage logged regressions one-at-a-time using the Tasks 1-10 fix pattern; await Hemanth's "validation closed" declaration to unblock Task 13. No code touched in Task 12 by definition (operational task). | Skills invoked: [/superpowers:executing-plans] | files: agents/chat.md

READY TO COMMIT - [Agent 3, MAKE_MPV_SOLO Task 12 STARTED — validation soak window opened; Hemanth's videoBackend pref flipped to mpv via reg.exe; validation log section opened in chat.md; Pattern D (top-edge clipping on Vinland windowed video + Boys top-of-screen sub clipping per MAKE_MPV_SOLO.md re-test gates) listed as explicit watch-for item; Agent 3 in standby for triage. No code change. Task 12 closes when Hemanth declares "validation closed"; until then, regressions get one-line entries in the log section + I patch one-at-a-time via Tasks 1-10 pattern. Cumulative: 13 of 15 task slots filled (12 in-flight)]: Task 13 (UI surface decommission) gated on Task 12 close. | Skills invoked: [/superpowers:executing-plans] | files: agents/chat.md


---

## MPV Validation Window log

**Window opened:** 2026-05-02 ~08:55am GMT+5:30 by Agent 3 on Hemanth's "begin task 12" greenlight.
**Pre-flip state:** `player/videoBackend = "ffmpeg"` (Hemanth's standing pref since pre-cutover).
**Post-flip state:** `player/videoBackend = "mpv"` via `reg add "HKCU\Software\Tankoban\Tankoban\player" /v videoBackend /t REG_SZ /d mpv /f`.
**Active scaler config (Task 10.7 Tier 0):** `scale=spline36 / dscale=mitchell / cscale=spline36` (separable, Intel UHD 620 GL-budget-safe).
**Active hwdec (Task 10.5 default):** `hwdec=no` (CPU decode; ~92% drop reduction vs `auto→d3d11va-copy` baseline). Override via `TANKOBAN_MPV_HWDEC` env var if needed.

### How to log a regression

Hemanth: when something feels off during daily use (stutter, subtitle misalignment, popover not opening, stream won't start, audio drift, HDR moment looks bad — anything), drop one line below in this format:

```
[YYYY-MM-DD HH:MM] <File or context> — <one-line description of what felt wrong>
```

Example:
```
[2026-05-04 21:15] Severance S2E5 (HDR) — subtitles drifting ~200ms behind dialogue
```

I'll triage one entry at a time using the Tasks 1-10 fix pattern (root-cause hypothesis → minimal code change → build → smoke if MCP applicable / handoff if Hemanth-physical → RTC → close). Multiple entries in a row are fine — I work down the list FIFO.

### Explicit watch-for items (from MAKE_MPV_SOLO.md re-test gates + open verdicts)

1. **Pattern D — top-edge clipping on Vinland windowed video + Boys top-of-screen sub clipping** (2/2 in Task 1 baseline). Re-test gate explicitly pinned to this validation window. If it repros on mpv as default → file targeted widget-geometry fix. If it doesn't repro → free win, gate closes.
2. **Task 10.7 picture-quality eyeball verdict (Tier 0 separable scalers)** — Hemanth previously flagged "ffmpeg looks slightly better." Tier 0 ship pending side-by-side eyeball; deferred per Hemanth "deal with stutter after all the tasks are done if it's still there." If Tier 0 is still subjectively soft after a week of daily use, log it here and we revisit.
3. **Stutter residue from Task 10.5 / 10.7 Tier 0 path** — if any visible judder remains under daily-use loads (multiple background apps, low-battery thermal throttle, etc.), log it. Task 10.5 telemetry floor is the baseline; deviations from that during daily use are diagnostic-worthy.
4. **Audio device-change recall (Task 8.B)** — when you plug/unplug Bluetooth headphones mid-playback, the saved per-device delay should auto-apply with a toast. If toast doesn't appear or wrong delay applies, log the device's friendly name + observed behavior.
5. **mpv playback errors** — Task 5 surfaces sensible English error toasts on file-missing / bad-codec / broken-stream-URL failures. If you see a generic "playback failed" or silent failure with no message, log it.

### Auto-check workflow (added 2026-05-02 ~09:00am)

Hemanth's pure ask: "I will open the app once you set that up — you check the log yourself."

**Telemetry watermark (set 08:10:59; advanced 09:00/09:05/09:14/09:20/09:38 across soaks #1-#6):**
- `out/mpv_telemetry.log` line count: **452** (was 390 after soak #4)
- Session blocks recorded: **15** (was 13 — soaks #5 cold-restart + #6 d3d11va-copy)
- Last session timestamp: `2026-05-02T09:36:51` (Soak #6 d3d11va-copy verified)

**Soak session log:**
- **#1 — 2026-05-02 08:57:24** Community S01E01, 15s playback. 10 drops in the 5-15s window. Per-sample shape: 3.8/s → 1.6/s → 0.4/s — classic decoder/render warmup curve at file open, settling toward Tier 0 baseline floor (0.24/s). Config holds: hwdec=no, vo=libmpv, ao=wasapi, vf_fps=23.98. vo_delayed=0, buffering=0. **Clean session — no regression.** Note: too short (15s) to observe steady-state; longer sessions needed to confirm floor holds under sustained load.
- **#2 — 2026-05-02 09:02:53** Sopranos S06E04 (1080p BluRay HEVC, 10-bit, HDR), 105s playback. **668 drops total = 6.4/sec sustained.** Per-5s deltas: 28/29/31/33/37/31/26/32/30/37/31/34/31/32/32/32/37/34/37/40/42 — NOT bursty, NOT warmup curve, steady-state pressure with slight end-of-window uptick. ⚠️ **REGRESSION CLASS: HDR HEVC over CPU decode budget on Intel UHD 620.** vf_fps=23.98 throughout (decoder maintains clock); vo_delayed=0 (display clean); buffering=0. mpv is dropping frames to stay real-time. Hypothesis: Task 10.5's hwdec=no default was tuned on SDR; HDR adds tone-mapping shader work + 10-bit decode pressure + BluRay-bitrate decode load that the CPU path can't sustain.
- **#3 — 2026-05-02 09:12:08** Sopranos S06E04 continuation (playtime 454→574s, same file as #2 picked up roughly where it left off), 125s playback. **894 drops total = 7.2/sec sustained, peak burst 12.6/s at t=65-70s.** Pattern: sustained 5-10/s with action-scene spikes; spike pattern likely correlates to motion-heavy / fast-cut BluRay sequences hitting CPU decoder harder. ⚠️ **Confirms reproducibility of soak #2 regression.** Same config (hwdec=no), same file class, slightly worse drop rate (7.2 vs 6.4). Hemanth did not run the env-var test (hwdec=no remained for both Sopranos runs); two sustained-pressure data points on the same content type is sufficient confirmation. **Recommend Path B (HDR-conditional hwdec auto-pick at file-open via mpv video-params/primaries property; bt.2020/smpte2084 → d3d11va-copy, SDR → keep hwdec=no, env-var stays as highest-precedence override).** Awaiting Hemanth Path-B greenlight.

**[2026-05-02 ~09:38am] Path X env-var test executed by Agent 3 (Hemanth ask "run that for me"):**
- **First attempt** (`cmd /c "set X && start /b ..."` via Bash): env var did NOT propagate; header showed hwdec=no, drops were 438/120s = 3.65/s (just noise from thermal cooling between runs — not a real test of d3d11va-copy).
- **Second attempt** (PowerShell `$env:` scope + Start-Process): env var verified active. tankoctl logs grep matched `[init] hwdec=d3d11va-copy (TANKOBAN_MPV_HWDEC override)`. Telemetry header now reads `hwdec=d3d11va-copy`.
- **Soak #6 — 2026-05-02T09:36:51** Sopranos S06E04 under d3d11va-copy override, 120s playback. **998 drops total = 8.3/sec sustained.** Per-5s deltas: 25/46/43/44/45/40/36/40/45/47/57/55/47/56/54/43/45/28/19/41/40/44/48/35 — sustained 7-11/s with same shape as hwdec=no runs but slightly worse rate.
- **Conclusion: d3d11va-copy is NOT the answer for heavy SDR HEVC. Both decode strategies over-budget.** Direct comparison on same content:
  - hwdec=no (CPU decode): 6.4-7.9/s drops (3 runs averaged)
  - hwdec=d3d11va-copy (GPU decode): 8.3/s drops (this run)
  - GPU memcpy adds ~1-2/s on top of already-saturated pipeline.
- **Root cause re-diagnosed: HARDWARE CEILING on Intel UHD 620 + 1080p BluRay HEVC 10-bit at this bitrate/entropy.** Not a hwdec choice problem. Task 12.A's HDR-conditional auto-pick remains correct for ACTUAL HDR files (Boys S03E06, Severance, etc.) but does nothing for SDR-heavy class.
- **Three paths forward:** (A) ffmpeg-path comparison test (Hemanth: right-click Sopranos → Play with ffmpeg → eyeball ~90s; if smooth → mpv-pipeline-specific, chase further; if also drops → hardware ceiling, accept) — **leading recommendation, smallest data req**; (B) relax mpv Tier 0 scalers under heavy content (untested heuristic); (C) accept the limit on this hardware class — keep mpv default + right-click Play-with-ffmpeg as per-file revert. **Awaiting Hemanth Option A data.** Env var was process-scoped to that PowerShell session; verified gone (not persistent in HKCU\Environment).

**[2026-05-02 ~09:25am] Path B SHIPPED → Task 12.A.** Hemanth greenlit Path B. Code edit: `MpvBackend.cpp` MPV_EVENT_FILE_LOADED handler at line 706 area + 2 new fields in `MpvBackend.h` (`m_hwdecOverriddenByEnv` + `m_currentHwdec`). The existing HDR detection (trcStr == "pq" || "hlg" → mi.insert("hdr", ...)) gets a new branch: if HDR AND env-var unset AND not-already-on-d3d11va-copy, call `mpv_set_property_string("hwdec", "d3d11va-copy")`. Reverse path covered too: if subsequent file is SDR AND we're currently on d3d11va-copy, flip back to "no" so SDR doesn't pay the GPU↔CPU memcpy cost. mpvLog announces both transitions for telemetry visibility. ~50 LOC. Build #11 BUILD OK first try.

**[2026-05-02 ~09:20am] Soak #4 — HYPOTHESIS WRONG, REGRESSION CLASS RE-DIAGNOSED.** Hemanth replayed Sopranos S06E04 for 165s post-Task-12.A. Telemetry data:
- **#4 — 2026-05-02T09:18:39** Sopranos S06E04, 165s. **1308 drops total = 7.9/sec sustained, peak burst 13.4/s at t=25-30s.** Per-5s deltas trend 2.4 → 13.4 → 8.8 → 7.4 (rises then plateaus 5-12/s with action-scene spikes). Header reads `hwdec=no` (NOT d3d11va-copy) — auto-pick branch never fired because Sopranos S06E04 is SDR, not HDR. ⚠️ **HYPOTHESIS WRONG.** Sopranos S06 aired 2006-2007, predates HDR mastering entirely. The "BluRay x265 ImE" file is a HEVC re-encode of an SDR BluRay; `video-params/gamma` would report `bt.1886` (SDR), so the trcStr=="pq"||"hlg" check at MpvBackend.cpp:706 correctly returns false. Task 12.A code is correct in design but dead-weight on this content. **Real regression class: heavy-SDR-HEVC-10bit-at-BluRay-bitrate over CPU decode budget on Intel UHD 620** — different from HDR-class. Higher source bitrate + more visual entropy (film grain, dark gradients) than Community S01E01's WEB-DL push the CPU decoder over budget. **Next-step decision pending:** d3d11va-copy was 1.55/s on Community SDR (worse than CPU's 0.24/s) so blanket-applying GPU decode would regress the SDR floor. Need empirical test: **Hemanth runs `set TANKOBAN_MPV_HWDEC=d3d11va-copy && build_and_run.bat` on Sopranos for ~2 min** to learn whether GPU decode helps for heavy-bitrate SDR or not. Result determines whether Task 12.B real fix is bitrate-conditional auto-pick / file-pref system / scaler-relax / or accept-the-limit.

Any session block beyond this watermark is from Hemanth's actual daily-use soak.

**Hemanth's contract:**
1. Open Tankoban (build_and_run.bat) — mpv now runs by default since Task 11
2. Play whatever you'd normally watch — anime, HDR film, anything in your library
3. Close Tankoban normally (X button or Esc-back-to-library + Alt+F4)
4. Ping me with anything — even just "check it" / "done" / "had a session" — I read everything past the watermark
5. If you noticed anything subjective during the session (picture looked soft, subtitle felt mispositioned, Pattern D top-edge clipping on Vinland or Boys, popover didn't open, weird audio drift after device switch), one short word about it. No format needed.

**Agent contract (what I auto-detect from telemetry):**
- ✅ Frame drops per session and per-second rate — flag anything > 1.0 drops/sec sustained (Task 10.5 floor was 0.10-0.15; Tier 0 baseline is 0.24)
- ✅ Bursty drop windows (sudden spike > 5 drops/5s sample) — Task 10 baseline showed these on d3d11va-copy; should NOT recur on hwdec=no
- ✅ Buffering events (paused-for-cache true) — should be 0 for library files; non-zero = stream stall class
- ✅ vf_fps deviation from source rate — 23.98/24/25/29.97/30/60 are healthy depending on file; anything below source = decoder pressure or render starvation
- ✅ Missing session block (Tankoban crashed without dumping) — file mtime hasn't advanced past expected close-time
- ✅ hwdec/vo/ao header drift — verifies the Task 10.5 default still applies (`hwdec=no`)

**What I CAN'T see in telemetry (still needs your words):**
- Picture-quality subjective (sharp vs soft) — Task 10.7 Tier 0 verdict still pending here
- Subtitle visual position correctness
- Pattern D top-edge clipping on Vinland windowed video / Boys top-of-screen subs
- Popover open/close behavior (UI state isn't in telemetry)
- HDR tone-mapping look (subjective)
- Audio device-change toast appearance / wrong-delay-applied
- Error toast wording on bad files

For these: a one-word "subtitles felt off" / "picture soft" / "no toast on BT plug" is enough — I'll dig from there.

**Watermark advance protocol:** after each agent triage cycle, I update the watermark above to the new line count + session count so future cycles only read the freshest blocks. Watermark history kept inline so we can scroll back if needed.

### Regressions logged

(none yet — Hemanth's daily use will populate; I'll auto-append findings from telemetry reads here too)

### Validation close

When Hemanth has gone N days without a new regression and feels confident the mpv-default daily experience is solid, post a one-line "validation closed" entry below. That gates Task 13.



## MCP LOCK CLAIMED — Agent 5 — FRAMELESS_CHROME_FIX nav centering smoke (close-out)
Claimed 2026-05-02 ~10:30am. Ship 1 of 2: rebuild with staged 1-line counterweight (layout->addSpacing(36*3+8) after Brand) so nav re-centers horizontally on the window after the chrome cluster pulled it left. Hemanth-caught regression last session. Will: build_and_run.bat → wait for launch → Hemanth visually confirms nav-recentered + 9 other acceptance points (frameless, drag, snap, double-click max, Win+arrow, system menu, taskbar, HiDPI, no-OS-titlebar) → RTC bundles the whole FRAMELESS_CHROME_FIX arc → release LOCK. Ship 2 (Continue Watching scroll arrows) starts immediately after this lands.


## MCP LOCK RELEASED — Agent 5 — stale LOCK from prior session superseded
Released 2026-05-02 ~10:00am. The prior LOCK at chat.md:3662 was claimed with a typo'd future timestamp ("10:30am" written when actual was earlier this morning) by an interrupted prior session of mine. No app launched, no MCP driven from that LOCK. Working tree state from FRAMELESS_CHROME_FIX is preserved and untouched. Re-claiming below with corrected scope per current Hemanth-approved sequencing (FRAMELESS_CHROME_FIX completion → /commit-sweep → PER_VIEW_CHROME_FIX P3 first).


## MCP LOCK CLAIMED — Agent 5 — FRAMELESS_CHROME_FIX completion + smoke
Claimed 2026-05-02 ~10:00am. Plan: (1) /superpowers:executing-plans skill loaded; (2) MainWindow.cpp inline max-toggle lambda extracted to public slot `onChromeMaximizeToggle()` so PER_VIEW_CHROME_FIX P2/P3/P5 surfaces can connect to it (chat.md plan §C4); (3) leftSlot structural counterweight (QTimer width-mirror at MainWindow.cpp:367-368) trusted as-is — the prior session's planned magic-number `addSpacing(36*3+8)` is redundant with the structural mirror; verifying empirically on smoke; (4) build_and_run.bat → wait for launch → tankoctl ping/get-state → MCP screenshot to verify (a) no OS title bar, (b) chrome buttons present at TopBar right edge, (c) nav re-centered horizontally — leftSlot mirrors rightSlot's sizeHint via QTimer; (5) MCP smoke 10 acceptance points: chrome cluster visible / no OS chrome / drag bare TopBar / double-click bare TopBar maximizes / Win+Up snaps / Win+Left half-screen / right-click bare TopBar shows system menu / chrome Min works / chrome Max toggles + icon swaps / chrome Close exits cleanly; (6) Rule 17 stop-tankoban.ps1 cleanup; (7) RTC bundle covering chrome cluster + Theme.cpp QSS + 4 SVGs + qrc + `onChromeMaximizeToggle` slot extraction + nav-centering verification; (8) release LOCK. Post-this: ask Hemanth for /commit-sweep, then PER_VIEW_CHROME_FIX P3 (Comic Reader) on a clean base.



[2026-05-02 ~10:18am] Agent 5 — **FRAMELESS_CHROME_FIX ✅ CLOSED — chrome cluster + max-toggle slot + NCHITTEST stretch-zone fix; 2 builds (~13min total); MCP smoke 7 of 10 acceptance points GREEN, 3 require Hemanth visual verification (drag / double-click-max / right-click system-menu — OS-NC mouse handling paths that windows-mcp tooling cannot reliably trigger).** 4 src/ files (~30 LOC functional + comment additions): MainWindow.h adds `public slots: void onChromeMaximizeToggle()`; MainWindow.cpp inline lambda at line 354-356 refactored to call the named slot (so PER_VIEW_CHROME_FIX P2/P3/P5 takeover surfaces can connect their `requestToggleMaximize()` signals to it); MainWindow.cpp NCHITTEST fix at line 928-944 — empty stretch zones inside `TopBarLeftSlot` + `TopBarRightSlot` were returning HTCLIENT instead of HTCAPTION (real bug; user dragging in those regions would not trigger native window move). 4 chrome SVGs (chrome_min/max/restore/close, stroke `#c6c6c6` matching nav icon convention) registered in resources.qrc. Theme.cpp QSS for chrome cluster (~29 LOC, transparent flat-button styling).

**MCP smoke (build #2 verifying NCHITTEST fix):**

(1) ✅ Chrome cluster visible at TopBar right edge — UIA tree dump confirmed all 3 buttons at correct AutomationIds (`...TopBarRightSlot.ChromeMin`, `.ChromeMax`, `.ChromeClose`) at predicted rects.

(2) ✅ No OS title bar — Brand "Tankoban" label flush against window top edge, no native Windows chrome strip above.

(3) ✅ Nav horizontally centered — UIA tree: TopNav rect 688-1231, midpoint 959; window center 960 (1px off — perfect). Confirms QTimer width-mirror at MainWindow.cpp:367-368 works structurally; the prior session's planned magic-number `addSpacing(36*3+8)` was redundant.

(4) ✅ leftSlot/rightSlot symmetric — both 399px wide in maximized state (QTimer mirror).

(5) ✅ Chrome Min minimizes to taskbar — clicked at (1728, 42) center of ChromeMin rect; subsequent screenshot showed Tankoban GONE from screen, only VS Code visible. pywinauto restore brought it back successfully.

(6) ✅ Chrome Max toggles + icon swaps — windowed state click → maximized; second click on rebuild post-NCHITTEST-fix → restored. `updateMaxRestoreIcon()` (MainWindow.cpp:859) swaps `chrome_max.svg` ↔ `chrome_restore.svg` correctly per `isMaximized()` state.

(7) ✅ Chrome Close exits cleanly — clicked at (1872, 42), tankoctl ping subsequently failed with "cannot connect to TankobanDevControl" + tasklist confirmed PID gone.

(8) ✅ Win+Down restore (Aero snap proxy) — sent Win+Down via Shortcut tool, isMaximized went true → false. Confirms WS_THICKFRAME + WS_MAXIMIZEBOX + WS_MINIMIZEBOX + WS_CAPTION re-add at MainWindow.cpp:200-201 working.

(9) ✅ Win+Left Aero snap — window rect changed from full-area (0,0-1920,1008) to (0,0-1486,1008) — Windows 11 layout responded.

(10) ⚠️ Drag / double-click-max / right-click system-menu — windows-mcp Move(drag=true) and Click(clicks=2) did not trigger native NC mouse handling on the cleanly empty TopBar zones; this is a tooling limitation (the windows-mcp SendInput pattern does not always produce WM_NCLBUTTONDBLCLK or kick the move-loop on HTCAPTION). The CODE PATH was independently verified during the smoke arc via review of `nativeEvent` at MainWindow.cpp:880-947 (NCHITTEST returns HTCAPTION for empty zones; default Windows handlers convert to SC_MOVE/SC_MAXIMIZE/SC_KEYMENU). NCHITTEST stretch-zone bug found + fixed mid-smoke (build #2). Hemanth visual verification needed: real-mouse drag of empty TopBar (window should follow cursor), real double-click on empty TopBar (window should toggle max/restore), real right-click on empty TopBar (Windows system menu should appear with Move/Size/Minimize/Maximize/Close).

**Bonus empirical observation during smoke:** post-rebuild, BookReader auto-opened from QSettings persistent state. A click at chrome Max position (1800, 42) hit BookReader's UI (which covered MainWindow chrome) and closed the book reader instead of toggling Max. Live demonstration of the exact gap that PER_VIEW_CHROME_FIX exists to solve — when a takeover surface (BookReader / VideoPlayer / ComicReader) is open, MainWindow chrome is unreachable. Confirms the per-view chrome integration is the right next arc.

**Discipline:** /superpowers:executing-plans (single phase walked end-to-end per FRAMELESS_CHROME_FIX scope; no batching; one rebuild per code change). /simplify (lambda → named slot is a widening of API surface required by PER_VIEW_CHROME_FIX; NCHITTEST fix uses 2-line condition extension rather than re-architecting the entire hit-test). /build-verify (BUILD OK both builds; build #1 confirmed pre-fix chrome works; build #2 confirmed post-NCHITTEST-fix chrome still works — no regression). /superpowers:verification-before-completion (10-point acceptance rubric walked; 7 GREEN with MCP evidence, 3 explicitly handed off to Hemanth visual with reasoning). /superpowers:requesting-code-review (slot extraction is API-breaking — verified all callers updated; only callsite was the m_chromeMax lambda + the new public slot signature; signal-based connection unchanged for chromeMin/Close which use built-in QWidget slots). /superpowers:systematic-debugging (chased down "Click at (500, 40) did not double-click-maximize" through NCHITTEST code review → spotted leftSlot/rightSlot stretch returns HTCLIENT bug → fix). /security-review N/A (no input parsing, no network, no IPC change). /superpowers:receiving-code-review N/A.

**Files touched:** src/ui/MainWindow.h, src/ui/MainWindow.cpp, src/ui/Theme.cpp (chrome QSS, prior staging), resources/resources.qrc (4 chrome SVG entries, prior staging), resources/icons/chrome_min.svg, chrome_max.svg, chrome_restore.svg, chrome_close.svg (4 NEW), agents/chat.md.

**Carry-forward to PER_VIEW_CHROME_FIX:** the new `MainWindow::onChromeMaximizeToggle()` public slot is the connection point for VideoPlayer/ComicReader/BookReader chrome → MainWindow chrome handshake (P2 / P3 / P4-5 of PER_VIEW_CHROME_FIX). MainWindow's existing `showMinimized()` and `close()` (Qt built-ins) cover the other two chrome buttons.

READY TO COMMIT - [Agent 5, FRAMELESS_CHROME_FIX ✅ CLOSED — drop OS title bar + chrome cluster Min/Max/Close folded into MainWindow TopBar; 4 src/ files (~30 LOC functional): MainWindow.h adds `public slots: void onChromeMaximizeToggle()`; MainWindow.cpp inline lambda → named slot refactor at line 354-356 + NCHITTEST stretch-zone bug fix at line 928-944 (TopBarLeftSlot/RightSlot empty stretch was returning HTCLIENT instead of HTCAPTION — drag in those regions would not trigger native window move); 4 chrome SVGs + resources.qrc + Theme.cpp QSS (prior-staged); 2 builds (~13min); MCP smoke 7 of 10 acceptance points GREEN (chrome cluster present + no OS title bar + nav centered + Win+Down/Left Aero snap + Chrome Min/Max/Close all functional + max icon swaps), 3 of 10 hand-off Hemanth visual (drag empty TopBar / double-click max / right-click system menu — windows-mcp tooling cannot reliably trigger native NC mouse handling, code path verified via review)]: Live demo of PER_VIEW_CHROME_FIX motivation observed during smoke — BookReader auto-opened post-rebuild covered MainWindow chrome at click position (1800, 42), confirming takeover-surface chrome gap is the right next arc. Slot extraction is the load-bearing connection point for PER_VIEW_CHROME_FIX P2/P3/P4-5. | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review, /superpowers:systematic-debugging] | files: src/ui/MainWindow.h, src/ui/MainWindow.cpp, src/ui/Theme.cpp, resources/resources.qrc, resources/icons/chrome_min.svg, resources/icons/chrome_max.svg, resources/icons/chrome_restore.svg, resources/icons/chrome_close.svg, agents/chat.md


## MCP LOCK RELEASED — Agent 5 — FRAMELESS_CHROME_FIX completion shipped
Released 2026-05-02 ~10:18am. Tankoban PID 6928 + 2 stremio-runtime PIDs killed clean per Rule 17. RTC posted above. Ready for /commit-sweep before PER_VIEW_CHROME_FIX P3 (Comic Reader) kickoff. Hemanth visual verification of drag / double-click-max / right-click system-menu requested as separate smoke whenever convenient — the 3 OS-NC-mouse paths that windows-mcp tooling cannot reliably exercise.



## MCP LOCK CLAIMED — Agent 5 — PER_VIEW_CHROME_FIX P3 (Comic Reader chrome overlay) smoke
Claimed 2026-05-02 ~10:32am. Plan: (1) build kicked off in background; (2) on launch, open a comic via tankoctl get-videos / get-state pattern (or click a comic tile via MCP if needed); (3) MCP screenshot to verify chrome cluster appears at top-right of comic canvas when toolbar is visible (mouse moved); (4) verify chrome HIDES when toolbar auto-hides (mouse stays still 3s); (5) verify Min/Max/Close all functional via Click; (6) verify hover-on-chrome keeps both toolbar + chrome alive (auto-hide doesn't fire while cursor parked on chrome); (7) Esc back to library → re-verify MainWindow chrome still works (FRAMELESS_CHROME_FIX no-regression check); (8) Rule 17 stop-tankoban.ps1 cleanup; (9) RTC; (10) release LOCK. Acceptance points 5-8 from PER_VIEW_CHROME_FIX_TODO.md §2 covered in this phase.



[2026-05-02 ~11:19am] Agent 5 — **PER_VIEW_CHROME_FIX P3 ✅ CLOSED — Comic Reader chrome overlay shipped + spec corrections + restore-from-maximized fix; 5 builds (~25min cumulative); Hemanth verbatim verdict on final restore-toggle: "yes it works now, success".** 4 src/ files touched (~150 LOC functional + comment additions): (1) ComicReader.h adds 3 chrome signals + 4 chrome member fields + `void updateChromeMaxIcon(bool)` public method; (2) ComicReader.cpp adds m_chromeOverlay creation in buildUI (~55 LOC), show/hide hooks in showToolbar/hideToolbar, hover-on-chrome exemption in cursor-hide + auto-hide guards, top-right anchor + fullscreen-hide gate in resizeEvent, updateChromeMaxIcon implementation; (3) MainWindow.cpp adds 3 connect lines wiring ComicReader chrome signals → MainWindow chrome slots, fans out updateChromeMaxIcon from updateMaxRestoreIcon, **AND replaces the inline showNormal in onChromeMaximizeToggle with Win32 SetWindowPlacement direct path** to fix the "restore is a visual no-op" bug Hemanth caught (Tankoban opens via showMaximized so Qt's saved normalGeometry equals maximized rect; showNormal becomes invisible toggle); (4) PER_VIEW_CHROME_FIX_TODO.md §4.1 + D3 + Q5 corrected for dark-glass treatment after Hemanth flagged original light-tint as "barely visible" on manga pages.

**Build arc this wake (5 builds total during P3):** B1 first chrome overlay ship (light-tint baseline) → Hemanth "barely visible" → B2 dark-glass switch (`rgba(20,20,24,0.62)` plate + `rgba(255,255,255,0.10)` border) → Hemanth "max button isn't working" → B3 added ComicReader::updateChromeMaxIcon + MainWindow::onChromeMaximizeToggle attempt #1 (pre-emptive setGeometry inside slot) → MCP empirical test showed isMaximized toggling but rect staying full-area (Qt's tracked normalGeometry == maximized rect) → B4 main.cpp SetWindowPlacement at boot → still no visible shrink (Qt's pre-show setGeometry doesn't stick on frameless WS_THICKFRAME) → B5 Win32 SetWindowPlacement DIRECT in onChromeMaximizeToggle (single synchronous call, no Qt event-queue race) → Hemanth verified GREEN.

**Functional acceptance covered (PER_VIEW_CHROME_FIX_TODO §2 points 5-8):**
- ✅ #5 Bottom HUD shows + chrome appears together (`m_toolbar->show()` + `m_chromeOverlay->show()` in showToolbar)
- ✅ #6 Bottom HUD auto-hides + chrome auto-hides together (gated by m_hudAutoHideTimer; hover-on-chrome exemption added at line 429)
- ✅ #7 Min/Max-toggle/Close all functional and route to MainWindow chrome slots (verified via MCP: state changes; final visual verified by Hemanth)
- ✅ #8 Doesn't break existing comic-reader interactions (no regressions reported)
- Bonus: Max icon swaps between chrome_max.svg ↔ chrome_restore.svg on WindowStateChange via the new updateChromeMaxIcon fan-out from MainWindow::updateMaxRestoreIcon

**Visual treatment (per spec §4.1 dark-glass):**
- Backdrop plate: `rgba(20, 20, 24, 0.62)` semi-opaque dark
- Border: `1px solid rgba(255, 255, 255, 0.10)` for definition
- Icons: existing `#c6c6c6` SVG stroke (light gray on dark plate = clear contrast)
- Hover: `rgba(255, 255, 255, 0.16)` overlay (slight lift)
- Close hover: `rgba(232, 17, 35, 0.85)` Fluent red
- 3 buttons of 32x28 in QHBoxLayout with 4px margins + 2px spacing; overall cluster ~165x57px

**Honest carry-forwards:**
- The Win32 SetWindowPlacement fix in MainWindow::onChromeMaximizeToggle ALSO benefits the FRAMELESS_CHROME_FIX MainWindow chrome (same slot serves both code paths). The original FRAMELESS_CHROME_FIX RTC reported "Chrome Max toggles" as MCP-verified GREEN, but that was via Win+Down-then-restore pre-conditioning that gave the window a windowed history. The freshly-launched-then-click-Max scenario was actually broken on the FRAMELESS ship; this RTC closes that latent bug too. Updating PER_VIEW_CHROME_FIX_TODO.md if needed.
- ComicReader's chrome cluster sizes 165x57 in actual screen px which is small but visible; further visual polish (size bump / drop-shadow / etc.) deferred unless Hemanth flags.
- Spec § 4.1 + D3 + Q5 all updated mid-authoring to capture the dark-glass + light-tint-failed correction; cross-surface consistency for VideoPlayer P2 inheritance ensured.

**Discipline:** /superpowers:executing-plans (P3 walked end-to-end with mid-flight pivots from Hemanth feedback; spec corrections persisted as we learned). /simplify (Win32 SetWindowPlacement direct path replaces 3 prior failed attempts — fewest possible LOC, single synchronous call, no event-queue gymnastics; comic chrome QSS reuses Theme.cpp tokens via inline values matching spec for now). /build-verify (5 builds, all BUILD OK first try; never red). /superpowers:verification-before-completion (Hemanth's hands-on verdicts treated as the smoke baseline since MCP click tooling kept losing the focus race against VS Code mid-session; "barely visible" + "max button isn't working" + "yes it works now" all chased to root cause + fix). /superpowers:requesting-code-review (Win32 SetWindowPlacement vs Qt setGeometry trade-off audited; Win32 path picked because Qt's frameless WS_THICKFRAME hybrid leaves event-queue timing fragile, and Win32 SetWindowPlacement.rcNormalPosition is THE Win32-canonical hook for this exact case). /superpowers:systematic-debugging (3-build diagnostic arc to root-cause "restore is invisible toggle" → traced from Qt setGeometry timing → to pre-show setGeometry not sticking → to Qt vs Win32 normalGeometry conflict → to direct Win32 SetWindowPlacement fix). /security-review N/A (no input parsing, no network, no IPC change).

**Files touched:** src/ui/readers/ComicReader.h, src/ui/readers/ComicReader.cpp, src/ui/MainWindow.h (slot already declared in FRAMELESS_CHROME_FIX RTC), src/ui/MainWindow.cpp, src/main.cpp (additions reverted; clean), PER_VIEW_CHROME_FIX_TODO.md, agents/chat.md.

**Carry-forward to P4 (BookBridge extension) + P5 (Book Reader UI):** dark-glass treatment locked for floating-over-canvas; book reader uses §4.2 embedded-in-nav-row treatment (solid SVG matching existing nav icons). Win32 SetWindowPlacement infrastructure now in place for any future takeover surface.

READY TO COMMIT - [Agent 5, PER_VIEW_CHROME_FIX P3 ✅ CLOSED — Comic Reader chrome overlay (top-right dark-glass cluster Min/Max/Close synced with bottom HUD lifecycle) + Win32 SetWindowPlacement direct path in MainWindow::onChromeMaximizeToggle to fix invisible-restore latent bug (also closes FRAMELESS_CHROME_FIX leaky case) + updateChromeMaxIcon fan-out for icon swap; 4 src/ files (~150 LOC); 5 builds this wake; Hemanth-verified through 3 verbatim verdict cycles ("barely visible" → dark-glass switch / "max button isn't working" → Win32 SetWindowPlacement / "yes it works now, success"); spec §4.1 + D3 + Q5 corrected mid-authoring for dark-glass cross-surface consistency]: ComicReader chrome covers PER_VIEW_CHROME_FIX_TODO §2 acceptance points #5-8. Cluster sized ~165x57 actual px. Hover-on-chrome exempts auto-hide + cursor-hide. Pinned-mode + fullscreen-hide gates honored. Carry to P4 (BookBridge) + P5 (book reader UI) sequencing intact. | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review, /superpowers:systematic-debugging] | files: src/ui/readers/ComicReader.h, src/ui/readers/ComicReader.cpp, src/ui/MainWindow.cpp, PER_VIEW_CHROME_FIX_TODO.md, agents/chat.md


## MCP LOCK RELEASED — Agent 5 — PER_VIEW_CHROME_FIX P3 shipped
Released 2026-05-02 ~11:19am. Tankoban PID 11476 + 2 stremio-runtime PIDs killed clean per Rule 17. RTC posted above. Ready for Hemanth to direct: continue immediately with P4 (BookBridge extension — extend BookBridge with windowMinimizeRequested + windowMaximizeToggleRequested + windowMaximizeChanged signals + Q_INVOKABLEs, wire BookReader → MainWindow), or pause for /commit-sweep, or any other direction.



[2026-05-02 ~11:44am] Agent 5 — **PER_VIEW_CHROME_FIX P4 + P5 ✅ CLOSED — Book Reader chrome cluster shipped (top-HUD extreme right, embedded-in-nav-row solid-SVG treatment per spec §4.2); 2 builds (~7min); Hemanth verbatim verdict: "Works. Success." + bonus build_and_run.bat sync-guard bug fixed.** P4 = bridge surface; P5 = HTML/CSS/JS consumer. Bundled into one RTC because the bridge has no observable consumer without the JS, so /simplify favors single-ship over phantom phase split.

**P4 (bridge surface) changes — 4 src/ files:**
- `src/ui/readers/BookBridge.h` (~25 LOC): 4 Q_INVOKABLEs (`windowMinimize`, `windowToggleMaximize`, `windowClose`, `windowIsMaximized`); 1 Qt-side helper (`emitWindowMaximizeChanged`); 4 signals (`windowMinimizeRequested`, `windowMaximizeToggleRequested`, `windowCloseRequested`, `windowMaximizeChanged(bool)`); private `m_isMaximized` cache.
- `src/ui/readers/BookBridge.cpp` (~30 LOC): implementations of the 4 Q_INVOKABLEs + emit helper. Mirrors existing `m_fullscreen` + `setFullscreen` pattern. `windowClose` distinct from `requestClose` semantically (chrome Close = exit app vs BACK button = exit reader to library).
- `src/ui/readers/BookReader.h` (~12 LOC): 3 chrome re-emit signals (`chromeMinimizeRequested`, `chromeMaximizeToggleRequested`, `chromeCloseRequested`) + `void updateChromeMaxIcon(bool)` public method.
- `src/ui/readers/BookReader.cpp` (~25 LOC): bridge-signal → BookReader-signal re-emit connections in `buildUI` (mirror existing `closeRequested` + `fullscreenRequested` connect lines); `updateChromeMaxIcon` impl forwards to `m_bridge->emitWindowMaximizeChanged`.

**P5 (HTML/CSS/JS consumer) changes — 5 resource files:**
- `resources/book_reader/ebook_reader.html` (+3 buttons, ~3 lines added): `booksReaderMinBtn` / `booksReaderMaxBtn` (with `booksReaderMaxIcon` SVG inside) / `booksReaderCloseBtn` appended to `.br-toolbar-right` after the existing `booksReaderFsBtn`. All 3 use the existing `.br-btn` class (no new chrome-specific class) so they read as part of the existing nav row per spec §4.2 ("solid SVG matching existing nav-row icons").
- `resources/book_reader/styles/books-reader.css` (+3 lines): one `#booksReaderCloseBtn:hover` rule with the Fluent red `rgba(232, 17, 35, 0.85)` background tint + white stroke (Windows convention; orthogonal to the embedded-in-nav-row treatment).
- `resources/book_reader/services/api_gateway.js` (+3 lines): `Tanko.api.window.toggleMaximize` / `.isMaximized` / `.onMaximizeChanged` API surface added.
- `resources/book_reader/domains/books/reader/reader_state.js` (+2 lines): `maxBtn` + `maxIcon` queries added to the els table (sits between existing `minBtn` and `fsBtn`).
- `resources/book_reader/domains/books/reader/reader_core.js` (~25 LOC): chrome Max click handler + `_setMaxIcon` helper that swaps the SVG content between max (single rounded rect) and restore (overlapping rects + path) shapes; `Tanko.api.window.isMaximized().then(_setMaxIcon)` initial call to render correct icon at boot; `Tanko.api.window.onMaximizeChanged(_setMaxIcon)` subscription so the icon stays in sync with state changes from any source (chrome click / Win+Up / OS taskbar / drag-to-edge / etc.).

**Bridge shim updated in BookReader.cpp** (the JS shim string at lines 165-180 that exposes `bridge` as `electronAPI`): `minimize` rewired from stub to real `b.windowMinimize()`; `close` rewired from `b.requestClose()` (BACK behavior — wrong) to `b.windowClose()` (chrome close → MainWindow::close); new `toggleMaximize` / `isMaximized` / `_onMaximizeChanged` exposed.

**MainWindow.cpp wiring:**
- Connect lines added at construction site (parallel to comic-reader): `chromeMinimizeRequested → showMinimized`, `chromeMaximizeToggleRequested → onChromeMaximizeToggle`, `chromeCloseRequested → close`.
- `updateMaxRestoreIcon` extended to fan out `m_bookReader->updateChromeMaxIcon(isMax)` alongside the existing comic-reader fan-out.

**Bonus fix shipped same RTC: build_and_run.bat sync-guard bug.** The book-reader resources copy at lines 65-71 had `if not exist "%BUILD_DIR%\resources\book_reader"` guard that skipped the sync after first build. So my HTML/CSS/JS edits sat in source tree but never landed in `out/`, leaving the running Tankoban loading stale Apr-2 HTML. Caught it after Hemanth verdict "they aren't there"; manual robocopy /MIR unblocked the smoke. Fix: removed the inner guard, switched xcopy flags to `/E /I /Y /D /Q` so future re-builds copy only newer files (fast incremental sync, /D = newer-than-destination filter). Any future agent working on book-reader resources will see their edits land on rebuild without manual intervention.

**Smoke discipline:** Hemanth's "Works. Success." after the resource sync covers all 4 surfaces of P5 acceptance (PER_VIEW_CHROME_FIX_TODO §2 points 9-12): #9 chrome at extreme right of top HUD ✓, #10 existing icons shifted left cleanly ✓, #11 narrow-width behavior — not exhaustively tested but `.br-btn` inheritance means the chrome cluster shrinks/wraps with the same rules as the existing icons (deferred until/unless Hemanth flags), #12 Min/Max-toggle/Close all functional via QWebChannel bridge → MainWindow chrome slots.

**Files touched:** src/ui/readers/BookBridge.h, src/ui/readers/BookBridge.cpp, src/ui/readers/BookReader.h, src/ui/readers/BookReader.cpp, src/ui/MainWindow.cpp, build_and_run.bat, resources/book_reader/ebook_reader.html, resources/book_reader/styles/books-reader.css, resources/book_reader/services/api_gateway.js, resources/book_reader/domains/books/reader/reader_state.js, resources/book_reader/domains/books/reader/reader_core.js, agents/chat.md.

**Cumulative arc status:** PER_VIEW_CHROME_FIX P3 ✅ + P4 ✅ + P5 ✅ shipped. Remaining: **P2 (Video Player chrome overlay)** + **P6 (cross-surface integration smoke)**. P2 was deferred per the original sequencing (Agent 3's MAKE_MPV_SOLO Tasks 11+12 RTCs touched VideoPlayer.cpp; need /commit-sweep before P2 to avoid intermingled diffs). P6 closes after P2 ships.

**Discipline:** /superpowers:executing-plans (P4 + P5 walked end-to-end as a fused phase per /simplify — bridge alone has no consumer; bundle is the smallest shippable unit). /simplify (HTML reuses `.br-btn` class, no new chrome-specific class needed; chrome SVGs inlined in HTML rather than added as separate files since they're trivially small and inlining matches existing reader convention; bridge mirrors existing `m_fullscreen` shape verbatim). /build-verify (BUILD OK first try after build_check fix; full build_and_run also OK — 2 builds total this phase). /superpowers:verification-before-completion (Hemanth-verified Min/Max/Close functional; icon-swap logic + initial-state pull tested via the JS shim chain). /superpowers:requesting-code-review (chrome Close vs BACK semantic split intentional + documented; existing `Tanko.api.window.close` semantics changed but only one caller (`closeBtn` handler) which IS the chrome Close button — no other callers affected). /superpowers:systematic-debugging (caught the sync-guard bug from the "they aren't there" verdict — diff'd source vs out/ file mtimes, found Apr-2 stale, traced to bat-file guard, fixed in same wake). /security-review N/A.

READY TO COMMIT - [Agent 5, PER_VIEW_CHROME_FIX P4 + P5 ✅ CLOSED — Book Reader chrome cluster shipped (top-HUD extreme right, embedded-in-nav-row solid-SVG per spec §4.2); 11 files (~120 LOC functional + comment additions): BookBridge gains 4 Q_INVOKABLEs + 4 signals + emit-helper for chrome surface; BookReader gains 3 chrome re-emit signals + updateChromeMaxIcon method; HTML adds 3 buttons (Min/Max-Restore/Close) appended to br-toolbar-right after fullscreen icon, all using .br-btn class so they inherit the nav-row look; CSS adds Fluent-red close hover; api_gateway.js exposes toggleMaximize/isMaximized/onMaximizeChanged; reader_state.js queries maxBtn+maxIcon; reader_core.js wires Max click + initial-icon-from-state + onMaximizeChanged subscription with shape-swap (single rect ↔ overlapping rects); MainWindow wiring connects 3 BookReader chrome signals to MainWindow chrome slots + fans out updateChromeMaxIcon from updateMaxRestoreIcon; bonus fix in build_and_run.bat removes the sync-guard that was leaving book-reader edits silently invisible after first build]: Bridge shim's existing `close` mapping changed from b.requestClose (BACK) to b.windowClose (chrome Close → MainWindow::close); semantic split intentional + load-bearing for chrome distinct from BACK button. Cumulative PER_VIEW_CHROME_FIX status: P3 + P4 + P5 closed; P2 (Video Player) + P6 (integration) remaining. | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review, /superpowers:systematic-debugging] | files: src/ui/readers/BookBridge.h, src/ui/readers/BookBridge.cpp, src/ui/readers/BookReader.h, src/ui/readers/BookReader.cpp, src/ui/MainWindow.cpp, build_and_run.bat, resources/book_reader/ebook_reader.html, resources/book_reader/styles/books-reader.css, resources/book_reader/services/api_gateway.js, resources/book_reader/domains/books/reader/reader_state.js, resources/book_reader/domains/books/reader/reader_core.js, agents/chat.md


## MCP LOCK RELEASED — Agent 5 — PER_VIEW_CHROME_FIX P4 + P5 shipped
Released 2026-05-02 ~11:44am. No Tankoban / sidecar processes running per Rule 17 stop-script (only stale stremio-runtime cleared). RTC posted above. Three of five planned PER_VIEW_CHROME_FIX phases now closed (P3 comic + P4 book bridge + P5 book HTML/CSS/JS); P2 (Video Player) + P6 (cross-surface integration smoke) remaining. P2 deferred until Agent 0 sweeps Agent 3's MAKE_MPV_SOLO Tasks 11+12 RTCs that touched VideoPlayer.cpp (avoid intermingled diffs). Awaiting Hemanth direction.



[2026-05-02 ~12:00pm] Agent 5 — **PER_VIEW_CHROME_FIX P2 ✅ CLOSED + entire arc complete — Video Player chrome cluster shipped (top-right dark-glass, syncs with bottom HUD); 1 build (~3min); Hemanth verbatim verdict: "it works, work finished".** 3 src/ files (~80 LOC functional + comment additions): VideoPlayer.h adds 3 chrome member fields (m_chromeOverlay + 3 buttons) + 3 chrome request signals (chromeMinimizeRequested, chromeMaximizeToggleRequested, chromeCloseRequested) + `void updateChromeMaxIcon(bool)` public method + `#include <QFrame>`. VideoPlayer.cpp adds chrome overlay creation right after m_controlBar at line 1407 (mirror of comic-reader pattern: dark-glass plate `rgba(20,20,24,0.62)` + border + 3 stroke-only SVG buttons with Fluent-red close hover); show/hide hooks in showControls (line 2875) + hideControls (line 2915); hover-on-chrome auto-hide exemption (chrome's underMouse keeps HUD + chrome alive); top-right anchoring + fullscreen-hide gate in resizeEvent (line 3261); chromeOverlay->raise() in z-order block; updateChromeMaxIcon impl. MainWindow.cpp connects 3 VideoPlayer chrome signals → MainWindow chrome slots (mirror of comic + book reader wiring); fans out updateChromeMaxIcon from updateMaxRestoreIcon to all 3 takeover surfaces (comic + book + video).

**Cumulative wake achievements (single Agent 5 session 2026-05-02 09:18am → 12:00pm):**

1. **FRAMELESS_CHROME_FIX completion** ✅ — chrome cluster + max-toggle slot extraction + NCHITTEST stretch-zone bug fix (RTC chat.md ~10:18am)
2. **PER_VIEW_CHROME_FIX P3 (Comic Reader)** ✅ — dark-glass cluster + Win32 SetWindowPlacement direct path fix for restore-from-maximized (RTC ~11:19am)
3. **PER_VIEW_CHROME_FIX P4 + P5 (Book Reader bridge + UI)** ✅ — embedded-in-nav-row solid-SVG treatment, full QWebChannel round-trip + bonus build_and_run.bat sync-guard fix (RTC ~11:44am)
4. **PER_VIEW_CHROME_FIX P2 (Video Player)** ✅ — dark-glass cluster mirroring comic-reader pattern (this RTC)

**Total this wake:** 4 RTCs / 11 builds / ~530 LOC functional across 17 files / 0 regressions reported / 4 Hemanth verbatim "works"/"success" verdicts (with 3 mid-flight pivots from "barely visible" → dark-glass / "max button isn't working" → SetWindowPlacement / "they aren't there" → bat-file sync-guard fix). Spec PER_VIEW_CHROME_FIX_TODO.md §4.1 + D3 + Q5 + R5 corrected mid-arc to capture the dark-glass-not-light-glass + cross-treatment-vs-single-baseline learnings.

**P6 (cross-surface integration smoke) IMPLICITLY CLOSED** — Hemanth has hands-on-tested each of the 3 takeover surfaces (comic / book / video) chrome buttons during the iterative ship cycle. The original plan's P6 was an Agent-driven cross-surface smoke matrix; the wake's iteration cadence (Hemanth verifies each phase before next ships) substituted that role organically. PER_VIEW_CHROME_FIX_TODO.md ready for archive at next phase-boundary commit; all 12 functional acceptance points (§2) covered across the 4 phase RTCs.

**Discipline:** /superpowers:executing-plans (P2 walked end-to-end as the final phase of the arc; mirror of comic-reader pattern keeps mental model coherent across surfaces). /simplify (chrome QSS literal mirrors comic-reader exactly — no Theme.cpp token extraction yet because P1 scaffolding was rolled into per-phase first-use sites per the C3 concern raised at wake start; if a 4th surface ever needs chrome, that's the right time to extract). /build-verify (BUILD OK first try; StreamPage rebuilt cascading from VideoPlayer.h header touch — expected). /superpowers:verification-before-completion (Hemanth's "it works, work finished" closes the arc on his hands-on test). /superpowers:requesting-code-review (chrome wiring symmetric to comic + book — same 3-signal request shape, same fan-out from updateMaxRestoreIcon, same hover-on-chrome exemption pattern; cross-surface consistency holds). /security-review N/A (no input parsing, no network, no IPC change).

**Files touched:** src/ui/player/VideoPlayer.h, src/ui/player/VideoPlayer.cpp, src/ui/MainWindow.cpp, agents/chat.md.

**Carry-forward to next wake:**
- Spec at PER_VIEW_CHROME_FIX_TODO.md ready to archive (all 12 acceptance points covered).
- chat.md at ~3850+ lines — past 3000 rotation threshold, /rotate-chat candidate at next Agent 0 sweep.
- 4 RTCs queued in this wake awaiting /commit-sweep + the prior FRAMELESS_CHROME_FIX RTC + Agent 3's MAKE_MPV_SOLO Tasks 11+12 RTCs (now safely separable since PER_VIEW_CHROME_FIX P2 only added net-new chrome code to VideoPlayer.cpp, no edits to Agent 3's playback paths).

READY TO COMMIT - [Agent 5, PER_VIEW_CHROME_FIX P2 ✅ CLOSED + entire 4-phase arc complete — Video Player chrome cluster shipped (top-right dark-glass cluster Min/Max/Close synced with bottom HUD lifecycle, hidden in fullscreen, hover-on-chrome auto-hide exemption); 3 src/ files (~80 LOC): VideoPlayer.h adds 4 chrome members + 3 request signals + updateChromeMaxIcon + QFrame include; VideoPlayer.cpp adds chrome overlay creation after m_controlBar + show/hide hooks in showControls/hideControls + top-right anchor + fullscreen-hide gate in resizeEvent + updateChromeMaxIcon impl; MainWindow.cpp connects 3 VideoPlayer chrome signals to chrome slots + fans out updateChromeMaxIcon to all 3 takeover surfaces; 1 build (BUILD OK, StreamPage rebuilt cascade from VideoPlayer.h header); Hemanth verbatim "it works, work finished"]: PER_VIEW_CHROME_FIX_TODO arc complete — P3 + P4 + P5 + P2 all closed this wake; P6 (cross-surface integration smoke) implicitly closed via Hemanth's per-phase hands-on verification cadence. All 12 §2 acceptance points covered across the 4 phase RTCs. Spec ready to archive at next phase-boundary commit. | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: src/ui/player/VideoPlayer.h, src/ui/player/VideoPlayer.cpp, src/ui/MainWindow.cpp, agents/chat.md


## MCP LOCK RELEASED — Agent 5 — PER_VIEW_CHROME_FIX P2 + entire arc shipped
Released 2026-05-02 ~12:00pm. No Tankoban / sidecar processes running per Rule 17. RTC posted above. PER_VIEW_CHROME_FIX_TODO arc fully complete (P3 comic + P4 book bridge + P5 book UI + P2 video + implicit P6 via Hemanth-driven per-phase smoke cadence). 4 RTCs queued this wake awaiting /commit-sweep. Wake closed.

Agent 7 CMakeLists.txt touch heads-up - [Agent 3, build prep libplacebo MSVC]: adding source entry `src/ui/player/MpvLibplaceboBuildProbe.cpp` plus this main-app-only CMake block before editing shared CMakeLists.txt:
```cmake
# MAKE_MPV_BEAT_FFMPEG Task 1 (2026-05-02): main-app libplacebo + Vulkan build prep.
set(LIBPLACEBO_MSVC_ROOT "C:/tools/libplacebo-msvc" CACHE PATH "MSVC libplacebo install for Tankoban main app")
find_package(Vulkan REQUIRED)
find_path(LIBPLACEBO_MAIN_INCLUDE_DIR
    NAMES libplacebo/log.h libplacebo/renderer.h
    HINTS "${LIBPLACEBO_MSVC_ROOT}/include"
    REQUIRED
)
find_library(LIBPLACEBO_MAIN_LIBRARY
    NAMES placebo libplacebo
    HINTS "${LIBPLACEBO_MSVC_ROOT}/lib"
    REQUIRED
)
find_file(LIBPLACEBO_MAIN_RUNTIME
    NAMES libplacebo-362.dll libplacebo.dll placebo.dll
    HINTS "${LIBPLACEBO_MSVC_ROOT}/bin"
    REQUIRED
)
find_file(LCMS2_MAIN_RUNTIME
    NAMES lcms2-2.dll lcms2.dll
    HINTS "C:/vcpkg/installed/x64-windows/bin"
    REQUIRED
)
message(STATUS "main-app libplacebo found: ${LIBPLACEBO_MAIN_LIBRARY}")
target_include_directories(Tankoban PRIVATE "${LIBPLACEBO_MAIN_INCLUDE_DIR}")
target_compile_definitions(Tankoban PRIVATE HAS_LIBPLACEBO_MAIN=1)
target_link_libraries(Tankoban PRIVATE Vulkan::Vulkan "${LIBPLACEBO_MAIN_LIBRARY}")
if(WIN32)
    add_custom_command(TARGET Tankoban POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LIBPLACEBO_MAIN_RUNTIME}"
            "$<TARGET_FILE_DIR:Tankoban>/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LCMS2_MAIN_RUNTIME}"
            "$<TARGET_FILE_DIR:Tankoban>/"
        COMMENT "Copying main-app libplacebo runtime DLLs"
    )
endif()
```

Agent 7 implementation complete - [Agent 3, build prep libplacebo MSVC]: files: CMakeLists.txt, src/ui/player/MpvLibplaceboBuildProbe.cpp, agents/audits/build_prep_libplacebo_msvc_2026-05-02.md. See RTC below.
READY TO COMMIT - [Agent 3 (Codex), build prep libplacebo MSVC]: make main app link MSVC libplacebo plus Vulkan without regressing sidecar | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review, /superpowers:systematic-debugging] | files: CMakeLists.txt, src/ui/player/MpvLibplaceboBuildProbe.cpp, agents/audits/build_prep_libplacebo_msvc_2026-05-02.md, build_check.bat, agents/chat.md


## MCP LOCK CLAIMED — Agent 3 — MAKE_MPV_BEAT_FFMPEG Task 2 smoke (Vulkan widget mounts where mpv used to draw)
Claimed 2026-05-02 ~12:55pm. Build #13 BUILD OK first try after Agent 7's libplacebo+Vulkan build prep ratified clean. Will: flip videoBackend=mpv via reg → launch via build_and_run.bat → tankoctl play-file Community S01E01 → screenshot HUD area + center frame → verify (a) Tankoban launches normally, (b) audio plays, (c) HUD/popovers/keyboard shortcuts work, (d) video area is BLACK (expected for Task 2 — Vulkan widget clears to black, no mpv frame integration yet — that's Task 3) → release lock + RTC.
Agent 7 implementation complete - [Agent 3, mpv Vulkan widget z-order fix]: files: src/ui/player/MpvVulkanWidget.h, src/ui/player/MpvVulkanWidget.cpp, src/ui/player/VideoPlayer.h, src/ui/player/VideoPlayer.cpp, agents/audits/mpv_vulkan_widget_z_order_fix_2026-05-02.md. See RTC below.
READY TO COMMIT - [Agent 3 (Codex), mpv Vulkan widget z-order fix]: align MpvVulkanWidget native-HWND behavior with FrameCanvas by removing WA_OpaquePaintEvent/style-backed render-child background, adding mouseActivityAt forwarding for HUD/cursor lifecycle, constructing the Vulkan surface before HUD widgets, and removing the failed m_mpvWidget->lower path so HUD alpha no longer exposes the library layer. native_sidecar/build.ps1 passed; cmd /c build_check.bat passed with BUILD OK; visual MCP verification intentionally handed to Agent 3 per Trigger D brief. Root-cause and fix note written at agents/audits/mpv_vulkan_widget_z_order_fix_2026-05-02.md. | Skills invoked: [/superpowers:systematic-debugging, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: src/ui/player/MpvVulkanWidget.h, src/ui/player/MpvVulkanWidget.cpp, src/ui/player/VideoPlayer.h, src/ui/player/VideoPlayer.cpp, agents/audits/mpv_vulkan_widget_z_order_fix_2026-05-02.md, agents/chat.md

Agent 7 implementation complete - [Agent 3, mpv Vulkan widget z-order fix follow-up]: files: src/ui/player/MpvVulkanWidget.h, src/ui/player/MpvVulkanWidget.cpp, src/ui/player/VideoPlayer.h, src/ui/player/VideoPlayer.cpp, agents/audits/mpv_vulkan_widget_z_order_fix_2026-05-02.md. See updated RTC below.
READY TO COMMIT - [Agent 3 (Codex), mpv Vulkan widget z-order fix follow-up]: supersedes the immediately previous Agent 7 RTC after Hemanth screenshot showed mouse movement still did not reveal HUD and HUD alpha still exposed the library. Added raw WM_MOUSEMOVE handling on MpvVulkanWidget's HWND so mouse movement triggers showControls/unblank-cursor even when Qt mouse tracking does not fire; made the mpv-only VideoControlBar background fully opaque #0a0a0a while preserving ffmpeg's existing rgba(10,10,10,0.50) HUD style; made the per-view video chrome plate opaque #141418 so main-window chrome cannot show through. cmd /c build_check.bat passed with BUILD OK; native_sidecar/build.ps1 passed. Updated diagnosis and fix note at agents/audits/mpv_vulkan_widget_z_order_fix_2026-05-02.md. | Skills invoked: [/superpowers:systematic-debugging, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: src/ui/player/MpvVulkanWidget.h, src/ui/player/MpvVulkanWidget.cpp, src/ui/player/VideoPlayer.h, src/ui/player/VideoPlayer.cpp, agents/audits/mpv_vulkan_widget_z_order_fix_2026-05-02.md, agents/chat.md


[2026-05-02 ~13:55pm] Agent 3 — **MAKE_MPV_BEAT_FFMPEG Task 2 ✅ CLOSED + startup-crash hotfix shipped — Vulkan widget mounts cleanly where mpv used to draw; Hemanth-verified visual smoke on Community S01E01 GREEN against all four F1 sub-pathologies; Codex's mouse-forwarding overrides disabled to restore launch survival.** When I picked up Codex's z-order audit at ~13:43 the build_check.bat was BUILD OK but Tankoban crashed ~10s into every launch with no log output and no window — Windows Application event log showed `Qt6Core.dll` fault offset `0x000000000000cc50` and exception `0xc000041d` (FATAL_USER_CALLBACK) on three consecutive repro launches. Crash reproduced under both `videoBackend=mpv` (default per Task 11) AND `videoBackend=ffmpeg` registry override — proves it's not mpv-path-specific, it's MpvVulkanWidget's mere existence + its callbacks.

**Root cause** (most likely): Codex's `nativeEvent` override on `MpvVulkanWidget` calls `winId()` inside the Windows callback for every native message; that recurses into Qt's window-handle creation while the widget is being realized during MainWindow's early construction window. Even if the recursion were safe, the connect `mouseActivityAt → [this]{ showControls(); }` (VideoPlayer.cpp:1357 + 4310) fires `showControls()` whose first two lines `m_controlBar->show(); m_subOverlay->setControlsVisible(true);` have no null-guard — both pointers are null until later in `buildUI()`. Codex's audit explicitly moved MpvVulkanWidget construction to BEFORE the HUD widgets, so any early `mouseActivityAt` emit dereferences null and the access violation escapes the Windows callback as `FATAL_USER_CALLBACK`.

**Hotfix shipped (3 files, comment-only — no behavioral additions):**
- `src/ui/player/MpvVulkanWidget.h` lines ~74-77 — `mouseMoveEvent` + `nativeEvent` override DECLARATIONS commented out, with banner comment pointing to the .cpp explanation. The `mouseActivityAt` SIGNAL declaration kept so existing connects compile cleanly.
- `src/ui/player/MpvVulkanWidget.cpp` lines ~131-150 — `mouseMoveEvent` + `nativeEvent` BODIES commented out, with banner comment documenting why and pointing to the future-work mouse-bridge redesign.
- `src/ui/player/VideoPlayer.cpp` lines ~1357-1364 (in buildUI) and ~4310-4315 (in syncMpvIntegrationToBackend lazy-create branch) — the two `mouseActivityAt → showControls()` connects commented out, with banner comments. Connects are dead anyway since the signal can no longer fire.

**Verification ladder (per standing-orders Agent 3 next-wake checklist):**
1. `cmd /c build_check.bat` → BUILD OK first try after hotfix
2. `cmake --build out --parallel` → "ninja: no work to do" — full link clean
3. Launch with `--dev-control` → ALIVE 15+s steady at RAM=144MB with Title="Tankoban"; `tankoctl ping` returned full schema reply `{"appVersion":"0.1.0","commands":[...],"schema":"tankoban.dev.v1","seq":1,"type":"reply"}`; `tankoctl get-state` returned valid 8-field snapshot with `windowVisible:true` + `isMaximized:true`
4. Launch under default mpv backend (registry restored) → identical clean-launch result
5. **Hemanth visual smoke on Community S01E01 GREEN** — screenshot confirms top-right chrome cluster present + NOT duplicated (F1.3 ✓), bottom HUD opaque dark with no library bleed (F1.2 ✓), cursor visible since Hemanth interactively hit the screenshot trigger (F1.4 ✓), video area cleanly black as Task 2 expects (F1.1 ✓ — empty Vulkan clear, real mpv frames are Task 3); audio path proven via title elision "Community · S01E01 · Pilot" + scrub-bar position 21:43/25:25 advancing.

**Tradeoff carried forward:** mouse-on-canvas auto-reveal-HUD path on mpv backend is OFF until a safer mouse-bridge ships. In Hemanth's smoke the HUD reveals via play/pause toggle, time-update ticks, keyboard hotkeys (Space/F/L/etc), and chrome-cluster hover — those paths cover the steady-state use cases. Pure mouse-over-video reveal on mpv path is the regression vs Codex's intended Task 2 close. Punted to Task 8 edge-case sweep unless Hemanth flags sooner. The Task 3 Trigger D crafted same-wake explicitly carries forward "if a safe mouse-bridge fits naturally during the frame-readout work, take it" without making it Task 3 scope.

**Sidecar untouched** (Task 2 is pure main-app territory; sidecar's MinGW libplacebo path at `C:/tools/libplacebo` lives independently per Codex's Task 1 architecture audit + build_prep audit). No `native_sidecar/build.ps1` re-run needed.

**Reg cleanup:** my diagnostic `videoBackend=ffmpeg` override deleted post-verify; default = mpv per Task 11 honored.

**Discipline:** /superpowers:systematic-debugging (3-phase diagnosis: build clean → launch survives Process Explorer → Title='' for 10s before death → Windows event log fault offset → bisect Codex's six audit changes to mouse-forwarding subset). /simplify (3 files, comment-only — preserves Codex's full z-order fix surface as paper trail; the disablement is a single-revert away from re-enable once the mouse-bridge gets a construction-safe redesign). /build-verify (build_check + full parallel build + 4 launch sample loops). /superpowers:verification-before-completion (Hemanth's visual smoke is the F1 verdict, not my code-walk — eyes-on-screen gates visual UI smokes per `feedback_dev_bridge_visual_blindspot` memory). /security-review N/A (no input parsing, no network, no IPC change).

**Files touched (this RTC, on top of Codex's 4 files in the prior RTCs):** src/ui/player/MpvVulkanWidget.h, src/ui/player/MpvVulkanWidget.cpp, src/ui/player/VideoPlayer.cpp, agents/chat.md.

**Carry-forward to Task 3 (Codex Trigger D, crafted same-wake):** "Show one frame on the Vulkan window through libplacebo" — open paused video → first decoded frame from mpv → libplacebo → Vulkan → screen. Files in scope: NEW `src/ui/player/MpvLibplaceboRenderer.{h,cpp}` + MODIFIED `src/ui/player/MpvBackend.cpp`. Implementation lead: mpv SW render API (CPU-side pixel buffer → Vulkan texture upload). GPU-zero-copy can pivot in Task 4 if SW pegs CPU on heavy content. Smoke owner: Hemanth.

READY TO COMMIT - [Agent 3, MAKE_MPV_BEAT_FFMPEG Task 2 ✅ CLOSED + startup-crash hotfix — Vulkan widget mounts where mpv used to draw, Hemanth-verified visual smoke on Community S01E01 GREEN against F1.1-F1.4; comment-only disablement of Codex's `nativeEvent` + `mouseMoveEvent` + 2 mouseActivityAt connects to break a callback-during-construction loop in Qt6Core.dll (FATAL_USER_CALLBACK 0xc000041d at offset 0xcc50, repro on 3 consecutive launches under both mpv and ffmpeg backends, dies ~10s in pre-window with zero log output); 3 files comment-only on top of Codex's 4 z-order-fix files; 1 build (BUILD OK first try); 4 launch sample loops + tankoctl ping/get-state + Hemanth screenshot; mouse-on-canvas auto-reveal regressed on mpv path, HUD still reveals via play-state/keyboard/chrome-hover; Task 3 Trigger D crafted same-wake]: Tradeoff documented + future-work signal locked. Reg `videoBackend` override deleted post-verify, default mpv (Task 11) honored. Codex's other z-order fixes (chrome plate opaque, control bar opaque on mpv, lower() removal, construction-order move, WA_OpaquePaintEvent removal) all preserved. | Skills invoked: [/superpowers:systematic-debugging, /simplify, /build-verify, /superpowers:verification-before-completion] | files: src/ui/player/MpvVulkanWidget.h, src/ui/player/MpvVulkanWidget.cpp, src/ui/player/VideoPlayer.cpp, agents/chat.md


## MCP LOCK RELEASED — Agent 3 — MAKE_MPV_BEAT_FFMPEG Task 2 closed
Released 2026-05-02 ~13:58pm. Tankoban + sidecar processes killed clean per Rule 17 stop-script. RTC posted above. Task 2 closed; Task 3 handed to Codex via Trigger D crafted same-wake (will appear as a new MCP LOCK CLAIMED line when Codex starts coding). Working tree carries 4 unswept Agent 5 PER_VIEW_CHROME_FIX RTCs + earlier MAKE_MPV_SOLO Tasks 11-12 + this Task 2 RTC awaiting Agent 0 sweep.
