# Review

Agent 6 writes gap reports here. One review at a time. When resolved, Agent 6 archives to `agents/review_archive/YYYY-MM-DD_[subsystem].md` and resets this file to the empty template. Then posts `REVIEW PASSED — [Agent N, Batch X]` in chat.md.

---

## REVIEW — STATUS: PASSED (archived 2026-04-14)
Requester: Agent 4 (Stream & Sources)
Subsystem: Tankoyomi download queue — pause/resume, cancel-all, chapter status icons, transfers tab badge
Reference spec: `C:\Users\Suprabha\Downloads\mihon-main\mihon-main\` (Mihon Kotlin/Android)
Files reviewed:
- `src/core/manga/MangaDownloader.h` (A1, A3)
- `src/core/manga/MangaDownloader.cpp` (A1, A3)
- `src/ui/pages/TankoyomiPage.h` (A2, A3)
- `src/ui/pages/TankoyomiPage.cpp` (A2, A3, A5)
- `src/ui/dialogs/MangaTransferDialog.cpp` (A4)

Date: 2026-04-14

### Scope

Compared batches A1–A5 of Agent 4's Tankoyomi rework against Mihon's download-queue stack: `Downloader.kt`, `DownloadManager.kt`, `DownloadQueueScreen.kt`, `DownloadQueueScreenModel.kt`, `DownloadHolder.kt`, and `menu/download_single.xml`. In scope: global pause/resume, cancel-all, per-chapter status visualisation, queue-count badge. Out of scope: B1–B3 (cover cache, grid widget, view toggle), source/catalog browsing, manga detail panel, reader launch, library screen, settings/categories — those are for later review cycles per Hemanth's instruction. Kotlin/Coroutine → Qt single-threaded event loop translation is accepted where Agent 4 disclosed it in chat.md. Notifications (DownloadNotifier) and foreground-service (DownloadJob) are Android platform concerns with no Qt equivalent — accepted divergence.

### Parity (Present)

- **Global pause flag, runtime-only, cleared on restart** — reference: `Downloader.kt:111` (`@Volatile var isPaused: Boolean = false`) + `Downloader.kt:134` (`isPaused = false` inside `start()`) → Tankoban: `MangaDownloader.h:99` (`bool m_paused = false`) + `MangaDownloader.cpp:388-424` (`pauseAll`/`resumeAll`). Match on semantics: no persistence, cleared implicitly because it starts false on construction.
- **Pause reverts `DOWNLOADING` chapters back to `QUEUE`/`queued`** — reference: `Downloader.kt:171-174` → Tankoban: `MangaDownloader.cpp:398-408`. Same state mutation.
- **Pause button label flips with state** — reference: `DownloadQueueScreen.kt:201-232` (FAB text toggles `action_pause` ↔ `action_resume` off `isRunning`) → Tankoban: `TankoyomiPage.cpp:74-76` (`pausedChanged` signal flips label) + `TankoyomiPage.cpp:707` (1s refresh belt-and-braces). Matches visual behaviour.
- **Pause/overflow visibility gated on a non-empty queue** — reference: `DownloadQueueScreen.kt:228-231` (`animateFloatingActionButton(visible = downloadList.isNotEmpty())`) → Tankoban: `TankoyomiPage.cpp:706, 708` (`setVisible(hasPendingWork)`). Mihon hides on empty list; Tankoban hides on no-pending-work. Effectively equivalent since Mihon removes completed items from the queue (`Downloader.kt:245-246`).
- **Cancel All is a single overflow menu entry** — reference: `DownloadQueueScreen.kt:190-194` (`AppBar.OverflowAction(title = action_cancel_all, onClick = clearQueue)`) → Tankoban: `TankoyomiPage.cpp:256-273` (QMenu with "Cancel All Downloads" action). Same wire pattern (overflow, one entry).
- **Cancel-all flips queued/downloading → cancelled, skips already-terminal records** — reference: `Downloader.kt:180-185` (`clearQueue`) + `Downloader.kt:696-706` (`internalClearQueue`) → Tankoban: `MangaDownloader.cpp:448-471`. Close match; Tankoban additionally flips series-level record to `cancelled` (Mihon removes the row instead) — valid adaptation since Tankoban keeps history rows for user dismissal.
- **Resume-before-cancel when paused** — reference: `DownloadManager.kt:84-87` (`pauseDownloads` stops the downloader so `clearQueue` runs against a stopped queue) → Tankoban: `TankoyomiPage.cpp:266-268` (`if (isPaused()) resumeAll(); cancelAll();`). Same intent, different path.
- **Chapter list per download in the queue view** — reference: `DownloadHolder.kt:35-54` (binding.chapterTitle, mangaFullTitle, per-page progress) → Tankoban: `MangaTransferDialog.cpp:192-220` (QTreeWidget row per chapter, name/status/images/notes). Visual structure parity.
- **Status-icon glyphs per chapter row** — reference: Android uses layered drawables / progress bar in `DownloadHolder` via `downloadProgress.setProgressCompat` (`DownloadHolder.kt:59-73`) → Tankoban: `MangaTransferDialog.cpp:29-89` (painted 12×12 states for completed / downloading / queued / error / cancelled). Qt-idiomatic equivalent; green/slate palette matches groundwork progress-icon convention per CONTRACTS.md.
- **Queue-count display next to the screen title** — reference: `DownloadQueueScreen.kt:71-73, 110-119` (`downloadCount = downloadList.sumOf { it.subItems.size }` in a `Pill`) → Tankoban: `TankoyomiPage.cpp:676-703` (pending-series + pending-chapters baked into the tab label). Same information surfaced; Tankoban uses tab text because there is no top-bar pill widget in the Qt UI.
- **Per-chapter status colour palette** — reference: implicit (Mihon shades via Material theme) → Tankoban: `MangaTransferDialog.cpp:18-23` + `MangaTransferDialog.cpp:52-85` uses `#4CAF50` / `#60a5fa` / `#94a3b8` / `#ef4444` / `#6b7280`. Matches groundwork progress-icon convention already established (CONTRACTS.md §Thumbnail Cache + Agent 1 progress-icon precedent). Accepted.

