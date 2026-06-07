// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 -- Phase 3 Task 17 (original);
// TANKOYOMI_VOLUME_PIVOT Phase 9 (2026-05-16) -- collapsed to single AniList
// strip. Premium catalog routing + scraper fan-out + Manga/Comics type split
// all removed. Backbone was AniListClient::searchByTitle.
//
// WEEBCENTRAL_IDENTITY_PIVOT Tasks 9+10 (2026-05-19) -- backbone swapped from
// AniListClient to MangaSourceRegistry. Search routes through
// m_sourceRegistry->find("weebcentral")->search(query, 60). Results are
// MangaResult-typed. Tile click emits resultPicked(MangaResult).

#include "ComicsTankoyomiSearchWidget.h"

#include "core/manga/MangaSourceRegistry.h"
#include "core/manga/MangaScraper.h"
#include "ui/Theme.h"
#include "ui/pages/TileStrip.h"
#include "ui/pages/TileCard.h"

#include <QDir>
#include <QFile>
#include <QFrame>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStandardPaths>
#include <QVBoxLayout>

ComicsTankoyomiSearchWidget::ComicsTankoyomiSearchWidget(
    MangaSourceRegistry* sourceRegistry,
    QNetworkAccessManager* nam, QWidget* parent)
    : QWidget(parent), m_sourceRegistry(sourceRegistry), m_nam(nam)
{
    buildUI();

    if (m_sourceRegistry) {
        // WESTERN_PARITY 2026-06-07 (Agent 1) — readallcomics added: Western
        // search now dispatches to it, so its searchFinished MUST be connected
        // or results never render (cross-engine review P0, 2026-06-07).
        for (const QString& id : { QStringLiteral("weebcentral"),
                                   QStringLiteral("readcomicsonline"),
                                   QStringLiteral("readallcomics") }) {
            if (auto* scraper = m_sourceRegistry->find(id)) {
                connect(scraper, &MangaScraper::searchFinished,
                        this,    &ComicsTankoyomiSearchWidget::onSearchFinished,
                        Qt::UniqueConnection);
            }
        }
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

    if (!m_sourceRegistry) {
        m_statusLabel->setText(QStringLiteral("Search unavailable"));
        return;
    }

    auto* scraper = m_sourceRegistry->find(m_activeSourceId);
    if (!scraper) {
        qWarning() << "[ComicsTankoyomiSearchWidget] scraper not found:" << m_activeSourceId;
        m_statusLabel->setText(QStringLiteral("Search unavailable"));
        return;
    }

    clearResults();
    m_statusLabel->setText(QStringLiteral("Searching... (%1)").arg(query));
    scraper->search(query, /*limit=*/60);
}

void ComicsTankoyomiSearchWidget::setActiveSourceId(const QString& sourceId)
{
    m_activeSourceId = sourceId;
}

void ComicsTankoyomiSearchWidget::clearResults()
{
    if (m_resultsStrip)  m_resultsStrip->clear();
    if (m_resultsHeader) m_resultsHeader->hide();
    if (m_statusLabel)   m_statusLabel->clear();
}

void ComicsTankoyomiSearchWidget::onSearchFinished(const QList<MangaResult>& results)
{
    int added = 0;
    for (const auto& r : results) {
        if (r.id.isEmpty()) continue;
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

void ComicsTankoyomiSearchWidget::addResultCard(const MangaResult& r)
{
    // FANDOM_LOCAL_LOADER_INTEGRATION 2026-05-22 (Agent 1, Hemanth-flagged) --
    // Subtitle dropped entirely. The previous "WeebCentral  Ongoing" string
    // was metadata-source-implementation-detail leaking into the user's
    // view; Hemanth's call: tile sub-label adds noise without value.
    // r.source + r.status remain on the MangaResult for downstream logic;
    // they just don't render on the tile anymore.
    auto* card = new TileCard(QString(), r.title, QString());
    card->setProperty("seriesId",    r.id);
    card->setProperty("seriesTitle", r.title);

    connect(card, &TileCard::clicked, this, [this, r]() {
        emit resultPicked(r);
    });

    m_resultsStrip->addTile(card);

    // Best-effort cover thumbnail via thumbnailUrl. Failures are silent;
    // placeholder remains until the dedicated image cache lands.
    if (m_nam && !r.thumbnailUrl.isEmpty()) {
        QNetworkRequest req{QUrl(r.thumbnailUrl)};
        req.setHeader(QNetworkRequest::UserAgentHeader,
            QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
        req.setTransferTimeout(10000);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QPointer<TileCard> guard(card);
        auto* reply = m_nam->get(req);
        const QString posterCacheDir =
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/Tankoban/data/wc_posters");
        QDir().mkpath(posterCacheDir);
        // Sanitise the series id so it can be used as a filename safely.
        const QString safeName = QString(r.id).replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
        const QString outPath  = posterCacheDir + QStringLiteral("/%1.jpg").arg(safeName);
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
