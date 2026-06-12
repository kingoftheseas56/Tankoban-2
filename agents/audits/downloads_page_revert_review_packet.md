# REVIEW: Surgical revert of DOWNLOADS_OVERHAUL_V2 UI (keep engine functions)

## Context
Hemanth smoked the Downloads command center (DOWNLOADS_OVERHAUL_V2, f9cd1c5..29e2616) and
rejected the UI: the right detail pane showed only title+progress+Pause/Cancel (tabs never
populated for him) and the page didn't earn its complexity. Decision (approved verbatim):
"revert the page + detail pane (the cosmetic UI you didn't like) and keep the engine functions".

## Definition of Done
1. StreamDownloadsPage.{cpp,h} restored byte-identical to pre-arc 415f4fa (the read-only page).
2. DownloadDetailPane.{cpp,h} + DownloadsCommandModel.{cpp,h} + test_downloads_command_model.cpp DELETED,
   CMake entries removed (TankobanSources.cmake x4, TankobanTests.cmake x2).
3. KEEP-SET MUST SURVIVE FUNCTIONAL:
   - TransferQueue global max-active cap (T1) + MainWindow setMaxActive(QSettings downloads/maxActive, 3)
   - SeasonCheckoutPanel + StreamPage::openSeasonCheckout/executeCheckoutPlan (T8/T9) — wired from series view
   - Queued display state (T2) + instant Queued feedback (T10) — series-view buttons
   - StreamDownloadIndex Failed entries + markFailedByGroup (T3.1/T3.2) — engine-side
   - TorrentClient promotion-aware gates (T1) untouched
4. MainWindow retryEpisodeRequested connect removed (old page lacks the signal);
   StreamPage::retryEpisodeDownload removed (dead — only caller was that connect).
5. No edits to other agents' dirty files (HangWatchdog.*, GlassBackground.*).
6. Old page compiles against today's engine: uses only StreamDownloadIndex::all(),
   TorrentClient::streamBulkGroups(), MetaAggregator::fetchMetaItem + signals
   entriesChanged/streamBulkGroupsChanged/metaItemReady — all verified present at HEAD.

## Review asks (in priority order)
1. Any REMAINING reference to the deleted classes/signals that will fail at runtime (not compile)?
   e.g. QMetaObject::invokeMethod by name, string-based connects, dev-bridge introspection paths,
   tankoctl snapshot code naming the new page widgets/objectNames.
2. Does any kept arc code now MISBEHAVE without the new page? e.g. markFailedByGroup produces
   index entries the old page renders wrongly; Queued state rows that the old page's status
   text doesn't know; checkout dispatches that relied on page-side cleanup.
3. Is the old page's reading of streamBulkGroups()/all() still correct given arc-era engine
   changes (lane-key changes in T11.1, episode stamping in T10.1)?
4. Anything else severed incorrectly.

