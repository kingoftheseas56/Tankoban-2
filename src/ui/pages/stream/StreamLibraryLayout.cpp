#include "StreamLibraryLayout.h"

#include "core/CoreBridge.h"
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
#include <QStandardPaths>
#include <QVBoxLayout>

StreamLibraryLayout::StreamLibraryLayout(CoreBridge* bridge, StreamLibrary* library,
                                         QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
    , m_library(library)
    , m_nam(new QNetworkAccessManager(this))
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
    QDir dir(m_posterCacheDir);
    if (!dir.exists()) return;

    QStringList files = dir.entryList({"*.jpg"}, QDir::Files);
    for (const QString& file : files) {
        QString imdbId = QFileInfo(file).baseName(); // "tt1234567"
        if (!m_library->has(imdbId))
            QFile::remove(dir.filePath(file));
    }
}
