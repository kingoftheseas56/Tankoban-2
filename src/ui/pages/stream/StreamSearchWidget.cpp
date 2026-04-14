#include "StreamSearchWidget.h"

#include "core/stream/CinemetaClient.h"
#include "core/stream/StreamLibrary.h"
#include "ui/pages/TileCard.h"
#include "ui/pages/TileStrip.h"

#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QVBoxLayout>

StreamSearchWidget::StreamSearchWidget(CinemetaClient* cinemeta, StreamLibrary* library,
                                       QWidget* parent)
    : QWidget(parent)
    , m_cinemeta(cinemeta)
    , m_library(library)
    , m_nam(new QNetworkAccessManager(this))
{
    m_posterCacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                       + "/Tankoban/data/stream_posters";
    QDir().mkpath(m_posterCacheDir);

    buildUI();

    connect(m_cinemeta, &CinemetaClient::catalogResults,
            this, &StreamSearchWidget::onCatalogResults);
    connect(m_cinemeta, &CinemetaClient::catalogError,
            this, &StreamSearchWidget::onCatalogError);
}

void StreamSearchWidget::search(const QString& query)
{
    clearResults();
    m_statusLabel->setText("Searching...");
    m_statusLabel->show();
    show();
    m_cinemeta->searchCatalog(query);
}

// ─── UI ──────────────────────────────────────────────────────────────────────

void StreamSearchWidget::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Top row: back button + status
    auto* topRow = new QWidget(this);
    auto* topLayout = new QHBoxLayout(topRow);
    topLayout->setContentsMargins(16, 8, 16, 8);
    topLayout->setSpacing(8);

    m_backBtn = new QPushButton("\u2190 Stream", topRow);
    m_backBtn->setObjectName("SidebarAction");
    m_backBtn->setFixedHeight(30);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setStyleSheet(
        "#SidebarAction { background: transparent; border: none; color: rgba(255,255,255,0.7);"
        "  font-size: 13px; padding: 0 8px; }"
        "#SidebarAction:hover { color: #fff; }");
    connect(m_backBtn, &QPushButton::clicked, this, [this]() {
        hide();
        emit backRequested();
    });
    topLayout->addWidget(m_backBtn);

    m_statusLabel = new QLabel(topRow);
    m_statusLabel->setStyleSheet("color: rgba(255,255,255,0.5); font-size: 12px;");
    topLayout->addWidget(m_statusLabel);
    topLayout->addStretch();

    root->addWidget(topRow);

    // Scroll area with TileStrip grid
    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setWidgetResizable(true);

    auto* scrollContent = new QWidget();
    auto* scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(16, 8, 16, 16);
    scrollLayout->setSpacing(0);

    m_strip = new TileStrip(scrollContent);
    scrollLayout->addWidget(m_strip, 1);

    m_scroll->setWidget(scrollContent);
    root->addWidget(m_scroll, 1);
}

void StreamSearchWidget::clearResults()
{
    m_strip->clear();
}

// ─── Search results ──────────────────────────────────────────────────────────

void StreamSearchWidget::onCatalogResults(const QList<CinemetaEntry>& results)
{
    clearResults();

    if (results.isEmpty()) {
        m_statusLabel->setText("No results found");
        m_statusLabel->show();
        return;
    }

    m_statusLabel->setText(QString::number(results.size()) + " results");

    for (const auto& entry : results)
        addResultCard(entry);
}

void StreamSearchWidget::onCatalogError(const QString& message)
{
    m_statusLabel->setText("Search failed: " + message);
}

void StreamSearchWidget::addResultCard(const CinemetaEntry& entry)
{
    // Check for cached poster
    QString posterPath = m_posterCacheDir + "/" + entry.imdb + ".jpg";
    QString thumbPath = QFile::exists(posterPath) ? posterPath : QString();

    // Subtitle: year + type + rating
    QStringList sub;
    if (!entry.year.isEmpty()) sub << entry.year;
    if (!entry.type.isEmpty()) sub << (entry.type == "series" ? "Series" : "Movie");
    if (!entry.imdbRating.isEmpty()) sub << entry.imdbRating;
    QString subtitle = sub.join(" \u00B7 ");

    auto* card = new TileCard(thumbPath, entry.name, subtitle);

    card->setProperty("imdbId", entry.imdb);
    card->setProperty("entryType", entry.type);
    card->setProperty("entryName", entry.name);
    card->setProperty("entryYear", entry.year);
    card->setProperty("entryPoster", entry.poster);
    card->setProperty("entryDesc", entry.description);
    card->setProperty("entryRating", entry.imdbRating);

    // "In Library" badge via status
    updateInLibraryBadge(card);

    // Click → add/remove from library
    connect(card, &TileCard::clicked, this, [this, card]() {
        QString imdbId = card->property("imdbId").toString();
        if (imdbId.isEmpty()) return;

        if (m_library->has(imdbId)) {
            m_library->remove(imdbId);
        } else {
            StreamLibraryEntry libEntry;
            libEntry.imdb        = card->property("imdbId").toString();
            libEntry.type        = card->property("entryType").toString();
            libEntry.name        = card->property("entryName").toString();
            libEntry.year        = card->property("entryYear").toString();
            libEntry.poster      = card->property("entryPoster").toString();
            libEntry.description = card->property("entryDesc").toString();
            libEntry.imdbRating  = card->property("entryRating").toString();
            m_library->add(libEntry);
        }
        updateInLibraryBadge(card);
        emit libraryChanged();
    });

    m_strip->addTile(card);

    // Download poster async if not cached
    if (thumbPath.isEmpty() && !entry.poster.isEmpty())
        downloadPoster(entry.imdb, entry.poster, card);
}

void StreamSearchWidget::updateInLibraryBadge(TileCard* card)
{
    QString imdbId = card->property("imdbId").toString();
    if (m_library->has(imdbId)) {
        // Show a green progress bar + "finished" status to indicate in-library
        card->setBadges(1.0, {}, "In Library", "finished");
    } else {
        card->setBadges(0.0, {}, {}, {});
    }
}

// ─── Poster download ─────────────────────────────────────────────────────────

void StreamSearchWidget::downloadPoster(const QString& imdbId, const QString& posterUrl,
                                         TileCard* card)
{
    QUrl url(posterUrl);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
    req.setTransferTimeout(10000);

    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, imdbId, card]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;

        QByteArray data = reply->readAll();
        if (data.isEmpty())
            return;

        // Save to cache
        QString path = m_posterCacheDir + "/" + imdbId + ".jpg";
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(data);
            file.close();
        }

        // Update the TileCard's poster
        card->setThumbPath(path);
    });
}