## Diff (working tree vs HEAD 29e2616+, excludes other agents' HangWatchdog/GlassBackground)
diff --git a/cmake/TankobanSources.cmake b/cmake/TankobanSources.cmake
index 6b14e5c..71e4533 100644
--- a/cmake/TankobanSources.cmake
+++ b/cmake/TankobanSources.cmake
@@ -223,7 +223,6 @@ set(SOURCES
     src/core/stream/StreamPackParser.cpp
     src/core/stream/SubtitlesAggregator.cpp
     src/core/stream/StreamDownloadIndex.cpp
-    src/core/stream/DownloadsCommandModel.cpp
     src/core/stream/QualityScorer.cpp
     src/core/stream/AutoSourcePicker.cpp
     src/core/stream/EpisodeDisplayState.cpp
@@ -236,8 +235,6 @@ set(SOURCES
     src/ui/pages/StreamPage.cpp
     src/ui/pages/stream/StreamLibraryLayout.cpp
     src/ui/pages/stream/StreamDownloadsPage.cpp
-    # DOWNLOADS_OVERHAUL_V2 T5 (2026-06-11) — right pane detail view.
-    src/ui/pages/stream/DownloadDetailPane.cpp
     # DOWNLOADS_OVERHAUL_V2 T8 (2026-06-11) — pack-first Season Checkout dialog.
     src/ui/pages/stream/SeasonCheckoutPanel.cpp
     src/ui/pages/stream/StreamSearchWidget.cpp
@@ -445,7 +442,6 @@ set(HEADERS
     src/core/stream/StreamPackParser.h
     src/core/stream/SubtitlesAggregator.h
     src/core/stream/StreamDownloadIndex.h
-    src/core/stream/DownloadsCommandModel.h
     src/core/stream/QualityScorer.h
     src/core/stream/AutoSourcePicker.h
     src/core/stream/UnifiedProgressStore.h
@@ -457,8 +453,6 @@ set(HEADERS
     src/ui/pages/StreamPage.h
     src/ui/pages/stream/StreamLibraryLayout.h
     src/ui/pages/stream/StreamDownloadsPage.h
-    # DOWNLOADS_OVERHAUL_V2 T5 (2026-06-11) — right pane detail view.
-    src/ui/pages/stream/DownloadDetailPane.h
     # DOWNLOADS_OVERHAUL_V2 T8 (2026-06-11) — pack-first Season Checkout dialog.
     src/ui/pages/stream/SeasonCheckoutPanel.h
     src/ui/pages/stream/StreamSearchWidget.h
diff --git a/cmake/TankobanTests.cmake b/cmake/TankobanTests.cmake
index b1234d3..08c1b43 100644
--- a/cmake/TankobanTests.cmake
+++ b/cmake/TankobanTests.cmake
@@ -26,7 +26,6 @@ if(TANKOBAN_BUILD_TESTS)
         tests/core/stream/test_quality_scorer.cpp
         tests/core/stream/test_auto_source_picker.cpp
         tests/core/stream/test_episode_display_state.cpp
-        tests/core/stream/test_downloads_command_model.cpp
         tests/core/stream/test_anime_catalog_resolver.cpp
         tests/core/stream/test_stream_download_index_dedup.cpp
         tests/core/stream/test_stream_pack_parser.cpp
@@ -108,7 +107,6 @@ if(TANKOBAN_BUILD_TESTS)
         src/core/stream/QualityScorer.cpp
         src/core/stream/AutoSourcePicker.cpp
         src/core/stream/EpisodeDisplayState.cpp
-        src/core/stream/DownloadsCommandModel.cpp
         src/core/stream/AnimeCatalogResolver.cpp
         src/core/stream/AnimeIdMapCache.cpp
         src/core/stream/StreamDownloadIndex.cpp
diff --git a/src/core/stream/DownloadsCommandModel.cpp b/src/core/stream/DownloadsCommandModel.cpp
deleted file mode 100644
index fd93f98..0000000
--- a/src/core/stream/DownloadsCommandModel.cpp
+++ /dev/null
@@ -1,83 +0,0 @@
-// DOWNLOADS_OVERHAUL_V2 (2026-06-11) — pure aggregation for the Downloads
-// command center. See DownloadsCommandModel.h for the full contract.
-#include "core/stream/DownloadsCommandModel.h"
-#include <algorithm>
-
-namespace tankostream::stream {
-
-const tankoban::queue::TransferItem* laneItemFor(
-    const QHash<QString, tankoban::queue::TransferLane>& lanes,
-    const QString& imdbId, int season, int episode)
-{
-    const auto it = lanes.constFind(QStringLiteral("imdb:") + imdbId);
-    if (it == lanes.constEnd()) return nullptr;
-    for (const auto& item : it->items) {
-        const int s = item.seasonNumber.value_or(0);
-        if (s != season) continue;
-        if (!item.episodeNumber.has_value()) return &item;   // season pack
-        if (item.episodeNumber.value() == episode) return &item;
-    }
-    return nullptr;
-}
-
-QList<DownloadRow> buildDownloadRows(const DownloadsSnapshot& snap,
-                                     qint64 nowMs, qint64 maxCompletedAgeMs)
-{
-    using tankoban::queue::TransferState;
-    QList<DownloadRow> rows;
-    rows.reserve(snap.indexEntries.size());
-
-    for (const auto& e : snap.indexEntries) {
-        DownloadRow r;
-        r.imdbId = e.imdbId; r.showTitle = e.imdbId; r.type = e.type;
-        r.season = e.season; r.episode = e.episode;
-        r.canonicalPath = e.canonicalPath; r.pct = e.progressPct;
-        r.addedAt = e.addedAt;
-        r.sourceGroupId = e.sourceGroupId;
-
-        const auto* li = laneItemFor(snap.lanes, e.imdbId, e.season, e.episode);
-        if (li) r.infoHash = li->transferId;
-
-        if (e.state == StreamDownloadIndex::Entry::Complete) {
-            if (maxCompletedAgeMs > 0 && nowMs - e.addedAt > maxCompletedAgeMs)
-                continue;   // display trim only — the index record stays
-            r.section = DownloadSection::Completed;
-        } else if (li && li->state == TransferState::Failed) {
-            r.section = DownloadSection::Failed;
-        } else if (li && li->state == TransferState::Paused) {
-            r.section = DownloadSection::Active;
-            r.paused = true;
-        } else if (li && li->state == TransferState::Queued) {
-            r.section = DownloadSection::Queued;
-        } else if (e.state == StreamDownloadIndex::Entry::Downloading
-                   || (li && li->state == TransferState::Running)) {
-            // Index Downloading with NO lane item is deliberate Active: after an
-            // app restart resumed torrents download with an empty queue — the
-            // transfer genuinely runs in the engine and progress keeps flowing
-            // via updateEpisodeProgress (review I1, plan-owner decision).
-            r.section = DownloadSection::Active;
-        } else if (e.state == StreamDownloadIndex::Entry::Failed) {
-            // Failure normally arrives via the INDEX, not the lane: TransferQueue
-            // erases items on terminal states (finishCurrent/cancel), so lanes
-            // snapshots never carry Failed in production — the lane-Failed branch
-            // above is defensive only. Lane branches stay ABOVE this one so a
-            // retry-re-queued episode (index still Failed, lane Queued/Running)
-            // shows Queued/Active, not Failed. (Review C1.)
-            r.section = DownloadSection::Failed;
-        } else {
-            r.section = DownloadSection::Queued;   // Pending, lane not visible yet
-        }
-        rows.append(r);
-    }
-
-    std::stable_sort(rows.begin(), rows.end(),
-        [](const DownloadRow& a, const DownloadRow& b) {
-            if (a.section != b.section) return a.section < b.section;
-            if (a.imdbId != b.imdbId)   return a.imdbId < b.imdbId;
-            if (a.season != b.season)   return a.season < b.season;
-            return a.episode < b.episode;
-        });
-    return rows;
-}
-
-}  // namespace tankostream::stream
diff --git a/src/core/stream/DownloadsCommandModel.h b/src/core/stream/DownloadsCommandModel.h
deleted file mode 100644
index 60b64d0..0000000
--- a/src/core/stream/DownloadsCommandModel.h
+++ /dev/null
@@ -1,79 +0,0 @@
-#pragma once
-// DOWNLOADS_OVERHAUL_V2 (2026-06-11) — pure aggregation for the Downloads
-// command center. Inputs are plain snapshots (testable without TorrentClient);
-// output is the status-sectioned, show-grouped row list the page renders.
-#include "core/stream/StreamDownloadIndex.h"
-#include "core/queue/TransferLane.h"
-#include <QHash>
-#include <QList>
-#include <QMetaType>
-#include <QString>
-
-namespace tankostream::stream {
-
-enum class DownloadSection { Failed, Active, Queued, Completed };
-
-struct DownloadRow {
-    QString imdbId;
-    QString showTitle;        // enriched later by the page (meta cache); imdbId fallback
-    QString type;             // "series" | "movie"
-    int     season = 0;
-    int     episode = 0;
-    QString infoHash;         // carrying transfer (empty when none, e.g. old history)
-    QString sourceGroupId;    // index entry's group ("tankorent:<infohash>"; may be empty)
-    QString canonicalPath;    // for Play on Completed rows
-    DownloadSection section = DownloadSection::Completed;
-    int     pct = 0;
-    bool    paused = false;
-    qint64  addedAt = 0;      // Completed auto-trim key
-};
-
-// Group convention is "tankorent:<lowercase-infohash>", stamped at the
-// TorrentClient registration sites (registerEpisode / registerPendingEpisode /
-// registerPendingMovie / markFailedByGroup callers). Returns the infohash for
-// tankorent groups; empty for anything else (e.g. migration-rescued entries).
-// Lets the Downloads page derive an engine hash for orphan rows whose lane
-// item is gone (T6 review C2/I1).
-inline QString infoHashFromGroup(const QString& sourceGroupId)
-{
-    return sourceGroupId.startsWith(QStringLiteral("tankorent:"))
-        ? sourceGroupId.mid(10)
-        : QString();
-}
-
-struct DownloadsSnapshot {
-    QList<StreamDownloadIndex::Entry>                 indexEntries;  // StreamDownloadIndex::all()
-    QHash<QString, tankoban::queue::TransferLane>     lanes;         // TransferQueue::lanesSnapshot()
-};
-
-// Find the lane item carrying (season, episode) for this show, if any. Season
-// packs carry no episodeNumber — they match any episode of their season.
-// Movie rows (season 0, episode 0) match a lane item with nullopt seasonNumber
-// via the same path: seasonNumber.value_or(0) == 0, and the item's missing
-// episodeNumber matches like a season pack does. When a lane holds both a
-// pack and a specific-episode item for the same season, lane list order wins
-// (first match is returned).
-// LIFETIME: the returned pointer aliases storage inside `lanes` — it is valid
-// only while the passed hash is alive and unmodified.
-// Exposed for reuse by the episode-row state gatherer (click-feedback task).
-const tankoban::queue::TransferItem* laneItemFor(
-    const QHash<QString, tankoban::queue::TransferLane>& lanes,
-    const QString& imdbId, int season, int episode);
-
-// Section rules (spec §3.1 + §6): Failed/Paused/Queued/Running come from the
-// lane item state of the episode's carrying transfer when one exists; otherwise
-// the index state drives. Index Complete -> Completed, trimmed when older than
-// maxCompletedAgeMs (0 = no trim). Index Failed -> Failed (lanes erase items on
-// terminal states, so failure normally arrives via the index; a lane item, e.g.
-// a retry re-queue, takes precedence). Index Downloading with no lane item ->
-// Active (orphaned resume — the engine still runs the transfer; review I1).
-// Index Pending with a Queued lane item (or no lane item yet) -> Queued.
-// Rows sort: section order Failed->Active->Queued->Completed, then imdbId,
-// then season, episode.
-QList<DownloadRow> buildDownloadRows(const DownloadsSnapshot& snap,
-                                     qint64 nowMs,
-                                     qint64 maxCompletedAgeMs);
-
-}  // namespace tankostream::stream
-
-Q_DECLARE_METATYPE(tankostream::stream::DownloadRow)
diff --git a/src/ui/MainWindow.cpp b/src/ui/MainWindow.cpp
index a3742bb..e9c6b18 100644
--- a/src/ui/MainWindow.cpp
+++ b/src/ui/MainWindow.cpp
@@ -928,14 +928,6 @@ void MainWindow::buildPageStack()
     });
     connect(m_streamDownloadsPage, &StreamDownloadsPage::playLocalFileRequested,
             this, &MainWindow::onPlayLocalFileFromStreamRequested);
-    // DOWNLOADS_OVERHAUL_V2 T6 — retry a failed download: the Downloads page
-    // cleans up the failed transfer (engine + index) before emitting; route
-    // the re-pick into StreamPage.
-    connect(m_streamDownloadsPage, &StreamDownloadsPage::retryEpisodeRequested,
-            this, [this](const QString& imdbId, int season, int episode) {
-                if (m_streamPage)
-                    m_streamPage->retryEpisodeDownload(imdbId, season, episode);
-            });
     dbg("4g4-streamdownloadspage-created");
 
     // COMICS_DOWNLOADS_SIDEBAR_PAGE 2026-05-26 (Agent 9) - Comics-mode
diff --git a/src/ui/pages/StreamPage.cpp b/src/ui/pages/StreamPage.cpp
index fb985fe..3bd8e05 100644
--- a/src/ui/pages/StreamPage.cpp
+++ b/src/ui/pages/StreamPage.cpp
@@ -3310,19 +3310,6 @@ void StreamPage::executeCheckoutPlan(const QString& imdbId, int season,
     }
 }
 
-// DOWNLOADS_OVERHAUL_V2 T6 — re-run the auto source pick for a failed episode.
-// The Downloads page cleans up the failed transfer (engine + index) before
-// emitting retryEpisodeRequested. We just need to restart the source-pick
-// pipeline for the correct media type.
-void StreamPage::retryEpisodeDownload(const QString& imdbId, int season, int episode)
-{
-    // season==0 && episode==0 means a movie; anything else is a series episode.
-    const QString mediaType = (season == 0 && episode == 0)
-        ? QStringLiteral("movie")
-        : QStringLiteral("series");
-    startAutoDownload(imdbId, mediaType, season, episode, /*forStream=*/false);
-}
-
 // THEATRE_DOWNLOAD_OVERHAUL UI refinement 2026-05-17 - movie auto-dispatch fast
 // path. Picking already done emitter-side in StreamDetailView (top-seeded
 // magnet selected from m_lastChoices, which is pre-sorted by buildPickerChoices
diff --git a/src/ui/pages/StreamPage.h b/src/ui/pages/StreamPage.h
index 0c016b3..841a51f 100644
--- a/src/ui/pages/StreamPage.h
+++ b/src/ui/pages/StreamPage.h
@@ -106,11 +106,6 @@ public:
     // app session.
     StreamLibrary* streamLibrary() const { return m_library; }
 
-    // DOWNLOADS_OVERHAUL_V2 T6 — re-run the auto source pick for a failed
-    // episode. The failed transfer was already cancelled/cleaned by the
-    // Downloads page before this is called.
-    void retryEpisodeDownload(const QString& imdbId, int season, int episode);
-
     QJsonObject devSnapshot() const;
     // v1.6 Phase D.4 (2026-05-19) — cross-mode library-section snapshot used
     // by library_get_* commands + embedded in devSnapshot under "library".
diff --git a/src/ui/pages/stream/DownloadDetailPane.cpp b/src/ui/pages/stream/DownloadDetailPane.cpp
deleted file mode 100644
index 5dcffb7..0000000
--- a/src/ui/pages/stream/DownloadDetailPane.cpp
+++ /dev/null
@@ -1,504 +0,0 @@
-// DOWNLOADS_OVERHAUL_V2 Task 5 (2026-06-11) — right pane of the Downloads
-// command center. Hosts a header (title + progress + stats), a section-aware
-// button row, and the three reused Tankorent property tabs (Files / Peers /
-// Trackers) pointed at the row's carrying torrent.
-//
-// Stats accessor: engine()->allStatuses() scanned for the matching infoHash to
-// obtain downloadRate / uploadRate / numPeers / totalWanted (DOWNLOADS_OVERHAUL_V2
-// review P1-B, 2026-06-11). Was TorrentClient::listActive(), which runs a SQLite
-// SELECT (repo.listTorrents) on the GUI thread every second — the bc179a1 hang
-// class. updateTotals (StreamDownloadsPage) was already moved off listActive to
-// allStatuses for the same reason (T7.1); this mirrors it. Falls back gracefully
-// when the row has no live handle (Completed/history rows with empty infoHash).
-
-#include "DownloadDetailPane.h"
-
-#include "core/torrent/TorrentClient.h"
-#include "core/torrent/TorrentEngine.h"   // allStatuses() / TorrentStatus (P1-B)
-#include "core/TorrentResult.h"   // humanSize()
-#include "ui/pages/tankorent/TorrentFilesTab.h"
-#include "ui/pages/tankorent/TorrentPeersTab.h"
-#include "ui/pages/tankorent/TorrentTrackersTab.h"
-
-#include <QFontMetrics>
-#include <QHBoxLayout>
-#include <QHideEvent>
-#include <QLabel>
-#include <QProgressBar>
-#include <QPushButton>
-#include <QResizeEvent>
-#include <QShowEvent>
-#include <QStackedWidget>
-#include <QTabWidget>
-#include <QTimer>
-#include <QVBoxLayout>
-
-// ─────────────────────────────────────────────────────────────────────────────
-// Local helpers
-// ─────────────────────────────────────────────────────────────────────────────
-namespace {
-
-// Reuse the same button stylesheet pattern as StreamDownloadsPage's topbar /
-// strip buttons: no-background, white text, rounded, pointer cursor.
-static const char* kBtnStyle =
-    "QPushButton {"
-    "  background: rgba(255,255,255,0.10);"
-    "  color: #dddddd;"
-    "  border: 1px solid rgba(255,255,255,0.18);"
-    "  border-radius: 4px;"
-    "  padding: 4px 12px;"
-    "  font-size: 12px;"
-    "}"
-    "QPushButton:hover {"
-    "  background: rgba(255,255,255,0.16);"
-    "  color: #eeeeee;"
-    "}"
-    "QPushButton:pressed {"
-    "  background: rgba(255,255,255,0.08);"
-    "}";
-
-// Format bytes/s into a human-friendly string: "1.2 MB/s", "348.0 KB/s", etc.
-// Mirrors TorrentPeersTab::formatSpeed — delegates to humanSize() for consistent
-// one-decimal KB/MB formatting across all Tankorent surfaces, then appends "/s".
-QString formatSpeed(qint64 bps)
-{
-    if (bps <= 0) return QStringLiteral("0 B/s");
-    return humanSize(bps) + QStringLiteral("/s");
-}
-
-} // namespace
-
-// ─────────────────────────────────────────────────────────────────────────────
-// Constructor
-// ─────────────────────────────────────────────────────────────────────────────
-
-DownloadDetailPane::DownloadDetailPane(QWidget* parent)
-    : QWidget(parent)
-{
-    buildUi();
-}
-
-void DownloadDetailPane::buildUi()
-{
-    auto* root = new QVBoxLayout(this);
-    root->setContentsMargins(0, 0, 0, 0);
-    root->setSpacing(0);
-
-    // ── Empty state ──────────────────────────────────────────────────────────
-    m_emptyLabel = new QLabel(tr("Select a download"), this);
-    m_emptyLabel->setAlignment(Qt::AlignCenter);
-    m_emptyLabel->setStyleSheet(
-        QStringLiteral("color: rgba(255,255,255,0.35); font-size: 14px;"
-                       " background: transparent;"));
-    root->addWidget(m_emptyLabel, 1);
-
-    // ── Content container ────────────────────────────────────────────────────
-    m_content = new QWidget(this);
-    m_content->hide();
-    auto* cv = new QVBoxLayout(m_content);
-    cv->setContentsMargins(16, 14, 16, 14);
-    cv->setSpacing(8);
-
-    // Title — manual eliding via reelideTitle() / resizeEvent().
-    // QLabel has no native elide mode: with wordWrap off a long text sets a hard
-    // minimum width that locks the splitter. We store the full title in m_fullTitle
-    // + tooltip, and call QFontMetrics::elidedText in resizeEvent (mirrors
-    // StreamSourceCard::reelideTitle). SizePolicy::Ignored lets the label shrink
-    // below its natural text width so the splitter can move freely.
-    m_titleLabel = new QLabel(m_content);
-    m_titleLabel->setObjectName(QStringLiteral("DownloadDetailPaneTitle"));
-    m_titleLabel->setStyleSheet(
-        QStringLiteral("font-size: 15px; font-weight: 600; color: #eeeeee;"));
-    m_titleLabel->setTextInteractionFlags(Qt::NoTextInteraction);
-    m_titleLabel->setWordWrap(false);
-    m_titleLabel->setMinimumWidth(0);
-    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
-    cv->addWidget(m_titleLabel, 0);
-
-    // Progress bar
-    m_progressBar = new QProgressBar(m_content);
-    m_progressBar->setRange(0, 100);
-    m_progressBar->setTextVisible(false);
-    m_progressBar->setFixedHeight(6);
-    m_progressBar->setStyleSheet(
-        QStringLiteral(
-            "QProgressBar {"
-            "  background: rgba(255,255,255,0.12);"
-            "  border: none;"
-            "  border-radius: 3px;"
-            "}"
-            "QProgressBar::chunk {"
-            "  background: rgba(255,255,255,0.55);"
-            "  border-radius: 3px;"
-            "}"));
-    cv->addWidget(m_progressBar, 0);
-
-    // Stats line
-    m_statsLabel = new QLabel(m_content);
-    m_statsLabel->setStyleSheet(
-        QStringLiteral("color: rgba(255,255,255,0.55); font-size: 12px;"));
-    cv->addWidget(m_statsLabel, 0);
-
-    // Button row
-    auto* btnRow = new QHBoxLayout;
-    btnRow->setSpacing(6);
-    btnRow->setContentsMargins(0, 0, 0, 0);
-
-    auto makeBtn = [this, &btnRow](const QString& text) -> QPushButton* {
-        auto* btn = new QPushButton(text, m_content);
-        btn->setStyleSheet(QString::fromLatin1(kBtnStyle));
-        btn->setCursor(Qt::PointingHandCursor);
-        btn->setFixedHeight(28);
-        btn->hide();
-        btnRow->addWidget(btn, 0);
-        return btn;
-    };
-
-    m_pauseBtn  = makeBtn(tr("Pause"));
-    m_resumeBtn = makeBtn(tr("Resume"));
-    m_cancelBtn = makeBtn(tr("Cancel"));
-    m_retryBtn  = makeBtn(tr("Retry"));
-    m_bumpBtn   = makeBtn(tr("Bump to top"));
-    m_playBtn   = makeBtn(tr("Play"));
-    btnRow->addStretch(1);
-    cv->addLayout(btnRow);
-
-    // Wire button signals — emit intent only, no client mutation
-    connect(m_pauseBtn,  &QPushButton::clicked, this, [this]() {
-        emit pauseRequested(m_row);
-    });
-    connect(m_resumeBtn, &QPushButton::clicked, this, [this]() {
-        emit resumeRequested(m_row);
-    });
-    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
-        emit cancelRequested(m_row);
-    });
-    connect(m_retryBtn,  &QPushButton::clicked, this, [this]() {
-        emit retryRequested(m_row);
-    });
-    connect(m_bumpBtn,   &QPushButton::clicked, this, [this]() {
-        emit bumpRequested(m_row);
-    });
-    connect(m_playBtn,   &QPushButton::clicked, this, [this]() {
-        emit playRequested(m_row);
-    });
-
-    // Tab widget placeholder — tabs installed lazily in ensureTabsBuilt()
-    m_tabWidget = new QTabWidget(m_content);
-    m_tabWidget->setObjectName(QStringLiteral("DownloadDetailPaneTabs"));
-    m_tabWidget->setStyleSheet(
-        QStringLiteral(
-            "QTabWidget::pane {"
-            "  border: none;"
-            "  background: transparent;"
-            "}"
-            "QTabBar::tab {"
-            "  color: rgba(255,255,255,0.65);"
-            "  background: transparent;"
-            "  padding: 5px 14px;"
-            "  font-size: 12px;"
-            "}"
-            "QTabBar::tab:selected {"
-            "  color: #eeeeee;"
-            "  border-bottom: 2px solid rgba(255,255,255,0.55);"
-            "}"
-            "QTabBar::tab:hover:!selected {"
-            "  color: #cccccc;"
-            "}"));
-    m_tabWidget->hide();
-    cv->addWidget(m_tabWidget, 1);
-
-    root->addWidget(m_content, 1);
-
-    // ── Stats refresh timer ──────────────────────────────────────────────────
-    m_statsTimer = new QTimer(this);
-    m_statsTimer->setInterval(1000);
-    m_statsTimer->setSingleShot(false);
-    connect(m_statsTimer, &QTimer::timeout, this, &DownloadDetailPane::refreshStats);
-}
-
-// ─────────────────────────────────────────────────────────────────────────────
-// setClient — lazy tab construction
-// ─────────────────────────────────────────────────────────────────────────────
-
-void DownloadDetailPane::setClient(TorrentClient* client)
-{
-    // Single-injection guard: tabs are wired to the first non-null client.
-    // Re-injection with a DIFFERENT client after tabs are built is unsupported —
-    // all three tab widgets hold a raw pointer to the original client and would
-    // need to be torn down and reconstructed to rebind. Warn and bail out.
-    if (m_tabsBuilt && m_client && client != m_client) {
-        qWarning("DownloadDetailPane::setClient: re-injection with a different "
-                 "client after tabs are built is unsupported — ignoring.");
-        return;
-    }
-    m_client = client;
-    // If we already have a row with a live infoHash, rebuild now that we have
-    // a client to point the tabs at.
-    if (m_hasRow && !m_row.infoHash.isEmpty())
-        rebuildUiForRow();
-}
-
-void DownloadDetailPane::ensureTabsBuilt()
-{
-    if (m_tabsBuilt || !m_client) return;
-
-    m_filesTab     = new TorrentFilesTab(m_client, m_tabWidget);
-    m_peersTab     = new TorrentPeersTab(m_client, m_tabWidget);
-    m_trackersTab  = new TorrentTrackersTab(m_client, m_tabWidget);
-
-    m_tabWidget->addTab(m_filesTab,    tr("Files"));
-    m_tabWidget->addTab(m_peersTab,    tr("Peers"));
-    m_tabWidget->addTab(m_trackersTab, tr("Trackers"));
-
-    m_tabsBuilt = true;
-}
-
-// ─────────────────────────────────────────────────────────────────────────────
-// setRow / clearRow
-// ─────────────────────────────────────────────────────────────────────────────
-
-void DownloadDetailPane::setRow(const tankostream::stream::DownloadRow& row,
-                                const QString& displayTitle)
-{
-    // Capture old hash BEFORE mutating m_row so rebuildUiForRow() can
-    // detect a same-hash re-selection (C1 short-circuit).
-    const QString oldHash = m_row.infoHash;
-
-    m_row          = row;
-    m_displayTitle = displayTitle;
-    m_hasRow       = true;
-
-    // C1: propagate whether this is a same-hash re-selection.
-    // rebuildUiForRow() uses this to skip the tab teardown on 4 Hz re-fires.
-    m_sameHashReselect = (row.infoHash == oldHash) && m_tabsBuilt;
-
-    m_emptyLabel->hide();
-    m_content->show();
-
-    rebuildUiForRow();
-}
-
-void DownloadDetailPane::clearRow()
-{
-    m_hasRow = false;
-    m_row    = {};
-    m_displayTitle.clear();
-    m_fullTitle.clear();
-    m_statsTimer->stop();
-    m_content->hide();
-    m_emptyLabel->show();
-}
-
-// ─────────────────────────────────────────────────────────────────────────────
-// rebuildUiForRow — update all content widgets for the current m_row
-// ─────────────────────────────────────────────────────────────────────────────
-
-void DownloadDetailPane::rebuildUiForRow()
-{
-    using DS = tankostream::stream::DownloadSection;
-
-    if (!m_hasRow) {
-        clearRow();
-        return;
-    }
-
-    // C1 short-circuit flag set by setRow() before this call.
-    // m_sameHashReselect == true means: the infoHash did not change AND tabs
-    // are already populated. rebuildUiForRow() will update title/progress/buttons
-    // but must NOT call setInfoHash() on any tab — that triggers full tree clear
-    // + per-file QComboBox allocation + a listActive() GUI-thread SQL scan, and
-    // the rebuild() → restoreSelection() → setRow() chain fires up to 4×/s.
-
-    // ── Title ──────────────────────────────────────────────────────────────
-    const QString episodePart =
-        (m_row.type == QLatin1String("movie"))
-        ? tr("Movie")
-        : QStringLiteral("S%1E%2")
-              .arg(m_row.season,  2, 10, QLatin1Char('0'))
-              .arg(m_row.episode, 2, 10, QLatin1Char('0'));
-
-    m_fullTitle = m_displayTitle.isEmpty()
-        ? episodePart
-        : m_displayTitle + QStringLiteral(" \xB7 ") + episodePart;
-
-    m_titleLabel->setToolTip(m_fullTitle);
-    reelideTitle();   // paints elided text at current width
-
-    // ── Progress ──────────────────────────────────────────────────────────
-    m_progressBar->setValue(m_row.pct);
-
-    // ── Button row: hide all first, then show by section ─────────────────
-    m_pauseBtn->hide();
-    m_resumeBtn->hide();
-    m_cancelBtn->hide();
-    m_retryBtn->hide();
-    m_bumpBtn->hide();
-    m_playBtn->hide();
-
-    switch (m_row.section) {
-    case DS::Active:
-        if (!m_row.paused) {
-            m_pauseBtn->show();
-        } else {
-            m_resumeBtn->show();
-        }
-        m_cancelBtn->show();
-        break;
-    case DS::Queued:
-        m_bumpBtn->show();
-        m_cancelBtn->show();
-        break;
-    case DS::Failed:
-        m_retryBtn->show();
-        m_cancelBtn->show();
-        break;
-    case DS::Completed:
-        m_playBtn->show();
-        break;
-    }
-
-    // ── Tabs: only when there is a live infoHash and client+tabs are ready ──
-    const bool hasLiveTorrent = !m_row.infoHash.isEmpty() && m_client;
-    if (hasLiveTorrent) {
-        ensureTabsBuilt();
-        // C1 short-circuit: when the infoHash has not changed and tabs are
-        // already populated (m_sameHashReselect == true), skip the full
-        // setInfoHash() teardown (tree clear + QComboBox reallocation +
-        // GUI-thread SQL listActive()). In-place progress updates are driven
-        // by the 1 Hz stats timer via refreshStats() → m_filesTab->refresh().
-        // setInfoHash() runs only when the hash actually changes (new selection)
-        // or on the very first population (m_tabsBuilt == false → ensureTabsBuilt
-        // sets it, m_sameHashReselect is false for first call).
-        if (!m_sameHashReselect) {
-            m_filesTab->setInfoHash(m_row.infoHash);
-            m_filesTab->refresh();
-            m_peersTab->setInfoHash(m_row.infoHash);
-            m_peersTab->refresh();
-            m_trackersTab->setInfoHash(m_row.infoHash);
-            m_trackersTab->refresh();
-        }
-        m_tabWidget->show();
-    } else {
-        m_tabWidget->hide();
-    }
-
-    // ── Stats timer: run only when visible and there is a live torrent ────
-    if (isVisible() && hasLiveTorrent) {
-        refreshStats();   // immediate first tick
-        m_statsTimer->start();
-    } else {
-        m_statsTimer->stop();
-        // No live torrent (Completed/history rows with empty infoHash):
-        // stats line is not meaningful — clear it.
-        m_statsLabel->setText(QString());
-    }
-}
-
-// ─────────────────────────────────────────────────────────────────────────────
-// refreshStats — 1 Hz tick: pull live status from engine()->allStatuses()
-// ─────────────────────────────────────────────────────────────────────────────
-// DOWNLOADS_OVERHAUL_V2 review P1-B (2026-06-11) — the accessor is now
-// engine()->allStatuses(), an in-memory snapshot of the live libtorrent handles
-// (no SQL). The previous accessor, TorrentClient::listActive(), ran a SQLite
-// SELECT (repo.listTorrents) on the GUI thread once per second — the bc179a1
-// hang class. updateTotals (StreamDownloadsPage::updateTotals, T7.1) was already
-// migrated to allStatuses for the same reason; this mirrors it. The field names
-// differ from TorrentInfo: TorrentStatus exposes downloadRate / uploadRate /
-// numPeers / totalWanted (cf. TorrentInfo's dlSpeed / ulSpeed / peers).
-
-void DownloadDetailPane::refreshStats()
-{
-    if (!isVisible() || !m_hasRow || !m_client || m_row.infoHash.isEmpty()) {
-        m_statsTimer->stop();
-        return;
-    }
-
-    // Scan the live engine status snapshot for the matching infoHash (no SQL).
-    auto* engine = m_client->engine();
-    if (!engine) {
-        m_statsTimer->stop();
-        m_statsLabel->setText(QString());
-        return;
-    }
-
-    const auto statuses = engine->allStatuses();
-    TorrentStatus info;
-    bool found = false;
-    for (const auto& s : statuses) {
-        if (s.infoHash == m_row.infoHash) {
-            info = s;
-            found = true;
-            break;
-        }
-    }
-
-    if (!found) {
-        // Torrent no longer has a live handle (just completed or removed) — stop
-        m_statsTimer->stop();
-        m_statsLabel->setText(QString());
-        return;
-    }
-
-    // Format: "<dl_speed> down · <ul_speed> up · <peers> peers · <size>"
-    QStringList parts;
-
-    parts << tr("%1 down").arg(formatSpeed(info.downloadRate));
-    parts << tr("%1 up").arg(formatSpeed(info.uploadRate));
-
-    if (info.numPeers > 0)
-        parts << tr("%1 peers").arg(info.numPeers);
-
-    if (info.totalWanted > 0)
-        parts << humanSize(info.totalWanted);
-
-    // Ride the 1 Hz timer to push in-place file-progress updates to the Files
-    // tab without a full setInfoHash() teardown (C1 short-circuit complement).
-    if (m_tabsBuilt && m_filesTab)
-        m_filesTab->refresh();
-
-    m_statsLabel->setText(parts.join(QStringLiteral(" \xB7 ")));
-}
-
-// ─────────────────────────────────────────────────────────────────────────────
-// Title eliding — mirrors StreamSourceCard::reelideTitle()
-// ─────────────────────────────────────────────────────────────────────────────
-
-void DownloadDetailPane::reelideTitle()
-{
-    if (!m_titleLabel || m_fullTitle.isEmpty()) return;
-    const int avail = m_titleLabel->width();
-    if (avail <= 0) {
-        // Widget not yet laid out — set the full text so the layout can measure
-        // it; resizeEvent will correct it once we have a real width.
-        m_titleLabel->setText(m_fullTitle);
-        return;
-    }
-    const QFontMetrics fm(m_titleLabel->font());
-    m_titleLabel->setText(fm.elidedText(m_fullTitle, Qt::ElideRight, avail));
-}
-
-void DownloadDetailPane::resizeEvent(QResizeEvent* event)
-{
-    QWidget::resizeEvent(event);
-    reelideTitle();
-}
-
-// ─────────────────────────────────────────────────────────────────────────────
-// Show / hide event — manage timer lifecycle
-// ─────────────────────────────────────────────────────────────────────────────
-
-void DownloadDetailPane::hideEvent(QHideEvent* event)
-{
-    QWidget::hideEvent(event);
-    m_statsTimer->stop();
-}
-
-void DownloadDetailPane::showEvent(QShowEvent* event)
-{
-    QWidget::showEvent(event);
-    if (m_hasRow && !m_row.infoHash.isEmpty() && m_client) {
-        refreshStats();
-        m_statsTimer->start();
-    }
-}
diff --git a/src/ui/pages/stream/DownloadDetailPane.h b/src/ui/pages/stream/DownloadDetailPane.h
deleted file mode 100644
index 1606e95..0000000
--- a/src/ui/pages/stream/DownloadDetailPane.h
+++ /dev/null
@@ -1,101 +0,0 @@
-#pragma once
-// DOWNLOADS_OVERHAUL_V2 Task 5 (2026-06-11) — right pane of the Downloads
-// command center. Renders one DownloadRow: header + numeric stats + controls +
-// the reused Tankorent property tabs pointed at the row's carrying torrent.
-// Emits intents only; StreamDownloadsPage routes them (this pane uses
-// TorrentClient strictly read-only, for the tabs + stats snapshot).
-#include "core/stream/DownloadsCommandModel.h"
-#include <QWidget>
-
-class TorrentClient;
-class TorrentFilesTab;
-class TorrentPeersTab;
-class TorrentTrackersTab;
-class QLabel;
-class QProgressBar;
-class QPushButton;
-class QTabWidget;
-class QTimer;
-
-class DownloadDetailPane : public QWidget {
-    Q_OBJECT
-public:
-    explicit DownloadDetailPane(QWidget* parent = nullptr);
-
-    // Single-injection: tabs bind the first non-null client passed here.
-    // Re-injection with a DIFFERENT client after tabs are built is unsupported
-    // (tabs hold a raw pointer to the original client; rebinding would require
-    // reconstructing all three tabs). Pass the permanent TorrentClient* once at
-    // setup and never call again with a different pointer.
-    void setClient(TorrentClient* client);
-
-    // Display the given row. displayTitle is the enriched show name (or imdbId
-    // fallback) resolved by the page from its title cache.
-    void setRow(const tankostream::stream::DownloadRow& row,
-                const QString& displayTitle);
-
-    // Reset to empty state: "Select a download" label, everything else hidden.
-    void clearRow();
-
-signals:
-    void pauseRequested(const tankostream::stream::DownloadRow& row);
-    void resumeRequested(const tankostream::stream::DownloadRow& row);
-    void cancelRequested(const tankostream::stream::DownloadRow& row);
-    void retryRequested(const tankostream::stream::DownloadRow& row);
-    void bumpRequested(const tankostream::stream::DownloadRow& row);
-    void playRequested(const tankostream::stream::DownloadRow& row);
-
-protected:
-    void hideEvent(QHideEvent* event) override;
-    void showEvent(QShowEvent* event) override;
-    void resizeEvent(QResizeEvent* event) override;
-
-private:
-    void buildUi();
-    void ensureTabsBuilt();   // construct tabs once m_client is non-null
-    void rebuildUiForRow();   // update visible controls / button row for current row
-    void refreshStats();      // 1s timer tick: pull TorrentInfo from listActive()
-    void reelideTitle();      // re-elide m_fullTitle into m_titleLabel at current width
-
-    // ── Injection state ──────────────────────────────────────────────────────
-    TorrentClient* m_client  = nullptr;
-    bool           m_tabsBuilt = false;
-
-    // ── Row state ────────────────────────────────────────────────────────────
-    tankostream::stream::DownloadRow m_row;
-    QString                          m_displayTitle;
-    QString                          m_fullTitle;          // unelided; reelideTitle() elides into m_titleLabel
-    bool                             m_hasRow            = false;
-    // Set by setRow() before calling rebuildUiForRow(): true when the incoming
-    // infoHash equals the previous one and tabs are already built. Lets
-    // rebuildUiForRow() skip the 4 Hz GUI-thread-SQL tab teardown (C1).
-    bool                             m_sameHashReselect  = false;
-
-    // ── Empty-state widget ───────────────────────────────────────────────────
-    QLabel* m_emptyLabel = nullptr;
-
-    // ── Content container (hidden in empty state) ────────────────────────────
-    QWidget* m_content = nullptr;
-
-    // ── Header ───────────────────────────────────────────────────────────────
-    QLabel*      m_titleLabel = nullptr;
-    QProgressBar* m_progressBar = nullptr;
-    QLabel*      m_statsLabel = nullptr;
-
-    // ── Button row ───────────────────────────────────────────────────────────
-    QPushButton* m_pauseBtn  = nullptr;
-    QPushButton* m_resumeBtn = nullptr;
-    QPushButton* m_cancelBtn = nullptr;
-    QPushButton* m_retryBtn  = nullptr;
-    QPushButton* m_bumpBtn   = nullptr;
-    QPushButton* m_playBtn   = nullptr;
-
-    // ── Property tabs (constructed lazily via ensureTabsBuilt) ───────────────
-    QTabWidget*       m_tabWidget    = nullptr;
-    TorrentFilesTab*  m_filesTab     = nullptr;
-    TorrentPeersTab*  m_peersTab     = nullptr;
-    TorrentTrackersTab* m_trackersTab = nullptr;
-
-    // ── Stats refresh timer ──────────────────────────────────────────────────
-    QTimer* m_statsTimer = nullptr;
-};
diff --git a/src/ui/pages/stream/StreamDownloadsPage.cpp b/src/ui/pages/stream/StreamDownloadsPage.cpp
index 7bd4f4b..61bd4bc 100644
--- a/src/ui/pages/stream/StreamDownloadsPage.cpp
+++ b/src/ui/pages/stream/StreamDownloadsPage.cpp
@@ -1,143 +1,125 @@
-// DOWNLOADS_OVERHAUL_V2 Task 4 (2026-06-11) — Master-Detail shell.
-// Task 5 (2026-06-11) — DownloadDetailPane replaces the right pane stub.
-// Task 7 (2026-06-11) — Top strip wired: live totals, Pause/Resume All,
-//   Clear Done, max-active knob.
-// T7.1 (2026-06-11) — review fixes: promotion-free queue pauseAll() drives
-//   Pause All (C1), Clear Done switched from an addedAt watermark to a
-//   hidden-keys set (I1), Resume All is FIFO + single-mechanism, totals read
-//   the engine snapshot directly.
-// Replaces the old two-section scrollable card list with a QSplitter:
-//   left  — QTreeWidget: Failed / Active / Queued / Completed sections,
-//            shows grouped, episodes as leaves.
-//   right — DownloadDetailPane: header + stats + controls + Tankorent tabs.
-// All signal-triggered refreshes debounce through a 250ms single-shot timer
-// so rapid lane-state updates don't hammer the tree.
-
 #include "StreamDownloadsPage.h"
