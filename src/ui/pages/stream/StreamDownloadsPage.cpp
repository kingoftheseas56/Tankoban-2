// DOWNLOADS_OVERHAUL_V2 Task 4 (2026-06-11) — Master-Detail shell.
// Task 5 (2026-06-11) — DownloadDetailPane replaces the right pane stub.
// Task 7 (2026-06-11) — Top strip wired: live totals, Pause/Resume All,
//   Clear Done, max-active knob.
// T7.1 (2026-06-11) — review fixes: promotion-free queue pauseAll() drives
//   Pause All (C1), Clear Done switched from an addedAt watermark to a
//   hidden-keys set (I1), Resume All is FIFO + single-mechanism, totals read
//   the engine snapshot directly.
// Replaces the old two-section scrollable card list with a QSplitter:
//   left  — QTreeWidget: Failed / Active / Queued / Completed sections,
//            shows grouped, episodes as leaves.
//   right — DownloadDetailPane: header + stats + controls + Tankorent tabs.
// All signal-triggered refreshes debounce through a 250ms single-shot timer
// so rapid lane-state updates don't hammer the tree.

#include "StreamDownloadsPage.h"
#include "DownloadDetailPane.h"

#include "core/torrent/TorrentClient.h"
#include "core/torrent/TorrentEngine.h"   // allStatuses() for updateTotals
#include "core/TorrentResult.h"   // humanSize()
#include "core/net/NetSeam.h"
#include "core/stream/StreamDownloadIndex.h"
#include "core/stream/MetaAggregator.h"
#include "core/stream/addon/MetaItem.h"
#include "core/queue/TransferQueue.h"

#include <QComboBox>
#include <QDateTime>
#include <QHeaderView>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QSettings>
#include <QVariant>

#include <algorithm>

// ──────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ──────────────────────────────────────────────────────────────────────────────
namespace {

// Section indices match DownloadSection enum order
static const char* kSectionNames[] = {"Failed", "Active", "Queued", "Completed"};

// Mirrors DownloadDetailPane::formatSpeed (local copy — not exported).
// Formats bytes/s as "1.2 MB/s", "348.0 KB/s", "0 B/s".
QString formatDownloadSpeed(qint64 bps)
{
    if (bps <= 0) return QStringLiteral("0 B/s");
    return humanSize(bps) + QStringLiteral("/s");
}

// Unique selection key for an episode leaf item: "<imdbId>|<season>|<episode>"
// Section is intentionally excluded so selection survives an item moving between
// sections (e.g. Queued→Active→Completed). For a movie (season==0, episode==0)
// we still use 0|0.
QString makeSelectionKey(const tankostream::stream::DownloadRow& r)
{
    return r.imdbId
           + QLatin1Char('|') + QString::number(r.season)
           + QLatin1Char('|') + QString::number(r.episode);
}

// "Clear Done" hidden-row key: "imdbId|season|episode|addedAt" (T7.1, review
// I1). addedAt disambiguates a re-download of the same episode — a fresh
// registration stamps a new addedAt, so clearing an old completed run can
// never hide the new one.
QString clearedDoneKey(const tankostream::stream::DownloadRow& r)
{
    return makeSelectionKey(r)
           + QLatin1Char('|') + QString::number(r.addedAt);
}

} // namespace

// ──────────────────────────────────────────────────────────────────────────────
// Static helpers
// ──────────────────────────────────────────────────────────────────────────────

// Mirrors the poster cache path used by StreamLibraryLayout, StreamDetailView,
// StreamSearchWidget, StreamContinueStrip — same directory, same ".jpg" extension
// so the shared cache is reused and GC by cleanupOrphanPosters works.
/*static*/ QString StreamDownloadsPage::posterCachePath(const QString& imdbId)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/Tankoban/data/stream_posters/") + imdbId
           + QStringLiteral(".jpg");
}

// ──────────────────────────────────────────────────────────────────────────────
// Constructor / buildUi
// ──────────────────────────────────────────────────────────────────────────────

StreamDownloadsPage::StreamDownloadsPage(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("StreamDownloadsPage"));

    // Load persistent "Clear Done" hidden-row keys (semantics in the header).
    // The pre-T7.1 "downloads/clearDoneBeforeMs" watermark settings key is
    // left orphaned — harmless, display-only state needs no migration.
    {
        const QStringList stored = QSettings()
            .value(QStringLiteral("downloads/clearedDoneKeys"))
            .toStringList();
        m_clearedDoneKeys = QSet<QString>(stored.cbegin(), stored.cend());
    }

    // 250ms debounce timer — all signal triggers start() this; rebuild() fires
    // once per burst.
    m_rebuildDebounce = new QTimer(this);
    m_rebuildDebounce->setSingleShot(true);
    m_rebuildDebounce->setInterval(250);
    connect(m_rebuildDebounce, &QTimer::timeout, this, &StreamDownloadsPage::rebuild);

    // 1 Hz live-speed timer — started/stopped in showEvent/hideEvent so it
    // only runs while the page is actually visible. Updates totals label only
    // (no tree rebuild).
    m_totalsTimer = new QTimer(this);
    m_totalsTimer->setInterval(1000);
    connect(m_totalsTimer, &QTimer::timeout,
            this, &StreamDownloadsPage::updateTotals);

    buildUi();
}

void StreamDownloadsPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Topbar ──────────────────────────────────────────────────────────────
    auto* topbar = new QFrame(this);
    topbar->setObjectName(QStringLiteral("StreamDownloadsTopbar"));
    topbar->setFixedHeight(48);
    auto* topbarLayout = new QHBoxLayout(topbar);
    topbarLayout->setContentsMargins(14, 6, 14, 6);
    topbarLayout->setSpacing(10);

    m_backBtn = new QPushButton(tr("< Back"), topbar);
    m_backBtn->setObjectName(QStringLiteral("StreamDownloadsBackBtn"));
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setFixedHeight(28);
    connect(m_backBtn, &QPushButton::clicked, this, &StreamDownloadsPage::backRequested);

    m_titleLabel = new QLabel(tr("Downloads"), topbar);
    m_titleLabel->setObjectName(QStringLiteral("StreamDownloadsTitle"));
    m_titleLabel->setStyleSheet(
        QStringLiteral("QLabel#StreamDownloadsTitle { font-size: 16pt;"
                       " font-weight: 600; color: #eeeeee; }"));

    topbarLayout->addWidget(m_backBtn, 0);
    topbarLayout->addWidget(m_titleLabel, 0);
    topbarLayout->addStretch(1);

    root->addWidget(topbar, 0);

    // ── Top strip (command buttons) ─────────────────────────────────────────
    auto* stripWidget = new QWidget(this);
    stripWidget->setObjectName(QStringLiteral("StreamDownloadsStrip"));
    stripWidget->setFixedHeight(40);
    auto* strip = new QHBoxLayout(stripWidget);
    strip->setContentsMargins(14, 6, 14, 6);
    strip->setSpacing(8);

    m_totalsLabel = new QLabel(stripWidget);
    m_totalsLabel->setObjectName(QStringLiteral("StreamDownloadsTotals"));
    m_totalsLabel->setStyleSheet(
        QStringLiteral("color: rgba(255,255,255,0.55); font-size: 12px;"));
    strip->addWidget(m_totalsLabel, 1);

    // Buttons — disabled until setTorrentClient() injects a live client.
    m_pauseAllBtn = new QPushButton(tr("Pause All"), stripWidget);
    m_pauseAllBtn->setEnabled(false);
    connect(m_pauseAllBtn, &QPushButton::clicked, this, [this]() {
        if (!m_client) return;
        auto* q = m_client->transferQueue();
        if (!q) return;
        // T7 review C1 — pauseAll() flips every Running head in one pass with
        // NO promotion (per-lane pauseCurrent promotes a waiter into each
        // freed slot, which the Running-replay then STARTS — "Pause All" must
        // not start new downloads). Queue-first ordering is SAFE here
        // precisely because pauseAll cannot promote/start anything; we then
        // engine-pause the flipped transfers.
        const QStringList paused = q->pauseAll();
        for (const QString& id : paused)
            m_client->pauseTorrent(id);
    });
    strip->addWidget(m_pauseAllBtn, 0);

    m_resumeAllBtn = new QPushButton(tr("Resume All"), stripWidget);
    m_resumeAllBtn->setEnabled(false);
    connect(m_resumeAllBtn, &QPushButton::clicked, this, [this]() {
        if (!m_client) return;
        auto* q = m_client->transferQueue();
        if (!q) return;
        // Snapshot once — promotions during the loop are fine here: cap gates
        // how many actually flip to Running; gated ones stay Queued and
        // auto-promote later via the C1 fall-through in TorrentClient.
        // T7.1: resume in head-enqueueSeq order (QHash iteration order is
        // arbitrary) so the cap's slots go to the oldest-enqueued paused heads
        // first — deterministic FIFO. No page-side engine-resume: single
        // mechanism — TorrentClient's Running-replay fall-through (T6.1 C1
        // fix) already engine-resumes every promoted head synchronously on
        // itemStateChanged(Running).
        const auto lanes = q->lanesSnapshot();
        QList<tankoban::queue::TransferLane> pausedLanes;
        for (auto it = lanes.cbegin(); it != lanes.cend(); ++it) {
            const auto& lane = it.value();
            if (lane.items.empty()) continue;
            if (lane.items.front().state == tankoban::queue::TransferState::Paused)
                pausedLanes.append(lane);
        }
        std::sort(pausedLanes.begin(), pausedLanes.end(),
                  [](const tankoban::queue::TransferLane& a,
                     const tankoban::queue::TransferLane& b) {
                      return a.items.front().enqueueSeq < b.items.front().enqueueSeq;
                  });
        for (const auto& lane : pausedLanes)
            q->resumeCurrent(lane.showId);
    });
    strip->addWidget(m_resumeAllBtn, 0);

    m_clearDoneBtn = new QPushButton(tr("Clear Done"), stripWidget);
    m_clearDoneBtn->setEnabled(false);
    connect(m_clearDoneBtn, &QPushButton::clicked, this, [this]() {
        if (!m_index) return;
        // T7 review I1 — "clear what I see now": key every row that is
        // Completed RIGHT NOW. A download still in flight at click time is
        // not keyed, so it stays visible when it completes (the old addedAt
        // watermark hid it — addedAt is the registration time, which predates
        // the click for in-flight transfers).
        //
        // Fresh buildDownloadRows snapshot, not the last rendered rows: the
        // tree can be up to ~250ms debounce-stale and clearing should act on
        // current model truth. maxCompletedAgeMs=0 so >30d age-trimmed
        // Completed rows are keyed too (they're hidden by the age trim
        // anyway; keying them is harmless) and so liveKeys spans every key
        // the index can still produce.
        tankostream::stream::DownloadsSnapshot snap;
        snap.indexEntries = m_index->all();
        if (m_client && m_client->transferQueue())
            snap.lanes = m_client->transferQueue()->lanesSnapshot();
        const auto rows = tankostream::stream::buildDownloadRows(
            snap, QDateTime::currentMSecsSinceEpoch(), /*maxCompletedAgeMs=*/0);
        QSet<QString> liveKeys;
        for (const auto& r : rows) {
            const QString key = clearedDoneKey(r);
            liveKeys.insert(key);
            if (r.section == tankostream::stream::DownloadSection::Completed)
                m_clearedDoneKeys.insert(key);
        }
        // Prune keys whose rows left the index entirely (cancel / evict /
        // re-download) so the persisted set can't grow unbounded.
        m_clearedDoneKeys.intersect(liveKeys);
        QSettings().setValue(
            QStringLiteral("downloads/clearedDoneKeys"),
            QVariant(QStringList(m_clearedDoneKeys.cbegin(),
                                 m_clearedDoneKeys.cend())));
        rebuild();
    });
    strip->addWidget(m_clearDoneBtn, 0);

    // Max-active label + combo
    auto* maxActiveLabel = new QLabel(tr("Max active:"), stripWidget);
    maxActiveLabel->setStyleSheet(
        QStringLiteral("color: rgba(255,255,255,0.55); font-size: 12px;"));
    strip->addWidget(maxActiveLabel, 0);

    m_maxActiveCombo = new QComboBox(stripWidget);
    // Items: display text with integral data (0 = unlimited).
    m_maxActiveCombo->addItem(tr("1"),         QVariant(1));
    m_maxActiveCombo->addItem(tr("2"),         QVariant(2));
    m_maxActiveCombo->addItem(tr("3"),         QVariant(3));
    m_maxActiveCombo->addItem(tr("5"),         QVariant(5));
    m_maxActiveCombo->addItem(tr("Unlimited"), QVariant(0));
    // Restore from settings (default 3).
    {
        const int saved = QSettings()
            .value(QStringLiteral("downloads/maxActive"), 3).toInt();
        const int idx = m_maxActiveCombo->findData(QVariant(saved));
        m_maxActiveCombo->setCurrentIndex(idx >= 0 ? idx : 2);  // fallback: "3"
    }
    m_maxActiveCombo->setEnabled(false);
    connect(m_maxActiveCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        const int v = m_maxActiveCombo->itemData(idx).toInt();
        QSettings().setValue(QStringLiteral("downloads/maxActive"), v);
        if (m_client && m_client->transferQueue())
            m_client->transferQueue()->setMaxActive(v);
    });
    strip->addWidget(m_maxActiveCombo, 0);

    root->addWidget(stripWidget, 0);

    // ── Main splitter ───────────────────────────────────────────────────────
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("StreamDownloadsSplitter"));
    m_splitter->setHandleWidth(1);
    m_splitter->setStyleSheet(
        QStringLiteral("QSplitter::handle { background: rgba(255,255,255,0.10); }"));

    // Left pane — master tree
    m_tree = new QTreeWidget(m_splitter);
    m_tree->setObjectName(QStringLiteral("StreamDownloadsTree"));
    m_tree->setColumnCount(3);
    m_tree->header()->hide();
    m_tree->setColumnWidth(0, 240);
    m_tree->setColumnWidth(1, 60);
    m_tree->setColumnWidth(2, 70);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(false);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setStyleSheet(
        QStringLiteral(
            "QTreeWidget#StreamDownloadsTree {"
            "  background: transparent;"
            "  color: #dddddd;"
            "  font-size: 13px;"
            "  border: none;"
            "}"
            "QTreeWidget#StreamDownloadsTree::item {"
            "  padding: 4px 2px;"
            "}"
            "QTreeWidget#StreamDownloadsTree::item:selected {"
            "  background: rgba(255,255,255,0.12);"
            "  color: #eeeeee;"
            "}"
            "QTreeWidget#StreamDownloadsTree::item:hover:!selected {"
            "  background: rgba(255,255,255,0.06);"
            "}"));

    // Right pane — DownloadDetailPane (Task 5)
    m_detailPane = new DownloadDetailPane(m_splitter);
    m_detailPane->setObjectName(QStringLiteral("StreamDownloadsDetailPane"));

    // ── Intent signals wired in T6 ───────────────────────────────────────────
    // Each handler re-resolves the current row state before acting (T5 review I4:
    // the pane's snapshot can be up to ~250ms stale). Guards log + no-op when
    // the row has moved to a section that makes the intent invalid.
    connect(m_detailPane, &DownloadDetailPane::pauseRequested, this,
            [this](const tankostream::stream::DownloadRow& row) {
                const auto fresh = freshRowFor(row);
                if (!fresh
                    || fresh->section != tankostream::stream::DownloadSection::Active
                    || fresh->paused
                    || !m_client) {
                    qInfo() << "[downloads] pause intent dropped (stale row)";
                    return;
                }
                // T6 review I3: orphan-Active rows (no lane item, so empty
                // infoHash) must not pause an unrelated lane head of the same
                // show — skip pauseCurrent entirely, and when we DO have a
                // hash, only pause the lane when its head IS this transfer.
                if (fresh->infoHash.isEmpty()) {
                    qInfo() << "[downloads] pause intent dropped (orphan row, no lane transfer)";
                    return;
                }
                m_client->pauseTorrent(fresh->infoHash);
                if (auto* q = m_client->transferQueue()) {
                    const QString showId = QStringLiteral("imdb:") + fresh->imdbId;
                    const auto lane = q->laneFor(showId);
                    if (lane && !lane->items.empty()
                        && lane->items.front().transferId == fresh->infoHash)
                        q->pauseCurrent(showId);
                }
            });

    connect(m_detailPane, &DownloadDetailPane::resumeRequested, this,
            [this](const tankostream::stream::DownloadRow& row) {
                const auto fresh = freshRowFor(row);
                if (!fresh || !fresh->paused || !m_client) {
                    qInfo() << "[downloads] resume intent dropped (stale row)";
                    return;
                }
                // Queue decides if a slot is free; engine-resume only when
                // promoted. When gated, the head goes Queued and auto-promotes
                // later — engine-resume happens here or not at all this click.
                auto* q = m_client->transferQueue();
                if (q) {
                    if (q->resumeCurrent(QStringLiteral("imdb:") + fresh->imdbId).has_value()
                        && !fresh->infoHash.isEmpty())
                        m_client->resumeTorrent(fresh->infoHash);
                } else if (!fresh->infoHash.isEmpty()) {
                    m_client->resumeTorrent(fresh->infoHash);
                }
            });

    connect(m_detailPane, &DownloadDetailPane::cancelRequested, this,
            [this](const tankostream::stream::DownloadRow& row) {
                const auto fresh = freshRowFor(row);
                // Guard: never delete a Completed torrent (I4 critical case).
                if (!fresh
                    || fresh->section == tankostream::stream::DownloadSection::Completed
                    || !m_client) {
                    qInfo() << "[downloads] cancel intent dropped (stale/completed row)";
                    return;
                }
                // T11.1 review I3: when the row's carrying lane item is a
                // season PACK (per-episode row, lane item with no episode
                // number), this row is ONE episode of a shared transfer —
                // queue-cancel/deleteTorrent here would silently kill every
                // sibling episode. Pack-level control belongs to a future
                // batch affordance; per-episode intents must never destroy
                // the shared transfer. Cancel = evict only THIS episode's
                // index entry (episode-level evict: evictByPath on the
                // entry's canonical key). The episode>0 guard excludes movie
                // rows, whose lane items also lack an episodeNumber.
                {
                    QHash<QString, tankoban::queue::TransferLane> lanes;
                    if (auto* q = m_client->transferQueue())
                        lanes = q->lanesSnapshot();
                    const auto* li = tankostream::stream::laneItemFor(
                        lanes, fresh->imdbId, fresh->season, fresh->episode);
                    if (li && !li->episodeNumber.has_value() && fresh->episode > 0) {
                        if (m_index && !fresh->canonicalPath.isEmpty())
                            m_index->evictByPath(
                                StreamDownloadIndex::computeCanonicalKey(fresh->canonicalPath));
                        else
                            qInfo() << "[downloads] pack-child cancel: no"
                                       " canonical path to evict — no-op";
                        return;
                    }
                }
                // T6 review C2: rows with no lane item (orphan resume, index
                // Failed, etc.) carry an empty infoHash — derive the engine
                // hash from the index group ("tankorent:<infohash>") so cancel
                // still reaches queue + engine.
                const QString hash = !fresh->infoHash.isEmpty()
                    ? fresh->infoHash
                    : tankostream::stream::infoHashFromGroup(fresh->sourceGroupId);
                if (auto* q = m_client->transferQueue(); q && !hash.isEmpty())
                    q->cancel(hash);
                // deleteFiles=true: cancel removes partial staging files.
                // Completed rows never reach here (guard above), so finished
                // media is safe.
                if (!hash.isEmpty())
                    m_client->deleteTorrent(hash, /*deleteFiles=*/true);
                // ALWAYS evict the index entries, even when no engine transfer
                // could be resolved — otherwise the cancelled episode lingers
                // as a ghost row forever (T6 review C2). Files are untouched.
                if (m_index && !fresh->sourceGroupId.isEmpty())
                    m_index->evictBySourceGroup(fresh->sourceGroupId);
            });

    connect(m_detailPane, &DownloadDetailPane::bumpRequested, this,
            [this](const tankostream::stream::DownloadRow& row) {
                const auto fresh = freshRowFor(row);
                if (!fresh
                    || fresh->section != tankostream::stream::DownloadSection::Queued
                    || !m_client
                    || fresh->infoHash.isEmpty())
                    return;
                if (auto* q = m_client->transferQueue())
                    q->bumpToFront(fresh->infoHash);
            });

    connect(m_detailPane, &DownloadDetailPane::playRequested, this,
            [this](const tankostream::stream::DownloadRow& row) {
                const auto fresh = freshRowFor(row);
                if (!fresh || fresh->canonicalPath.isEmpty()) return;
                QString title = m_titleCache.value(fresh->imdbId);
                if (title.isEmpty())
                    title = QFileInfo(fresh->canonicalPath).completeBaseName();
                emit playLocalFileRequested(
                    fresh->canonicalPath,
                    fresh->imdbId,
                    title,
                    fresh->season,
                    fresh->episode);
            });

    connect(m_detailPane, &DownloadDetailPane::retryRequested, this,
            [this](const tankostream::stream::DownloadRow& row) {
                const auto fresh = freshRowFor(row);
                if (!fresh
                    || fresh->section != tankostream::stream::DownloadSection::Failed) {
                    qInfo() << "[downloads] retry intent dropped (stale row)";
                    return;
                }
                // Clean up the failed transfer first: remove from queue + engine
                // (delete partial files). Failed rows usually have no lane item
                // (queues erase on terminal states), so derive the engine hash
                // from the index group when needed.
                // T11.1 review I3: when the row's carrying lane item is a season
                // PACK (lane item with no episodeNumber; episode>0 excludes
                // movies), skip engine/queue cleanup entirely — the pack may be
                // healthy and shared with sibling episodes. Pack-level control
                // belongs to a future batch affordance; per-episode intents must
                // never destroy the shared transfer.
                if (m_client) {
                    QHash<QString, tankoban::queue::TransferLane> lanes;
                    if (auto* q = m_client->transferQueue())
                        lanes = q->lanesSnapshot();
                    const auto* li = tankostream::stream::laneItemFor(
                        lanes, fresh->imdbId, fresh->season, fresh->episode);
                    const bool packChild =
                        li && !li->episodeNumber.has_value() && fresh->episode > 0;
                    if (!packChild) {
                        // Index-based multi-episode pack guard: a failed pack's
                        // lane item is already erased (finishCurrent(Failed)
                        // removes terminal lane items), so the lane-based guard
                        // above can't fire. The index is the durable truth —
                        // count sibling entries sharing the same sourceGroupId.
                        // If MORE THAN ONE entry shares the group the transfer
                        // was carrying multiple episodes; deleting the pack hash
                        // would destroy finished-but-unmoved siblings. Skip
                        // engine/queue cleanup entirely and let the retry
                        // re-dispatch handle it. Single-entry groups (normal
                        // per-episode transfers) proceed as before.
                        // (integration + security review convergence, T11.2)
                        bool sharedPackTransfer = false;
                        if (m_index && !fresh->sourceGroupId.isEmpty()) {
                            const auto allEntries = m_index->all();
                            int groupCount = 0;
                            for (const auto& e : allEntries) {
                                if (e.sourceGroupId == fresh->sourceGroupId)
                                    ++groupCount;
                            }
                            sharedPackTransfer = groupCount > 1;
                        }
                        if (sharedPackTransfer) {
                            qInfo() << "[downloads] retry: shared pack transfer,"
                                       " skipping engine cleanup"
                                    << "group=" << fresh->sourceGroupId;
                        } else {
                            const QString hash = !fresh->infoHash.isEmpty()
                                ? fresh->infoHash
                                : tankostream::stream::infoHashFromGroup(fresh->sourceGroupId);
                            if (!hash.isEmpty()) {
                                if (auto* q = m_client->transferQueue())
                                    q->cancel(hash);
                                m_client->deleteTorrent(hash, /*deleteFiles=*/true);
                            }
                        }
                    }
                }
                // T11.1 review I4 (spec §6): do NOT evict the Failed index
                // entries on retry — if the re-pick finds no sources, the entry
                // stays Failed and the row stays in the Failed section instead
                // of vanishing. On success the re-dispatch overwrites the entry
                // via the same canonical path (registerPendingEpisode). Trade-
                // off: a different-source re-pick may create a second entry at
                // a new path; bestEntryForEpisode + duplicate handling exist;
                // accepted v1.
                emit retryEpisodeRequested(fresh->imdbId, fresh->season, fresh->episode);
            });

    m_splitter->addWidget(m_tree);
    m_splitter->addWidget(m_detailPane);
    m_splitter->setStretchFactor(0, 2);
    m_splitter->setStretchFactor(1, 3);

    root->addWidget(m_splitter, 1);

    // ── Tree selection → detail pane ────────────────────────────────────────
    connect(m_tree, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem* current, QTreeWidgetItem* /*previous*/) {
        if (!current) {
            m_selectedRow.reset();
            m_detailPane->clearRow();
            return;
        }
        const QVariant v = current->data(0, Qt::UserRole);
        if (!v.isValid() || !v.canConvert<tankostream::stream::DownloadRow>()) {
            m_selectedRow.reset();
            m_detailPane->clearRow();
            return;
        }
        const auto r = v.value<tankostream::stream::DownloadRow>();
        m_selectedRow = r;
        m_detailPane->setRow(r, displayShowTitle(r.imdbId));
    });

    // ── Double-click: play Completed episodes ───────────────────────────────
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int /*column*/) {
        if (!item) return;
        const QVariant v = item->data(0, Qt::UserRole);
        if (!v.isValid() || !v.canConvert<tankostream::stream::DownloadRow>()) return;
        const auto r = v.value<tankostream::stream::DownloadRow>();
        if (r.section != tankostream::stream::DownloadSection::Completed) return;
        if (r.canonicalPath.isEmpty()) return;
        // Prefer enriched catalog title; fall back to filename (preserves pre-T4
        // derivation — never shows a raw imdbId in the player HUD).
        QString title = m_titleCache.value(r.imdbId);
        if (title.isEmpty())
            title = QFileInfo(r.canonicalPath).completeBaseName();
        emit playLocalFileRequested(
            r.canonicalPath,
            r.imdbId,
            title,
            r.season,
            r.episode);
    });
}

