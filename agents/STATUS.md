# Agent Status

Each agent overwrites their own section at session start and end. Never append — overwrite your entry.
Last header touch: 2026-04-16 (Agent 0 — Track 1 cleanup; STATUS header field discipline introduced)
Last agent-section touch: 2026-04-16 (Agent 2 — EDGE_TTS_FIX_TODO end-to-end shipped in single session: Phases 1+2+3+5 CLOSED across 11 batches. Phase 5 today: 5.1 edgeDirect deletion + isInitDone export + 5.2 ttsSupported() rewrite + 5.3 failure-reason surface in voice picker + api_gateway.js comment refresh. Phase 4 streaming conditionally deferred per Rule 14. SIX READY TO COMMIT lines on the wire awaiting Agent 0 sweep + Hemanth full-TODO smoke.)

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
Status: ACTIVE — **EDGE_TTS_FIX_TODO end-to-end SHIPPED in single session** (Phase 4 conditionally deferred per my Rule 14 call). Phases 1+2+3+5 all CLOSED across 11 substantive batches. SIX READY TO COMMIT lines on the wire for Agent 0's sweep. Per contracts-v2 + Rule 15, no main-app build from agent side; awaiting Hemanth full-TODO smoke per `build_and_run.bat`.
Current task: Awaiting Hemanth full-TODO smoke. Phase 4 (streaming) deferred conditionally — only ships if smoke shows audible first-audio delay (cache + ~1-2s first-listen is within Readest reliability bar). Memory consolidation pending after green smoke: `project_book_tts_implemented` to be created; `project_tts_kokoro` to bump.
Active files (Phase 1 totals — 1576 lines new code across 4 new files + 3 modified):
- NEW src/core/tts/EdgeTtsClient.{h,cpp} (110 + 517 lines): Qt direct WSS client for Microsoft Edge Read Aloud — Sec-MS-GEC token (SHA256-of-(filetime-5min-rounded + token)), WSS handshake with Edge headers (User-Agent + Origin + MUID cookie), speech.config + SSML message builders (single-voice + single-prosody Edge consumer subset), binary frame parser (2-byte BE prefix + header + audio split), probe round-trip with QEventLoop + 8s timeout, 23-voice static table (en-US 13 + en-GB 5 + en-AU 2 + en-IN 3), synth Phase-2-pending stub.
- NEW src/core/tts/EdgeTtsWorker.{h,cpp} (64 + 73 lines): QObject wrapped onto QThread; 7 slots (probe/getVoices/synth/synthStream/cancelStream/warmup/resetInstance) + 9 signals (probeFinished/voicesReady/synthFinished/streamChunk/streamBound/streamEnded/streamError/warmupFinished/resetFinished); lazy EdgeTtsClient construction in ensureClient() so QWebSocket lives on the worker thread.
- MODIFIED src/ui/readers/BookBridge.h (+60 lines): 7 Q_INVOKABLE TTS *Start methods + 6 *Finished signals + 7 worker-handler private slots + QThread/Worker pointers + 4 m_pending*ReqId correlation slots + dtor declaration.
- MODIFIED src/ui/readers/BookBridge.cpp (+200 lines): ctor spawns parent-less Worker → moves to new QThread (parent=this) → wires 7 worker→bridge signal connections → starts thread; dtor quits + waits 5000ms + explicit-deletes worker (deleteLater-after-event-loop-exit not reliable in Qt 6); 7 *Start slot impls dispatch via QMetaObject::invokeMethod with Qt::QueuedConnection; 7 worker-handler slots re-package into QJsonObject + re-emit on matching *Finished signal. Known edge case: 8s probe timeout can outlive 5s wait → thread leak (no crash); deferred to Phase 5.3 cleanup if observed.
- MODIFIED src/ui/readers/BookReader.cpp (49-line IIFE replacing 9-line stub block at :169-177): closure-private _r resolver-map + _next reqId counter + _on signal-subscriber + _call(starter, args) helper; per-method shim for probe/getVoices/synth/synthStream/cancelStream/warmup/resetInstance; defaults preserved (voice=en-US-AriaNeural for probe, rate=pitch=1.0).
- MODIFIED CMakeLists.txt (6 additive touches across Phase 1): line 27 WebSockets find_package, lines 61-62 EdgeTtsClient.cpp + EdgeTtsWorker.cpp sources, lines 178-179 .h headers, lines 308-311 Qt6::WebSockets target_link + HAS_WEBSOCKETS=1 compile def.
Blockers: None. Awaiting Hemanth Phase 1 smoke + 1-line codec console check.
Next: On green smoke → Phase 2.1 (synth round-trip MP3 base64 — isolate-commit per TODO §2.1; reuses Batch 1.1 WSS handshake + message-build helpers; difference is accumulating ALL binary audio frames into a buffer + parsing audio.metadata frames into the boundaries array before emitting synthFinished with non-empty payload). On Phase 2 close → JS engine `tts_engine_edge.js:1086-1104` audioBase64 playback path lights up + sentence highlight via existing Foliate marks. Phase 3 (cache + voice/rate switching) follows.
Last session: 2026-04-16
Governance seen: gov-v3 | Contracts seen: contracts-v2

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
Status: ACTIVE — **regression investigation**. STREAM_LIFECYCLE_FIX_TODO shipped end-to-end this session (10 batches, Phase 1+2 prior + Phase 3+4+5 today, all audit P0/P1/P2 closed on Stream side), BUT Hemanth's smoke is surfacing 3 consecutive playback regressions: (1) blank 4-5 minute start before first frame, (2) stuck image with advancing clock, (3) eventual play with significant A/V desync. Single-root hypothesis: HTTP byte delivery is being interrupted mid-serve. Most plausible culprit on my side: Batch 3.3 wired `TorrentEngine::torrentError → StreamEngine::streamError → StreamPlayerController::onEngineStreamError → stopStream(Failure) → cancellation token flips`. Pre-3.3 that signal was unconnected; post-3.3 any transient libtorrent error tears the stream down.
Active HELP request: posted to Agent 3 in chat.md (this session's tail). Offered Hemanth a 1-line surgical revert test — comment out the ctor `connect(m_engine, &StreamEngine::streamError, this, &StreamPlayerController::onEngineStreamError)` in `StreamPlayerController.cpp`. Two outcomes: revert fixes → my 3.3 is the cause, I redesign with transient-vs-fatal filter; revert doesn't fix → Agent 3's domain (m_openPending gate from PLAYER_LIFECYCLE 3, SHM overlay from PLAYER_PERF 3.B, on_video_event restructure from PLAYER_UX 1, Shape 2 fence timing from PLAYER_LIFECYCLE 2.1).
Evidence standing: requested `_player_debug.txt` + `sidecar_debug_live.log` from failing runs. Not shipping new code until logs land per `feedback_evidence_before_analysis`.
Scope note: Stream mode only (Agent 4B owns Sources).
Current task: Standing by for either (a) Hemanth's revert-test result, or (b) log content. Neither unblocks without Hemanth's next input.
Active files (dirty, pre-sweep): `src/ui/pages/stream/StreamPlayerController.{h,cpp}` (Phase 3 clearSessionState + onEngineStreamError slot + ctor connect — the 3.3 ctor connect is the revert target). `src/ui/pages/StreamPage.cpp` (3.2 timer triple-gate + 4.1 Shift+N guard + 4.2 resetNextEpisodePrefetch routing). `src/core/stream/StreamEngine.{h,cpp}` + `src/core/stream/StreamHttpServer.{h,cpp}` (Phase 5 cancellation token + waitForPieces threading).
Blockers: Evidence gap. Can't fix what I can't see.
Ship discipline: 7 READY TO COMMIT lines still on the wire. Agent 0 should HOLD the sweep until the regression investigation resolves — if the fix is a revert-or-redesign on one of my batches, the sweep would need retraction. Better to not commit broken code.
Open debt (carried): unchanged from prior STATUS.
Next: Read logs when Hemanth shares. Redesign 3.3 with transient-error filter OR help Agent 3 narrow their suspects per the HELP request. No new STREAM work until this closes.
Last session: 2026-04-16
Governance seen: gov-v2 | Contracts seen: contracts-v2

---

## Agent 4B (Sources — Tankorent + Tankoyomi)
Status: ACTIVE — Agent 4's HELP REQUEST response: shipped 3-point qDebug instrumentation (diagnostic-only). TANKORENT_FIX_TODO COMPLETE status unchanged (7 phases / ~15 batches shipped). 8 READY TO COMMIT lines now on the wire for Agent 0's next sweep (7 Tankorent + 1 instrumentation).
Current task: Standing by for Hemanth to: (1) rebuild main app per contracts-v2 (2 files touched: stream + torrent engines), (2) repro the 0%-buffering scenario from same source, (3) share `_player_debug.txt` filtered to `[STREAM]` lines. Evidence-branch decisions documented in chat.md — 4 hypothesis branches each with a distinct log signature. Next session I consume the log + ship the surgical fix against the surviving hypothesis.
Instrumentation active files (diagnostic-only, remove after bug closes — all marked with `// STREAM diagnostic (Agent 4B — temporary trace ...)`): src/core/torrent/TorrentEngine.cpp (contiguousBytesFromOffset logs havePiece0/counted/fileSize when fileOffset==0), src/core/stream/StreamEngine.cpp (applyStreamPriorities logs selected/totalFiles; onMetadataReady Phase 2.2 block logs headRange/pieceCount + QDebug include).
Active files (Phase 2 Batch 2.1 — no UI change, pure data plumbing): src/core/TorrentResult.h (added QDateTime include + 3 new struct fields infoHash/publishDate/detailsUrl + canonicalizeInfoHash inline helper — 40-hex-lowercase strict), all 7 indexer .cpp files populate at parse time — NyaaIndexer (data-timestamp → publishDate, /view path → detailsUrl, btih regex → infoHash), PirateBayIndexer (info_hash/added/description.php?id=), YtsIndexer (hash/date_uploaded_unix/movie.url), EztvIndexer (btih regex only — relative date + no per-episode URL skipped), ExtTorrentsIndexer (btih regex + EXT_BASE+detailPath), TorrentsCsvIndexer (infohash/created_unix), X1337xIndexer (btih regex + X1337X_BASE+detailPath). Every indexer emits qDebug warning when infoHash empty (no user-visible error per TODO success criterion).
Phase 1 active files (still pending smoke): src/ui/pages/TankorentPage.h (dispatchIndexers private method declaration), src/ui/pages/TankorentPage.cpp (QHash/QSet includes; kMediaTypeIndexers allowlist at :703-713 videos/books/audiobooks/comics → indexer-id sets; dispatchIndexers helper at :715-754 filtering by allowlist ∩ source-combo, m_pendingSearches set before connect+search loop; startSearch refactored at :756-786 to read m_searchTypeCombo->currentData(), surface "No sources available for <type> search" on empty dispatch; 1337x stays commented in dispatch per TODO Batch 1.1 scope — Phase 4.3 re-enables).
Scope when execution resumes: `src/core/indexers/*`, `src/core/torrent/*`, `src/core/TorrentIndexer.h`, `src/core/TorrentResult.h`, `src/core/manga/*`, `src/ui/pages/SourcesPage.*`, `src/ui/pages/TankorentPage.*`, `src/ui/pages/TankoyomiPage.*`, `src/ui/dialogs/AddTorrentDialog.*`, `src/ui/dialogs/TorrentFilesDialog.*`, `src/ui/dialogs/AddMangaDialog.*`, `src/ui/dialogs/SpeedLimitDialog.*`, `src/ui/dialogs/SeedingRulesDialog.*`, `src/ui/dialogs/QueueLimitsDialog.*`.
Blockers: None.
Scope rationale: 2026-04-16 Hemanth — Tankorent + Tankoyomi (the "Sources" surface) split out from Agent 4's prior Stream-and-Sources combined scope. Agent 4 focuses on Stream mode going forward. Agent 4B owns Sources end-to-end: torrent indexers, torrent engine, manga scrapers, source-side UI pages, source-side dialogs.
Inherited open debt from Agent 4's Congress 4 observer position: (a) `TorrentClient::torrentCompleted → CoreBridge::rootFoldersChanged` wiring so finished downloads auto-rescan library, (b) `TorrentClient::downloadProgress(folderPath) → float` query contract for Agent 5's future list-view Download column. Both pre-parity, non-blocking. Will be addressed in-line with TANKORENT_FIX_TODO or on Agent 5's HELP request.
Co-ordination: Cross-agent with Agent 4 (split partner) if Stream mode consumes Tankorent indexers as a stream source. Cross-agent with Agent 5 when Sources pages need library-UX polish. Agent 6 gates every TODO phase exit.
Next: Validation findings block posted to chat.md as a single entry once audit read-through is complete. Agent 0 then authors `TANKORENT_FIX_TODO.md` (~8 phases / ~20 batches expected) and I execute phased per Rule 6 + Rule 11.
Last session: 2026-04-16 (TANKORENT_FIX_TODO closed + HELP REQUEST ACK'd from Agent 4 re: stream buffering regression)
Governance seen: gov-v2 | Contracts seen: contracts-v2

---

## Agent 5 (Library UX)
Status: ACTIVE — Stream main-page scroll parity fix shipped. Moved `m_searchBarFrame` out of `rootLayout` into `m_scrollLayout` as the first child of `m_browseScroll`'s content widget, matching Comics/Videos/Books where the search bar scrolls with content. Frame internal margins (20,20,20,8)→(0,20,0,0) to avoid doubling against `m_scrollLayout`'s (20,0,20,20). Search history dropdown re-verifies position via `m_searchInput->mapTo(this,…)` every show, so it re-anchors correctly with the scrolled bar. Knock-on: when search results overlay (StreamSearchWidget) shows, `m_browseScroll` hides so the bar hides too — user hits Back to refine (Option A per Hemanth). Awaiting main-app build + smoke.
Current task: Awaiting Hemanth smoke on StreamPage scroll-parity fix. On green → post READY TO COMMIT.
Active files: src/ui/pages/StreamPage.cpp (3 touches — -1 line in buildUI, margin change + comment refresh in buildSearchBar, +1 line in buildBrowseLayer).
Blockers: None.
Open debt (pre-parity, non-blocking):
 - BooksPage / ComicsPage Auto-rename + inline-rename parity (TileCard::beginRename is already generic, ready to wire when Hemanth asks).
 - Tankorent list-view "Download" column — consumer side of Agent 4B's `TorrentClient::downloadProgress(folderPath)` API (shipped Batch 7.2); wire via HELP on their side when I execute.
 - rootFoldersChanged auto-rescan already flows end-to-end (Agent 4B Batch 7.1); no code needed on my side.
Scope note: Per Hemanth 2026-04-14, Agent 5 owns ALL library-side UX across every mode (Comics, Books, Videos, Stream). Page-owning agents (1/2/3) own reader/player internals only. Do not defer library UX to them.
Last session: 2026-04-16
Governance seen: gov-v2 | Contracts seen: contracts-v2

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