-#include "DownloadDetailPane.h"
 
 #include "core/torrent/TorrentClient.h"
-#include "core/torrent/TorrentEngine.h"   // allStatuses() for updateTotals
-#include "core/TorrentResult.h"   // humanSize()
 #include "core/net/NetSeam.h"
 #include "core/stream/StreamDownloadIndex.h"
 #include "core/stream/MetaAggregator.h"
 #include "core/stream/addon/MetaItem.h"
-#include "core/queue/TransferQueue.h"
 
-#include <QComboBox>
-#include <QDateTime>
-#include <QHeaderView>
 #include <QDir>
 #include <QFile>
+#include <QFrame>
 #include <QFileInfo>
-#include <QFont>
+#include <QHash>
 #include <QHBoxLayout>
 #include <QLabel>
+#include <QJsonArray>
+#include <QJsonObject>
+#include <QList>
 #include <QNetworkAccessManager>
 #include <QNetworkReply>
 #include <QNetworkRequest>
 #include <QPixmap>
 #include <QPushButton>
-#include <QSplitter>
+#include <QRegularExpression>
+#include <QScrollArea>
+#include <QSet>
 #include <QStandardPaths>
-#include <QTimer>
-#include <QTreeWidget>
-#include <QTreeWidgetItem>
-#include <QTreeWidgetItemIterator>
+#include <QStringList>
 #include <QVBoxLayout>
-#include <QSettings>
-#include <QVariant>
 
 #include <algorithm>
 
-// ──────────────────────────────────────────────────────────────────────────────
-// Internal helpers
-// ──────────────────────────────────────────────────────────────────────────────
 namespace {
 
-// Section indices match DownloadSection enum order
-static const char* kSectionNames[] = {"Failed", "Active", "Queued", "Completed"};
-
-// Mirrors DownloadDetailPane::formatSpeed (local copy — not exported).
-// Formats bytes/s as "1.2 MB/s", "348.0 KB/s", "0 B/s".
-QString formatDownloadSpeed(qint64 bps)
+// Mirrors the on-disk poster cache used by StreamLibraryLayout / StreamDetailView /
+// StreamSearchWidget / StreamContinueStrip: <GenericData>/Tankoban/data/stream_posters/<imdbId>.jpg.
+// The ".jpg" extension is load-bearing — those consumers enumerate "*.jpg" and
+// cleanupOrphanPosters only GCs .jpg files, so an extensionless file would be a
+// private, never-shared, never-collected cache.
+QString posterCachePath(const QString& imdbId)
 {
-    if (bps <= 0) return QStringLiteral("0 B/s");
-    return humanSize(bps) + QStringLiteral("/s");
+    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
+           + QStringLiteral("/Tankoban/data/stream_posters/") + imdbId
+           + QStringLiteral(".jpg");
 }
 
-// Unique selection key for an episode leaf item: "<imdbId>|<season>|<episode>"
-// Section is intentionally excluded so selection survives an item moving between
-// sections (e.g. Queued→Active→Completed). For a movie (season==0, episode==0)
-// we still use 0|0.
-QString makeSelectionKey(const tankostream::stream::DownloadRow& r)
+// " · " separator built from a code point so the source stays ASCII (the build
+// does not force MSVC /utf-8, so raw UTF-8 in literals would be misread).
+QString dotSep()
 {
-    return r.imdbId
-           + QLatin1Char('|') + QString::number(r.season)
-           + QLatin1Char('|') + QString::number(r.episode);
+    return QStringLiteral("  ") + QChar(0x00B7) + QStringLiteral("  ");
 }
 
-// "Clear Done" hidden-row key: "imdbId|season|episode|addedAt" (T7.1, review
-// I1). addedAt disambiguates a re-download of the same episode — a fresh
-// registration stamps a new addedAt, so clearing an old completed run can
-// never hide the new one.
-QString clearedDoneKey(const tankostream::stream::DownloadRow& r)
+QString prettifyFilenameTitle(QString text)
 {
-    return makeSelectionKey(r)
-           + QLatin1Char('|') + QString::number(r.addedAt);
-}
+    text = QFileInfo(text).completeBaseName();
+    text.replace(QRegularExpression(QStringLiteral("[._]+")), QStringLiteral(" "));
+    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
+    text = text.trimmed();
+
+    const QRegularExpression episodeRe(QStringLiteral("\\bS\\d{1,2}E\\d{1,3}\\b"),
+                                       QRegularExpression::CaseInsensitiveOption);
+    const QRegularExpression seasonRe(QStringLiteral("\\bS\\d{1,2}\\b"),
+                                      QRegularExpression::CaseInsensitiveOption);
+
+    int cutAt = -1;
+    const QRegularExpressionMatch episodeMatch = episodeRe.match(text);
+    if (episodeMatch.hasMatch()) {
+        cutAt = episodeMatch.capturedStart();
+    } else {
+        const QRegularExpressionMatch seasonMatch = seasonRe.match(text);
+        if (seasonMatch.hasMatch())
+            cutAt = seasonMatch.capturedStart();
+    }
 
-} // namespace
+    if (cutAt > 0)
+        text = text.left(cutAt).trimmed();
 
-// ──────────────────────────────────────────────────────────────────────────────
-// Static helpers
-// ──────────────────────────────────────────────────────────────────────────────
+    text.remove(QRegularExpression(QStringLiteral("\\[[^\\]]*\\]")));
+    text.remove(QRegularExpression(QStringLiteral("\\([^\\)]*\\)")));
+    text = text.trimmed();
+    return text;
+}
 
-// Mirrors the poster cache path used by StreamLibraryLayout, StreamDetailView,
-// StreamSearchWidget, StreamContinueStrip — same directory, same ".jpg" extension
-// so the shared cache is reused and GC by cleanupOrphanPosters works.
-/*static*/ QString StreamDownloadsPage::posterCachePath(const QString& imdbId)
+QString bestTitleFromEntries(const QList<StreamDownloadIndex::Entry>& entries,
+                             const QString& fallback)
 {
-    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
-           + QStringLiteral("/Tankoban/data/stream_posters/") + imdbId
-           + QStringLiteral(".jpg");
+    for (const auto& entry : entries) {
+        const QString title = prettifyFilenameTitle(entry.canonicalPath);
+        if (!title.isEmpty() && title != fallback)
+            return title;
+    }
+    return fallback;
 }
 
-// ──────────────────────────────────────────────────────────────────────────────
-// Constructor / buildUi
-// ──────────────────────────────────────────────────────────────────────────────
-
-StreamDownloadsPage::StreamDownloadsPage(QWidget* parent)
-    : QFrame(parent)
+QString bestTitleFromGroups(const QList<QJsonObject>& groups, const QString& fallback)
 {
-    setObjectName(QStringLiteral("StreamDownloadsPage"));
-
-    // Load persistent "Clear Done" hidden-row keys (semantics in the header).
-    // The pre-T7.1 "downloads/clearDoneBeforeMs" watermark settings key is
-    // left orphaned — harmless, display-only state needs no migration.
-    {
-        const QStringList stored = QSettings()
-            .value(QStringLiteral("downloads/clearedDoneKeys"))
-            .toStringList();
-        m_clearedDoneKeys = QSet<QString>(stored.cbegin(), stored.cend());
+    for (const QJsonObject& group : groups) {
+        const QString direct = group.value(QStringLiteral("showTitle")).toString(
+            group.value(QStringLiteral("title")).toString());
+        if (!direct.isEmpty())
+            return direct;
+
+        const QJsonArray items = group.value(QStringLiteral("items")).toArray();
+        for (const QJsonValue& value : items) {
+            const QJsonObject item = value.toObject();
+            const QString filename = item.value(QStringLiteral("canonicalFilename")).toString(
+                item.value(QStringLiteral("canonicalPath")).toString());
+            const QString title = prettifyFilenameTitle(filename);
+            if (!title.isEmpty() && title != fallback)
+                return title;
+        }
     }
+    return fallback;
+}
 
-    // 250ms debounce timer — all signal triggers start() this; rebuild() fires
-    // once per burst.
-    m_rebuildDebounce = new QTimer(this);
-    m_rebuildDebounce->setSingleShot(true);
-    m_rebuildDebounce->setInterval(250);
-    connect(m_rebuildDebounce, &QTimer::timeout, this, &StreamDownloadsPage::rebuild);
-
-    // 1 Hz live-speed timer — started/stopped in showEvent/hideEvent so it
-    // only runs while the page is actually visible. Updates totals label only
-    // (no tree rebuild).
-    m_totalsTimer = new QTimer(this);
-    m_totalsTimer->setInterval(1000);
-    connect(m_totalsTimer, &QTimer::timeout,
-            this, &StreamDownloadsPage::updateTotals);
+} // namespace
 