// ──────────────────────────────────────────────────────────────────────────────
// showEvent — refresh on navigation
// ──────────────────────────────────────────────────────────────────────────────

void StreamDownloadsPage::showEvent(QShowEvent* event)
{
    QFrame::showEvent(event);
    // Cheap insurance: navigating to this page always shows current state, never
    // a stale tree left over from when the page was last visible.
    m_rebuildDebounce->start();
    // Start live-speed ticker only while visible to avoid unnecessary work.
    m_totalsTimer->start();
}

void StreamDownloadsPage::hideEvent(QHideEvent* event)
{
    QFrame::hideEvent(event);
    m_totalsTimer->stop();
}

// ──────────────────────────────────────────────────────────────────────────────
// Injection setters
// ──────────────────────────────────────────────────────────────────────────────

void StreamDownloadsPage::setTorrentClient(TorrentClient* client)
{
    if (m_client == client)
        return;
    if (m_client)
        disconnect(m_client, nullptr, this, nullptr);
    m_client = client;
    // Disconnect the old TransferQueue's signals before wiring the new client.
    // The queue is a separate QObject from TorrentClient so the client disconnect
    // above does not cover it; m_connectedQueue tracks it explicitly.
    if (m_connectedQueue)
        disconnect(m_connectedQueue, nullptr, this, nullptr);
    m_connectedQueue = nullptr;

    if (m_client) {
        // Legacy signal still in TorrentClient — keep it for coverage.
        connect(m_client, &TorrentClient::streamBulkGroupsChanged,
                this, [this](const QString&) { m_rebuildDebounce->start(); },
                Qt::QueuedConnection);

        // TransferQueue signals (new for T4).
        if (auto* tq = m_client->transferQueue()) {
            connect(tq, &tankoban::queue::TransferQueue::laneChanged,
                    this, [this](const QString&) { m_rebuildDebounce->start(); },
                    Qt::QueuedConnection);
            connect(tq, &tankoban::queue::TransferQueue::itemStateChanged,
                    this, [this](const QString&, tankoban::queue::TransferState) {
                        m_rebuildDebounce->start();
                    },
                    Qt::QueuedConnection);
            m_connectedQueue = tq;
        }
    }

    // Forward client to the detail pane so it can construct tabs lazily.
    if (m_detailPane)
        m_detailPane->setClient(m_client);

    // Enable strip controls only once we have a live client — buttons are
    // created disabled and stay that way until injection (T7).
    const bool hasClient = (m_client != nullptr);
    m_pauseAllBtn->setEnabled(hasClient);
    m_resumeAllBtn->setEnabled(hasClient);
    m_clearDoneBtn->setEnabled(hasClient);
    m_maxActiveCombo->setEnabled(hasClient);

    m_rebuildDebounce->start();
}

