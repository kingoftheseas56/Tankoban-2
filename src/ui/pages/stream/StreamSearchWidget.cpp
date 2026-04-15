#include "StreamSearchWidget.h"

#include "core/stream/MetaAggregator.h"
#include "core/stream/StreamLibrary.h"
#include "ui/pages/TileCard.h"
#include "ui/pages/TileStrip.h"

#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStandardPaths>
#include <QVBoxLayout>

using tankostream::addon::MetaItemPreview;
using tankostream::stream::MetaAggregator;

StreamSearchWidget::StreamSearchWidget(MetaAggregator* meta, StreamLibrary* library,
                                       QWidget* parent)
    : QWidget(parent)
    , m_meta(meta)
    , m_library(library)
    , m_nam(new QNetworkAccessManager(this))
{
    m_posterCacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                       + "/Tankoban/data/stream_posters";
    QDir().mkpath(m_posterCacheDir);

    buildUI();

    if (m_meta) {
        connect(m_meta, &MetaAggregator::catalogResults,
                this, &StreamSearchWidget::onCatalogResults);
        connect(m_meta, &MetaAggregator::catalogError,
                this, &StreamSearchWidget::onCatalogError);
    }

    // Phase 1 Batch 1.2 — the detail view now owns the Add/Remove library
    // toggle. When the user toggles state there, StreamLibrary fires
    // libraryChanged; walk our tiles and refresh the "In Library" badge so
    // the user-visible state stays coherent on back-navigate to the search
    // results.
    if (m_library) {
        connect(m_library, &StreamLibrary::libraryChanged,
                this, &StreamSearchWidget::refreshAllBadges);
    }
}

void StreamSearchWidget::search(const QString& query)
{
    clearResults();
    // Phase 4 Batch 4.1 — full-page "Searching..." label removed; the subtle
    // spinner in StreamPage's search bar is the loading affordance (TODO
    // explicitly calls out "not a full-page Searching state"). Status label
    // still used for error + no-results messages.
    m_statusLabel->hide();
    show();
    if (m_meta) {
        m_meta->searchCatalog(query);
    } else {
        m_statusLabel->setText("Meta aggregator unavailable");
        m_statusLabel->show();
    }
}

// ─── UI ──────────────────────────────────────────────────────────────────────

void StreamSearchWidget::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

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
    m_tiles.clear();
    m_previewsById.clear();
}

void StreamSearchWidget::refreshAllBadges()
{
    for (TileCard* card : m_tiles) {
        if (card) updateInLibraryBadge(card);
    }
}

// ─── Search results ──────────────────────────────────────────────────────────

void StreamSearchWidget::onCatalogResults(const QList<MetaItemPreview>& results)
{
    clearResults();

    if (results.isEmpty()) {
        m_statusLabel->setText("No results found");
        m_statusLabel->show();
        return;
    }

    m_statusLabel->setText(QString::number(results.size()) + " results");

    for (const MetaItemPreview& entry : results)
        addResultCard(entry);
}

void StreamSearchWidget::onCatalogError(const QString& message)
{
    m_statusLabel->setText("Search failed: " + message);
}

void StreamSearchWidget::addResultCard(const MetaItemPreview& entry)
{
    const QString posterPath = m_posterCacheDir + "/" + entry.id + ".jpg";
    const QString thumbPath = QFile::exists(posterPath) ? posterPath : QString();

    QStringList sub;
    if (!entry.releaseInfo.isEmpty()) sub << entry.releaseInfo;
    if (!entry.type.isEmpty())        sub << (entry.type == "series" ? "Series" : "Movie");
    if (!entry.imdbRating.isEmpty())  sub << entry.imdbRating;
    QString subtitle = sub.join(" \u00B7 ");

    auto* card = new TileCard(thumbPath, entry.name, subtitle);

    const QString posterUrl = entry.poster.toString();
    card->setProperty("imdbId", entry.id);
    card->setProperty("entryType", entry.type);
    card->setProperty("entryName", entry.name);
    card->setProperty("entryYear", entry.releaseInfo);
    card->setProperty("entryPoster", posterUrl);
    card->setProperty("entryDesc", entry.description);
    card->setProperty("entryRating", entry.imdbRating);

    updateInLibraryBadge(card);

    // Phase 1 Batch 1.2 — click on a search result now opens the detail view
    // via StreamPage::showDetail(preview). The Add/Remove library toggle
    // moved into the detail view header. "In Library" badge remains as a
    // visual cue and is refreshed externally via libraryChanged above.
    connect(card, &TileCard::clicked, this, [this, card]() {
        const QString imdbId = card->property("imdbId").toString();
        if (imdbId.isEmpty()) return;
        const auto it = m_previewsById.constFind(imdbId);
        if (it == m_previewsById.constEnd()) return;
        emit metaActivated(it.value());
    });

    m_strip->addTile(card);
    m_tiles.push_back(card);
    m_previewsById.insert(entry.id, entry);

    if (thumbPath.isEmpty() && !posterUrl.isEmpty())
        downloadPoster(entry.id, posterUrl, card);
}

void StreamSearchWidget::updateInLibraryBadge(TileCard* card)
{
    QString imdbId = card->property("imdbId").toString();
    if (m_library->has(imdbId)) {
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

    QPointer<TileCard> guard(card);
    auto* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, imdbId, guard]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;

        QByteArray data = reply->readAll();
        if (data.isEmpty())
            return;

        QString path = m_posterCacheDir + "/" + imdbId + ".jpg";
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(data);
            file.close();
        }

        if (guard) {
            guard->setThumbPath(path);
        }
    });
}