+StreamDownloadsPage::StreamDownloadsPage(QWidget* parent)
+    : QFrame(parent)
+{
+    setObjectName("StreamDownloadsPage");
     buildUi();
 }
 
@@ -147,25 +129,24 @@ void StreamDownloadsPage::buildUi()
     root->setContentsMargins(0, 0, 0, 0);
     root->setSpacing(0);
 
-    // ── Topbar ──────────────────────────────────────────────────────────────
+    // Topbar: back button + title
     auto* topbar = new QFrame(this);
-    topbar->setObjectName(QStringLiteral("StreamDownloadsTopbar"));
+    topbar->setObjectName("StreamDownloadsTopbar");
     topbar->setFixedHeight(48);
     auto* topbarLayout = new QHBoxLayout(topbar);
     topbarLayout->setContentsMargins(14, 6, 14, 6);
     topbarLayout->setSpacing(10);
 
     m_backBtn = new QPushButton(tr("< Back"), topbar);
-    m_backBtn->setObjectName(QStringLiteral("StreamDownloadsBackBtn"));
+    m_backBtn->setObjectName("StreamDownloadsBackBtn");
     m_backBtn->setCursor(Qt::PointingHandCursor);
     m_backBtn->setFixedHeight(28);
     connect(m_backBtn, &QPushButton::clicked, this, &StreamDownloadsPage::backRequested);
 
     m_titleLabel = new QLabel(tr("Downloads"), topbar);
-    m_titleLabel->setObjectName(QStringLiteral("StreamDownloadsTitle"));
+    m_titleLabel->setObjectName("StreamDownloadsTitle");
     m_titleLabel->setStyleSheet(
-        QStringLiteral("QLabel#StreamDownloadsTitle { font-size: 16pt;"
-                       " font-weight: 600; color: #eeeeee; }"));
+        "QLabel#StreamDownloadsTitle { font-size: 16pt; font-weight: 600; color: #eeeeee; }");
 
     topbarLayout->addWidget(m_backBtn, 0);
     topbarLayout->addWidget(m_titleLabel, 0);
@@ -173,901 +154,527 @@ void StreamDownloadsPage::buildUi()
 
     root->addWidget(topbar, 0);
 
-    // ── Top strip (command buttons) ─────────────────────────────────────────
-    auto* stripWidget = new QWidget(this);
-    stripWidget->setObjectName(QStringLiteral("StreamDownloadsStrip"));
-    stripWidget->setFixedHeight(40);
-    auto* strip = new QHBoxLayout(stripWidget);
-    strip->setContentsMargins(14, 6, 14, 6);
-    strip->setSpacing(8);
-
-    m_totalsLabel = new QLabel(stripWidget);
-    m_totalsLabel->setObjectName(QStringLiteral("StreamDownloadsTotals"));
-    m_totalsLabel->setStyleSheet(
-        QStringLiteral("color: rgba(255,255,255,0.55); font-size: 12px;"));
-    strip->addWidget(m_totalsLabel, 1);
-
-    // Buttons — disabled until setTorrentClient() injects a live client.
-    m_pauseAllBtn = new QPushButton(tr("Pause All"), stripWidget);
-    m_pauseAllBtn->setEnabled(false);
-    connect(m_pauseAllBtn, &QPushButton::clicked, this, [this]() {
-        if (!m_client) return;
-        auto* q = m_client->transferQueue();
-        if (!q) return;
-        // T7 review C1 — pauseAll() flips every Running head in one pass with
-        // NO promotion (per-lane pauseCurrent promotes a waiter into each
-        // freed slot, which the Running-replay then STARTS — "Pause All" must
-        // not start new downloads). Queue-first ordering is SAFE here
-        // precisely because pauseAll cannot promote/start anything; we then
-        // engine-pause the flipped transfers.
-        const QStringList paused = q->pauseAll();
-        for (const QString& id : paused)
-            m_client->pauseTorrent(id);
-    });
-    strip->addWidget(m_pauseAllBtn, 0);
-
-    m_resumeAllBtn = new QPushButton(tr("Resume All"), stripWidget);
-    m_resumeAllBtn->setEnabled(false);
-    connect(m_resumeAllBtn, &QPushButton::clicked, this, [this]() {
-        if (!m_client) return;
-        auto* q = m_client->transferQueue();
-        if (!q) return;
-        // Snapshot once — promotions during the loop are fine here: cap gates
-        // how many actually flip to Running; gated ones stay Queued and
-        // auto-promote later via the C1 fall-through in TorrentClient.
-        // T7.1: resume in head-enqueueSeq order (QHash iteration order is
-        // arbitrary) so the cap's slots go to the oldest-enqueued paused heads
-        // first — deterministic FIFO. No page-side engine-resume: single
-        // mechanism — TorrentClient's Running-replay fall-through (T6.1 C1
-        // fix) already engine-resumes every promoted head synchronously on
-        // itemStateChanged(Running).
-        const auto lanes = q->lanesSnapshot();
-        QList<tankoban::queue::TransferLane> pausedLanes;
-        for (auto it = lanes.cbegin(); it != lanes.cend(); ++it) {
-            const auto& lane = it.value();
-            if (lane.items.empty()) continue;
-            if (lane.items.front().state == tankoban::queue::TransferState::Paused)
-                pausedLanes.append(lane);
-        }
-        std::sort(pausedLanes.begin(), pausedLanes.end(),
-                  [](const tankoban::queue::TransferLane& a,
-                     const tankoban::queue::TransferLane& b) {
-                      return a.items.front().enqueueSeq < b.items.front().enqueueSeq;
-                  });
-        for (const auto& lane : pausedLanes)
-            q->resumeCurrent(lane.showId);
-    });
-    strip->addWidget(m_resumeAllBtn, 0);
-
-    m_clearDoneBtn = new QPushButton(tr("Clear Done"), stripWidget);
-    m_clearDoneBtn->setEnabled(false);
-    connect(m_clearDoneBtn, &QPushButton::clicked, this, [this]() {
-        if (!m_index) return;
-        // T7 review I1 — "clear what I see now": key every row that is
-        // Completed RIGHT NOW. A download still in flight at click time is
-        // not keyed, so it stays visible when it completes (the old addedAt
-        // watermark hid it — addedAt is the registration time, which predates
-        // the click for in-flight transfers).
-        //
-        // Fresh buildDownloadRows snapshot, not the last rendered rows: the
-        // tree can be up to ~250ms debounce-stale and clearing should act on
-        // current model truth. maxCompletedAgeMs=0 so >30d age-trimmed
-        // Completed rows are keyed too (they're hidden by the age trim
-        // anyway; keying them is harmless) and so liveKeys spans every key
-        // the index can still produce.
-        tankostream::stream::DownloadsSnapshot snap;
-        snap.indexEntries = m_index->all();
-        if (m_client && m_client->transferQueue())
-            snap.lanes = m_client->transferQueue()->lanesSnapshot();
-        const auto rows = tankostream::stream::buildDownloadRows(
-            snap, QDateTime::currentMSecsSinceEpoch(), /*maxCompletedAgeMs=*/0);
-        QSet<QString> liveKeys;
-        for (const auto& r : rows) {
-            const QString key = clearedDoneKey(r);
-            liveKeys.insert(key);
-            if (r.section == tankostream::stream::DownloadSection::Completed)
-                m_clearedDoneKeys.insert(key);
-        }
-        // Prune keys whose rows left the index entirely (cancel / evict /
-        // re-download) so the persisted set can't grow unbounded.
-        m_clearedDoneKeys.intersect(liveKeys);
-        QSettings().setValue(
-            QStringLiteral("downloads/clearedDoneKeys"),
-            QVariant(QStringList(m_clearedDoneKeys.cbegin(),
-                                 m_clearedDoneKeys.cend())));
-        rebuild();
-    });
-    strip->addWidget(m_clearDoneBtn, 0);
-
-    // Max-active label + combo
-    auto* maxActiveLabel = new QLabel(tr("Max active:"), stripWidget);
-    maxActiveLabel->setStyleSheet(
-        QStringLiteral("color: rgba(255,255,255,0.55); font-size: 12px;"));
-    strip->addWidget(maxActiveLabel, 0);
-
-    m_maxActiveCombo = new QComboBox(stripWidget);
-    // Items: display text with integral data (0 = unlimited).
-    m_maxActiveCombo->addItem(tr("1"),         QVariant(1));
-    m_maxActiveCombo->addItem(tr("2"),         QVariant(2));
-    m_maxActiveCombo->addItem(tr("3"),         QVariant(3));
-    m_maxActiveCombo->addItem(tr("5"),         QVariant(5));
-    m_maxActiveCombo->addItem(tr("Unlimited"), QVariant(0));
-    // Restore from settings (default 3).
-    {
-        const int saved = QSettings()
-            .value(QStringLiteral("downloads/maxActive"), 3).toInt();
-        const int idx = m_maxActiveCombo->findData(QVariant(saved));
-        m_maxActiveCombo->setCurrentIndex(idx >= 0 ? idx : 2);  // fallback: "3"
-    }
-    m_maxActiveCombo->setEnabled(false);
-    connect(m_maxActiveCombo,
-            QOverload<int>::of(&QComboBox::currentIndexChanged),
-            this, [this](int idx) {
-        const int v = m_maxActiveCombo->itemData(idx).toInt();
-        QSettings().setValue(QStringLiteral("downloads/maxActive"), v);
-        if (m_client && m_client->transferQueue())
-            m_client->transferQueue()->setMaxActive(v);
-    });
-    strip->addWidget(m_maxActiveCombo, 0);
-
-    root->addWidget(stripWidget, 0);
-
-    // ── Main splitter ───────────────────────────────────────────────────────
-    m_splitter = new QSplitter(Qt::Horizontal, this);
-    m_splitter->setObjectName(QStringLiteral("StreamDownloadsSplitter"));
-    m_splitter->setHandleWidth(1);
-    m_splitter->setStyleSheet(
-        QStringLiteral("QSplitter::handle { background: rgba(255,255,255,0.10); }"));
-
-    // Left pane — master tree
-    m_tree = new QTreeWidget(m_splitter);
-    m_tree->setObjectName(QStringLiteral("StreamDownloadsTree"));
-    m_tree->setColumnCount(3);
-    m_tree->header()->hide();
-    m_tree->setColumnWidth(0, 240);
-    m_tree->setColumnWidth(1, 60);
-    m_tree->setColumnWidth(2, 70);
-    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
-    m_tree->setRootIsDecorated(true);
-    m_tree->setAlternatingRowColors(false);
-    m_tree->setFrameShape(QFrame::NoFrame);
-    m_tree->setStyleSheet(
-        QStringLiteral(
-            "QTreeWidget#StreamDownloadsTree {"
-            "  background: transparent;"
-            "  color: #dddddd;"
-            "  font-size: 13px;"
-            "  border: none;"
-            "}"
-            "QTreeWidget#StreamDownloadsTree::item {"
-            "  padding: 4px 2px;"
-            "}"
-            "QTreeWidget#StreamDownloadsTree::item:selected {"
-            "  background: rgba(255,255,255,0.12);"
-            "  color: #eeeeee;"
-            "}"
-            "QTreeWidget#StreamDownloadsTree::item:hover:!selected {"
-            "  background: rgba(255,255,255,0.06);"
-            "}"));
-
-    // Right pane — DownloadDetailPane (Task 5)
-    m_detailPane = new DownloadDetailPane(m_splitter);
-    m_detailPane->setObjectName(QStringLiteral("StreamDownloadsDetailPane"));
-
-    // ── Intent signals wired in T6 ───────────────────────────────────────────
-    // Each handler re-resolves the current row state before acting (T5 review I4:
-    // the pane's snapshot can be up to ~250ms stale). Guards log + no-op when
-    // the row has moved to a section that makes the intent invalid.
-    connect(m_detailPane, &DownloadDetailPane::pauseRequested, this,
-            [this](const tankostream::stream::DownloadRow& row) {
-                const auto fresh = freshRowFor(row);
-                if (!fresh
-                    || fresh->section != tankostream::stream::DownloadSection::Active
-                    || fresh->paused
-                    || !m_client) {
-                    qInfo() << "[downloads] pause intent dropped (stale row)";
-                    return;
-                }
-                // T6 review I3: orphan-Active rows (no lane item, so empty
-                // infoHash) must not pause an unrelated lane head of the same
-                // show — skip pauseCurrent entirely, and when we DO have a
-                // hash, only pause the lane when its head IS this transfer.
-                if (fresh->infoHash.isEmpty()) {
-                    qInfo() << "[downloads] pause intent dropped (orphan row, no lane transfer)";
-                    return;
-                }
-                m_client->pauseTorrent(fresh->infoHash);
-                if (auto* q = m_client->transferQueue()) {
-                    const QString showId = QStringLiteral("imdb:") + fresh->imdbId;
-                    const auto lane = q->laneFor(showId);
-                    if (lane && !lane->items.empty()
-                        && lane->items.front().transferId == fresh->infoHash)
-                        q->pauseCurrent(showId);
-                }
-            });
-
-    connect(m_detailPane, &DownloadDetailPane::resumeRequested, this,
-            [this](const tankostream::stream::DownloadRow& row) {
-                const auto fresh = freshRowFor(row);
-                if (!fresh || !fresh->paused || !m_client) {
-                    qInfo() << "[downloads] resume intent dropped (stale row)";
-                    return;
-                }
-                // Queue decides if a slot is free; engine-resume only when
-                // promoted. When gated, the head goes Queued and auto-promotes
-                // later — engine-resume happens here or not at all this click.
-                auto* q = m_client->transferQueue();
-                if (q) {
-                    if (q->resumeCurrent(QStringLiteral("imdb:") + fresh->imdbId).has_value()
-                        && !fresh->infoHash.isEmpty())
-                        m_client->resumeTorrent(fresh->infoHash);
-                } else if (!fresh->infoHash.isEmpty()) {
-                    m_client->resumeTorrent(fresh->infoHash);
-                }
-            });
-
-    connect(m_detailPane, &DownloadDetailPane::cancelRequested, this,
-            [this](const tankostream::stream::DownloadRow& row) {
-                const auto fresh = freshRowFor(row);
-                // Guard: never delete a Completed torrent (I4 critical case).
-                if (!fresh
-                    || fresh->section == tankostream::stream::DownloadSection::Completed
-                    || !m_client) {
-                    qInfo() << "[downloads] cancel intent dropped (stale/completed row)";
-                    return;
-                }
-                // T11.1 review I3: when the row's carrying lane item is a
-                // season PACK (per-episode row, lane item with no episode
-                // number), this row is ONE episode of a shared transfer —
-                // queue-cancel/deleteTorrent here would silently kill every
-                // sibling episode. Pack-level control belongs to a future
-                // batch affordance; per-episode intents must never destroy
-                // the shared transfer. Cancel = evict only THIS episode's
-                // index entry (episode-level evict: evictByPath on the
-                // entry's canonical key). The episode>0 guard excludes movie
-                // rows, whose lane items also lack an episodeNumber.
-                {
-                    QHash<QString, tankoban::queue::TransferLane> lanes;
-                    if (auto* q = m_client->transferQueue())
-                        lanes = q->lanesSnapshot();
-                    const auto* li = tankostream::stream::laneItemFor(
-                        lanes, fresh->imdbId, fresh->season, fresh->episode);
-                    if (li && !li->episodeNumber.has_value() && fresh->episode > 0) {
-                        if (m_index && !fresh->canonicalPath.isEmpty())
-                            m_index->evictByPath(
-                                StreamDownloadIndex::computeCanonicalKey(fresh->canonicalPath));
-                        else
-                            qInfo() << "[downloads] pack-child cancel: no"
-                                       " canonical path to evict — no-op";
-                        return;
-                    }
-                }
-                // T6 review C2: rows with no lane item (orphan resume, index
-                // Failed, etc.) carry an empty infoHash — derive the engine
-                // hash from the index group ("tankorent:<infohash>") so cancel
-                // still reaches queue + engine.
-                const QString hash = !fresh->infoHash.isEmpty()
-                    ? fresh->infoHash
-                    : tankostream::stream::infoHashFromGroup(fresh->sourceGroupId);
-                if (auto* q = m_client->transferQueue(); q && !hash.isEmpty())
-                    q->cancel(hash);
-                // deleteFiles=true: cancel removes partial staging files.
-                // Completed rows never reach here (guard above), so finished
-                // media is safe.
-                if (!hash.isEmpty())
-                    m_client->deleteTorrent(hash, /*deleteFiles=*/true);
-                // ALWAYS evict the index entries, even when no engine transfer
-                // could be resolved — otherwise the cancelled episode lingers
-                // as a ghost row forever (T6 review C2). Files are untouched.
-                if (m_index && !fresh->sourceGroupId.isEmpty())
-                    m_index->evictBySourceGroup(fresh->sourceGroupId);
-            });
-
-    connect(m_detailPane, &DownloadDetailPane::bumpRequested, this,
-            [this](const tankostream::stream::DownloadRow& row) {
-                const auto fresh = freshRowFor(row);
-                if (!fresh
-                    || fresh->section != tankostream::stream::DownloadSection::Queued
-                    || !m_client
-                    || fresh->infoHash.isEmpty())
-                    return;
-                if (auto* q = m_client->transferQueue())
-                    q->bumpToFront(fresh->infoHash);
-            });
-
-    connect(m_detailPane, &DownloadDetailPane::playRequested, this,
-            [this](const tankostream::stream::DownloadRow& row) {
-                const auto fresh = freshRowFor(row);
-                if (!fresh || fresh->canonicalPath.isEmpty()) return;
-                QString title = m_titleCache.value(fresh->imdbId);
-                if (title.isEmpty())
-                    title = QFileInfo(fresh->canonicalPath).completeBaseName();
-                emit playLocalFileRequested(
-                    fresh->canonicalPath,
-                    fresh->imdbId,
-                    title,
-                    fresh->season,
-                    fresh->episode);
-            });
-
-    connect(m_detailPane, &DownloadDetailPane::retryRequested, this,
-            [this](const tankostream::stream::DownloadRow& row) {
-                const auto fresh = freshRowFor(row);
-                if (!fresh
-                    || fresh->section != tankostream::stream::DownloadSection::Failed) {
-                    qInfo() << "[downloads] retry intent dropped (stale row)";
-                    return;
-                }
-                // Clean up the failed transfer first: remove from queue + engine
-                // (delete partial files). Failed rows usually have no lane item
-                // (queues erase on terminal states), so derive the engine hash
-                // from the index group when needed.
-                // T11.1 review I3: when the row's carrying lane item is a season
-                // PACK (lane item with no episodeNumber; episode>0 excludes
-                // movies), skip engine/queue cleanup entirely — the pack may be
-                // healthy and shared with sibling episodes. Pack-level control
-                // belongs to a future batch affordance; per-episode intents must
-                // never destroy the shared transfer.
-                if (m_client) {
-                    QHash<QString, tankoban::queue::TransferLane> lanes;
-                    if (auto* q = m_client->transferQueue())
-                        lanes = q->lanesSnapshot();
-                    const auto* li = tankostream::stream::laneItemFor(
-                        lanes, fresh->imdbId, fresh->season, fresh->episode);
-                    const bool packChild =
-                        li && !li->episodeNumber.has_value() && fresh->episode > 0;
-                    if (!packChild) {
-                        // Index-based multi-episode pack guard: a failed pack's
-                        // lane item is already erased (finishCurrent(Failed)
-                        // removes terminal lane items), so the lane-based guard
-                        // above can't fire. The index is the durable truth —
-                        // count sibling entries sharing the same sourceGroupId.
-                        // If MORE THAN ONE entry shares the group the transfer
-                        // was carrying multiple episodes; deleting the pack hash
-                        // would destroy finished-but-unmoved siblings. Skip
-                        // engine/queue cleanup entirely and let the retry
-                        // re-dispatch handle it. Single-entry groups (normal
-                        // per-episode transfers) proceed as before.
-                        // (integration + security review convergence, T11.2)
-                        bool sharedPackTransfer = false;
-                        if (m_index && !fresh->sourceGroupId.isEmpty()) {
-                            const auto allEntries = m_index->all();
-                            int groupCount = 0;
-                            for (const auto& e : allEntries) {
-                                if (e.sourceGroupId == fresh->sourceGroupId)
-                                    ++groupCount;
-                            }
-                            sharedPackTransfer = groupCount > 1;
-                        }
-                        if (sharedPackTransfer) {
-                            qInfo() << "[downloads] retry: shared pack transfer,"
-                                       " skipping engine cleanup"
-                                    << "group=" << fresh->sourceGroupId;
-                        } else {
-                            const QString hash = !fresh->infoHash.isEmpty()
-                                ? fresh->infoHash
-                                : tankostream::stream::infoHashFromGroup(fresh->sourceGroupId);
-                            if (!hash.isEmpty()) {
-                                if (auto* q = m_client->transferQueue())
-                                    q->cancel(hash);
-                                m_client->deleteTorrent(hash, /*deleteFiles=*/true);
-                            }
-                        }
-                    }
-                }
-                // T11.1 review I4 (spec §6): do NOT evict the Failed index
-                // entries on retry — if the re-pick finds no sources, the entry
-                // stays Failed and the row stays in the Failed section instead
-                // of vanishing. On success the re-dispatch overwrites the entry
-                // via the same canonical path (registerPendingEpisode). Trade-
-                // off: a different-source re-pick may create a second entry at
-                // a new path; bestEntryForEpisode + duplicate handling exist;
-                // accepted v1.
-                emit retryEpisodeRequested(fresh->imdbId, fresh->season, fresh->episode);
-            });
-
-    m_splitter->addWidget(m_tree);
-    m_splitter->addWidget(m_detailPane);
-    m_splitter->setStretchFactor(0, 2);
-    m_splitter->setStretchFactor(1, 3);
-
-    root->addWidget(m_splitter, 1);
-
-    // ── Tree selection → detail pane ────────────────────────────────────────
-    connect(m_tree, &QTreeWidget::currentItemChanged,
-            this, [this](QTreeWidgetItem* current, QTreeWidgetItem* /*previous*/) {
-        if (!current) {
-            m_selectedRow.reset();
-            m_detailPane->clearRow();
-            return;
-        }
-        const QVariant v = current->data(0, Qt::UserRole);
-        if (!v.isValid() || !v.canConvert<tankostream::stream::DownloadRow>()) {
-            m_selectedRow.reset();
-            m_detailPane->clearRow();
-            return;
-        }
-        const auto r = v.value<tankostream::stream::DownloadRow>();
-        m_selectedRow = r;
-        m_detailPane->setRow(r, displayShowTitle(r.imdbId));
-    });
-
-    // ── Double-click: play Completed episodes ───────────────────────────────
-    connect(m_tree, &QTreeWidget::itemDoubleClicked,
-            this, [this](QTreeWidgetItem* item, int /*column*/) {
-        if (!item) return;
-        const QVariant v = item->data(0, Qt::UserRole);
-        if (!v.isValid() || !v.canConvert<tankostream::stream::DownloadRow>()) return;
-        const auto r = v.value<tankostream::stream::DownloadRow>();
-        if (r.section != tankostream::stream::DownloadSection::Completed) return;
-        if (r.canonicalPath.isEmpty()) return;
-        // Prefer enriched catalog title; fall back to filename (preserves pre-T4
-        // derivation — never shows a raw imdbId in the player HUD).
-        QString title = m_titleCache.value(r.imdbId);
-        if (title.isEmpty())
-            title = QFileInfo(r.canonicalPath).completeBaseName();
-        emit playLocalFileRequested(
-            r.canonicalPath,
-            r.imdbId,
-            title,
-            r.season,
-            r.episode);
-    });
-}
-
-// ──────────────────────────────────────────────────────────────────────────────
-// showEvent — refresh on navigation
-// ──────────────────────────────────────────────────────────────────────────────
-
-void StreamDownloadsPage::showEvent(QShowEvent* event)
-{
-    QFrame::showEvent(event);
-    // Cheap insurance: navigating to this page always shows current state, never
-    // a stale tree left over from when the page was last visible.
-    m_rebuildDebounce->start();
-    // Start live-speed ticker only while visible to avoid unnecessary work.
-    m_totalsTimer->start();
+    // Scrollable body
+    m_scroll = new QScrollArea(this);
+    m_scroll->setObjectName("StreamDownloadsScroll");
+    m_scroll->setWidgetResizable(true);
+    m_scroll->setFrameShape(QFrame::NoFrame);
+
+    m_scrollContent = new QWidget(m_scroll);
+    m_scrollContent->setObjectName("StreamDownloadsScrollContent");
+    m_contentLayout = new QVBoxLayout(m_scrollContent);
+    m_contentLayout->setContentsMargins(20, 12, 20, 20);
+    m_contentLayout->setSpacing(18);
+
+    // Active section
+    m_activeHeader = new QLabel(tr("ACTIVE"), m_scrollContent);
+    m_activeHeader->setObjectName("StreamDownloadsSectionHeader");
+    m_activeHeader->setStyleSheet(
+        "QLabel#StreamDownloadsSectionHeader { font-size: 9pt; font-weight: 700;"
+        " color: rgba(255,255,255,0.55); letter-spacing: 1.2px; }");
+
+    m_activeBody = new QWidget(m_scrollContent);
+    m_activeBody->setObjectName("StreamDownloadsActiveBody");
+    m_activeBodyLayout = new QVBoxLayout(m_activeBody);
+    m_activeBodyLayout->setContentsMargins(0, 0, 0, 0);
+    m_activeBodyLayout->setSpacing(8);
+
+    // History section
+    m_historyHeader = new QLabel(tr("HISTORY"), m_scrollContent);
+    m_historyHeader->setObjectName("StreamDownloadsSectionHeader");
+    m_historyHeader->setStyleSheet(
+        "QLabel#StreamDownloadsSectionHeader { font-size: 9pt; font-weight: 700;"
+        " color: rgba(255,255,255,0.55); letter-spacing: 1.2px; }");
+
+    m_historyBody = new QWidget(m_scrollContent);
+    m_historyBody->setObjectName("StreamDownloadsHistoryBody");
+    m_historyBodyLayout = new QVBoxLayout(m_historyBody);
+    m_historyBodyLayout->setContentsMargins(0, 0, 0, 0);
+    m_historyBodyLayout->setSpacing(8);
+
+    // Empty state placeholder (shown when both sections have zero rows).
+    m_emptyState = new QLabel(
+        tr("No downloads yet.\n\nDispatch a season pack from any Theatre detail view."),
+        m_scrollContent);
+    m_emptyState->setObjectName("StreamDownloadsEmptyState");
+    m_emptyState->setAlignment(Qt::AlignCenter);
+    m_emptyState->setStyleSheet(
+        "QLabel#StreamDownloadsEmptyState { color: rgba(255,255,255,0.45);"
+        " font-size: 11pt; padding: 60px 20px; }");
+
+    m_contentLayout->addWidget(m_activeHeader, 0);
+    m_contentLayout->addWidget(m_activeBody, 0);
+    m_contentLayout->addWidget(m_historyHeader, 0);
+    m_contentLayout->addWidget(m_historyBody, 0);
+    m_contentLayout->addWidget(m_emptyState, 0);
+    m_contentLayout->addStretch(1);
+
+    m_scroll->setWidget(m_scrollContent);
+    root->addWidget(m_scroll, 1);
 }
 