void StreamDownloadsPage::setStreamDownloadIndex(StreamDownloadIndex* index)
{
    if (m_index == index)
        return;
    if (m_index)
        disconnect(m_index, nullptr, this, nullptr);
    m_index = index;
    if (m_index) {
        connect(m_index, &StreamDownloadIndex::entriesChanged,
                this, [this]() { m_rebuildDebounce->start(); },
                Qt::QueuedConnection);
        // entryStateChanged carries per-piece progress; updateEpisodeProgress
        // deliberately does NOT emit entriesChanged, so without this connect
        // the Active section's pct column freezes mid-download.
        connect(m_index, &StreamDownloadIndex::entryStateChanged,
                this, [this](const QString&) { m_rebuildDebounce->start(); },
                Qt::QueuedConnection);
    }
    m_rebuildDebounce->start();
}

void StreamDownloadsPage::setMetaAggregator(tankostream::stream::MetaAggregator* agg)
{
    if (m_meta == agg)
        return;
    if (m_meta)
        disconnect(m_meta, nullptr, this, nullptr);
    m_meta = agg;
    if (m_meta) {
        connect(m_meta,
                &tankostream::stream::MetaAggregator::metaItemReady,
                this, &StreamDownloadsPage::onMetaItemReady,
                Qt::UniqueConnection);
    }
    m_rebuildDebounce->start();
}

