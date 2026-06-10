# REVIEW PACKET — THEATRE_STREAMING_RESTORE P2 (Sources picker, pick-first) + P3 (Tankorent tab)

**Requester:** Agent 4 (Opus) · **Date:** 2026-06-10 · **Reviewer: a DIFFERENT engine** (producer≠reviewer).
**Change class:** Theatre play-flow change (stream trigger) + Qt UI wiring → cross-engine review before merge.

## Context

Hemanth: "bring back sources, tankorent and stream server js." P0 restored the Stremio engine, P1 wired
streaming. This round (Hemanth's 2026-06-10 answers): **pick-first** sources + **per-source Play & Download**.

## P3 — Tankorent tab (trivial)
`SidebarDrawer.cpp`: removed `m_btnTankorent->setVisible(false)` and re-added `layout->addWidget(m_btnTankorent)`.
TankorentPage + its MainWindow nav wiring were kept intact through download-only; this only re-shows the entry.

## P2 — Sources picker, pick-first
Behavior change: a series episode click previously auto-streamed the best auto-pick (P1). Now series **and**
movies load + SHOW the Sources pane; nothing auto-plays. The user picks a source's **Play** (stream) or
**Download** (libtorrent).

1. **StreamDetailView::buildUI** — re-show the Sources pane. The right pane was fully removed in P4.1
   (`m_rightPaneStack`/`m_sourcesPanel` null; header+list constructed hidden, off-layout). Now: left col
   stretch 3, a new right col (stretch 2) with a visible "Sources" header + `m_sourcesList`. The list's four
   signals (sourceActivated / addToTankorentRequested / directDownloadRequested / autoLaunchCancelRequested)
   are wired as before. `m_rightPaneStack` stays null (season-pack TheatreDownloadPanel slide-in out of scope;
   StreamPage's panel wiring null-guards it).
2. **StreamSourceCard::buildUI** — added an action column with two QToolButtons: **Play** (emits existing
   `clicked` → StreamPage streams) + **Download** (emits existing `directDownloadRequested` → libtorrent).
   Whole-card left-click still emits `clicked` (Play). New QSS for the two buttons.
3. **StreamPage::onPlayRequested** — removed the series early-return that funneled to beginPlayOrDownload
   (auto-stream/download). Series now falls through to the same source-load+populate path as movies.
4. **StreamPage::onSourceActivated** — picking a source now `m_playerController->startStream(imdbId,
   mediaType, season, episode, choice.stream)` + buffer overlay, instead of `beginPlayOrDownload(&choice)`.

## Definition of Done — verify adversarially
1. Series episode click loads + shows sources, does NOT auto-play or auto-download. Movie detail-open still
   shows sources (unchanged). Owned/downloaded episodes still play locally (playLocalFileFromStreamRequested
   fires ahead of playRequested — confirm this path is untouched).
2. Per-source Play streams; per-source Download downloads. The two QToolButtons don't double-fire with the
   whole-card click (QToolButton consumes the press; card mouseReleaseEvent won't also fire).
3. The explicit download actions elsewhere (single/season/selected/bulk) still download (onSingleEpisode-
   DownloadRequested → startAutoDownload(forStream=false) → finishAutoDownloadPick download branch — intact).
4. No dangling/null deref: m_sourcesList/m_sourcesHeader now non-null + in-layout; setStreamSources /
   setStreamSourcesError / setStreamSourcesLoading still valid against the visible list.
5. Dead code now: `beginPlayOrDownload` (no callers) + finishAutoDownloadPick forStream=true branch (no
   caller). Harmless (member fn, no warning) — flagged for P2.x cleanup. Confirm nothing else calls them.
6. P3: un-hiding the tab can't break other modes (it's a global drawer entry; nav id "tankorent" handled).

## Concerns to probe
- onPlayRequested for series now runs beginSession + StreamAggregator::load on every episode click — any
  perf/re-entrancy issue vs the old early-return? (The movie path already did this.)
- Does removing the series early-return leave any series-specific assumption downstream in onPlayRequested?

## Diff
See `agents/audits/theatre_restore_p2_diff.txt` (StreamSourceCard / StreamDetailView / StreamPage / SidebarDrawer).
