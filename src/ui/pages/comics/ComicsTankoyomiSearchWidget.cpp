// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 -- Phase 3 Task 17 (original);
// TANKOYOMI_VOLUME_PIVOT Phase 9 (2026-05-16) -- collapsed to single AniList
// strip. Premium catalog routing + scraper fan-out + Manga/Comics type split
// all removed. Backbone is now AniListClient::searchByTitle and the strip
// holds MediaPreview-typed tiles. Click on a tile emits seriesActivated.

#include "ComicsTankoyomiSearchWidget.h"

#include "core/manga/anilist/AniListClient.h"
#include "ui/Theme.h"
#include "ui/pages/TileStrip.h"
#include "ui/pages/TileCard.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStandardPaths>
#include <QVBoxLayout>

ComicsTankoyomiSearchWidget::ComicsTankoyomiSearchWidget(
    tankoban::manga::anilist::AniListClient* client,
    QNetworkAccessManager* nam, QWidget* parent)
    : QWidget(parent), m_client(client), m_nam(nam)
{
    buildUI();

    if (m_client) {
        connect(m_client, &tankoban::manga::anilist::AniListClient::searchSucceeded,
                this,     &ComicsTankoyomiSearchWidget::onSearchSucceeded);
        connect(m_client, &tankoban::manga::anilist::AniListClient::searchFailed,
                this,     &ComicsTankoyomiSearchWidget::onSearchFailed);
    }
}

void ComicsTankoyomiSearchWidget::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* topRow = new QWidget(this);
    auto* topLayout = new QHBoxLayout(topRow);
    topLayout->setContentsMargins(16, 8, 16, 8);
    topLayout->setSpacing(8);

    m_backBtn = new QPushButton(QStringLiteral("← Back to library"), topRow);
    m_backBtn->setObjectName("SidebarAction");
    m_backBtn->setFixedHeight(30);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setStyleSheet(
        "#SidebarAction { background: transparent; border: none; color: rgba(255,255,255,0.7);"
        "  font-size: 13px; padding: 0 8px; }"
        "#SidebarAction:hover { color: #fff; }");
    connect(m_backBtn, &QPushButton::clicked, this, &ComicsTankoyomiSearchWidget::backRequested);
    topLayout->addWidget(m_backBtn);

    m_statusLabel = new QLabel(topRow);
    m_statusLabel->setObjectName("ComicsSearchStatus");
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
    scrollLayout->setSpacing(12);

    m_resultsHeader = new QLabel(QStringLiteral("RESULTS"), scrollContent);
    m_resultsHeader->setObjectName("LibraryHeadingSmall");
    scrollLayout->addWidget(m_resultsHeader);
    m_resultsStrip = new TileStrip(scrollContent);
    m_resultsStrip->setDensity(0);
    scrollLayout->addWidget(m_resultsStrip);

    scrollLayout->addStretch(1);

    m_resultsHeader->hide();

    m_scroll->setWidget(scrollContent);
    root->addWidget(m_scroll, 1);
}

void ComicsTankoyomiSearchWidget::search(const QString& query)
{
    m_currentQuery = query;
    if (!m_client) {
        m_statusLabel->setText(QStringLiteral("Search unavailable"));
        return;
    }

    clearResults();
    m_pendingReqId = m_nextRequestId++;
    m_statusLabel->setText(QStringLiteral("Searching... (%1)").arg(query));
    m_client->searchByTitle(query, m_pendingReqId);
}

void ComicsTankoyomiSearchWidget::clearResults()
{
    if (m_resultsStrip) m_resultsStrip->clear();
    m_seenAnilistIds.clear();
    if (m_resultsHeader) m_resultsHeader->hide();
    if (m_statusLabel)   m_statusLabel->clear();
}

void ComicsTankoyomiSearchWidget::onSearchSucceeded(
    int requestId,
    const QList<tankoban::manga::anilist::MediaPreview>& results)
{
    // Late-arriving response for a prior search call: ignore.
    if (requestId != m_pendingReqId) return;

    int added = 0;
    for (const auto& r : results) {
        if (r.anilistId <= 0) continue;
        if (m_seenAnilistIds.contains(r.anilistId)) continue;
        m_seenAnilistIds.insert(r.anilistId, true);
        addResultCard(r);
        ++added;
    }

    if (added > 0) {
        m_resultsHeader->setVisible(true);
        m_statusLabel->setText(QStringLiteral("Done: %1 result%2")
                                   .arg(added).arg(added == 1 ? QString() : QStringLiteral("s")));
    } else {
        m_statusLabel->setText(QStringLiteral("No results for \"%1\"").arg(m_currentQuery));
    }
}

void ComicsTankoyomiSearchWidget::onSearchFailed(int requestId, const QString& reason)
{
    if (requestId != m_pendingReqId) return;
    m_statusLabel->setText(QStringLiteral("Search failed: %1").arg(reason));
}

void ComicsTankoyomiSearchWidget::addResultCard(
    const tankoban::manga::anilist::MediaPreview& r)
{
    // TileCard ctor expects (thumbPath, title, subtitle). MediaPreview carries
    // a URL not a local path; we hand the URL straight to the card and rely
    // on TileCard's async poster loader (if available) or future Phase 12
    // banner-loader retrofit. For v1 we pass empty string for the thumb path
    // (clean placeholder) and let the page-side QNetworkAccessManager warm
    // the cache once a series is opened. Subtitle uses the AniList format
    // ("MANGA"/"MANHWA"/etc) plus year when available -- consistent with the
    // Stream-blueprint detail surface vocabulary.
    QString subtitle;
    if (!r.format.isEmpty()) subtitle = r.format;
    if (r.yearStarted > 0) {
        if (!subtitle.isEmpty()) subtitle += QStringLiteral("  ");
        subtitle += QString::number(r.yearStarted);
    }

    auto* card = new TileCard(QString(), r.title, subtitle);
    card->setProperty("anilistId", r.anilistId);
    card->setProperty("seriesTitle", r.title);

    connect(card, &TileCard::clicked, this, [this, r]() {
        emit seriesActivated(r);
    });

    m_resultsStrip->addTile(card);

    // Best-effort cover thumbnail: hand the AniList coverThumbUrl to a
    // direct NAM fetch and update the card on completion. This is the
    // simplest viable poster path until the dedicated Phase 12 image cache
    // lands; failures are silent (placeholder remains).
    if (m_nam && !r.coverThumbUrl.isEmpty()) {
        QNetworkRequest req{QUrl(r.coverThumbUrl)};
        req.setHeader(QNetworkRequest::UserAgentHeader,
            QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
        req.setTransferTimeout(10000);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QPointer<TileCard> guard(card);
        auto* reply = m_nam->get(req);
        const QString posterCacheDir =
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/Tankoban/data/anilist_posters");
        QDir().mkpath(posterCacheDir);
        const QString outPath = posterCacheDir + QStringLiteral("/anilist_%1.jpg")
                                                     .arg(r.anilistId);
        connect(reply, &QNetworkReply::finished, this, [reply, outPath, guard]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) return;
            const QByteArray data = reply->readAll();
            if (data.isEmpty()) return;
            QFile f(outPath);
            if (!f.open(QIODevice::WriteOnly)) return;
            f.write(data);
            f.close();
            if (guard) guard->setThumbPath(outPath);
        });
    }
}