// ──────────────────────────────────────────────────────────────────────────────
// Core rebuild
// ──────────────────────────────────────────────────────────────────────────────

void StreamDownloadsPage::rebuild()
{
    if (!m_index) return;

    tankostream::stream::DownloadsSnapshot snap;
    snap.indexEntries = m_index->all();
    if (m_client && m_client->transferQueue())
        snap.lanes = m_client->transferQueue()->lanesSnapshot();

    auto rows = tankostream::stream::buildDownloadRows(
        snap, QDateTime::currentMSecsSinceEpoch(), kCompletedTrimMs);

    // DOWNLOADS_OVERHAUL_V2 T7.1 — Clear Done filter: hide Completed rows the
    // user explicitly cleared (keys captured at click time). A download that
    // was still in flight at click time was never keyed — same addedAt, but
    // not Completed then — so it surfaces normally when it finishes (review
    // I1). Display-only; index is untouched.
    if (!m_clearedDoneKeys.isEmpty()) {
        rows.erase(
            std::remove_if(rows.begin(), rows.end(),
                [this](const tankostream::stream::DownloadRow& r) {
                    return r.section == tankostream::stream::DownloadSection::Completed
                           && m_clearedDoneKeys.contains(clearedDoneKey(r));
                }),
            rows.end());
    }

    const QString selectedKey = currentSelectionKey();
    m_tree->clear();

    QTreeWidgetItem* sectionItems[4] = {};
    QHash<QString, QTreeWidgetItem*> showNodes;  // key: "<sectionIdx>|<imdbId>"

    for (const auto& r : rows) {
        const int s = int(r.section);

        // Section header node
        if (!sectionItems[s]) {
            sectionItems[s] = new QTreeWidgetItem(m_tree,
                {QString::fromLatin1(kSectionNames[s]), QString(), QString()});
            sectionItems[s]->setExpanded(true);
            sectionItems[s]->setFlags(Qt::ItemIsEnabled);
            QFont f = sectionItems[s]->font(0);
            f.setPointSize(9);
            f.setBold(true);
            sectionItems[s]->setFont(0, f);
            sectionItems[s]->setForeground(
                0, QBrush(QColor(255, 255, 255, 140)));
        }

        // Show grouping node
        const QString showKey = QString::number(s) + QLatin1Char('|') + r.imdbId;
        QTreeWidgetItem*& showNode = showNodes[showKey];
        if (!showNode) {
            showNode = new QTreeWidgetItem(sectionItems[s],
                {displayShowTitle(r.imdbId), QString(), QString()});
            showNode->setExpanded(true);
            showNode->setFlags(Qt::ItemIsEnabled);

            // Poster icon (from cache if available — no new network machinery)
            const auto pit = m_posterCache.constFind(r.imdbId);
            if (pit != m_posterCache.constEnd() && !pit->isNull())
                showNode->setIcon(0, QIcon(*pit));
        }

        // Episode leaf
        auto* item = new QTreeWidgetItem(showNode);
        item->setText(0, r.type == QLatin1String("movie")
            ? tr("Movie")
            : QStringLiteral("S%1E%2")
                  .arg(r.season, 2, 10, QLatin1Char('0'))
                  .arg(r.episode, 2, 10, QLatin1Char('0')));
        item->setText(1,
            r.section == tankostream::stream::DownloadSection::Completed
                ? QString()
                : QStringLiteral("%1%").arg(r.pct));
        item->setText(2, statusText(r));
        item->setData(0, Qt::UserRole, QVariant::fromValue(r));

        if (r.section == tankostream::stream::DownloadSection::Failed)
            item->setForeground(0, QBrush(QColor(0xf3, 0xa6, 0xa6)));

        // Kick off enrichment fetch for any new imdbId we haven't seen yet.
        // m_metaRequested guards against a per-rebuild storm: negative results
        // aren't cached by MetaAggregator, so a failing id would be refetched
        // on every rebuild without this session-scoped set.
        if (!m_titleCache.contains(r.imdbId) && m_meta
                && !m_metaRequested.contains(r.imdbId)) {
            m_metaRequested.insert(r.imdbId);
            const QString type = (r.type == QLatin1String("movie"))
                ? QStringLiteral("movie")
                : QStringLiteral("series");
            m_meta->fetchMetaItem(r.imdbId, type);
        }
    }

    restoreSelection(selectedKey);
    updateTotals();
}

