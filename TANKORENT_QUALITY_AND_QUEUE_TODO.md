# TANKORENT_QUALITY_AND_QUEUE_TODO

**Owner:** Agent 4 (Stream + Tankorent)
**Authored:** 2026-05-27
**Spec:** [docs/superpowers/specs/2026-05-27-tankorent-quality-and-queue-design.md](docs/superpowers/specs/2026-05-27-tankorent-quality-and-queue-design.md)
**Plan:** [docs/superpowers/plans/2026-05-27-tankorent-quality-and-queue.md](docs/superpowers/plans/2026-05-27-tankorent-quality-and-queue.md)

## One-line scope

Make Tankorent search faithful to source-site results, recognize season packs, run downloads per-show-sequentially across parallel show lanes, and surface all of it inside Theatre's series-view source sidebar with a Netflix-clean Downloads page.

## Phase cursor

| Phase | Status | Tasks |
|-------|--------|-------|
| P1 — Per-show lane queue infrastructure | NOT STARTED | T1.1 – T1.11 |
| P2 — Nyaa parity audit | NOT STARTED | T2.1 – T2.5 |
| P3 — Tankorent as source-addon in Theatre series view | NOT STARTED | T3.1 – T3.4 |
| P4 — Pack detection + badges + filter chip | NOT STARTED | T4.1 – T4.5 |
| P5 — Theatre Downloads page Netflix revision | NOT STARTED | T5.1 – T5.4 |
| P6 — Other six indexer parity (Trigger E fan-out) | NOT STARTED | T6.1 – T6.7 |

## Standing context

- **Theatre only this arc.** Comics + Books series-view Tankorent addon = future Agent 1 / Agent 2 arcs. Comics + Books integration into TransferQueue = future, queue interface ships public in P1.
- **TDD applies to pure-logic primitives only** (TransferQueue, SeasonClassifier). UI / IPC / libtorrent-integration code ships under code-walk verification + Hemanth smoke per CLAUDE.md.
- **Per-task build gate:** ninja sub-target compile while BooksPage.cpp:69 wedge persists; full-link gate at end-of-phase once A2 fixes BooksPage.
- **Lane queue replaces libtorrent global `active_downloads=1` cap.** Per-show gating moves to `tankoban::queue::TransferQueue` (T1.6 reverts the global cap).

## Discovered findings

Captured as the arc progresses.

## Smoke checkpoints

- **End of P1:** Queue Daredevil S02 pack + Invincible S04 pack via Tankorent. Two cards visible in Theatre Downloads, both progressing in parallel; within each lane, one episode at a time.
- **End of P2:** Nyaa.si query "Daredevil" returns same row count as Tankorent's Nyaa indexer (within dedupe slack).
- **End of P3:** Open a show in Theatre → Tankorent section appears below Torrentio in Sources sidebar → click Search runs Nyaa query → results render → switch indexer dropdown → results clear with "Click Search to load."
- **End of P4:** Badges render with correct colors (multi-season green / single-season blue / episode grey); filter chips clear + force re-search; pack queue downloads E01 before E02.
- **End of P5:** Two parallel shows in Downloads each render as one card; no filenames anywhere; pause/cancel/reorder/bump all work.
- **End of P6:** Source-site parity confirmed for all 7 indexers.
