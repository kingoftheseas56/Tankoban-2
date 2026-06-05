# Stream/Theatre Autonomous On-Device Smoke Harness — Design

**Owner:** Agent 4 (Stream mode + Tankorent)
**Authored:** 2026-06-05
**Status:** design — pending Hemanth spec-review, then `writing-plans`.

## 1. Goal

An autonomous, on-device smoke-test harness that drives the *real* Tankoban app
end-to-end across Stream/Theatre journeys, decides pass/fail itself, and produces
**fully-traceable** findings. Hemanth makes only final taste calls — he does not
click, run commands, or read logs. The harness must run for long stretches,
survive app crashes/relaunches, and be reusable by Agents 1/2/3 for their domains.

Deliverable: the harness + a first real, fully-traceable bug report.

## 2. The two-lane oracle (core constraint)

Every assertion is verified by exactly one lane:

- **SELF lane** (functional / state / timing) — the harness drives via `tankoctl`
  (`stream-*` / `player-*` / `sidecar-*` / `library-*` / `get-*` / the OBS-10
  `introspect-*` verbs), reads back the resulting state, compares it to a stated
  `expect`, and reads the four log streams (`events.jsonl`,
  `stream_telemetry.log`, `sidecar_debug_live.log`, `ipc_latency.log`) plus
  `out/HANG_DETECTED.json`. The harness decides pass/fail directly.
- **VISUAL lane** (look-and-feel) — ffmpeg records the whole session; the harness
  emits a timestamped **visual work-order**; **Hemanth couriers the MP4 +
  work-order to the Gemini LLM** (the chat app's native video understanding — NOT
  the Gemini API; the harness calls no vision API). Gemini's per-checkpoint
  descriptions are pasted back as JSON and the harness folds them into findings.
  Covers what state-reads can't: black/frozen screen, subtitle overlap, a cover
  that never painted, and — since the recording carries audio — whether sound
  actually came out.

  **Scope: the harness checks the plumbing, it does not watch.** A video smoke is
  a handful of **spot-checks** at sampled positions (first frame painted, audio
  leaving the speakers, subs rendered, a mid-seek re-paints, the end is reached
  cleanly) — seconds each, never a playthrough. Judging whether the video *looks
  right / feels good / is enjoyable* is Hemanth's taste call, deliberately OUT of
  scope. The agent proves the pipe carries water; Hemanth decides if the water's
  good.

## 3. Make-or-break: correlation

A Gemini description of a long recording is useless unless every visual finding
ties back to the action that caused it. **Before and after every step the harness
emits `tankoctl log-mark <step-id> START|END`** — one synchronized marker across
the recording timeline + all four log streams. A finding MUST read:

> triggering action → wall-clock timestamp → video offset → Gemini's description
> of that window → the matching log line.

A finding that cannot be joined on a `log-mark` label is dropped — it is not done.

## 4. Architecture

Built as a thin Stream/Theatre layer **on top of the existing**
`scripts/agents/drive_journal.py` `Driver` (which already does log-mark
bracketing + `events.jsonl` delta capture + ffmpeg recording) — extended with an
oracle, a data-driven catalogue, recovery, and the visual-description pass.

### 4.1 Components

- **Catalogue (`scripts/agents/smoke/catalogue_stream.json`)** — data, not code.
  Journeys → steps → assertions. Editable without touching the runner; this is
  what makes the harness reusable (Agents 1/2/3 drop in their own catalogue).
- **Runner (`scripts/agents/smoke/run_smoke.py`)** — loads the catalogue, owns the
  session. Per step: `log-mark <id> START` → execute the action verb → poll the
  SELF probes (with per-assertion timeout) → evaluate each `expect` → record
  pass/fail with the deciding log lines → `log-mark <id> END pass|fail`.
- **Recording lifecycle (runner-owned)** — the runner itself starts ONE
  continuous ffmpeg screen recording at session start (ddagrab via
  `screen_record.py`; gdigrab fallback) and stops it at session end. Because it
  captures the *screen*, it is app-independent: it keeps rolling across app
  crashes/relaunches, so a crash's frozen/black frame lands on the same timeline
  as the `log-mark`s. Output: `out/smoke_<session>.mp4`. Nothing external starts
  or stops it.