// ──────────────────────────────────────────────────────────────────────────────
// Selection helpers
// ──────────────────────────────────────────────────────────────────────────────

QString StreamDownloadsPage::currentSelectionKey() const
{
    if (!m_selectedRow.has_value()) return {};
    return makeSelectionKey(*m_selectedRow);
}

void StreamDownloadsPage::restoreSelection(const QString& key)
{
    if (key.isEmpty()) return;
    // Walk every leaf item in the tree looking for a matching key.
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        const QVariant v = (*it)->data(0, Qt::UserRole);
        if (v.isValid() && v.canConvert<tankostream::stream::DownloadRow>()) {
            const auto r = v.value<tankostream::stream::DownloadRow>();
            if (makeSelectionKey(r) == key) {
                m_tree->setCurrentItem(*it);
                return;
            }
        }
        ++it;
    }
    // Key gone (item removed) — clear selection state
    m_selectedRow.reset();
    m_detailPane->clearRow();
}

// ──────────────────────────────────────────────────────────────────────────────
// Row text helpers
// ──────────────────────────────────────────────────────────────────────────────

QString StreamDownloadsPage::statusText(const tankostream::stream::DownloadRow& r) const
{
    using tankostream::stream::DownloadSection;
    switch (r.section) {
    case DownloadSection::Failed:    return QStringLiteral("failed");
    case DownloadSection::Active:    return r.paused ? QStringLiteral("paused") : QString();
    case DownloadSection::Queued:    return QStringLiteral("queued");
    case DownloadSection::Completed: return QStringLiteral("done");
    }
    return {};
}

