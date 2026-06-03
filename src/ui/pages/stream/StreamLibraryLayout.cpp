#include "StreamLibraryLayout.h"

#include "core/CoreBridge.h"
#include "core/net/NetSeam.h"
#include "core/stream/StreamDownloadIndex.h"
#include "core/torrent/TorrentClient.h"
#include "core/stream/StreamLibrary.h"
#include "core/stream/StreamProgress.h"
#include "ui/pages/TileCard.h"
#include "ui/pages/TileStrip.h"

#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QShowEvent>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

StreamLibraryLayout::StreamLibraryLayout(CoreBridge* bridge, StreamLibrary* library,
                                         QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
    , m_library(library)
    , m_nam(tankoban::net::NetSeam::instance()->createManager(this, QStringLiteral("stream-library-layout")))
{
    m_posterCacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                       + "/Tankoban/data/stream_posters";
    QDir().mkpath(m_posterCacheDir);

    buildUI();
}

void StreamLibraryLayout::refresh()
{
    populateTiles();
    cleanupOrphanPosters();
}

// STREAM_DOWNLOADED_LIBRARY Phase 3 (2026-05-10) — wire the download index
// so tiles can render the DOWNLOADED chip. Subscribes to entriesChanged so
// bulk-completion / migration / eviction events flip chip visibility on
// already-rendered tiles without rebuilding the strip. Queued connection
// because mutating callers (validateAll, registerEpisode) may run on a
// worker thread.
void StreamLibraryLayout::setStreamDownloadIndex(StreamDownloadIndex* idx)
{
    m_downloadIndex = idx;
    if (m_downloadIndex) {
        connect(m_downloadIndex, &StreamDownloadIndex::entriesChanged,
                this, &StreamLibraryLayout::refreshTileBadges,
                Qt::QueuedConnection);
        // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 Task 16 — also listen for
        // the granular entryStateChanged signal so movie tiles repaint when a
        // single Pending/Downloading/Complete transition fires (entriesChanged
        // is add/remove-only). The existing refreshTileBadges walk is cheap
        // (small N for library tiles); granular-by-imdbId optimization left
        // as v1.x polish.
        connect(m_downloadIndex, &StreamDownloadIndex::entryStateChanged,
                this,
                [this](const QString& /*imdbId*/, int /*season*/, int /*episode*/) {
                    refreshTileBadges();
                },
                Qt::QueuedConnection);
        // Apply badges to any tiles already rendered before the wire landed.
        refreshTileBadges();
    }
}

// STREAM_DOWNLOADS_NETFLIX_OVERHAUL — wire TorrentClient so the
// DOWNLOADING chip can query imdbHasActiveCohort and refresh on
// streamBulkGroupsChanged. Pattern mirrors setStreamDownloadIndex.
void StreamLibraryLayout::setTorrentClient(TorrentClient* client)
{
    if (m_torrentClient == client) return;
    if (m_torrentClient) disconnect(m_torrentClient, nullptr, this, nullptr);
    m_torrentClient = client;
    if (m_torrentClient) {
        connect(m_torrentClient, &TorrentClient::streamBulkGroupsChanged,
                this, [this](const QString&) { refreshTileBadges(); },
                Qt::QueuedConnection);
    }
    refreshTileBadges();
}

// STREAM_DOWNLOADED_LIBRARY Phase 7 (2026-05-10) — eager disk-state
// validation when the user opens Stream library home. Off-thread; lazy
// on click (StreamDetailView::onEpisodeActivated) continues to be the
// per-episode safety net. validateAll snapshots under-lock + stats off-
// lock + evicts under-lock again (per StreamDownloadIndex's threading
// contract). Cheap (~10s of stats) for typical libraries; off-thread
// so home-open doesn't hitch. Spec §10.4.
void StreamLibraryLayout::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_downloadIndex) {
        StreamDownloadIndex* idx = m_downloadIndex;
        (void) QtConcurrent::run([idx]() { idx->validateAll(); });
    }
}

