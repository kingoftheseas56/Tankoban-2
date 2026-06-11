// DOWNLOADS_OVERHAUL_V2 Task 4 (2026-06-11) — Master-Detail shell.
// Replaces the old two-section scrollable card list with a QSplitter:
//   left  — QTreeWidget: Failed / Active / Queued / Completed sections,
//            shows grouped, episodes as leaves.
//   right — detail pane stub (Task 5 builds the real pane).
// All signal-triggered refreshes debounce through a 250ms single-shot timer
// so rapid lane-state updates don't hammer the tree.

#include "StreamDownloadsPage.h"

#include "core/torrent/TorrentClient.h"
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
#include <QVariant>

// ──────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ──────────────────────────────────────────────────────────────────────────────
namespace {

// Section indices match DownloadSection enum order
static const char* kSectionNames[] = {"Failed", "Active", "Queued", "Completed"};

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

    // 250ms debounce timer — all signal triggers start() this; rebuild() fires
    // once per burst.
    m_rebuildDebounce = new QTimer(this);
    m_rebuildDebounce->setSingleShot(true);
    m_rebuildDebounce->setInterval(250);
    connect(m_rebuildDebounce, &QTimer::timeout, this, &StreamDownloadsPage::rebuild);

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

    // Buttons and combo — created but NOT wired (Task 7). Disabled now.
    m_pauseAllBtn = new QPushButton(tr("Pause All"), stripWidget);
    m_pauseAllBtn->setEnabled(false);
    strip->addWidget(m_pauseAllBtn, 0);

    m_resumeAllBtn = new QPushButton(tr("Resume All"), stripWidget);
    m_resumeAllBtn->setEnabled(false);
    strip->addWidget(m_resumeAllBtn, 0);

    m_clearDoneBtn = new QPushButton(tr("Clear Done"), stripWidget);
    m_clearDoneBtn->setEnabled(false);
    strip->addWidget(m_clearDoneBtn, 0);

    m_maxActiveCombo = new QComboBox(stripWidget);
    m_maxActiveCombo->addItem(tr("Max: unlimited"));
    m_maxActiveCombo->addItem(tr("Max: 1"));
    m_maxActiveCombo->addItem(tr("Max: 2"));
    m_maxActiveCombo->addItem(tr("Max: 3"));
    m_maxActiveCombo->setEnabled(false);
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

    // Right pane — detail stub (Task 5 replaces)
    m_detailPlaceholder = new QLabel(tr("Select a download"), m_splitter);
    m_detailPlaceholder->setObjectName(QStringLiteral("StreamDownloadsDetailPlaceholder"));
    m_detailPlaceholder->setAlignment(Qt::AlignCenter);
    m_detailPlaceholder->setStyleSheet(
        QStringLiteral("QLabel#StreamDownloadsDetailPlaceholder {"
                       "  color: rgba(255,255,255,0.35);"
                       "  font-size: 14px;"
                       "  background: transparent;"
                       "}"));

    m_splitter->addWidget(m_tree);
    m_splitter->addWidget(m_detailPlaceholder);
    m_splitter->setStretchFactor(0, 2);
    m_splitter->setStretchFactor(1, 3);

    root->addWidget(m_splitter, 1);

    // ── Tree selection → detail pane ────────────────────────────────────────
    connect(m_tree, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem* current, QTreeWidgetItem* /*previous*/) {
        if (!current) {
            m_selectedRow.reset();
            m_detailPlaceholder->setText(tr("Select a download"));
            return;
        }
        const QVariant v = current->data(0, Qt::UserRole);
        if (!v.isValid() || !v.canConvert<tankostream::stream::DownloadRow>()) {
            m_selectedRow.reset();
            m_detailPlaceholder->setText(tr("Select a download"));
            return;
        }
        const auto r = v.value<tankostream::stream::DownloadRow>();
        m_selectedRow = r;

        // Minimal detail: show title + SxxExx + section — Task 5 replaces.
        const QString episodeStr = (r.type == QLatin1String("movie"))
            ? tr("Movie")
            : QStringLiteral("S%1E%2")
                  .arg(r.season, 2, 10, QLatin1Char('0'))
                  .arg(r.episode, 2, 10, QLatin1Char('0'));
        const QString sectionStr = QString::fromLatin1(
            kSectionNames[int(r.section)]);
        m_detailPlaceholder->setText(
            displayShowTitle(r.imdbId)
            + QLatin1Char('\n') + episodeStr
            + QLatin1Char('\n') + sectionStr);
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

    const auto rows = tankostream::stream::buildDownloadRows(
        snap, QDateTime::currentMSecsSinceEpoch(), kCompletedTrimMs);

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
    m_detailPlaceholder->setText(tr("Select a download"));
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
// Totals (stub — Task 7 fills)
// ──────────────────────────────────────────────────────────────────────────────

void StreamDownloadsPage::updateTotals()
{
    // Task 7 fills in real counts. Leave blank for now so the strip doesn't
    // show stale/wrong numbers.
    m_totalsLabel->setText(QString());
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