QString StreamDownloadsPage::displayShowTitle(const QString& imdbId) const
{
    const auto it = m_titleCache.constFind(imdbId);
    if (it != m_titleCache.constEnd() && !it->isEmpty())
        return *it;
    return imdbId;
}

// ──────────────────────────────────────────────────────────────────────────────
// Intent helper — DOWNLOADS_OVERHAUL_V2 T6
// ──────────────────────────────────────────────────────────────────────────────

// Re-resolve the row's CURRENT state by key before acting on an intent —
// the pane's snapshot can be a debounce stale (~250ms+, T5 review I4).
// Returns nullopt when the episode no longer exists in the model (e.g. was
// removed between the button press and the signal delivery).
// trim=0 deliberately: an intent against a just-trimmed Completed row should
// still resolve so the guard can fire; trimming only happens in the UI tree.
std::optional<tankostream::stream::DownloadRow>
StreamDownloadsPage::freshRowFor(const tankostream::stream::DownloadRow& stale) const
{
    if (!m_index) return std::nullopt;
    tankostream::stream::DownloadsSnapshot snap;
    snap.indexEntries = m_index->all();
    if (m_client && m_client->transferQueue())
        snap.lanes = m_client->transferQueue()->lanesSnapshot();
    const auto rows = tankostream::stream::buildDownloadRows(
        snap, QDateTime::currentMSecsSinceEpoch(), /*maxCompletedAgeMs=*/0);
    // Two-pass match (T6 review I2): duplicate index entries for one episode
    // are documented reality (e.g. an old Failed entry alongside a freshly
    // re-registered Pending one). Prefer the row in the section the user acted
    // on; only if none exists fall back to the first (imdbId,season,episode)
    // match.
    for (const auto& r : rows) {
        if (r.imdbId == stale.imdbId
            && r.season  == stale.season
            && r.episode == stale.episode
            && r.section == stale.section)
            return r;
    }
    for (const auto& r : rows) {
        if (r.imdbId == stale.imdbId
            && r.season  == stale.season
            && r.episode == stale.episode)
            return r;
    }
    return std::nullopt;
}

