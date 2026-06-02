# Night Watch — Backlog

Status: `[ ]` todo · `[~]` in-flight (one owner working) · `[s]` staged-for-morning · `[x]` auto-committed (green-gated) · `[!]` failed (see report).
The foreman picks the next `[ ]` per track each tick, marks it `[~]`, and records the outcome. Rules: spec `docs/superpowers/specs/2026-06-02-night-watch-design.md`.

## Track A — App heaviness (execute; low-risk auto-commit, behavior/UI → stage)
Owner per area in brackets. Seeded from the 2026-06-02 stability sweep + recap stability queue.
- [~] **stream** StreamDetailView episode-list refresh storm — 1Hz timer × ~1100 rows × QFileInfo+listActive+snapshot per row [agent4] — *fix built 21:50, One Piece smoke pending; near-done*
- [ ] **comics** `AniListClient.cpp:132` + `MangaUpdatesClient.cpp:99` `QThread::msleep(1000)` on the GUI thread (1s freeze per metadata lookup) [agent1]
- [ ] **books** `BooksPage.cpp:1061` `validateAll()` sync on GUI (N×QFileInfo::exists) — Stream does this via QtConcurrent, Books doesn't [agent2]
- [ ] **books** `TankoLibraryPage.cpp:1408` `existingCachedCoverPath()` QDir::entryList per row on GUI [agent2]
- [ ] **books** `BooksCatalogueLibraryStore.cpp:208` synchronous save on GUI [agent2]
- [ ] **theme** `ThemePicker.cpp:38/72` QSettings opened ×2 per interaction [agent5]
- [ ] **theme** `Theme.cpp:1127` `tintedSvgIcon` synchronous render on GUI [agent5]
- [ ] **player** `FrameCanvas.cpp` present-cadence P2 — verify present p50/p99 telemetry under TANKOBAN_DEBUG_LOG=1 [agent3]
- [ ] **scan** fresh heavy-area sweep: grep src/ for sync I/O / QThread::msleep / per-row QFileInfo|QDir|SQL on the GUI thread — find areas NOT yet listed [foreman→fan-out]

## Track C — Office reliability (research+execute; low-risk auto-commit, structural → stage)
Source: `agents/audits/office_reachability_hardening_2026-06-02.md` (22 findings). #1/#2/#3/#9/#19 already landed this session.
- [ ] **#5** wrong-engine guard: closed agent7(Codex)/agent9(DeepSeek) summon spawns a `claude -p --model opus` impostor — `classify_summon` NON_CLAUDE guard + spawn_brother refuse [agent0/foreman]
- [ ] **#6** re-check `_is_live` before firing fallback (don't duplicate-spawn a heads-down live brother) [agent0/foreman]
- [ ] **#13/#14/#15** asks-escalation hygiene: sentence-final `?` + no-rush suppressor + owed-ask expiry (`office_asks.py`) [agent0/foreman]
- [ ] **#7/#8** orphan-watch reaper + watch singleton (`office_watch.sh`) [agent0/foreman]
- [ ] **#10/#11** unify liveness window + roster activity-fallback (`office_status.py`) [agent0/foreman]
- [ ] **#12** summon-fate trail (`agents/.office_delivery.log`) [agent0/foreman]
- [ ] **#16/#18/#20/#21/#22** P2 tail (concurrency cap / model validation / atomic beat / force-kill cursor / lease-exemption) [agent0/foreman]

## Track B — Repo legibility (research; PROPOSE+STAGE only)
- [~] research workflow `wf_90f53135-c76` running (our repo + external best-practice → prioritized legibility backlog + agent-map draft). Foreman: collect on completion → stage proposals → launch the next deeper pass.

## Track D — Observability layer / "eyes closed" (research; propose+stage, additive read-only may auto-apply)
- [~] Agent 5 on it (summon 848); brief `agents/night_ops/briefs/D_observability_layer.md`. Pillars: tankoctl-saturation + perf-sampling + hang-capture + visual-verify + telemetry. Foreman: keep Agent 5 fed (re-summon next pillar when he reports), collect designs → stage.