-void StreamDownloadsPage::hideEvent(QHideEvent* event)
-{
-    QFrame::hideEvent(event);
-    m_totalsTimer->stop();
-}
-
-// ──────────────────────────────────────────────────────────────────────────────
-// Injection setters
-// ──────────────────────────────────────────────────────────────────────────────
-
 void StreamDownloadsPage::setTorrentClient(TorrentClient* client)
 {
-    if (m_client == client)
+    if (m_torrentClient == client)
         return;
-    if (m_client)
-        disconnect(m_client, nullptr, this, nullptr);
-    m_client = client;
-    // Disconnect the old TransferQueue's signals before wiring the new client.
-    // The queue is a separate QObject from TorrentClient so the client disconnect
-    // above does not cover it; m_connectedQueue tracks it explicitly.
-    if (m_connectedQueue)
-        disconnect(m_connectedQueue, nullptr, this, nullptr);
-    m_connectedQueue = nullptr;
-
-    if (m_client) {
-        // Legacy signal still in TorrentClient — keep it for coverage.
-        connect(m_client, &TorrentClient::streamBulkGroupsChanged,
-                this, [this](const QString&) { m_rebuildDebounce->start(); },
+    if (m_torrentClient) {
+        disconnect(m_torrentClient, nullptr, this, nullptr);
+    }
+    m_torrentClient = client;
+    if (m_torrentClient) {
+        connect(m_torrentClient, &TorrentClient::streamBulkGroupsChanged,
+                this, [this](const QString&) { refreshActive(); },
                 Qt::QueuedConnection);
-
-        // TransferQueue signals (new for T4).
-        if (auto* tq = m_client->transferQueue()) {
-            connect(tq, &tankoban::queue::TransferQueue::laneChanged,
-                    this, [this](const QString&) { m_rebuildDebounce->start(); },
-                    Qt::QueuedConnection);
-            connect(tq, &tankoban::queue::TransferQueue::itemStateChanged,
-                    this, [this](const QString&, tankoban::queue::TransferState) {
-                        m_rebuildDebounce->start();
-                    },
-                    Qt::QueuedConnection);
-            m_connectedQueue = tq;
-        }
     }
-
-    // Forward client to the detail pane so it can construct tabs lazily.
-    if (m_detailPane)
-        m_detailPane->setClient(m_client);
-
-    // Enable strip controls only once we have a live client — buttons are
-    // created disabled and stay that way until injection (T7).
-    const bool hasClient = (m_client != nullptr);
-    m_pauseAllBtn->setEnabled(hasClient);
-    m_resumeAllBtn->setEnabled(hasClient);
-    m_clearDoneBtn->setEnabled(hasClient);
-    m_maxActiveCombo->setEnabled(hasClient);
-
-    m_rebuildDebounce->start();
+    refreshActive();
 }
 
 void StreamDownloadsPage::setStreamDownloadIndex(StreamDownloadIndex* index)
 {
-    if (m_index == index)
+    if (m_streamDownloadIndex == index)
         return;
-    if (m_index)
-        disconnect(m_index, nullptr, this, nullptr);
-    m_index = index;
-    if (m_index) {
-        connect(m_index, &StreamDownloadIndex::entriesChanged,
-                this, [this]() { m_rebuildDebounce->start(); },
-                Qt::QueuedConnection);
-        // entryStateChanged carries per-piece progress; updateEpisodeProgress
-        // deliberately does NOT emit entriesChanged, so without this connect
-        // the Active section's pct column freezes mid-download.
-        connect(m_index, &StreamDownloadIndex::entryStateChanged,
-                this, [this](const QString&) { m_rebuildDebounce->start(); },
+    if (m_streamDownloadIndex) {
+        disconnect(m_streamDownloadIndex, nullptr, this, nullptr);
+    }
+    m_streamDownloadIndex = index;
+    if (m_streamDownloadIndex) {
+        connect(m_streamDownloadIndex, &StreamDownloadIndex::entriesChanged,
+                this, &StreamDownloadsPage::refreshHistory,
                 Qt::QueuedConnection);
     }
-    m_rebuildDebounce->start();
+    refreshHistory();
 }
 
 void StreamDownloadsPage::setMetaAggregator(tankostream::stream::MetaAggregator* agg)
 {
-    if (m_meta == agg)
+    if (m_metaAggregator == agg)
         return;
-    if (m_meta)
-        disconnect(m_meta, nullptr, this, nullptr);
-    m_meta = agg;
-    if (m_meta) {
-        connect(m_meta,
+    if (m_metaAggregator)
+        disconnect(m_metaAggregator, nullptr, this, nullptr);
+    m_metaAggregator = agg;
+    if (m_metaAggregator) {
+        connect(m_metaAggregator,
                 &tankostream::stream::MetaAggregator::metaItemReady,
                 this, &StreamDownloadsPage::onMetaItemReady,
                 Qt::UniqueConnection);
     }
-    m_rebuildDebounce->start();
+    // Re-render so the enrichment fetch fires now that the provider exists.
+    refreshHistory();
+    refreshActive();
 }
 
-// ──────────────────────────────────────────────────────────────────────────────
-// Core rebuild
-// ──────────────────────────────────────────────────────────────────────────────
-
-void StreamDownloadsPage::rebuild()
+QWidget* StreamDownloadsPage::makePosterWidget(const QString& imdbId, const QString& title)
 {
-    if (!m_index) return;
-
-    tankostream::stream::DownloadsSnapshot snap;
-    snap.indexEntries = m_index->all();
-    if (m_client && m_client->transferQueue())
-        snap.lanes = m_client->transferQueue()->lanesSnapshot();
-
-    auto rows = tankostream::stream::buildDownloadRows(
-        snap, QDateTime::currentMSecsSinceEpoch(), kCompletedTrimMs);
-
-    // DOWNLOADS_OVERHAUL_V2 T7.1 — Clear Done filter: hide Completed rows the
-    // user explicitly cleared (keys captured at click time). A download that
-    // was still in flight at click time was never keyed — same addedAt, but
-    // not Completed then — so it surfaces normally when it finishes (review
-    // I1). Display-only; index is untouched.
-    if (!m_clearedDoneKeys.isEmpty()) {
-        rows.erase(
-            std::remove_if(rows.begin(), rows.end(),
-                [this](const tankostream::stream::DownloadRow& r) {
-                    return r.section == tankostream::stream::DownloadSection::Completed
-                           && m_clearedDoneKeys.contains(clearedDoneKey(r));
-                }),
-            rows.end());
-    }
+    auto* pl = new QLabel;
+    pl->setObjectName("StreamDownloadsPoster");
+    pl->setFixedSize(96, 144);
+    pl->setScaledContents(true);
+    pl->setAlignment(Qt::AlignCenter);
+    pl->setWordWrap(true);
+    pl->setStyleSheet(
+        "QLabel#StreamDownloadsPoster {"
+        "  border-top-left-radius: 12px; border-bottom-left-radius: 12px;"
+        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
+        "    stop:0 rgba(255,255,255,0.10), stop:1 rgba(255,255,255,0.03));"
+        "  color: rgba(255,255,255,0.45); font-size: 9pt; padding: 6px;"
+        "}");
 
-    const QString selectedKey = currentSelectionKey();
-    m_tree->clear();
-
-    QTreeWidgetItem* sectionItems[4] = {};
-    QHash<QString, QTreeWidgetItem*> showNodes;  // key: "<sectionIdx>|<imdbId>"
-
-    for (const auto& r : rows) {
-        const int s = int(r.section);
-
-        // Section header node
-        if (!sectionItems[s]) {
-            sectionItems[s] = new QTreeWidgetItem(m_tree,
-                {QString::fromLatin1(kSectionNames[s]), QString(), QString()});
-            sectionItems[s]->setExpanded(true);
-            sectionItems[s]->setFlags(Qt::ItemIsEnabled);
-            QFont f = sectionItems[s]->font(0);
-            f.setPointSize(9);
-            f.setBold(true);
-            sectionItems[s]->setFont(0, f);
-            sectionItems[s]->setForeground(
-                0, QBrush(QColor(255, 255, 255, 140)));
-        }
-
-        // Show grouping node
-        const QString showKey = QString::number(s) + QLatin1Char('|') + r.imdbId;
-        QTreeWidgetItem*& showNode = showNodes[showKey];
-        if (!showNode) {
-            showNode = new QTreeWidgetItem(sectionItems[s],
-                {displayShowTitle(r.imdbId), QString(), QString()});
-            showNode->setExpanded(true);
-            showNode->setFlags(Qt::ItemIsEnabled);
-
-            // Poster icon (from cache if available — no new network machinery)
-            const auto pit = m_posterCache.constFind(r.imdbId);
-            if (pit != m_posterCache.constEnd() && !pit->isNull())
-                showNode->setIcon(0, QIcon(*pit));
-        }
-
-        // Episode leaf
-        auto* item = new QTreeWidgetItem(showNode);
-        item->setText(0, r.type == QLatin1String("movie")
-            ? tr("Movie")
-            : QStringLiteral("S%1E%2")
-                  .arg(r.season, 2, 10, QLatin1Char('0'))
-                  .arg(r.episode, 2, 10, QLatin1Char('0')));
-        item->setText(1,
-            r.section == tankostream::stream::DownloadSection::Completed
-                ? QString()
-                : QStringLiteral("%1%").arg(r.pct));
-        item->setText(2, statusText(r));
-        item->setData(0, Qt::UserRole, QVariant::fromValue(r));
-
-        if (r.section == tankostream::stream::DownloadSection::Failed)
-            item->setForeground(0, QBrush(QColor(0xf3, 0xa6, 0xa6)));
-
-        // Kick off enrichment fetch for any new imdbId we haven't seen yet.
-        // m_metaRequested guards against a per-rebuild storm: negative results
-        // aren't cached by MetaAggregator, so a failing id would be refetched
-        // on every rebuild without this session-scoped set.
-        if (!m_titleCache.contains(r.imdbId) && m_meta
-                && !m_metaRequested.contains(r.imdbId)) {
-            m_metaRequested.insert(r.imdbId);
-            const QString type = (r.type == QLatin1String("movie"))
-                ? QStringLiteral("movie")
-                : QStringLiteral("series");
-            m_meta->fetchMetaItem(r.imdbId, type);
-        }
+    const QString path = posterCachePath(imdbId);
+    QPixmap pm;
+    if (QFile::exists(path) && pm.load(path)) {
+        pl->setPixmap(pm.scaled(96, 144, Qt::KeepAspectRatioByExpanding,
+                                Qt::SmoothTransformation));
+    } else {
+        pl->setText(title);  // placeholder until art loads
     }
-
-    restoreSelection(selectedKey);
-    updateTotals();
+    return pl;
 }
 
-// ──────────────────────────────────────────────────────────────────────────────
-// Selection helpers
-// ──────────────────────────────────────────────────────────────────────────────
-
-QString StreamDownloadsPage::currentSelectionKey() const
+void StreamDownloadsPage::savePosterFrom(const QString& imdbId, const QUrl& posterUrl)
 {
-    if (!m_selectedRow.has_value()) return {};
-    return makeSelectionKey(*m_selectedRow);
+    if (posterUrl.isEmpty() || QFile::exists(posterCachePath(imdbId)))
+        return;
+    if (!m_posterNam)
+        m_posterNam = tankoban::net::NetSeam::instance()->createManager(this, QStringLiteral("stream-downloads-poster"));
+    QNetworkRequest req(posterUrl);
+    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
+                     QNetworkRequest::NoLessSafeRedirectPolicy);
+    QNetworkReply* reply = m_posterNam->get(req);
+    connect(reply, &QNetworkReply::finished, this, [this, reply, imdbId]() {
+        reply->deleteLater();
+        if (reply->error() != QNetworkReply::NoError)
+            return;
+        QPixmap pm;
+        if (!pm.loadFromData(reply->readAll()))
+            return;
+        const QString path = posterCachePath(imdbId);
+        QDir().mkpath(QFileInfo(path).absolutePath());
+        pm.save(path, "JPG");
+        const QPixmap scaled = pm.scaled(96, 144, Qt::KeepAspectRatioByExpanding,
+                                         Qt::SmoothTransformation);
+        for (QHash<QString, DownloadCardRefs>* map : {&m_historyCards, &m_activeCards}) {
+            auto it = map->constFind(imdbId);
+            if (it != map->constEnd()) {
+                if (auto* lbl = qobject_cast<QLabel*>(it->posterWidget)) {
+                    lbl->setText(QString());
+                    lbl->setPixmap(scaled);
+                }
+            }
+        }
+    });
 }
 
-void StreamDownloadsPage::restoreSelection(const QString& key)
+void StreamDownloadsPage::onMetaItemReady(const tankostream::addon::MetaItem& item)
 {
-    if (key.isEmpty()) return;
-    // Walk every leaf item in the tree looking for a matching key.
-    QTreeWidgetItemIterator it(m_tree);
-    while (*it) {
-        const QVariant v = (*it)->data(0, Qt::UserRole);
-        if (v.isValid() && v.canConvert<tankostream::stream::DownloadRow>()) {
-            const auto r = v.value<tankostream::stream::DownloadRow>();
-            if (makeSelectionKey(r) == key) {
-                m_tree->setCurrentItem(*it);
-                return;
+    const QString imdbId = item.preview.id;
+    savePosterFrom(imdbId, item.preview.poster);
+
+    for (QHash<QString, DownloadCardRefs>* map : {&m_historyCards, &m_activeCards}) {
+        auto it = map->find(imdbId);
+        if (it == map->end())
+            continue;
+        DownloadCardRefs& refs = it.value();
+        if (!item.preview.name.isEmpty() && refs.titleLabel)
+            refs.titleLabel->setText(item.preview.name);
+
+        // Movie row (single entry) — enrich to the catalog name.
+        if (!item.preview.name.isEmpty()) {
+            auto mit = refs.rowTitleByKey.constFind(QStringLiteral("movie"));
+            if (mit != refs.rowTitleByKey.constEnd() && mit.value())
+                mit.value()->setText(item.preview.name);
+        }
+
+        for (const tankostream::addon::Video& v : item.videos) {
+            if (!v.seriesInfo.has_value() || v.title.isEmpty())
+                continue;
+            const int s = v.seriesInfo->season;
+            const int ep = v.seriesInfo->episode;
+            auto rit = refs.rowTitleByKey.constFind(QStringLiteral("%1:%2").arg(s).arg(ep));
+            if (rit != refs.rowTitleByKey.constEnd() && rit.value()) {
+                rit.value()->setText(QStringLiteral("S%1E%2")
+                                         .arg(s, 2, 10, QLatin1Char('0'))
+                                         .arg(ep, 2, 10, QLatin1Char('0'))
+                                     + dotSep() + v.title);
             }
         }
-        ++it;
     }
-    // Key gone (item removed) — clear selection state
-    m_selectedRow.reset();
-    m_detailPane->clearRow();
 }
 
-// ──────────────────────────────────────────────────────────────────────────────
-// Row text helpers
-// ──────────────────────────────────────────────────────────────────────────────
-
-QString StreamDownloadsPage::statusText(const tankostream::stream::DownloadRow& r) const
+void StreamDownloadsPage::updateEmptyState()
 {
-    using tankostream::stream::DownloadSection;
-    switch (r.section) {
-    case DownloadSection::Failed:    return QStringLiteral("failed");
-    case DownloadSection::Active:    return r.paused ? QStringLiteral("paused") : QString();
-    case DownloadSection::Queued:    return QStringLiteral("queued");
-    case DownloadSection::Completed: return QStringLiteral("done");
+    if (!m_emptyState || !m_activeBody || !m_historyBody
+        || !m_activeBodyLayout || !m_historyBodyLayout) {
+        return;
     }
-    return {};
-}
 
-QString StreamDownloadsPage::displayShowTitle(const QString& imdbId) const
-{
-    const auto it = m_titleCache.constFind(imdbId);
-    if (it != m_titleCache.constEnd() && !it->isEmpty())
-        return *it;
-    return imdbId;
+    const bool anyActive = !m_activeBody->isHidden() && m_activeBodyLayout->count() > 0;
+    const bool anyHistory = !m_historyBody->isHidden() && m_historyBodyLayout->count() > 0;
+    m_emptyState->setVisible(!anyActive && !anyHistory);
 }
 
-// ──────────────────────────────────────────────────────────────────────────────
-// Intent helper — DOWNLOADS_OVERHAUL_V2 T6
-// ──────────────────────────────────────────────────────────────────────────────
-
-// Re-resolve the row's CURRENT state by key before acting on an intent —
-// the pane's snapshot can be a debounce stale (~250ms+, T5 review I4).
-// Returns nullopt when the episode no longer exists in the model (e.g. was
-// removed between the button press and the signal delivery).
-// trim=0 deliberately: an intent against a just-trimmed Completed row should
-// still resolve so the guard can fire; trimming only happens in the UI tree.
-std::optional<tankostream::stream::DownloadRow>
-StreamDownloadsPage::freshRowFor(const tankostream::stream::DownloadRow& stale) const
+void StreamDownloadsPage::refreshActive()
 {
-    if (!m_index) return std::nullopt;
-    tankostream::stream::DownloadsSnapshot snap;
-    snap.indexEntries = m_index->all();
-    if (m_client && m_client->transferQueue())
-        snap.lanes = m_client->transferQueue()->lanesSnapshot();
-    const auto rows = tankostream::stream::buildDownloadRows(
-        snap, QDateTime::currentMSecsSinceEpoch(), /*maxCompletedAgeMs=*/0);
-    // Two-pass match (T6 review I2): duplicate index entries for one episode
-    // are documented reality (e.g. an old Failed entry alongside a freshly
-    // re-registered Pending one). Prefer the row in the section the user acted
-    // on; only if none exists fall back to the first (imdbId,season,episode)
-    // match.
-    for (const auto& r : rows) {
-        if (r.imdbId == stale.imdbId
-            && r.season  == stale.season
-            && r.episode == stale.episode
-            && r.section == stale.section)
-            return r;
-    }
-    for (const auto& r : rows) {
-        if (r.imdbId == stale.imdbId
-            && r.season  == stale.season
-            && r.episode == stale.episode)
-            return r;
-    }
-    return std::nullopt;
-}
+    if (!m_activeBodyLayout)
+        return;
 
-// ──────────────────────────────────────────────────────────────────────────────
-// Totals — DOWNLOADS_OVERHAUL_V2 T7
-// ──────────────────────────────────────────────────────────────────────────────
+    while (auto* item = m_activeBodyLayout->takeAt(0)) {
+        if (auto* w = item->widget()) w->deleteLater();
+        delete item;
+    }
+    m_activeCards.clear();  // stored handles point at the widgets just torn down
 
-void StreamDownloadsPage::updateTotals()
-{
-    if (!m_client) {
-        m_totalsLabel->setText(QString());
+    if (!m_torrentClient) {
+        m_activeHeader->setVisible(false);
+        m_activeBody->setVisible(false);
+        updateEmptyState();
         return;
     }
 
-    // Running count from the queue (queue-aware, cap-correct).
-    int n = 0;
-    if (auto* q = m_client->transferQueue())
-        n = q->runningCount();
-
-    // Aggregate download speed straight from the engine's thread-safe status
-    // snapshot (T7.1). listActive() would work too but runs a SQLite SELECT
-    // (repo.listTorrents) per call just to overlay the same live engine
-    // fields — pointless at 1 Hz when speed is the only thing we need.
-    // Paused / seeding handles report downloadRate 0, so a plain sum is fine.
-    qint64 totalBps = 0;
-    if (auto* engine = m_client->engine()) {
-        const auto statuses = engine->allStatuses();
-        for (const auto& s : statuses)
-            totalBps += s.downloadRate;
+    const QJsonObject groups = m_torrentClient->streamBulkGroups();
+    if (groups.isEmpty()) {
+        m_activeHeader->setVisible(false);
+        m_activeBody->setVisible(false);
+        updateEmptyState();
+        return;
     }
 
-    // Format: "N active · 1.2 MB/s"
-    if (n == 0 && totalBps == 0) {
-        m_totalsLabel->setText(tr("0 active"));
-    } else {
-        m_totalsLabel->setText(
-            tr("%1 active  ·  %2")
-                .arg(n)
-                .arg(formatDownloadSpeed(totalBps)));
+    // Group by imdbId. Each group's items[] array carries the per-episode
+    // state machine entries. We display one card per show (imdbId), with
+    // an aggregated progress label and a per-episode summary count.
+    QHash<QString, QList<QJsonObject>> byImdb;
+    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
+        if (!it.value().isObject()) continue;
+        const QJsonObject group = it.value().toObject();
+        const QJsonObject sourceIds = group.value(QStringLiteral("sourceIds")).toObject();
+        QString imdbId = group.value(QStringLiteral("imdbId")).toString();
+        if (imdbId.isEmpty())
+            imdbId = sourceIds.value(QStringLiteral("seriesId")).toString();
+        if (imdbId.isEmpty())
+            continue;
+        byImdb[imdbId].append(group);
     }
-}
 
-// ──────────────────────────────────────────────────────────────────────────────
-// Meta enrichment — rewired from old onMetaItemReady to serve the tree
-// ──────────────────────────────────────────────────────────────────────────────
-
-void StreamDownloadsPage::onMetaItemReady(const tankostream::addon::MetaItem& item)
-{
-    const QString imdbId = item.preview.id;
-    if (imdbId.isEmpty()) return;
+    if (byImdb.isEmpty()) {
+        m_activeHeader->setVisible(false);
+        m_activeBody->setVisible(false);
+        updateEmptyState();
+        return;
+    }
 
-    // Update title cache
-    if (!item.preview.name.isEmpty())
-        m_titleCache.insert(imdbId, item.preview.name);
+    m_activeHeader->setVisible(true);
+    m_activeBody->setVisible(true);
+
+    QStringList imdbsSorted = byImdb.keys();
+    std::sort(imdbsSorted.begin(), imdbsSorted.end());
+    for (const QString& imdbId : imdbsSorted) {
+        const QList<QJsonObject>& showGroups = byImdb[imdbId];
+
+        // Aggregate across this show's season groups into one progress summary.
+        QSet<int> seasons;
+        int total = 0, done = 0, active = 0, pending = 0, failed = 0;
+        for (const QJsonObject& group : showGroups) {
+            const int season = group.value(QStringLiteral("season")).toInt(
+                group.value(QStringLiteral("sourceIds")).toObject()
+                     .value(QStringLiteral("season")).toInt(0));
+            if (season > 0) seasons.insert(season);
+            const QJsonArray items = group.value(QStringLiteral("items")).toArray();
+            for (const auto& v : items) {
+                ++total;
+                const QString state = v.toObject().value(QStringLiteral("itemState")).toString();
+                if (state == QLatin1String("Published") || state == QLatin1String("Completed"))
+                    ++done;
+                else if (state == QLatin1String("Downloading") || state == QLatin1String("Publishing"))
+                    ++active;
+                else if (state == QLatin1String("Pending"))
+                    ++pending;
+                else
+                    ++failed;
+            }
+        }
 
-    // Save poster to disk cache and update in-memory QPixmap cache.
-    savePosterFrom(imdbId, item.preview.poster);
+        const QString showTitle = bestTitleFromGroups(showGroups, imdbId);
+
+        auto* card = new QFrame(m_activeBody);
+        card->setObjectName("StreamDownloadsActiveCard");
+        card->setStyleSheet(
+            "QFrame#StreamDownloadsActiveCard {"
+            "  background: rgba(255,255,255,0.038);"
+            "  border: 1px solid rgba(255,255,255,0.08);"
+            "  border-radius: 12px;"
+            "}");
+        auto* h = new QHBoxLayout(card);
+        h->setContentsMargins(0, 0, 0, 0);
+        h->setSpacing(0);
+
+        DownloadCardRefs refs;
+        refs.card = card;
+        refs.posterWidget = makePosterWidget(imdbId, showTitle);
+        h->addWidget(refs.posterWidget, 0, Qt::AlignTop);
+
+        auto* right = new QVBoxLayout();
+        right->setContentsMargins(16, 13, 16, 13);
+        right->setSpacing(6);
+
+        refs.titleLabel = new QLabel(showTitle, card);
+        refs.titleLabel->setObjectName("StreamDownloadsShowTitle");
+        refs.titleLabel->setStyleSheet(
+            "QLabel#StreamDownloadsShowTitle { color: #ededed; font-size: 15px; font-weight: 600; }");
+        right->addWidget(refs.titleLabel);
+
+        auto* sub = new QLabel(card);
+        QString subText = tr("%1 of %2 downloaded").arg(done).arg(total);
+        if (seasons.size() == 1)
+            subText = tr("Season %1").arg(*seasons.begin()) + dotSep() + subText;
+        sub->setText(subText);
+        sub->setStyleSheet("color: rgba(255,255,255,0.55); font-size: 12px;");
+        right->addWidget(sub);
+
+        // Grayscale progress bar — fill/empty via layout stretch (responsive).
+        auto* track = new QFrame(card);
+        track->setFixedHeight(4);
+        track->setStyleSheet("background: rgba(255,255,255,0.12); border-radius: 2px;");
+        auto* tl = new QHBoxLayout(track);
+        tl->setContentsMargins(0, 0, 0, 0);
+        tl->setSpacing(0);
+        auto* fill = new QFrame(track);
+        fill->setStyleSheet("background: rgba(255,255,255,0.55); border-radius: 2px;");
+        tl->addWidget(fill, done);
+        tl->addStretch(total > done ? total - done : 0);
+        right->addWidget(track);
+
+        QStringList parts;
+        if (active > 0) parts << tr("%1 downloading").arg(active);
+        if (pending > 0) parts << tr("%1 queued").arg(pending);
+        if (failed > 0) parts << tr("%1 stuck").arg(failed);
+        if (!parts.isEmpty()) {
+            auto* state = new QLabel(parts.join(dotSep()), card);
+            state->setStyleSheet("color: rgba(255,255,255,0.50); font-size: 11px;");
+            right->addWidget(state);
+        }
 
-    // Propagate title update into the tree's show-group nodes
-    // (iterate all top-level section nodes → their show-group children).
-    const int topCount = m_tree->topLevelItemCount();
-    for (int si = 0; si < topCount; ++si) {
-        QTreeWidgetItem* sectionNode = m_tree->topLevelItem(si);
-        if (!sectionNode) continue;
-        const int showCount = sectionNode->childCount();
-        for (int sh = 0; sh < showCount; ++sh) {
-            QTreeWidgetItem* showNode = sectionNode->child(sh);
-            if (!showNode) continue;
-            // Infer imdbId from first episode child
-            if (showNode->childCount() == 0) continue;
-            const QVariant v = showNode->child(0)->data(0, Qt::UserRole);
-            if (!v.isValid() || !v.canConvert<tankostream::stream::DownloadRow>())
-                continue;
-            const auto r = v.value<tankostream::stream::DownloadRow>();
-            if (r.imdbId != imdbId) continue;
+        h->addLayout(right, 1);
 
-            if (!item.preview.name.isEmpty())
-                showNode->setText(0, item.preview.name);
+        m_activeCards.insert(imdbId, refs);
+        m_activeBodyLayout->addWidget(card);
 
-            // Set poster icon on show node if we now have it cached
-            const auto pit = m_posterCache.constFind(imdbId);
-            if (pit != m_posterCache.constEnd() && !pit->isNull())
-                showNode->setIcon(0, QIcon(*pit));
-        }
+        if (m_metaAggregator)
+            m_metaAggregator->fetchMetaItem(imdbId, QStringLiteral("series"));
     }
-}
 
-// ──────────────────────────────────────────────────────────────────────────────
-// Poster download + caching (kept from pre-T4; rewired to m_posterCache)
-// ──────────────────────────────────────────────────────────────────────────────
+    updateEmptyState();
+}
 
-void StreamDownloadsPage::savePosterFrom(const QString& imdbId, const QUrl& posterUrl)
+void StreamDownloadsPage::refreshHistory()
 {
-    // If already in memory cache, nothing to do.
-    if (m_posterCache.contains(imdbId)) return;
+    if (!m_historyBodyLayout)
+        return;
 
-    const QString path = posterCachePath(imdbId);
-    // Try on-disk cache first (another consumer may have fetched it already).
-    QPixmap pm;
-    if (QFile::exists(path) && pm.load(path)) {
-        m_posterCache.insert(imdbId, pm.scaled(16, 24, Qt::KeepAspectRatioByExpanding,
-                                                Qt::SmoothTransformation));
+    while (auto* item = m_historyBodyLayout->takeAt(0)) {
+        if (auto* w = item->widget()) w->deleteLater();
+        delete item;
+    }
+    m_historyCards.clear();  // stored handles point at the widgets just torn down
+
+    if (!m_streamDownloadIndex) {
+        m_historyHeader->setVisible(false);
+        m_historyBody->setVisible(false);
+        updateEmptyState();
         return;
     }
 
-    if (posterUrl.isEmpty()) return;
+    const QList<StreamDownloadIndex::Entry> all = m_streamDownloadIndex->all();
+    if (all.isEmpty()) {
+        m_historyHeader->setVisible(false);
+        m_historyBody->setVisible(false);
+        updateEmptyState();
+        return;
+    }
 
-    if (!m_posterNam)
-        m_posterNam = tankoban::net::NetSeam::instance()->createManager(
-            this, QStringLiteral("stream-downloads-poster"));
+    QHash<QString, QList<StreamDownloadIndex::Entry>> byImdb;
+    for (const auto& e : all) {
+        if (e.state != StreamDownloadIndex::Entry::Complete)
+            continue;
+        byImdb[e.imdbId].append(e);
+    }
 
-    QNetworkRequest req(posterUrl);
-    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
-                     QNetworkRequest::NoLessSafeRedirectPolicy);
-    QNetworkReply* reply = m_posterNam->get(req);
-    connect(reply, &QNetworkReply::finished, this, [this, reply, imdbId]() {
-        reply->deleteLater();
-        if (reply->error() != QNetworkReply::NoError) return;
-        QPixmap pm;
-        if (!pm.loadFromData(reply->readAll())) return;
+    if (byImdb.isEmpty()) {
+        m_historyHeader->setVisible(false);
+        m_historyBody->setVisible(false);
+        updateEmptyState();
+        return;
+    }
 
-        // Persist to disk (shared cache)
-        const QString path = posterCachePath(imdbId);
-        QDir().mkpath(QFileInfo(path).absolutePath());
-        pm.save(path, "JPG");
+    m_historyHeader->setVisible(true);
+    m_historyBody->setVisible(true);
+
+    QStringList imdbsSorted = byImdb.keys();
+    std::sort(imdbsSorted.begin(), imdbsSorted.end(),
+              [&byImdb](const QString& a, const QString& b) {
+                  qint64 maxA = 0, maxB = 0;
+                  for (const auto& e : byImdb[a]) maxA = std::max(maxA, e.addedAt);
+                  for (const auto& e : byImdb[b]) maxB = std::max(maxB, e.addedAt);
+                  return maxA > maxB;
+              });
+
+    for (const QString& imdbId : imdbsSorted) {
+        QList<StreamDownloadIndex::Entry> entries = byImdb[imdbId];
+        std::sort(entries.begin(), entries.end(),
+                  [](const StreamDownloadIndex::Entry& a, const StreamDownloadIndex::Entry& b) {
+                      if (a.season != b.season) return a.season < b.season;
+                      return a.episode < b.episode;
+                  });
 
-        // Store tree-icon sized pixmap in memory cache
-        m_posterCache.insert(imdbId, pm.scaled(16, 24, Qt::KeepAspectRatioByExpanding,
-                                               Qt::SmoothTransformation));
-
-        // Patch existing show nodes that belong to this imdbId
-        const int topCount = m_tree->topLevelItemCount();
-        for (int si = 0; si < topCount; ++si) {
-            QTreeWidgetItem* sectionNode = m_tree->topLevelItem(si);
-            if (!sectionNode) continue;
-            const int showCount = sectionNode->childCount();
-            for (int sh = 0; sh < showCount; ++sh) {
-                QTreeWidgetItem* showNode = sectionNode->child(sh);
-                if (!showNode || showNode->childCount() == 0) continue;
-                const QVariant v = showNode->child(0)->data(0, Qt::UserRole);
-                if (!v.isValid() || !v.canConvert<tankostream::stream::DownloadRow>())
-                    continue;
-                const auto r = v.value<tankostream::stream::DownloadRow>();
-                if (r.imdbId == imdbId)
-                    showNode->setIcon(0, QIcon(m_posterCache.value(imdbId)));
+        const bool isMovie = entries.first().type == QLatin1String("movie");
+        const QString showTitle = bestTitleFromEntries(entries, imdbId);
+
+        auto* card = new QFrame(m_historyBody);
+        card->setObjectName("StreamDownloadsHistoryCard");
+        card->setStyleSheet(
+            "QFrame#StreamDownloadsHistoryCard {"
+            "  background: rgba(255,255,255,0.038);"
+            "  border: 1px solid rgba(255,255,255,0.08);"
+            "  border-radius: 12px;"
+            "}");
+        auto* h = new QHBoxLayout(card);
+        h->setContentsMargins(0, 0, 0, 0);
+        h->setSpacing(0);
+
+        DownloadCardRefs refs;
+        refs.card = card;
+        refs.posterWidget = makePosterWidget(imdbId, showTitle);
+        h->addWidget(refs.posterWidget, 0, Qt::AlignTop);
+
+        auto* right = new QVBoxLayout();
+        right->setContentsMargins(16, 13, 16, 11);
+        right->setSpacing(3);
+
+        refs.titleLabel = new QLabel(showTitle, card);
+        refs.titleLabel->setObjectName("StreamDownloadsShowTitle");
+        refs.titleLabel->setStyleSheet(
+            "QLabel#StreamDownloadsShowTitle { color: #ededed; font-size: 15px; font-weight: 600; }");
+        right->addWidget(refs.titleLabel);
+
+        auto* sub = new QLabel(card);
+        sub->setText(isMovie ? tr("Movie") : tr("%n episode(s)", "", entries.size()));
+        sub->setStyleSheet("color: rgba(255,255,255,0.55); font-size: 12px;");
+        right->addWidget(sub);
+
+        auto* rows = new QVBoxLayout();
+        rows->setContentsMargins(0, 8, 0, 0);
+        rows->setSpacing(2);
+
+        for (const auto& e : entries) {
+            auto* row = new QPushButton(card);
+            row->setObjectName("StreamDownloadsHistoryRow");
+            row->setCursor(Qt::PointingHandCursor);
+            row->setFlat(true);
+            row->setStyleSheet(
+                "QPushButton#StreamDownloadsHistoryRow {"
+                "  text-align: left; padding: 7px 10px; color: rgba(255,255,255,0.86);"
+                "  font-size: 13px; border: none; background: transparent;"
+                "}"
+                "QPushButton#StreamDownloadsHistoryRow:hover {"
+                "  background: rgba(255,255,255,0.065); border-radius: 6px;"
+                "}");
+
+            // Title-only. Placeholder = prettified filename; metaItemReady swaps
+            // in the real catalog title (rebuilding the SxxExx · title text).
+            const QString placeholder = prettifyFilenameTitle(e.canonicalPath);
+            if (isMovie) {
+                row->setText(placeholder.isEmpty() ? showTitle : placeholder);
+                refs.rowTitleByKey.insert(QStringLiteral("movie"), row);
+            } else {
+                row->setText(QStringLiteral("S%1E%2")
+                                 .arg(e.season, 2, 10, QLatin1Char('0'))
+                                 .arg(e.episode, 2, 10, QLatin1Char('0'))
+                             + dotSep() + placeholder);
+                refs.rowTitleByKey.insert(
+                    QStringLiteral("%1:%2").arg(e.season).arg(e.episode), row);
             }
+
+            const QString canonicalPath = e.canonicalPath;
+            const QString rowImdb = e.imdbId;
+            const int rowSeason = e.season;
+            const int rowEpisode = e.episode;
+            connect(row, &QPushButton::clicked, this,
+                    [this, canonicalPath, rowImdb, rowSeason, rowEpisode]() {
+                        emit playLocalFileRequested(canonicalPath, rowImdb,
+                                                    QFileInfo(canonicalPath).completeBaseName(),
+                                                    rowSeason, rowEpisode);
+                    });
+            rows->addWidget(row);
         }
-    });
+
+        right->addLayout(rows);
+        h->addLayout(right, 1);
+
+        m_historyCards.insert(imdbId, refs);
+        m_historyBodyLayout->addWidget(card);
+
+        if (m_metaAggregator)
+            m_metaAggregator->fetchMetaItem(
+                imdbId, isMovie ? QStringLiteral("movie") : QStringLiteral("series"));
+    }
+
+    updateEmptyState();
 }
diff --git a/src/ui/pages/stream/StreamDownloadsPage.h b/src/ui/pages/stream/StreamDownloadsPage.h
index d318656..583f6b6 100644
--- a/src/ui/pages/stream/StreamDownloadsPage.h
+++ b/src/ui/pages/stream/StreamDownloadsPage.h
@@ -1,41 +1,33 @@
 #pragma once
 
-// DOWNLOADS_OVERHAUL_V2 Task 4 (2026-06-11) — Master-Detail shell rebuild.
-// Task 5 (2026-06-11) — m_detailPlaceholder replaced by DownloadDetailPane.
-// Task 7 (2026-06-11) — Top strip wired: live totals, Pause All / Resume All /
-//   Clear Done, max-active knob.
-// The page is now driven by tankostream::stream::buildDownloadRows so the
-// Active / History split is replaced by a single four-section (Failed /
-// Active / Queued / Completed) grouped tree with a real detail pane on the
-// right. Public API (class name, constructor signature, injection setters,
-// signals) is identical to the pre-T4 page so MainWindow wiring is unchanged.
-
-#include "core/stream/DownloadsCommandModel.h"
+// STREAM_DOWNLOADS_SIDEBAR_PAGE 2026-05-25 (Agent 4 commission via Trigger D
+// to Agent 7) - aggregate Downloads page accessible from the SidebarDrawer's
+// new "Downloads" entry. Renders two sections:
+//   - Active: in-flight Theatre downloads, grouped by IMDb show, sourced from
+//     TorrentClient::streamBulkGroups() + streamBulkSnapshotForImdbSeason.
+//   - History: completed Theatre downloads, grouped by IMDb show, sourced
+//     from StreamDownloadIndex::all().
+//
+// Closes the 2026-05-12 STREAM_DOWNLOADS_NETFLIX_OVERHAUL spec gap that the
+// original arc marked closed without shipping. Read-only display in v1;
+// inline cancel/pause/resume controls deferred to v1.x.
 
 #include <QFrame>
 #include <QHash>
-#include <QPixmap>
-#include <QPointer>
-#include <QSet>
 #include <QString>
-#include <optional>
 
-class QComboBox;
 class QLabel;
 class QPushButton;
-class QSplitter;
-class QTimer;
-class QTreeWidget;
-class QTreeWidgetItem;
-class QNetworkAccessManager;
+class QScrollArea;
+class QVBoxLayout;
+class QWidget;
 class QUrl;
+class QNetworkAccessManager;
 class TorrentClient;
 class StreamDownloadIndex;
-class DownloadDetailPane;
 
 namespace tankostream::stream { class MetaAggregator; }
-namespace tankostream::addon  { struct MetaItem; }
-namespace tankoban::queue     { class TransferQueue; }
+namespace tankostream::addon { struct MetaItem; }
 
 class StreamDownloadsPage : public QFrame
 {
@@ -44,111 +36,73 @@ public:
     explicit StreamDownloadsPage(QWidget* parent = nullptr);
     ~StreamDownloadsPage() override = default;
 
-    // Injection points — same-pointer guard + disconnect pattern mirrors
-    // StreamLibraryLayout / pre-T4 version. MainWindow wiring unchanged.
+    // Injection points. Mirror the TorrentClient + StreamDownloadIndex
+    // wire-up pattern from StreamLibraryLayout (see StreamPage.cpp's
+    // setStreamDownloadIndex call). Same-pointer guard prevents
+    // re-subscribing on repeat calls. Disconnects from the previous
+    // pointer (if any) before binding the new one.
     void setTorrentClient(TorrentClient* client);
     void setStreamDownloadIndex(StreamDownloadIndex* index);
+
+    // THEATRE/COMICS Downloads redesign 2026-05-29 — read-time metadata
+    // enrichment source. fetchMetaItem(imdbId,"series") yields both the poster
+    // and per-episode titles; reentrant + 60s-cached, so safe to drive here
+    // without disturbing the detail-view series fetch.
     void setMetaAggregator(tankostream::stream::MetaAggregator* agg);
 
 signals:
+    // Topbar back-button click - MainWindow's slot returns to the
+    // previously-active page (it tracks lastActivePage in activatePage).
     void backRequested();
+
+    // Clicking a History row emits this signal so MainWindow can route
+    // through its existing onPlayLocalFileFromStreamRequested slot
+    // (parameter parity with StreamDetailView::playLocalFileFromStreamRequested).
     void playLocalFileRequested(const QString& canonicalPath,
                                 const QString& imdbId,
                                 const QString& showTitle,
                                 int season,
                                 int episode);
-    // DOWNLOADS_OVERHAUL_V2 T6 — emitted by the retry intent handler after
-    // cleaning up the failed transfer. StreamPage re-runs the auto source pick.
-    void retryEpisodeRequested(const QString& imdbId, int season, int episode);
-
-protected:
-    void showEvent(QShowEvent* event) override;
-    void hideEvent(QHideEvent* event) override;
 
 private slots:
-    void rebuild();
+    void refreshActive();
+    void refreshHistory();
     void onMetaItemReady(const tankostream::addon::MetaItem& item);
 
 private:
-    // Build constants
-    static constexpr qint64 kCompletedTrimMs = 30LL * 24 * 60 * 60 * 1000;
-
     void buildUi();
-    void updateTotals();
-
-    // Tree helpers
-    QString currentSelectionKey() const;
-    void    restoreSelection(const QString& key);
-
-    // Row helpers
-    QString displayShowTitle(const QString& imdbId) const;
-    QString statusText(const tankostream::stream::DownloadRow& r) const;
-
-    // DOWNLOADS_OVERHAUL_V2 T6 — re-resolve the row's CURRENT state by key
-    // before acting on an intent — the pane's snapshot can be a debounce stale
-    // (T5 review I4). Returns nullopt when the episode no longer exists.
-    std::optional<tankostream::stream::DownloadRow>
-    freshRowFor(const tankostream::stream::DownloadRow& stale) const;
-
-    // Poster / meta enrichment (kept from pre-T4 — rewired to serve the tree)
+    void updateEmptyState();
+    QWidget* makePosterWidget(const QString& imdbId, const QString& title);
     void savePosterFrom(const QString& imdbId, const QUrl& posterUrl);
-    static QString posterCachePath(const QString& imdbId);
-
-    // Injection state
-    TorrentClient*                       m_client  = nullptr;
-    StreamDownloadIndex*                 m_index   = nullptr;
-    tankostream::stream::MetaAggregator* m_meta    = nullptr;
-    QNetworkAccessManager*               m_posterNam = nullptr;
-    // Tracks the currently connected TransferQueue so we can disconnect it on
-    // client re-set (the queue is a separate QObject from TorrentClient).
-    QPointer<tankoban::queue::TransferQueue> m_connectedQueue;
-
-    // Enrichment caches (title + poster) — keyed by imdbId
-    QHash<QString, QString>  m_titleCache;
-    QHash<QString, QPixmap>  m_posterCache;
-    // Guards against per-rebuild refetch of dead ids: negative results are not
-    // cached by MetaAggregator, so without this set every rebuild would re-fire
-    // fetchMetaItem for any id that never resolves.
-    QSet<QString>            m_metaRequested;
-
-    // Debounce timer — all signal triggers funnel through here
-    QTimer* m_rebuildDebounce = nullptr;
-
-    // Top-strip live-speed timer — runs only while page is visible (1 Hz).
-    // Kept separate from the rebuild debounce so speed updates don't cause full
-    // tree rebuilds; updateTotals() is cheap (no tree churn).
-    QTimer* m_totalsTimer = nullptr;
-
-    // "Clear Done" hidden-row keys — loaded from QSettings ("downloads/
-    // clearedDoneKeys", stored as QStringList). Key shape:
-    // "imdbId|season|episode|addedAt" (addedAt disambiguates re-downloads of
-    // the same episode). A click captures the keys of rows that are Completed
-    // RIGHT NOW; rows still in flight at click time keep their key out of the
-    // set and stay visible when they finish (T7 review I1 — the old addedAt
-    // watermark hid in-flight completions). Display-only: the
-    // StreamDownloadIndex is untouched.
-    QSet<QString> m_clearedDoneKeys;
-
-    // Topbar
-    QPushButton* m_backBtn    = nullptr;
-    QLabel*      m_titleLabel = nullptr;
-
-    // Top strip (Task 7 wires these; disabled for now)
-    QLabel*    m_totalsLabel   = nullptr;
-    QPushButton* m_pauseAllBtn  = nullptr;
-    QPushButton* m_resumeAllBtn = nullptr;
-    QPushButton* m_clearDoneBtn = nullptr;
-    QComboBox*   m_maxActiveCombo = nullptr;
-
-    // Master tree
-    QTreeWidget* m_tree = nullptr;
-
-    // Detail pane (Task 5 — replaces stub)
-    DownloadDetailPane* m_detailPane = nullptr;
-
-    // Splitter
-    QSplitter* m_splitter = nullptr;
 
-    // Selection state
-    std::optional<tankostream::stream::DownloadRow> m_selectedRow;
+    // Per-show widget handles, keyed by imdbId, so async metadata can repaint
+    // the right card after the synchronous (placeholder) render.
+    struct DownloadCardRefs {
+        QFrame*  card         = nullptr;
+        QWidget* posterWidget = nullptr;   // QLabel painting poster / placeholder
+        QLabel*  titleLabel   = nullptr;   // show title
+        // "<season>:<episode>" (or "movie") -> the clickable row button whose
+        // text is rebuilt to the real title on enrichment.
+        QHash<QString, QPushButton*> rowTitleByKey;
+    };
+
+    TorrentClient*       m_torrentClient = nullptr;
+    StreamDownloadIndex* m_streamDownloadIndex = nullptr;
+    tankostream::stream::MetaAggregator* m_metaAggregator = nullptr;
+    QNetworkAccessManager* m_posterNam = nullptr;
+    QHash<QString, DownloadCardRefs> m_historyCards;
+    QHash<QString, DownloadCardRefs> m_activeCards;
+
+    QPushButton*  m_backBtn        = nullptr;
+    QLabel*       m_titleLabel     = nullptr;
+    QScrollArea*  m_scroll         = nullptr;
+    QWidget*      m_scrollContent  = nullptr;
+    QVBoxLayout*  m_contentLayout  = nullptr;
+    QLabel*       m_activeHeader   = nullptr;
+    QWidget*      m_activeBody     = nullptr;
+    QVBoxLayout*  m_activeBodyLayout = nullptr;
+    QLabel*       m_historyHeader  = nullptr;
+    QWidget*      m_historyBody    = nullptr;
+    QVBoxLayout*  m_historyBodyLayout = nullptr;
+    QLabel*       m_emptyState     = nullptr;
 };
diff --git a/tests/core/stream/test_downloads_command_model.cpp b/tests/core/stream/test_downloads_command_model.cpp
deleted file mode 100644
index 57c8407..0000000
--- a/tests/core/stream/test_downloads_command_model.cpp
+++ /dev/null
@@ -1,197 +0,0 @@
-#include <gtest/gtest.h>
-#include "core/stream/DownloadsCommandModel.h"
-
-using namespace tankostream::stream;
-using tankoban::queue::TransferItem;
-using tankoban::queue::TransferLane;
-using tankoban::queue::TransferState;
-
-namespace {
-StreamDownloadIndex::Entry entry(const QString& imdb, int s, int e,
-                                 StreamDownloadIndex::Entry::State st, int pct,
-                                 qint64 addedAt = 1000) {
-    StreamDownloadIndex::Entry x;
-    x.imdbId = imdb; x.type = "series"; x.season = s; x.episode = e;
-    x.state = st; x.progressPct = pct; x.addedAt = addedAt;
-    x.canonicalPath = "C:/v/" + imdb + ".mkv";
-    return x;
-}
-TransferLane lane(const QString& imdb, TransferState headState, int s, int e,
-                  const QString& hash) {
-    TransferLane l; l.showId = "imdb:" + imdb;
-    TransferItem it; it.transferId = hash; it.showId = l.showId;
-    it.seasonNumber = s; it.episodeNumber = e; it.state = headState;
-    l.items.push_back(it);
-    return l;
-}
-}  // namespace
-
-TEST(DownloadsCommandModelTest, DownloadingEntryWithRunningLaneIsActive) {
-    DownloadsSnapshot snap;
-    snap.indexEntries = { entry("tt1", 1, 12, StreamDownloadIndex::Entry::Downloading, 62) };
-    snap.lanes.insert("imdb:tt1", lane("tt1", TransferState::Running, 1, 12, "h1"));
-    const auto rows = buildDownloadRows(snap, /*nowMs=*/0, /*trim=*/0);
-    ASSERT_EQ(rows.size(), 1);
-    EXPECT_EQ(rows[0].section, DownloadSection::Active);
-    EXPECT_EQ(rows[0].pct, 62);
-    EXPECT_EQ(rows[0].infoHash, "h1");
-}
-
-TEST(DownloadsCommandModelTest, FailedLaneItemIsFailedSection) {
-    DownloadsSnapshot snap;
-    snap.indexEntries = { entry("tt1", 1, 12, StreamDownloadIndex::Entry::Downloading, 30) };
-    snap.lanes.insert("imdb:tt1", lane("tt1", TransferState::Failed, 1, 12, "h1"));
-    const auto rows = buildDownloadRows(snap, 0, 0);
-    ASSERT_EQ(rows.size(), 1);
-    EXPECT_EQ(rows[0].section, DownloadSection::Failed);
-}
-
-TEST(DownloadsCommandModelTest, PendingWithQueuedLaneIsQueued) {
-    DownloadsSnapshot snap;
-    snap.indexEntries = { entry("tt1", 1, 13, StreamDownloadIndex::Entry::Pending, 0) };
-    snap.lanes.insert("imdb:tt1", lane("tt1", TransferState::Queued, 1, 13, "h2"));
-    const auto rows = buildDownloadRows(snap, 0, 0);
-    ASSERT_EQ(rows.size(), 1);
-    EXPECT_EQ(rows[0].section, DownloadSection::Queued);
-}
-
-TEST(DownloadsCommandModelTest, PausedLaneItemIsActivePaused) {
-    DownloadsSnapshot snap;
-    snap.indexEntries = { entry("tt1", 1, 12, StreamDownloadIndex::Entry::Downloading, 45) };
-    snap.lanes.insert("imdb:tt1", lane("tt1", TransferState::Paused, 1, 12, "h1"));
-    const auto rows = buildDownloadRows(snap, 0, 0);
-    ASSERT_EQ(rows.size(), 1);
-    EXPECT_EQ(rows[0].section, DownloadSection::Active);
-    EXPECT_TRUE(rows[0].paused);
-}
-
-TEST(DownloadsCommandModelTest, CompleteIsCompletedAndTrims) {
-    DownloadsSnapshot snap;
-    snap.indexEntries = {
-        entry("tt1", 1, 11, StreamDownloadIndex::Entry::Complete, 100, /*addedAt=*/1000),
-        entry("tt1", 1, 10, StreamDownloadIndex::Entry::Complete, 100, /*addedAt=*/100),
-    };
-    // now=1400, trim=500 -> age(ep10)=1400-100=1300 > 500 -> dropped;
-    // age(ep11)=1400-1000=400 <= 500 -> kept
-    const auto rows = buildDownloadRows(snap, 1400, 500);
-    ASSERT_EQ(rows.size(), 1);
-    EXPECT_EQ(rows[0].episode, 11);
-    EXPECT_EQ(rows[0].section, DownloadSection::Completed);
-}
-
-TEST(DownloadsCommandModelTest, SeasonPackLaneItemMatchesAnyEpisode) {
-    DownloadsSnapshot snap;
-    snap.indexEntries = {
-        entry("tt1", 1, 3, StreamDownloadIndex::Entry::Downloading, 20),
-        entry("tt1", 1, 4, StreamDownloadIndex::Entry::Downloading, 10),
-    };
-    TransferLane l; l.showId = "imdb:tt1";
-    TransferItem pack; pack.transferId = "packhash"; pack.showId = l.showId;
-    pack.seasonNumber = 1;   // no episodeNumber -> season pack
-    pack.state = TransferState::Running;
-    l.items.push_back(pack);
-    snap.lanes.insert("imdb:tt1", l);
-    const auto rows = buildDownloadRows(snap, 0, 0);
-    ASSERT_EQ(rows.size(), 2);
-    EXPECT_EQ(rows[0].infoHash, "packhash");
-    EXPECT_EQ(rows[1].infoHash, "packhash");
-    EXPECT_EQ(rows[0].section, DownloadSection::Active);
-}
-
-// Review C1 — failure normally arrives via the INDEX: TransferQueue erases
-// items on terminal states, so lanes never carry Failed in production.
-TEST(DownloadsCommandModelTest, IndexFailedWithNoLaneIsFailedSection) {
-    DownloadsSnapshot snap;
-    snap.indexEntries = { entry("tt1", 1, 12, StreamDownloadIndex::Entry::Failed, 30) };
-    const auto rows = buildDownloadRows(snap, 0, 0);
-    ASSERT_EQ(rows.size(), 1);
-    EXPECT_EQ(rows[0].section, DownloadSection::Failed);
-    EXPECT_TRUE(rows[0].infoHash.isEmpty());
-}
-
-// Review C1 ordering — a retry re-queues a lane item while the index still
-// says Failed; the lane state must win so the row shows Queued, not Failed.
-TEST(DownloadsCommandModelTest, IndexFailedWithQueuedLaneIsQueued) {
-    DownloadsSnapshot snap;
-    snap.indexEntries = { entry("tt1", 1, 12, StreamDownloadIndex::Entry::Failed, 30) };
-    snap.lanes.insert("imdb:tt1", lane("tt1", TransferState::Queued, 1, 12, "h9"));
-    const auto rows = buildDownloadRows(snap, 0, 0);
-    ASSERT_EQ(rows.size(), 1);
-    EXPECT_EQ(rows[0].section, DownloadSection::Queued);
-    EXPECT_EQ(rows[0].infoHash, "h9");
-}
-
-// Review I1 (plan-owner decision, pinned): index Downloading with NO lane item
-// is the app-restart shape — resumed torrents download with an empty queue.
-// The transfer genuinely runs in the engine (progress keeps flowing via
-// updateEpisodeProgress), so the row stays Active despite the empty infoHash.
-TEST(DownloadsCommandModelTest, DownloadingWithNoLaneIsActiveOrphan) {
-    DownloadsSnapshot snap;
-    snap.indexEntries = { entry("tt1", 1, 12, StreamDownloadIndex::Entry::Downloading, 40) };
-    const auto rows = buildDownloadRows(snap, 0, 0);
-    ASSERT_EQ(rows.size(), 1);
-    EXPECT_EQ(rows[0].section, DownloadSection::Active);
-    EXPECT_TRUE(rows[0].infoHash.isEmpty());
-}
-
-// Movie rows (season 0, episode 0) match a lane item with nullopt season/
-// episode through the same no-episodeNumber path season packs use.
-TEST(DownloadsCommandModelTest, MovieRowMatchesLaneItemWithoutSeasonEpisode) {
-    DownloadsSnapshot snap;
-    StreamDownloadIndex::Entry m = entry("tt1", 0, 0, StreamDownloadIndex::Entry::Downloading, 55);
-    m.type = "movie";
-    snap.indexEntries = { m };
-    TransferLane l; l.showId = "imdb:tt1";
-    TransferItem it; it.transferId = "mh1"; it.showId = l.showId;
-    it.state = TransferState::Running;   // no seasonNumber, no episodeNumber
-    l.items.push_back(it);
-    snap.lanes.insert("imdb:tt1", l);
-    const auto rows = buildDownloadRows(snap, 0, 0);
-    ASSERT_EQ(rows.size(), 1);
-    EXPECT_EQ(rows[0].infoHash, "mh1");
-    EXPECT_EQ(rows[0].section, DownloadSection::Active);
-}
-
-// Fallback pinned: Pending with no lane item at all (lane not visible yet,
-// e.g. enqueue raced the snapshot) -> Queued.
-TEST(DownloadsCommandModelTest, PendingWithNoLaneIsQueued) {
-    DownloadsSnapshot snap;
-    snap.indexEntries = { entry("tt1", 1, 13, StreamDownloadIndex::Entry::Pending, 0) };
-    const auto rows = buildDownloadRows(snap, 0, 0);
-    ASSERT_EQ(rows.size(), 1);
-    EXPECT_EQ(rows[0].section, DownloadSection::Queued);
-}
-
-// Review C2/I1 — rows carry the index entry's sourceGroupId so the page can
-// evict ghost rows and derive an engine hash when the lane item is gone.
-// infoHashFromGroup honors the "tankorent:<lowercase-infohash>" convention
-// stamped at the TorrentClient registration sites.
-TEST(DownloadsCommandModelTest, RowCarriesSourceGroupIdAndHashRoundTrips) {
-    DownloadsSnapshot snap;
-    auto e = entry("tt1", 1, 12, StreamDownloadIndex::Entry::Failed, 30);
-    e.sourceGroupId = "tankorent:abcdef0123456789abcdef0123456789abcdef01";
-    snap.indexEntries = { e };
-    const auto rows = buildDownloadRows(snap, 0, 0);
-    ASSERT_EQ(rows.size(), 1);
-    EXPECT_EQ(rows[0].sourceGroupId, e.sourceGroupId);
-    EXPECT_EQ(infoHashFromGroup(rows[0].sourceGroupId),
-              "abcdef0123456789abcdef0123456789abcdef01");
-    EXPECT_TRUE(infoHashFromGroup(QString()).isEmpty());
-    EXPECT_TRUE(infoHashFromGroup(QStringLiteral("getcomics:xyz")).isEmpty());
-}
-
-TEST(DownloadsCommandModelTest, SectionOrderThenShowSeasonEpisode) {
-    DownloadsSnapshot snap;
-    snap.indexEntries = {
-        entry("tt2", 1, 1, StreamDownloadIndex::Entry::Complete, 100),
-        entry("tt1", 1, 2, StreamDownloadIndex::Entry::Downloading, 10),
-        entry("tt1", 1, 1, StreamDownloadIndex::Entry::Downloading, 50),
-    };
-    snap.lanes.insert("imdb:tt1", lane("tt1", TransferState::Running, 1, 1, "h1"));
-    const auto rows = buildDownloadRows(snap, 0, 0);
-    ASSERT_EQ(rows.size(), 3);
-    EXPECT_EQ(rows[0].section, DownloadSection::Active);
-    EXPECT_EQ(rows[0].episode, 1);
-    EXPECT_EQ(rows[1].section, DownloadSection::Active);
-    EXPECT_EQ(rows[2].section, DownloadSection::Completed);
-}