// ──────────────────────────────────────────────────────────────────────────────
// Totals — DOWNLOADS_OVERHAUL_V2 T7
// ──────────────────────────────────────────────────────────────────────────────

void StreamDownloadsPage::updateTotals()
{
    if (!m_client) {
        m_totalsLabel->setText(QString());
        return;
    }

    // Running count from the queue (queue-aware, cap-correct).
    int n = 0;
    if (auto* q = m_client->transferQueue())
        n = q->runningCount();

    // Aggregate download speed straight from the engine's thread-safe status
    // snapshot (T7.1). listActive() would work too but runs a SQLite SELECT
    // (repo.listTorrents) per call just to overlay the same live engine
    // fields — pointless at 1 Hz when speed is the only thing we need.
    // Paused / seeding handles report downloadRate 0, so a plain sum is fine.
    qint64 totalBps = 0;
    if (auto* engine = m_client->engine()) {
        const auto statuses = engine->allStatuses();
        for (const auto& s : statuses)
            totalBps += s.downloadRate;
    }

    // Format: "N active · 1.2 MB/s"
    if (n == 0 && totalBps == 0) {
        m_totalsLabel->setText(tr("0 active"));
    } else {
        m_totalsLabel->setText(
            tr("%1 active  ·  %2")
                .arg(n)
                .arg(formatDownloadSpeed(totalBps)));
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Meta enrichment — rewired from old onMetaItemReady to serve the tree
// ──────────────────────────────────────────────────────────────────────────────

void StreamDownloadsPage::onMetaItemReady(const tankostream::addon::MetaItem& item)
{
    const QString imdbId = item.preview.id;
    if (imdbId.isEmpty()) return;

    // Update title cache
    if (!item.preview.name.isEmpty())
        m_titleCache.insert(imdbId, item.preview.name);

    // Save poster to disk cache and update in-memory QPixmap cache.
    savePosterFrom(imdbId, item.preview.poster);

    // Propagate title update into the tree's show-group nodes
    // (iterate all top-level section nodes → their show-group children).
    const int topCount = m_tree->topLevelItemCount();
    for (int si = 0; si < topCount; ++si) {
        QTreeWidgetItem* sectionNode = m_tree->topLevelItem(si);
        if (!sectionNode) continue;
        const int showCount = sectionNode->childCount();
        for (int sh = 0; sh < showCount; ++sh) {
            QTreeWidgetItem* showNode = sectionNode->child(sh);
            if (!showNode) continue;
            // Infer imdbId from first episode child
            if (showNode->childCount() == 0) continue;
            const QVariant v = showNode->child(0)->data(0, Qt::UserRole);
            if (!v.isValid() || !v.canConvert<tankostream::stream::DownloadRow>())
                continue;
            const auto r = v.value<tankostream::stream::DownloadRow>();
            if (r.imdbId != imdbId) continue;

            if (!item.preview.name.isEmpty())
                showNode->setText(0, item.preview.name);

            // Set poster icon on show node if we now have it cached
            const auto pit = m_posterCache.constFind(imdbId);
            if (pit != m_posterCache.constEnd() && !pit->isNull())
                showNode->setIcon(0, QIcon(*pit));
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Poster download + caching (kept from pre-T4; rewired to m_posterCache)
// ──────────────────────────────────────────────────────────────────────────────

void StreamDownloadsPage::savePosterFrom(const QString& imdbId, const QUrl& posterUrl)
{
    // If already in memory cache, nothing to do.
    if (m_posterCache.contains(imdbId)) return;

    const QString path = posterCachePath(imdbId);
    // Try on-disk cache first (another consumer may have fetched it already).
    QPixmap pm;
    if (QFile::exists(path) && pm.load(path)) {
        m_posterCache.insert(imdbId, pm.scaled(16, 24, Qt::KeepAspectRatioByExpanding,
                                                Qt::SmoothTransformation));
        return;
    }

    if (posterUrl.isEmpty()) return;

    if (!m_posterNam)
        m_posterNam = tankoban::net::NetSeam::instance()->createManager(
            this, QStringLiteral("stream-downloads-poster"));

    QNetworkRequest req(posterUrl);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_posterNam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, imdbId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        QPixmap pm;
        if (!pm.loadFromData(reply->readAll())) return;

        // Persist to disk (shared cache)
        const QString path = posterCachePath(imdbId);
        QDir().mkpath(QFileInfo(path).absolutePath());
        pm.save(path, "JPG");

        // Store tree-icon sized pixmap in memory cache
        m_posterCache.insert(imdbId, pm.scaled(16, 24, Qt::KeepAspectRatioByExpanding,
                                               Qt::SmoothTransformation));

        // Patch existing show nodes that belong to this imdbId
        const int topCount = m_tree->topLevelItemCount();
        for (int si = 0; si < topCount; ++si) {
            QTreeWidgetItem* sectionNode = m_tree->topLevelItem(si);
            if (!sectionNode) continue;
            const int showCount = sectionNode->childCount();
            for (int sh = 0; sh < showCount; ++sh) {
                QTreeWidgetItem* showNode = sectionNode->child(sh);
                if (!showNode || showNode->childCount() == 0) continue;
                const QVariant v = showNode->child(0)->data(0, Qt::UserRole);
                if (!v.isValid() || !v.canConvert<tankostream::stream::DownloadRow>())
                    continue;
                const auto r = v.value<tankostream::stream::DownloadRow>();
                if (r.imdbId == imdbId)
                    showNode->setIcon(0, QIcon(m_posterCache.value(imdbId)));
            }
        }
    });
}
