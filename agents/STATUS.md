# Agent Status

Each agent overwrites their own section at session start and end. Never append — overwrite your entry.
Last header touch: 2026-04-16 (Agent 0 — Track 1 cleanup; STATUS header field discipline introduced)
Last agent-section touch: 2026-04-16 (Agent 4 — posted HELP REQUEST to Agent 3 in chat.md: Hemanth's smoke surfaced 3 playback regressions (blank 4-5min start, stuck frame + advancing clock, A/V desync) across consecutive attempts. Single-root hypothesis = HTTP byte delivery interruption. My most plausible culprit is STREAM_LIFECYCLE 3.3's streamError wiring; offered Hemanth a 1-line comment-out revert test. If revert doesn't fix, asking Agent 3 to investigate their domain: m_openPending gate (PLAYER_LIFECYCLE 3), SHM overlay (PLAYER_PERF 3.B), on_video_event restructure (PLAYER_UX 1), Shape 2 fence timing (PLAYER_LIFECYCLE 2.1). Evidence request on _player_debug.txt + sidecar_debug_live.log.)

Per Rule 12: when you overwrite your own section, bump the `Last agent-section touch` line in the same edit. The header touch line is Agent 0's responsibility, bumped when anything outside an individual agent's section changes.

---

## Agent 0 (Coordinator)
Status: Active — Congress 4 OPEN
Current task: Library UX 1:1 Parity congress. Awaiting positions from Agents 1-5.
Active files: agents/CONGRESS.md, agents/STATUS.md, agents/chat.md
Blockers: None
Next: Synthesize positions once all agents have posted. Issue batched work orders.
Last session: 2026-03-26

---

## Agent 1 (Comic Reader)
Status: ACTIVE — COMIC_READER_AUDIT validation pass COMPLETE. 10/10 P1 CONFIRMED BROKEN, 3/3 TKMAX divergences CONFIRMED REGRESSED, 3 cross-surface P2 observations flagged for TODO authoring. Zero code touched per observation-only discipline. Awaiting Agent 0 step 3 (identity-direction question to Hemanth + COMIC_READER_FIX_TODO.md authoring).
Current task: Standing by for TODO to land.
Active files: None (read-only pass). Evidence spans src/ui/readers/ComicReader.{h,cpp}, src/ui/readers/ScrollStripCanvas.cpp, src/ui/readers/DecodeTask.cpp, src/core/ArchiveReader.cpp, src/ui/pages/SeriesView.cpp, src/ui/pages/ComicsPage.cpp.
Blockers: None. Gate is Hemanth identity-direction answer + Agent 0 authoring the phased TODO.
Next: Once TODO lands, execute phased per Rule 6 + Rule 11 with Agent 6 review per phase gate. Phase ordering will depend on identity call: Tankoban-Max internal-parity (default, filters + Mega Settings + loupe first) vs Mihon manga-depth (mode breadth + webtoon tuning first) vs YACReader desktop-polish (thumbnails + magnifier + CBR/RAR discovery first). Priority signal from findings: P1-1 (RTL persistence) + P1-10 (format discovery) are cheap cross-cutting fixes independent of identity call — both plausibly qualify as Phase 1 regardless.
Last session: 2026-04-16

---

## Agent 2 (Book Reader)
Status: ACTIVE — Phase 1 (1.1+1.2+1.3) + Phase 2 (2.1+2.2) + Phase 3 (3.1) + Phase 5 (5.1+5.2) all shipped. Phase 4 deferred per Hemanth (speculative optimization, simpler fallback path exists). BUILD_EXIT=0. Agent 6 is idle with my work at queue position 1 — awaiting Hemanth summon for review. Awaiting Hemanth smoke.
Current task: Awaiting smoke + Agent 6 summon. Phase 5 closes BOOK_READER_FIX_TODO: 5.1 added toolbar pin/unpin (Shift+T) on top of the pre-existing HUD auto-hide infrastructure (the audit's "toolbar always visible" was pattern-matching static HTML — auto-hide was already wired via reader_core.js:scheduleHudAutoHide + .br-hud-hidden CSS); 5.2 resolved the "scroll mode removed" comment drift — scrolled FLOW mode stays (supported by foliate-js + flowBtn toolbar toggle + Batch 3.1 keyboard awareness), only the `scrollContent()` helper was removed. Comments aligned.
Active files (Phase 5 on top of 1.1-3.1 — JS-only, synced to out/): resources/book_reader/domains/books/reader/reader_core.js (added toggleHudPin function respecting state.hudPinned; setHudVisible + scheduleHudAutoHide early-return when pinned; exported toggleHudPin + isHudPinned on window.booksReaderController), resources/book_reader/domains/books/reader/reader_keyboard.js (added Shift+T handler calling toggleHudPin + showing toast "Toolbar pinned" / "Toolbar auto-hide restored"), resources/book_reader/domains/books/reader/engine_foliate.js (updated two "scroll mode no longer available" comments to clarify scrollContent helper vs scrolled flow mode; removed redundant list marker comment), resources/book_reader/domains/books/reader/reader_nav.js (syncProgressOnActivity now has explanatory no-op comment instead of "scroll mode removed"; explains relocate-event drives progress sync for scrolled flow).
Blockers: None. Awaiting smoke for Rule 11 on all eight shipped batches (1.1+1.2+1.3+2.1+2.2+3.1+5.1+5.2).
Next: On green smoke, post EIGHT READY TO COMMIT lines. All shipped phases (1+2+3+5) READY FOR REVIEW to Agent 6 as one combined submission. Phase 4 (file loading) explicitly deferred — documented in chat.md rationale. BOOK_READER_FIX_TODO is done on my side.
Last session: 2026-04-15

---

## Agent 3 (Video Player)
Status: ACTIVE — **PLAYER_UX_FIX_TODO complete end-to-end** (Phase 1 + 2 + 3 + 4.1 + 5 + 6 all SHIPPED). Batch 4.2 split to its own follow-up SUBTITLE_GEOMETRY_FIX_TODO per Hemanth's Option A on the 4.2 decision. Phase 6 today: 6.1 chip state CSS + 6.4 popover dismiss unification + 6.2 IINA-parity Tracks metadata (sidecar demuxer enriched) + 6.3 EQ presets + custom profiles. Sidecar rebuilt BUILD_EXIT=0 twice. All 4 Phase 6 READY TO COMMIT lines on the wire; previous phase commits already landed via Agent 0 sweeps.
Current task: None — standing by for Hemanth main-app build + full PLAYER_UX_FIX smoke matrix (slow-open + file-switch + close + crash-recovery + HDR content + HDR dropdown + IINA-parity Tracks popover + EQ preset round-trip + popover dismiss consistency).
Active files (Phase 1): native_sidecar/src/main.cpp (new section 3a after line 331: write_event("tracks_changed") + write_event("media_info") with probe->* direct access; deleted counterparts from on_video_event lambda first_frame block; trimmed lambda capture from `[sid, shm_name, width, height, stride, slot_bytes, codec_name, tracks_payload, probe_hdr, probe_color_pri, probe_color_trc, probe_max_cll, probe_max_fall, probe_chapters]` to `[sid, shm_name, width, height, stride, slot_bytes, codec_name]`; removed 6 dead capture-helper locals at former line 334-339). src/ui/player/VideoPlayer.h (new signals playerOpeningStarted(QString) + playerIdle() in signals block after progressUpdated). src/ui/player/VideoPlayer.cpp (onStateChanged extended with `state=="opening"` + `state=="idle"` branches that debugLog + emit respective signals).
Blockers: None on my side.
Next: (1) Hemanth builds main app + smokes slow-open scenario (HEVC 10-bit ≥2GB or network URL). Expected _player_debug.txt trace: `SEND open → RECV state_changed{opening} → RECV tracks_changed → RECV media_info → [observable gap] → RECV first_frame → RECV state_changed{playing}`, plus `[VideoPlayer] state=opening file=<path>` debug line per open. (2) On green smoke, open Phase 2 (Loading UX — wire playerOpeningStarted to a "Loading <filename>" HUD widget; add sidecar on_video_event "buffering" case + Qt dispatch for stream-stall indicator).
Last session: 2026-04-16
Governance seen: gov-v2 | Contracts seen: contracts-v2

---

## Agent 4 (Stream mode)
Status: IDLE — **STREAM_LIFECYCLE_FIX_TODO CLOSED end-to-end 2026-04-16.** All 5 phases shipped across 2 sessions (Phase 1+2 prior, Phase 3+4+5 this session). Total 10 batches: 1.1, 1.2, 1.3 (PlaybackSession foundation + full consumer migration) → 2.1, 2.2 (StopReason enum + source-switch reentrancy split, P0-1 close) → 3.1, 3.2, 3.3 (clearSessionState helper + 3s failure timer generation-check + StreamEngine::streamError wiring, P1-1/P1-2/P1-4 close) → 4.1, 4.2 (Shift+N guard reshape + onSourceActivated routing, P2-2/P2-3 close) → 5.1, 5.2 (per-stream cancellation token + waitForPieces threading, P1-3 close). **Every audit finding (3 P0 + 5 P1 + 3 P2) is closed on Stream side.** (P0-2/P0-3/P1-5 remain Agent 3's PLAYER_LIFECYCLE scope — their Phase 3 just shipped this session too per their STATUS.) STREAM_UX_PARITY Batch 2.6 (Shift+N player shortcut) closed as byproduct of my 4.1 unblock — code was already in git from a prior session; only my guard fix was needed.
Ship discipline this session: 7 READY TO COMMIT lines on the wire (3.1, 3.2, 3.3, 4.1, 4.2, 5.1, 5.2-isolate). No main-app builds from bash per contracts-v2. Standing by for Hemanth build + smoke across the cumulative surface.
Scope note: Stream mode only (Agent 4B owns Sources).
Current task: None — STREAM_LIFECYCLE is done, STREAM_UX_PARITY is done. Standing by for next summon.
Active files (dirty, pre-sweep): `src/ui/pages/stream/StreamPlayerController.{h,cpp}` (Phase 3 clearSessionState helper + onEngineStreamError slot + ctor connect). `src/ui/pages/StreamPage.cpp` (Phase 3.2 3s timer triple-gate + Phase 4.1 Shift+N triple-gate + Phase 4.2 resetNextEpisodePrefetch routing). `src/core/stream/StreamEngine.{h,cpp}` (Phase 5.1 cancellation token + setStreamEngine wiring). `src/core/stream/StreamHttpServer.{h,cpp}` (Phase 5.2 StreamEngine back-pointer + waitForPieces cancelled param + handleConnection token capture). 7 files across 7 batches this session.
Blockers: None. Hemanth's build + behavioral smoke is the next gate. If anything regresses, I retract.
Sweep guidance for Agent 0: Batches 3.x could squash as "Phase 3" per Phase 1 precedent, or sweep individually per Rule 11. Batches 4.1/4.2 same. Batch 5.2 is TODO-designated isolate-commit per the "first cancellation-threading" rationale — should land as its own commit. No strong opinion on 5.1 isolate vs fold into 5.2's commit — 5.1 is API-only and inert without 5.2's consumer.
Open debt (carried, no urgency): rootFoldersChanged consumer hook, download column wiring (Agent 5), periodic manifest refresh, gear SVG, DANGER-red Uninstall ruling, `calendarVideosIds` e2e trace, Phase 6 REVIEW P2 context items, URI_COMPONENT_ENCODE_SET, normalizeManifestUrl intentional divergence. Failure-flow unification (merge onStreamStopped(Failure) + onStreamFailed) deferred indefinitely — current split works.
Next: Nothing queued. On next summon, ready for any Stream-mode fix / feature / polish work. Trigger B still suspended so I ship solo. Rule 6 + Rule 11 + `feedback_one_fix_per_rebuild` + `feedback_credit_prototype_source` all continue to apply.
Last session: 2026-04-16
Governance seen: gov-v2 | Contracts seen: contracts-v2

---

## Agent 4B (Sources — Tankorent + Tankoyomi)
Status: ACTIVE — TANKORENT_FIX_TODO COMPLETE. All 7 phases shipped across ~15 batches. Phase 7 closed with Batch 7.2: added TorrentClient::downloadProgress(folderPath) → float (size-weighted aggregation of listActive() progress across torrents saving under folderPath, same QDir::absolutePath + Qt::CaseInsensitive startsWith prefix match as Batch 7.1, returns 0.0 on no matches / 1.0 on all-complete / [0,1] otherwise). Pure API addition — Agent 5 consumes on their own timeline via HELP request when wiring the list-view Download column.
Current task: Awaiting Hemanth smoke + Agent 6 review across all 7 phases. Documented deferrals for polish follow-up: `.torrent` file/URL support (needs TorrentEngine::addTorrentFile), AddTorrentDialog Normal=1 quirk vs libtorrent-correct 4, "Harvesting..." panel indicator during Cloudflare harvest, GeoIP Peers-tab country integration, libtorrent 2.x API usage verification in Phase 6.3/6.4/6.5. Standing by for next summons — TANKORENT_FIX_TODO side of Sources is done; Tankoyomi + any other Agent 4B work remains in queue.
Active files (Phase 2 Batch 2.1 — no UI change, pure data plumbing): src/core/TorrentResult.h (added QDateTime include + 3 new struct fields infoHash/publishDate/detailsUrl + canonicalizeInfoHash inline helper — 40-hex-lowercase strict), all 7 indexer .cpp files populate at parse time — NyaaIndexer (data-timestamp → publishDate, /view path → detailsUrl, btih regex → infoHash), PirateBayIndexer (info_hash/added/description.php?id=), YtsIndexer (hash/date_uploaded_unix/movie.url), EztvIndexer (btih regex only — relative date + no per-episode URL skipped), ExtTorrentsIndexer (btih regex + EXT_BASE+detailPath), TorrentsCsvIndexer (infohash/created_unix), X1337xIndexer (btih regex + X1337X_BASE+detailPath). Every indexer emits qDebug warning when infoHash empty (no user-visible error per TODO success criterion).
Phase 1 active files (still pending smoke): src/ui/pages/TankorentPage.h (dispatchIndexers private method declaration), src/ui/pages/TankorentPage.cpp (QHash/QSet includes; kMediaTypeIndexers allowlist at :703-713 videos/books/audiobooks/comics → indexer-id sets; dispatchIndexers helper at :715-754 filtering by allowlist ∩ source-combo, m_pendingSearches set before connect+search loop; startSearch refactored at :756-786 to read m_searchTypeCombo->currentData(), surface "No sources available for <type> search" on empty dispatch; 1337x stays commented in dispatch per TODO Batch 1.1 scope — Phase 4.3 re-enables).
Scope when execution resumes: `src/core/indexers/*`, `src/core/torrent/*`, `src/core/TorrentIndexer.h`, `src/core/TorrentResult.h`, `src/core/manga/*`, `src/ui/pages/SourcesPage.*`, `src/ui/pages/TankorentPage.*`, `src/ui/pages/TankoyomiPage.*`, `src/ui/dialogs/AddTorrentDialog.*`, `src/ui/dialogs/TorrentFilesDialog.*`, `src/ui/dialogs/AddMangaDialog.*`, `src/ui/dialogs/SpeedLimitDialog.*`, `src/ui/dialogs/SeedingRulesDialog.*`, `src/ui/dialogs/QueueLimitsDialog.*`.
Blockers: None.
Scope rationale: 2026-04-16 Hemanth — Tankorent + Tankoyomi (the "Sources" surface) split out from Agent 4's prior Stream-and-Sources combined scope. Agent 4 focuses on Stream mode going forward. Agent 4B owns Sources end-to-end: torrent indexers, torrent engine, manga scrapers, source-side UI pages, source-side dialogs.
Inherited open debt from Agent 4's Congress 4 observer position: (a) `TorrentClient::torrentCompleted → CoreBridge::rootFoldersChanged` wiring so finished downloads auto-rescan library, (b) `TorrentClient::downloadProgress(folderPath) → float` query contract for Agent 5's future list-view Download column. Both pre-parity, non-blocking. Will be addressed in-line with TANKORENT_FIX_TODO or on Agent 5's HELP request.
Co-ordination: Cross-agent with Agent 4 (split partner) if Stream mode consumes Tankorent indexers as a stream source. Cross-agent with Agent 5 when Sources pages need library-UX polish. Agent 6 gates every TODO phase exit.
Next: Validation findings block posted to chat.md as a single entry once audit read-through is complete. Agent 0 then authors `TANKORENT_FIX_TODO.md` (~8 phases / ~20 batches expected) and I execute phased per Rule 6 + Rule 11.
Last session: 2026-04-16 (birth + first task accepted)

---

## Agent 5 (Library UX)
Status: IDLE — 2026-04-16 polish sweep committed (3b8faa9): TileCard text-zone, Comics ghost-folder + count-pill, Stream subtitle parity + column alignment + continue-watching pill. All six verified green by Hemanth in-session.
Current task: None — standing by for next polish pick or feature ask.
Active files: None (clean tree on Agent-5-owned files; git status shows only Agent 3 FrameCanvas/presenter + chat.md dirty).
Blockers: None.
Open debt (pre-parity, non-blocking):
 - BooksPage / ComicsPage Auto-rename + inline-rename parity (TileCard::beginRename is already generic, ready to wire when Hemanth asks).
 - Tankorent list-view "Download" column — consumer side of Agent 4B's `TorrentClient::downloadProgress(folderPath)` API (shipped Batch 7.2); wire via HELP on their side when I execute.
 - rootFoldersChanged auto-rescan already flows end-to-end (Agent 4B Batch 7.1); no code needed on my side.
Scope note: Per Hemanth 2026-04-14, Agent 5 owns ALL library-side UX across every mode (Comics, Books, Videos, Stream). Page-owning agents (1/2/3) own reader/player internals only. Do not defer library UX to them.
Last session: 2026-04-16

---

## Agent 6 (Objective Compliance Reviewer) — DECOMMISSIONED 2026-04-16 until further notice
Status: DECOMMISSIONED 2026-04-16 per Hemanth. Do not summon. Review workflow suspended; Hemanth approves phase exits directly via smoke. READY TO COMMIT (Rule 11) remains mandatory for all agents; READY FOR REVIEW lines are retired.
Current task: None. No new reviews accepted.
Active files: None. `agents/REVIEW.md` stays empty-template; `agents/review_archive/` preserved as historical record.
Blockers: Hemanth decision to redesign Agent 6's role or reactivate review protocol. Decommission is "until further notice" — not permanent retirement.
Queue (at time of decommission, preserved for potential reactivation): Agent 3 VIDEO_PLAYER_FIX Phases 5/2/4/7 pending; Agent 3 D3D11 Phase 7 cutover; Agent 4 STREAM_UX_PARITY Phase 4 on completion; Agent 1 COMIC_READER_FIX; Agent 3 PLAYER_PERF_FIX Phase 2 just shipped READY FOR REVIEW (treated as informational only now — no review will land).
Archived full roster (for historical reference): A1–A5 + B1–B5 + C1–C3 + Tankorent A–F + D3D11Widget Phases 1/2/3+4+5/6 + Tankostream Phases 1/2/3/4/5/6 + Player Polish Phases 1/2/3/4/5/6 + Book Reader Phases 1/2/3/5 + Stream UX Parity Phases 1+2+3 + VIDEO_PLAYER_FIX Phase 1 + VIDEO_PLAYER_FIX Phase 3. 13 reviews total.
Last session: 2026-04-16

---

## Agent 7 (Prototype Reference Author + Comparative Auditor — Codex)
Status: Active. Prototypes delivered across STREAM_PARITY_TODO.md Phases 1-6 (last run: Phase 6 Calendar, 2026-04-14). Audit mode now live as of 2026-04-15.
Current task: None — accepts three trigger types (see below).
Active files: agents/prototypes/ + agents/audits/ (exclusive), driven by AGENTS.md at repo root.
Blockers: None
Scope: Writes (a) reference-only prototype code, (b) comparative audit reports. Never touches src/, never touches agents/*.md (except prototypes/README.md + audits/README.md), never commits, never prescribes fixes. Isolated — not in anyone's reading order, not in Congress. Runs as a Codex CLI session.
Triggers: (A) Reactive — `REQUEST PROTOTYPE` line from a domain agent. (B) Proactive TODO-batch mode — point Codex at a TODO file; he walks unimplemented batches, capped one phase ahead of implementation frontier with drift-check gate. (C) Audit — `REQUEST AUDIT` line; Codex reads subsystem src/, web-searches cited reference apps, writes a structured comparative report to agents/audits/ with observations separated from hypotheses. See GOVERNANCE.md "PROTOTYPE + AUDIT Protocol" section.
Next: Likely first audit run is Agent 2's book reader vs Readest/Foliate (Hemanth-flagged — book reader is glitchy and needs gap analysis before a fix plan can be written).
Last session: 2026-04-14 (Phase 6 prototypes)