- **Visual lane (`scripts/agents/smoke/visual_workorder.py`, post-session)** — the
  harness calls NO vision API. After the SELF run it **auto-clips** each
  to-be-seen window from the full MP4 (`ffmpeg -ss -t` → `out/smoke_<session>_clips/
  <id>.mp4`) — every VISUAL assertion's window + every SELF-fail window + every
  watchdog window — and emits a `visual_workorder.md` (clip → question →
  LOOK-HARD flag). **Hemanth couriers the CLIPS (small, focused — never the
  2-hour file) to the Gemini LLM** (chat app, native video understanding) and
  pastes back `{id: description}` JSON; `fold_visual.py` merges them into the
  findings. Clipping is the cost lever (§7): only the moments state+logs couldn't
  judge get an eye on them.
- **Correlator (`scripts/agents/smoke/findings.py`)** — joins SELF results,
  VISUAL descriptions, and log lines on the `log-mark` label into the findings
  report (§6). Drops any finding that cannot be joined.
- **Recovery & the `blocked` verdict** — between steps the runner pings the
  bridge; on `ping` timeout or `HANG_DETECTED.json` appearing it records a
  death/freeze finding, marks the remaining checks of that journey **`blocked`**
  (verdict distinct from `fail` — "blocked-by-crash, needs Agent 3's idle-spin fix
  / OBS-4," never a false functional fail), kills any stray `Tankoban.exe` (Rule 1
  — a stray instance is known to relaunch; check `tasklist`), relaunches via
  `run_drive_mode.bat`, waits for `ping`, and resumes at the next journey. The
  continuous recording is unaffected. **The player idle-spin crash is a known
  active blocker** (Agent 4's 2026-06-05 RTC: app dies seconds-to-minutes after
  open) — a long autonomous run WILL hit it; the harness survives + reports it
  honestly, and some playback smokes simply cannot complete until Agent 3's fix
  lands.

### 4.2 Decided forks (Rule 14 technical calls)

- **A — JSON catalogue** over hardcoded Python / per-journey scripts: checks as
  data → editable + reusable across agents.
- **B — frame-sampling** over full-MP4 upload to Gemini: `call_gemini_visual`
  already takes images; sampling is the natural home for the cost lever.
- **C — build ON `drive_journal.Driver`** over a fresh harness: reuse the
  log-mark + events-delta + recording machinery, add only oracle/catalogue/
  recovery/visual.

### 4.3 OBS-10 usage

Read true on-screen domain state without brute-forcing keys:
- `introspect-object StreamDetailView` → currentImdb/currentTitle/episodeRows/
  sourcesPanel/movieDownloadButtonEnabled.
- `introspect-object EpisodeTile` → per-tile `devSnapshot()` (state, percent,
  episode number) for J4.
- `introspect-tree` → confirm which screen/layer is live.
- `introspect-actions` → player QShortcut/QAction enabled/checked state for J6.
Where a stream widget has a blind spot (no `devSnapshot()`), the spec flags it as
a follow-on to teach that widget `IDevInspectable` (separate small change, not
this harness).

## 5. The check catalogue

Assertion row shape:
```json
{ "id": "op.play.seek_lands", "lane": "SELF",
  "probe": ["get-player"], "expect": { "positionSec": {"approx": 600, "tolerance": 5} },
  "timeoutSec": 10, "on_fail": "continue", "needs": ["op.play.starts"] }
```
VISUAL rows carry `"gemini_prompt"` instead of `"expect"`. `on_fail` ∈
`continue | abort_journey | relaunch`. `needs` gates a row on a prereq's pass.

All `tankoctl` verb names below are dash-form; exact names are reconfirmed against
the live `ping` `commands[]` at execution time.

