// src/ui/pages/comics/ComicsCatalogScreen.cpp
// See header for design notes.

#include "ComicsCatalogScreen.h"

#include "core/manga/fandom/LocalFandomCatalogLoader.h"
#include "core/manga/fandom/FandomTypes.h"
#include "ui/pages/TileCard.h"

#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace tankoban::ui::comics {

// ─────────────────────── ComicsCatalogScreen ──────────────────────

ComicsCatalogScreen::ComicsCatalogScreen(QNetworkAccessManager* nam, QWidget* parent)
    : QWidget(parent), m_nam(nam)
{
    setObjectName("ComicsCatalogScreen");
    buildUi();
    loadAllCatalogs();
}

ComicsCatalogScreen::~ComicsCatalogScreen() = default;

void ComicsCatalogScreen::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);

    // Header row: back button + title.
    auto* header = new QHBoxLayout();
    header->setSpacing(12);

    m_backBtn = new QPushButton(tr("← Back"), this);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setStyleSheet(
        "QPushButton { background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.12);"
        " border-radius: 6px; color: #eee; padding: 6px 14px; font-size: 13px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.15); }");
    connect(m_backBtn, &QPushButton::clicked, this, &ComicsCatalogScreen::backRequested);
    header->addWidget(m_backBtn, 0, Qt::AlignLeft);

    auto* heading = new QLabel(tr("Catalog"), this);
    heading->setStyleSheet("color: #fff; font-size: 22px; font-weight: 600;");
    header->addWidget(heading, 1, Qt::AlignLeft);
    root->addLayout(header);

    // Scroll area hosting the tile grid.
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_gridHost = new QWidget();
    m_gridHost->setObjectName("CatalogGridHost");
    m_grid = new QGridLayout(m_gridHost);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(16);
    m_grid->setVerticalSpacing(20);
    m_grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    m_scroll->setWidget(m_gridHost);
    root->addWidget(m_scroll, 1);

    m_emptyLabel = new QLabel(
        tr("No catalogued series yet. Run the Fandom catalog ingest pipeline "
           "to populate data/fandom_catalog/."), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet("color: rgba(255,255,255,0.6); font-size: 14px;");
    m_emptyLabel->hide();
    root->addWidget(m_emptyLabel);
}

void ComicsCatalogScreen::refresh()
{
    clearTiles();
    loadAllCatalogs();
}

void ComicsCatalogScreen::clearTiles()
{
    for (auto* tile : m_tiles) {
        m_grid->removeWidget(tile);
        tile->deleteLater();
    }
    m_tiles.clear();
}

void ComicsCatalogScreen::loadAllCatalogs()
{
    const QString dataDir = tankoban::manga::fandom::LocalFandomCatalogLoader::canonicalDataDir();
    QDir dir(dataDir);
    if (!dir.exists()) {
        m_emptyLabel->show();
        return;
    }

    QStringList jsonFiles = dir.entryList(QStringList() << "*.json", QDir::Files, QDir::Name);
    // Drop any file whose stem ends in GROUND_TRUTH (dev backup), and ignore
    // anything in subdirs (entryList Files-only handles that already).
    jsonFiles.erase(std::remove_if(jsonFiles.begin(), jsonFiles.end(),
                                   [](const QString& n) {
                                       return n.contains("GROUND_TRUTH", Qt::CaseInsensitive);
                                   }),
                    jsonFiles.end());

    if (jsonFiles.isEmpty()) {
        m_emptyLabel->show();
        return;
    }
    m_emptyLabel->hide();

    for (const QString& fname : jsonFiles) {
        const QString full = dir.absoluteFilePath(fname);
        auto opt = tankoban::manga::fandom::LocalFandomCatalogLoader::loadFromFile(full);
        if (!opt) continue;
        addTile(*opt);
    }
}

void ComicsCatalogScreen::addTile(const tankoban::manga::fandom::FandomCatalog& catalog)
{
    if (catalog.seriesId.isEmpty()) return;

    // Derive a display title from the seriesId slug ("one-piece" → "One Piece").
    // FandomCatalog has no top-level seriesTitle field; slug-capitalisation is
    // good enough for v1.
    QString title = catalog.seriesId;
    title.replace('-', ' ');
    QStringList words = title.split(' ', Qt::SkipEmptyParts);
    for (auto& w : words) {
        if (!w.isEmpty()) w[0] = w[0].toUpper();
    }
    title = words.join(' ');

    // Subtitle: volume count.
    const QString subtitle = tr("%1 volumes").arg(catalog.volumes.size());

    auto* tile = new TileCard(QString{}, title, subtitle, m_gridHost);
    // Catalog grid tiles use Stream-mode default dimensions (200×308) — not the
    // volume-row cover size from feedback_bigger_manga_covers.md (110×150), which
    // only applies inside the series detail view.
    tile->setCardSize(TileCard::DEFAULT_WIDTH, TileCard::DEFAULT_IMAGE_HEIGHT);
    tile->setBadges(/*progressFraction=*/0.0, /*pageBadge=*/QString(),
                    /*countBadge=*/subtitle, /*status=*/QString());
    tile->setProvenance(QStringLiteral("Fandom"));
    connect(tile, &TileCard::clicked, this, [this, tile, seriesId = catalog.seriesId, title]() {
        emit seriesActivated(seriesId, title, 0);
    });
    m_tiles.append(tile);

    // Grid placement: 6 columns per task spec. addTile owns placement so
    // loadAllCatalogs stays a pure "load + call addTile" loop.
    constexpr int kCols = 6;
    const int idx = m_tiles.size() - 1;
    m_grid->addWidget(tile, idx / kCols, idx % kCols);

    // Kick off cover fetch for vol 1 — prefer Japanese, fall back to English.
    if (!catalog.volumes.isEmpty()) {
        const auto& v1 = catalog.volumes.first();
        const QString coverUrl = !v1.coverUrlJapanese.isEmpty()
                                  ? v1.coverUrlJapanese
                                  : v1.coverUrlEnglish;
        if (!coverUrl.isEmpty() && m_nam) {
            fetchCover(tile, coverUrl);
        }
    }
}

void ComicsCatalogScreen::fetchCover(TileCard* tile, const QString& url)
{
    QNetworkRequest req((QUrl(url)));
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QByteArray("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                             "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"));
    QNetworkReply* reply = m_nam->get(req);
    QPointer<TileCard> tilePtr(tile);
    connect(reply, &QNetworkReply::finished, this, [reply, tilePtr]() {
        reply->deleteLater();
        if (!tilePtr) return;
        if (reply->error() != QNetworkReply::NoError) return;
        QPixmap pm;
        const QByteArray bytes = reply->readAll();
        if (!pm.loadFromData(bytes)) return;
        tilePtr->setThumbPixmap(pm);
    });
}

} // namespace tankoban::ui::comics