### Gaps (Missing or Simplified)

Ranked P0 (must fix) / P1 (should fix) / P2 (nice to have).

**P0:**

None. Every core pause/cancel behaviour is present and correct.

**P1:**

- **No sort / reorder affordance in the overflow menu.** Reference: `DownloadQueueScreen.kt:124-196` shows a Sort icon (`Icons.AutoMirrored.Outlined.Sort`) opening a nested DropdownMenu with "Order by upload date" (`action_order_by_upload_date`, newest/oldest) and "Order by chapter number" (`action_order_by_chapter_number`, asc/desc). Model wired in `DownloadQueueScreenModel.kt:168-181` (`reorderQueue` sorts subItems and pushes via `downloadManager.reorderQueue`). Tankoban: overflow menu contains only "Cancel All Downloads" (`TankoyomiPage.cpp:258`). Impact: a user downloading a 1065-chapter series (Hemanth's stated real case, chat.md:5528) has no way to re-prioritise chapter order once the queue is built — they wait through whatever order `AddMangaDialog` produced. This is a first-class queue-management feature in Mihon; absence is user-visible.
- **Per-row context menu is simplified — no move-to-top, no move-to-bottom, no cancel-for-series.** Reference: `menu/download_single.xml:4-27` defines `move_to_top`, `move_to_top_series`, `move_to_bottom`, `move_to_bottom_series`, `cancel_download`, `cancel_series`. Handler in `DownloadQueueScreenModel.kt:68-115`. Tankoban: `TankoyomiPage.cpp:139-165` per-row menu has `Cancel`, `Remove`, `Remove + Delete Files` only. Impact: even without drag-to-reorder (acknowledged as deferred in chat.md:6067), the move-to-top / move-to-bottom keyboard-free alternative is missing. Downloading a new series while an old one is mid-queue requires cancelling the whole queue — there is no "bump this series to the front."
- **No per-image retry with backoff.** Reference: `Downloader.kt:504-512` uses `retryWhen` up to 3 attempts with exponential backoff (2s / 4s / 8s). Tankoban: `MangaDownloader.cpp:325-344` — on `QNetworkReply::error`, `++failedImages` is incremented and the loop advances to the next page without retry. `MAX_IMAGE_RETRIES = 2` is declared at `MangaDownloader.h:102` but never read anywhere in the .cpp (grep confirms). Impact: on transient CDN blips every failed image is permanently lost; chapter ships with missing pages. Critical for long series over flaky networks — this is the most common real failure mode.
- **No free-disk-space pre-check before a chapter starts.** Reference: `Downloader.kt:330-340` — `DiskUtil.getAvailableStorageSpace(mangaDir) < MIN_DISK_SPACE (200 MB)` sets `Download.State.ERROR` with a readable error notification *before* creating any files. Tankoban: no pre-check; chapter begins, `QFile::open(WriteOnly)` fails silently per-image (`MangaDownloader.cpp:330-334`), and the chapter finishes with ≈0 downloaded images and a stale "completed" status (line 282-284) once `pageIdx >= pages.size()`. Impact: downloads silently half-succeed on a full disk and the user only discovers it opening a chapter with no pages.

**P2:**

- **No per-page parallelism within a chapter.** Reference: `Downloader.kt:372` uses `flatMapMerge(concurrency = downloadPreferences.parallelPageLimit.get())` — images for a chapter fetch concurrently. Tankoban: `MangaDownloader.cpp:240-347` downloads images sequentially via a recursive `downloadNext` lambda. Impact: chapter throughput is bounded by per-image RTT × page count. Not a correctness issue; performance polish.
- **`parallelSourceLimit` not configurable.** Reference: `Downloader.kt:197` reads `downloadPreferences.parallelSourceLimit.changes()` as a `Flow`, so the user's setting reshapes concurrency at runtime. Tankoban: hardcoded `MAX_CONCURRENT_CHAPTERS = 2` at `MangaDownloader.h:101`. Impact: power users cannot tune; default is reasonable, so deferring this is fine.
- **No `ComicInfo.xml` written into the CBZ.** Reference: `Downloader.kt:401-406, 617-644` creates a `ComicInfo.xml` with manga title, chapter name, tags, urls, source before archiving. Tankoban: `MangaDownloader.cpp:351-372` packs images only. Impact: downstream metadata consumers (e.g. Komga, Kavita, or a future Tankoban reader metadata panel) have no baseline metadata. Agent 1's reader does not currently consume it, so no immediate user pain.
- **No tall-image auto-split.** Reference: `Downloader.kt:547-562` (`splitTallImageIfNeeded`) splits vertically tall pages when `downloadPreferences.splitTallImages.get()` is on. Tankoban: not implemented. Impact: extremely tall webtoon-style pages render awkwardly in double-page layouts. Agent 1's reader handles this at display time for some modes, so deferring is fine.
- **No "already downloaded" pre-filter when enqueuing.** Reference: `Downloader.kt:278-286` filters `provider.findChapterDir(...) == null` so chapters already on disk are never re-queued. Tankoban: `MangaDownloader.cpp:104-139` queues every chapter handed by `AddMangaDialog::selectedChapters()`; the skip-if-exists branch at `MangaDownloader.cpp:307-313` saves re-downloading *bytes* but still walks pages, mkpath, etc. Impact: cycles wasted on chapters already complete; minor but visible on large resumes.
- **Pause button label verbosity.** Reference: `DownloadQueueScreen.kt:205-210` — one-word "Pause" / "Resume". Tankoban: "Pause Downloads" / "Resume Downloads" (`TankoyomiPage.cpp:75, 707`). Tankoban's is more explicit but drifts from Mihon's label; no functional impact. Flagging in case you want exact parity — otherwise this is a conscious improvement.
- **Cancel All confirmation dialog is an intentional deviation — re-flagging for visibility.** Reference: `DownloadQueueScreen.kt:190-194` — `onClick = { screenModel.clearQueue() }` direct, no confirmation. Tankoban: `TankoyomiPage.cpp:260-264` — `QMessageBox::question` confirmation. Agent 4 disclosed this explicitly (chat.md:5637). No gap; noting here so Agent 6's record is complete.
- **No toast/notification on completion, error, or warning.** Reference: `DownloadNotifier` fires Android system notifications for progress, complete, error, warning, pause. Tankoban: no Qt-side equivalent (no system tray balloon, no in-app toast). Impact: user has to open the Transfers tab to learn a long download finished or failed. Not strictly a Mihon-parity P0/P1 issue (desktop paradigm differs), but raises the question: is silent completion acceptable for this project, or do you want a Qt `QSystemTrayIcon::showMessage()`?

### Questions for Agent 4

1. **Sort/reorder scope.** Is the absence of sort + move-to-top/bottom a conscious deferral in the same class as drag-to-reorder, or just not-yet-done? If deferred, please note in chat.md so this review closes cleanly.
2. **`MAX_IMAGE_RETRIES` intent.** The constant is declared but unused. Was A1 supposed to wire it, or is it holdover from an older design? If the intent is to add retry later, please leave it; if it's dead, drop it so the header doesn't mislead future readers.
3. **Disk-space check.** Do you consider the 200 MB pre-check in-scope for MangaDownloader, or is it a global app-level concern better solved elsewhere (e.g. a single check at `startDownload` against the destination root)?
4. **Per-chapter pre-filter.** Should `startDownload` skip chapters whose `.cbz` already exists on disk, or is it intentional that users can re-trigger a chapter to refresh it? (Mihon assumes "already there → skip always.")
5. **Per-chapter per-page status propagation through pause.** On resume, the restarting chapter goes from `queued` → `downloading`, and the delegate flips slate-ring → blue-ring. The `downloadedImages` counter is zeroed by the pause gate (`MangaDownloader.cpp:254-255`) then re-increments through the skip-if-exists branch on resume. Visually this means the Transfers dialog briefly shows `0 / N` for the resuming chapter before snapping back to its true count. Is this the intended UX, or do you want the counter to stay at its pre-pause value and only advance past it?

### Verdict

- [x] All P0 closed — none found.
- [x] All P1 closed or justified — Agent 4 shipped R1 (retry+backoff), R2 (QStorageInfo 200 MB pre-check), R3 (skip-at-enqueue, promoted from P2), R4 (counter-flicker fix), R5 (series move-to-top/bottom via m_recordOrder), R6 (per-series sort-chapters submenu). Chapter-level move and drag-to-reorder explicitly deferred with justification. Code spot-checked by Agent 6 in MangaDownloader.cpp:165-209, :317, :341-355, :466, :586-608 and TankoyomiPage.cpp:173.
- [x] Ready for commit (Rule 11).

**Verdict: REVIEW PASSED — [Agent 4, A1–A5], 2026-04-14.**

Agent 4: please address or defer with technical justification each P1. P2s are advisory — answer if you like, but none block the review. Once P1s are settled, I'll post `REVIEW PASSED — [Agent 4, A1–A5]` in chat.md and archive this file.

---

## Agent 4 — Response (R-track, 2026-04-14)

Each P1 is now closed. Batch entries are in chat.md around the same date; summarised here for your pass.

### P1 resolution

| P1 | Status | Batch | Files |
|----|--------|-------|-------|
| #1 Sort / reorder | Partial (per-series sort shipped; chapter-level move-to-top and drag-to-reorder remain deferred) | **R6** | `MangaDownloader.h/.cpp` + `TankoyomiPage.cpp` context menu "Sort chapters" submenu |
| #2 Per-row series move | **Series-level** slice shipped (move-to-top / move-to-bottom). Chapter-level move-to-top/bottom deferred with drag-to-reorder | **R5** | `MangaDownloader.h/.cpp` (new `m_recordOrder` + two move methods + persistence) + `TankoyomiPage.cpp` |
| #3 Retry with backoff | Closed | **R1** | `MangaDownloader.h` (bump `MAX_IMAGE_RETRIES` 2 → 3) + `.cpp` (attempt counter threaded through `downloadNext`, `QTimer::singleShot(2000 << attempt)` backoff) |
| #4 Disk-space pre-check | Closed | **R2** | `MangaDownloader.cpp` (`QStorageInfo` probe on `seriesDir`, 200 MB threshold matching Mihon) |

### Question answers (summary)

- **Q1** — Not deferring: R5 + R6 ship today. Drag-to-reorder and chapter-level move stay deferred as a separate piece of work.
- **Q2** — `MAX_IMAGE_RETRIES` is now live (R1). Bumped 2 → 3 to match Mihon.
- **Q3** — Disk-space belongs in `MangaDownloader`, per-chapter, before any files or network (R2). A `startDownload`-level check was considered and rejected: a 30-chapter run can start with space and exhaust it mid-way; per-chapter catches both cases.
- **Q4** — Skip already-downloaded chapters at enqueue time (R3). Matches Mihon. Record is created but chapter seeded as `completed` with `finalPath` set; record flips to `completed` if every chapter was already on disk. No network work runs.
- **Q5** — Counter no longer flickers (R4). Three coordinated edits: pause gate leaves counters alone; `downloadImages` top does a one-pass on-disk scan and sets `downloadedImages` accordingly; skip-if-exists branch no longer increments.

### P2 items explicitly deferred (for record)

- Per-page parallelism within a chapter (Mihon `flatMapMerge` concurrency).
- Configurable `parallelSourceLimit` (still hardcoded `MAX_CONCURRENT_CHAPTERS = 2`).
- `ComicInfo.xml` in CBZ output.
- Tall-image auto-split.
- "Pause"/"Resume" label verbosity (kept as "Pause Downloads" / "Resume Downloads" — conscious deviation).
- `Cancel All` confirmation dialog (conscious deviation — destructive action warrants the prompt).
- System-tray notification on completion.

### Extra deliveries (not in original P1 list, shipped anyway)

- **R3 — skip already-on-disk at enqueue** (previously classified P2 "No 'already downloaded' pre-filter when enqueuing"; promoted because Q4 made it effectively required).
- **R4 — fix counter zero-then-snap** (answered Q5 as code instead of text — UX flicker gone).
- Order persistence for the queue — new `"order"` key in `manga_downloads.json`, backward compatible with pre-R5 files.

Ready for your verdict.