### J1 — One Piece (anime, full chain) — the proof journey
| id | lane | assertion |
|---|---|---|
| op.search.returns | SELF | `search "One Piece"` results include `tt0388629` |
| op.search.no_hang | SELF | search <15s; no new `HANG_DETECTED.json` |
| op.detail.opens | SELF | open detail (ui-click result tile) → `introspect-object StreamDetailView`: currentImdb=tt0388629, episodeRows>0 |
| op.detail.renders | VISUAL | hero art + episode list painted, not blank |
| op.dispatch.accepted | SELF | `dispatch-episode tt0388629 1 <latest>` → status=dispatched, infoHash non-empty |
| op.dispatch.amatsu_source | SELF | picked release name (from `get-downloads` canonicalPath) is a Nyaa fansub release (group bracket / MultiSub / known group) → Amatsu fed the pick |
| op.dispatch.single_not_batch | SELF | picked release name has no episode-range marker (refined picker) — else flag finding |
| op.dl.starts | SELF | `get-downloads` entry exists; downloaded bytes climb within 30s |
| op.dl.no_silent_stall | SELF | within 60s: progress>0 OR explicit error (catches peers>0 / dlspeed=0 firewall stall) |
| op.play.starts | SELF | `play-file <known-onepiece.mkv>` → `get-player` playing, position advancing |
| op.play.sidecar_ok | SELF | `sidecar-get-process-state` running; `sidecar-get-ipc-latency` sane; no decoder-queue stall |
| op.play.seek_lands | SELF | `player-seek 600` → `get-player` position ≈600±5s |
| op.play.subs_present | SELF | `player-get-subtitle-tracks` ≥1 English track |
| op.play.video_paints | VISUAL | spot @ start: first frame painted, not black/frozen |
| op.play.subs_on_screen | VISUAL | spot @ a dialogue moment: English subtitle text visible |
| op.play.audio_present | VISUAL | spot sample: sound on the recording's audio track |
| op.play.seek_repaints | VISUAL | spot @ after seek: frame re-paints (not frozen on the old frame) |
| op.play.reaches_end | SELF | seek near end → playback reaches the end cleanly (position→duration, no hang) |
| op.resume.position | SELF | close+reopen player → resumes near last position |