void StreamLibraryLayout::refreshTileBadges()
{
    if (!m_strip) return;

    const QList<TileCard*> tiles = m_strip->findChildren<TileCard*>();
    for (TileCard* card : tiles) {
        const QString imdb = card->property("imdbId").toString();
        if (imdb.isEmpty()) continue;
        const bool downloaded =
            m_downloadIndex && m_downloadIndex->hasAnyForImdb(imdb);
        const bool downloading =
            m_torrentClient && m_torrentClient->imdbHasActiveCohort(imdb);
        QLabel* dlChip = card->findChild<QLabel*>(QStringLiteral("DownloadedChip"));
        QLabel* dlActiveChip = card->findChild<QLabel*>(QStringLiteral("DownloadingChip"));
        if (dlActiveChip) dlActiveChip->setVisible(downloading);
        if (dlChip)       dlChip->setVisible(downloaded && !downloading);
    }
}

// ─── UI ──────────────────────────────────────────────────────────────────────

void StreamLibraryLayout::buildUI()
{
    // 2026-04-15 — margins/spacing aligned with video mode (VideosPage.cpp:110-111).
    // 2026-04-16 — margins zeroed: StreamLibraryLayout is mounted inside
    // StreamPage's m_scrollLayout which ALREADY applies (20,0,20,20) margins +
    // spacing(24) to its children. The earlier (20,0,20,20) here stacked on top
    // of that, putting Shows & Movies tiles at 40px from the page edge while
    // the Continue Watching strip above (StreamHomeBoard — margins 0,0,0,0)
    // sat at 20px — breaking the vertical-column alignment Hemanth expects from
    // the other three library modes.
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(24);

    // Header row: SHOWS + sort + density
    auto* headerRow = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    // Stream library UX rework 2026-04-15 — header label reflects that
    // the grid contains BOTH in-progress (also in Continue Watching)
    // AND user-added future-viewing titles, across shows and movies.
    m_sectionLabel = new QLabel("SHOWS & MOVIES", headerRow);
    m_sectionLabel->setObjectName("LibraryHeading");
    headerLayout->addWidget(m_sectionLabel);
    headerLayout->addStretch();

    // Sort combo
    m_sortCombo = new QComboBox(headerRow);
    m_sortCombo->setObjectName("LibrarySortCombo");
    // Stream library UX 2026-04-15 — match Videos-mode sort combo width
    // (VideosPage.cpp:188). Was 170px; pulling it in to 150 normalizes
    // the header row across both modes.
    m_sortCombo->setFixedWidth(150);
    m_sortCombo->setFixedHeight(28);
    m_sortCombo->addItem("Name A\u2192Z",       "name_asc");
    m_sortCombo->addItem("Name Z\u2192A",       "name_desc");
    m_sortCombo->addItem("Recently added",       "updated_desc");
    m_sortCombo->addItem("Oldest added",         "updated_asc");
    m_sortCombo->addItem("Rating High\u2192Low", "rating_desc");
    m_sortCombo->addItem("Rating Low\u2192High", "rating_asc");
    m_sortCombo->setStyleSheet(
        "QComboBox#LibrarySortCombo {"
        "  background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        "  border-radius: 6px; color: #ccc; padding: 2px 8px; font-size: 12px; }"
        "QComboBox#LibrarySortCombo:hover { border-color: rgba(255,255,255,0.2); }"
        "QComboBox#LibrarySortCombo::drop-down { border: none; }"
        "QComboBox#LibrarySortCombo QAbstractItemView {"
        "  background: #1e1e1e; color: #ccc; selection-background-color: rgba(255,255,255,0.1);"
        "  border: 1px solid rgba(255,255,255,0.12); }");

    QString savedSort = QSettings("Tankoban", "Tankoban").value("library_sort_stream", "name_asc").toString();
    for (int i = 0; i < m_sortCombo->count(); ++i) {
        if (m_sortCombo->itemData(i).toString() == savedSort) {
            m_sortCombo->setCurrentIndex(i);
            break;
        }
    }
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        QString key = m_sortCombo->itemData(idx).toString();
        QSettings("Tankoban", "Tankoban").setValue("library_sort_stream", key);
        m_strip->sortTiles(key);
    });
    headerLayout->addWidget(m_sortCombo);

    // Density slider
    auto* densitySmall = new QLabel("A", headerRow);
    densitySmall->setStyleSheet("color: rgba(255,255,255,0.4); font-size: 10px;");
    headerLayout->addWidget(densitySmall);

    m_densitySlider = new QSlider(Qt::Horizontal, headerRow);
    m_densitySlider->setRange(0, 2);
    m_densitySlider->setFixedWidth(100);
    m_densitySlider->setFixedHeight(20);
    int savedDensity = QSettings("Tankoban", "Tankoban").value("grid_cover_size_stream", 1).toInt();
    m_densitySlider->setValue(qBound(0, savedDensity, 2));
    connect(m_densitySlider, &QSlider::valueChanged, this, [this](int val) {
        QSettings("Tankoban", "Tankoban").setValue("grid_cover_size_stream", val);
        m_strip->setDensity(val);
    });
    headerLayout->addWidget(m_densitySlider);

    auto* densityLarge = new QLabel("A", headerRow);
    densityLarge->setStyleSheet("color: rgba(255,255,255,0.4); font-size: 16px;");
    headerLayout->addWidget(densityLarge);

    root->addWidget(headerRow);

    // Empty state label
    m_emptyLabel = new QLabel(
        "Your library is empty. Use Search or Catalog to add shows and movies.",
        this);
    m_emptyLabel->setObjectName("LibraryEmptyLabel");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet("color: rgba(238,238,238,0.58); font-size: 14px; padding: 60px;");
    root->addWidget(m_emptyLabel);

    // Tile strip
    m_strip = new TileStrip(this);
    m_strip->setDensity(savedDensity);
    m_strip->hide();
    root->addWidget(m_strip, 1);

    // Wire tile signals. Single-click opens detail (Stream-mode UX); double-
    // click also opens detail (preserves muscle memory). StreamPage::showDetail
    // is idempotent against back-to-back calls so the paired emit on a double-
    // click gesture doesn't reset state.
    auto openDetail = [this](TileCard* card) {
        QString imdb = card->property("imdbId").toString();
        if (!imdb.isEmpty())
            emit showClicked(imdb);
    };
    connect(m_strip, &TileStrip::tileSingleClicked, this, openDetail);
    connect(m_strip, &TileStrip::tileDoubleClicked, this, openDetail);
    connect(m_strip, &TileStrip::tileRightClicked, this, [this](TileCard* card, const QPoint& pos) {
        QString imdb = card->property("imdbId").toString();
        if (!imdb.isEmpty())
            emit showRightClicked(imdb, pos);
    });
}

// ─── Tile population ─────────────────────────────────────────────────────────

void StreamLibraryLayout::populateTiles()
{
    m_strip->clear();

    auto entries = m_library->getAll();

    if (entries.isEmpty()) {
        m_emptyLabel->show();
        m_strip->hide();
        return;
    }

    m_emptyLabel->hide();
    m_strip->show();

    // Load all stream progress for badge computation
    QJsonObject allProgress = m_bridge->allProgress("stream");

    for (const auto& entry : entries) {
        // Poster path — check local cache
        QString posterPath = posterCachePath(entry.imdb);
        if (!QFile::exists(posterPath) && !entry.poster.isEmpty())
            downloadPoster(entry.imdb, entry.poster);

        // Subtitle: year + IMDb rating (canonical Stream format across
        // StreamLibraryLayout / StreamSearchWidget / CatalogBrowseScreen).
        // Type (Series/Movie) dropped — cover art communicates it faster
        // than text, and dropping it lets the subtitle fit at every density
        // without eliding. Rating keeps its "IMDb" prefix so a bare number
        // isn't ambiguous. Year's trailing en-dash for ongoing series
        // (Stremio format "2023–") is normalized to "2023–present" so the
        // dash doesn't read as a dangling separator next to the middle-dot.
        QStringList sub;
        if (!entry.year.isEmpty()) {
            QString y = entry.year;
            if (y.endsWith(QChar(0x2013)) || y.endsWith(QChar('-'))) {
                y.chop(1);
                y += QStringLiteral("\u2013present");
            }
            sub << y;
        }
        if (!entry.imdbRating.isEmpty())
            sub << QStringLiteral("IMDb ") + entry.imdbRating;
        QString subtitle = sub.join(" \u00B7 ");

        auto* card = new TileCard(
            QFile::exists(posterPath) ? posterPath : QString(),
            entry.name,
            subtitle
        );

        card->setProperty("imdbId", entry.imdb);
        card->setProperty("seriesName", entry.name);
        card->setProperty("newestMtime", entry.addedAt);

        // Compute progress badge from watch state
        // Scan all progress keys matching this IMDB ID
        double bestPercent = 0.0;
        bool anyFinished = true;
        bool hasProgress = false;
        QString prefix = "stream:" + entry.imdb;

        for (auto it = allProgress.begin(); it != allProgress.end(); ++it) {
            if (!it.key().startsWith(prefix))
                continue;
            hasProgress = true;
            QJsonObject state = it->toObject();
            double pct = StreamProgress::percent(state);
            if (pct > bestPercent) bestPercent = pct;
            if (!StreamProgress::isFinished(state)) anyFinished = false;
        }

        if (hasProgress) {
            QString status = anyFinished ? "finished" : "reading";
            int pctInt = static_cast<int>(bestPercent);
            card->setBadges(bestPercent / 100.0, {}, {}, status);
            if (!anyFinished && pctInt > 0)
                card->setBadges(bestPercent / 100.0, {}, QString::number(pctInt) + "%", "reading");
        }

        // Sort properties for TileStrip::sortTiles()
        card->setProperty("sortTitle", entry.name.toLower());
        card->setProperty("sortRating", entry.imdbRating.toDouble());

        // STREAM_DOWNLOADED_LIBRARY Phase 3 (2026-05-10) — DOWNLOADED chip
        // overlay. Tidied 2026-05-12 per Hemanth feedback "asymmetrical
        // downloaded badge, please make it tidy". Changes: dropped the 1px
        // gray border (read as a hairline on the dark poster), bumped
        // border-radius from 3 → 4 to match the poster's corner curvature,
        // padding to symmetric 3px vertical × 7px horizontal (prior 1×5
        // made it look horizontally stretched + vertically pinched), and
        // nudged the chip inward from (8,8) → (10,10) so its left edge
        // doesn't collide with the poster's top-left rounded corner.
        // Visibility flipped by refreshTileBadges() on entriesChanged.
        auto* dlChip = new QLabel(QStringLiteral("DOWNLOADED"), card);
        dlChip->setObjectName(QStringLiteral("DownloadedChip"));
        dlChip->setStyleSheet(QStringLiteral(
            "#DownloadedChip { color: #eeeeee; border: none;"
            " border-radius: 4px; padding: 3px 7px; font-size: 9px;"
            " font-weight: 600; letter-spacing: 0.4px;"
            " background-color: rgba(0, 0, 0, 190); }"));
        dlChip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        // STREAM_DOWNLOADS_NETFLIX_OVERHAUL 2026-05-12 post-smoke — call
        // adjustSize() after setStyleSheet() so the chip's geometry shrinks
        // to (text-width + QSS-padding) instead of Qt's default sizeHint
        // which caches before the stylesheet's padding contribution is
        // accounted for. Without this, the background-color rect renders
        // wider than the text + asymmetric (Hemanth's "lots of open space
        // after the text" observation).
        dlChip->adjustSize();
        dlChip->setVisible(m_downloadIndex && m_downloadIndex->hasAnyForImdb(entry.imdb));
        dlChip->move(10, 10);
        dlChip->raise();

        // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — DOWNLOADING chip. Same QSS family
        // as DOWNLOADED. In-flight wins: when imdbHasActiveCohort returns true,
        // only DOWNLOADING renders; DOWNLOADED is hidden until the cohort
        // terminates. Spec §7.2.
        auto* dlActiveChip = new QLabel(QStringLiteral("DOWNLOADING"), card);
        dlActiveChip->setObjectName(QStringLiteral("DownloadingChip"));
        dlActiveChip->setStyleSheet(QStringLiteral(
            "#DownloadingChip { color: #eeeeee; border: none;"
            " border-radius: 4px; padding: 3px 7px; font-size: 9px;"
            " font-weight: 600; letter-spacing: 0.4px;"
            " background-color: rgba(0, 0, 0, 190); }"));
        dlActiveChip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        dlActiveChip->adjustSize();   // see chip-asymmetry note above
        const bool downloading =
            m_torrentClient && m_torrentClient->imdbHasActiveCohort(entry.imdb);
        dlActiveChip->setVisible(downloading);
        dlActiveChip->move(10, 10);  // same corner as DOWNLOADED chip
        dlActiveChip->raise();
        // In-flight wins — hide DOWNLOADED when DOWNLOADING shows.
        if (downloading) {
            dlChip->setVisible(false);
        }

        m_strip->addTile(card);
    }

    // Apply current sort
    QString sortKey = m_sortCombo->currentData().toString();
    m_strip->sortTiles(sortKey);
}