(Playback rows are discrete spot-checks at sampled positions — not a playthrough.
"Does it stutter / look right / feel good" over a full episode = soak (§12) +
Hemanth's taste, not a normal-smoke assertion.)

Playback assertions run against a known-present episode via `play-file` so they do
not block on a multi-GB download finishing; `op.dl.*` tracks the download
independently.

### J2 — Western series (Torrentio, full chain)
Same shape as J1 minus anime specifics. Pick a standard SxxExx show.
| id | lane | assertion |
|---|---|---|
| w.search.returns | SELF | search returns the target show's imdb |
| w.detail.opens | SELF | detail opens; episodeRows>0 |
| w.dispatch.accepted | SELF | `dispatch-episode` → dispatched, infoHash set |
| w.dispatch.torrentio_source | SELF | picked release matches requested SxxExx (identity gate held) |
| w.dl.starts / w.dl.no_silent_stall | SELF | bytes climb / no silent stall |
| w.play.starts / seek_lands / sidecar_ok | SELF | as J1 |
| w.play.video_paints / smooth / audio_present | VISUAL | as J1 |
| w.resume.position | SELF | resume near last position |

### J3 — Addons / sources
| id | lane | assertion |
|---|---|---|
| ad.amatsu_enabled | SELF | addon registry (introspect / stream_addons.json read-back) shows `org.community.amatsu` enabled |
| ad.torrentio_enabled | SELF | `com.stremio.torrentio.addon` enabled |
| ad.cinemeta_enabled | SELF | `com.linvo.cinemeta` enabled |
| ad.no_dupe | SELF | no duplicate addon ids |

### J4 — Episode-state tiles (3-state)
| id | lane | assertion |
|---|---|---|
| tile.notdl_initial | SELF | a never-downloaded episode tile reports state=not-downloaded (`introspect-object EpisodeTile` devSnapshot) |
| tile.downloading_pct | SELF | during an active download, that episode's tile reports downloading + percent>0 |
| tile.play_when_done | SELF | a completed episode's tile reports Play state |
| tile.right_episode | SELF | tile↔episode-number mapping correct (no off-by-one) |
| tile.renders | VISUAL | tile states visually distinct (badge/percent/Play) |

### J5 — Season / pack dispatch
| id | lane | assertion |
|---|---|---|
| pack.dispatch_accepted | SELF | `dispatch-season <imdb> <season>` → dispatched |
| pack.episodes_register | SELF | episodes from the pack register in `get-downloads` / index |
| pack.no_verify_timeout | SELF | pack verification completes (flag if "pack verification timeout") |

### J6 — Player controls
| id | lane | assertion |
|---|---|---|
| pc.pause_resume | SELF | `player-pause` → position frozen; `player-resume` → advancing |
| pc.audio_switch | SELF | `player-get-audio-tracks` ≥1; `player-select-audio-track` changes active track |
| pc.sub_switch | SELF | `player-select-subtitle-track` changes active track |
| pc.speed / pc.volume | SELF | speed/volume set reflected in `get-player` |
| pc.sub_switch_visual | VISUAL | subtitle change visible on screen |

### J7 — Sidecar health (across playback)
| id | lane | assertion |
|---|---|---|
| sc.process_running | SELF | `sidecar-get-process-state` running during playback |
| sc.ipc_latency_sane | SELF | `sidecar-get-ipc-latency` under threshold; no growing backlog |
| sc.no_decoder_stall | SELF | decoder/render queues not stuck (frozen-PTS signature absent) |

### J8 — Tankorent standalone page (standalone scope)
| id | lane | assertion |
|---|---|---|
| tk.search_returns | SELF | `sources-search-tankorent "<term>"` returns results |
| tk.indexer_health | SELF | `sources-get-indexer-health` reports per-indexer up/unreachable honestly |
| tk.add_magnet | SELF | `sources-add-magnet <uri>` → appears in pending downloads |
| tk.pause_resume | SELF | `sources-pause-torrent` / `sources-resume-torrent` reflected in state |
| tk.remove | SELF | `sources-remove-torrent` removes it |
| tk.page_renders | VISUAL | results table painted |

## 6. Findings report format (traceable)

`out/smoke_<session>_findings.md` + `.jsonl`. One record per assertion:
```json
{ "id": "op.play.video_paints", "journey": "J1", "lane": "VISUAL",
  "verdict": "FAIL",
  "action": "play-file One Piece S?? (op.play.starts)",
  "ts": "2026-06-05T14:32:06Z",
  "video_offset": "00:18:12",
  "gemini": "Screen is solid black; no frames render for ~9s, then window title shows 'Not Responding'.",
  "log": "events.jsonl:2026-06-05T14:32:07Z HANG_DETECTED gui-thread blocked 9300ms",
  "stack_available": false,
  "note": "freeze — internal stack needs OBS-4 (out-of-process dump), not built." }
```
Every record carries the five-part chain (action → ts → video_offset → gemini/log
→ verdict). Records missing the `log-mark` join are dropped, not emitted.

## 7. Cost lever (visual lane)

The lever is **clip selection**, not API fps tiers (the harness calls no vision
API — Hemanth couriers clips to the Gemini LLM):

- The 2-hour recording is **never** sent for description. Only **flagged windows**
  are clipped: every VISUAL assertion's window, plus every SELF-fail window, plus
  every watchdog hit. Typically ~the dozen moments state+logs couldn't judge.
- Each clip is short (~window ± a few seconds) and named by assert-id, so Gemini's
  attention is directed and the courier payload stays small.
- The work-order marks SELF-failed/watchdog windows **LOOK HARD** so Gemini spends
  its detail where something already looked wrong on the data side.
- The full MP4 is retained on disk for the record, and every assertion window NOT
  clipped is listed in the work-order as "not flagged — passed on data side" (no
  silent truncation).
- **Short by construction — the real cost lever.** Normal smokes are journey-driven
  and fast (~20–30 min of driving for the whole catalogue); playback is spot-checks
  of *seconds*, never a playthrough. So clips are tiny and a Gemini review of a
  90-second journey is 90 seconds — the "describe 2 hours" problem does not exist
  in the default mode.
- **Speed levers (Agent 0, `c6f59d2` / runbook §3c) — SOAK mode only.** `tankoctl
  player-set-speed N` and `screen_record.py speedup` exist for the opt-in soak run
  (§12); the default smoke plays through nothing, so there is nothing to speed up.

## 8. Honest blind spot (design-around, not hide)

During a hard freeze the `introspect-*` channel is GUI-thread-bound and therefore
dead too. The harness still **detects** the freeze (watchdog JSON + `ping`
timeout + the recording's frozen frame) and reports it as a finding, but it
**cannot** read the internal call-stack in that moment — that needs the
out-of-process crash-dump path (OBS-4, not built). Freeze findings are flagged
`stack_available: false, note: needs OBS-4`. Never faked.

## 9. Sequence (explicit) + first proof run

Order is load-bearing:
- **(0) Validate the recorder FIRST.** Agent 0 is hardening `screen_record.py`
  now (he owns the drive harness). Before any visual-lane code, run a
  real-duration (~15 min), in-process/visible (never detached-hidden) recording
  and confirm it yields a complete, playable MP4 + that `ffmpeg -ss -t` can clip
  it. The whole visual lane is built on this; do not assume it works.
- **(1) One fully-traceable finding** on a SHORT ~15–20 min session running
  **J1 (One Piece) + J2 (Western)** — the five-part chain (action → ts → video
  offset → Gemini clip description → log line) — shown to Hemanth.
- **(2) Then scale** to long multi-journey runs (all J1–J8).

**Desktop coordination:** a long run holds the desktop — no other brother can
drive it meanwhile. Claim the MCP/desktop lane lease (Rules 19/22) for the run's
duration and release on completion.

Given the known idle-spin crash, the first traceable finding may well BE that
crash (a `blocked`/freeze finding flagged `needs OBS-4`) — an acceptable, valuable
proof that the chain works.

## 10. Reusability

Catalogue is per-domain data; runner/recording/visual/correlator/recovery are
domain-agnostic. Agents 1/2/3 reuse the harness by authoring
`catalogue_<domain>.json` and pointing the runner at it. The Stream catalogue is
the worked reference.

## 11. Dependencies / open items

- **Recorder (Agent 0-owned, in progress):** the visual lane sits on a hardened
  `screen_record.py`. Agent 0 is fixing the long/detached-recording + ddagrab
  flakiness. Agent 4 does NOT edit `screen_record.py` (avoid the shared-file
  collision) — Task 0 *validates* the hardened recorder before any visual-lane
  work. No vision API / no `GEMINI_API_KEY` needed: the visual lane is a manual
  courier of clips to the Gemini LLM (§4.1). If the recorder is unavailable, the
  SELF lane still runs fully; clips are flagged "recorder unavailable," not faked.
- **Desktop lane (Rules 19/22):** a long run holds the desktop — claim the
  MCP/desktop lease for the run and release on completion so no brother collides.
- Exact `tankoctl` verb names reconfirmed against live `ping` at execution.
- Stream widgets lacking `devSnapshot()` (blind spots found during J4) → small
  follow-on `IDevInspectable` adds, tracked separately.
- OBS-4 (out-of-process crash dump) is the upstream unlock for freeze call-stacks
  — out of scope here; flagged where it bites. The player idle-spin crash
  (Agent 3) is the active blocker that makes some playback smokes `blocked`.

## 12. Soak mode (opt-in, not default)

The default harness is fast + journey-driven and never runs long. A separate
**opt-in soak mode** catches *time-dependent* bugs a quick pass can't: the player
idle-spin crash (fires "minutes after open"), memory/handle growth, stutter that
only surfaces over time. Soak loops one journey (or holds a playback) for N
minutes under the same recorder + watchdog + log-mark machinery, periodically
sampling `sidecar-get-ipc-latency` + process memory, and reports any
`HANG_DETECTED` / growth / `blocked` event with its timestamp. The speed levers
(§7) live here. Soak is run occasionally and deliberately — never part of the
normal smoke, which plays through nothing.