// ─── Poster downloading ─────────────────────────────────────────────────────

QString StreamLibraryLayout::posterCachePath(const QString& imdbId) const
{
    return m_posterCacheDir + "/" + imdbId + ".jpg";
}

void StreamLibraryLayout::downloadPoster(const QString& imdbId, const QString& posterUrl)
{
    QUrl url(posterUrl);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
    req.setTransferTimeout(10000);

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, imdbId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;

        QByteArray data = reply->readAll();
        if (data.isEmpty())
            return;

        QString path = posterCachePath(imdbId);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(data);
            file.close();
        }

        // Update the specific tile's poster without rebuilding the entire strip
        for (int i = 0; i < m_strip->totalCount(); ++i) {
            auto* card = m_strip->tileAtIndex(i);
            if (card && card->property("imdbId").toString() == imdbId) {
                card->setThumbPath(path);
                break;
            }
        }
    });
}

void StreamLibraryLayout::cleanupOrphanPosters()
{
    // A005 (night-watch app-heaviness, 2026-06-03) — the orphan sweep is a
    // QDir::entryList + per-file QFile::remove loop: bounded O(N-orphan) but
    // fully synchronous I/O that previously ran on the GUI thread during every
    // library refresh(). Move it off-thread so a refresh never hitches.
    //
    // A005 hardening C3 (2026-06-03) — coalesce overlapping sweeps. exchange(true)
    // atomically claims the in-flight slot; if a sweep is already running we skip
    // (return) instead of launching a second one. That removes the rapid-refresh
    // race where two worker threads raced to QFile::remove the same orphan (one
    // wins, the other gets a benign failure). A poster orphaned between the
    // running sweep's start and this skipped call simply lingers until the next
    // refresh() after the current sweep finishes — best-effort cosmetic cleanup,
    // no correctness cost. The worker clears the flag on every exit path.
    if (m_orphanSweepRunning->exchange(true))
        return;

    // A005 hardening C2 (2026-06-03) — capture ONLY by value: cacheDir (QString),
    // library (StreamLibrary::has() is mutex-protected and the library outlives
    // this widget), and `running` (the shared in-flight flag, lifetime-safe).
    // The old QPointer guard was read from the worker thread (unsafe concurrent
    // read against GUI-thread deletion); dropping it is sound because the worker
    // never dereferences the widget — the by-value captures are what keep it safe.
    // Mirrors the showEvent() validateAll fire-and-forget pattern above.
    const QString cacheDir = m_posterCacheDir;
    StreamLibrary* library = m_library;
    auto running = m_orphanSweepRunning;
    (void) QtConcurrent::run([cacheDir, library, running]() {
        // Clear the in-flight flag on EVERY return path so a later refresh() can
        // relaunch (RAII — covers the early bails below and normal completion).
        struct ClearOnExit {
            std::shared_ptr<std::atomic_bool> flag;
            ~ClearOnExit() { flag->store(false); }
        } clearOnExit{running};

        if (!library) return;
        QDir dir(cacheDir);
        if (!dir.exists()) return;

        const QStringList files = dir.entryList({"*.jpg"}, QDir::Files);
        for (const QString& file : files) {
            const QString imdbId = QFileInfo(file).baseName(); // "tt1234567"
            if (!library->has(imdbId))
                QFile::remove(dir.filePath(file));
        }
    });
}
