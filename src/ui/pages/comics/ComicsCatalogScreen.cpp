// src/ui/pages/comics/ComicsCatalogScreen.cpp
// See header for design notes.

#include "ComicsCatalogScreen.h"

#include "core/manga/fandom/LocalFandomCatalogLoader.h"
#include "core/manga/fandom/FandomTypes.h"
#include "ui/pages/TileCard.h"

#include <algorithm>
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
    clearTiles();

    const QString dataDir = tankoban::manga::fandom::LocalFandomCatalogLoader::canonicalDataDir();
    QDir dir(dataDir);
    if (!dir.exists()) {
        m_emptyLabel->show();
        return;
    }

    QFileInfoList files = dir.entryInfoList(QStringList() << "*.json", QDir::Files | QDir::Readable);

    // Drop any file whose stem ends in GROUND_TRUTH (dev backup).
    files.erase(std::remove_if(files.begin(), files.end(),
                               [](const QFileInfo& fi) {
                                   return fi.fileName().contains("GROUND_TRUTH", Qt::CaseInsensitive);
                               }),
                files.end());

    // Pass 1: load + filter (empty-volumes stubs skipped)
    QList<tankoban::manga::fandom::FandomCatalog> catalogs;
    catalogs.reserve(files.size());
    for (const QFileInfo& fi : files) {
        const auto loaded = tankoban::manga::fandom::LocalFandomCatalogLoader::loadFromFile(fi.absoluteFilePath());
        if (!loaded.has_value()) continue;
        if (loaded->volumes.isEmpty()) continue;
        catalogs.append(*loaded);
    }

    if (catalogs.isEmpty()) {
        m_emptyLabel->show();
        return;
    }
    m_emptyLabel->hide();

    // Pass 2: sort by seriesTitle (case-insensitive), falling back to seriesId
    // slug when title is empty (legacy or incomplete catalogs).
    std::sort(catalogs.begin(), catalogs.end(),
              [](const tankoban::manga::fandom::FandomCatalog& a,
                 const tankoban::manga::fandom::FandomCatalog& b) {
                  const QString& ka = a.seriesTitle.isEmpty() ? a.seriesId : a.seriesTitle;
                  const QString& kb = b.seriesTitle.isEmpty() ? b.seriesId : b.seriesTitle;
                  return ka.compare(kb, Qt::CaseInsensitive) < 0;
              });

    // Pass 3: paint
    for (const auto& cat : catalogs) {
        addTile(cat);
    }
}

void ComicsCatalogScreen::addTile(const tankoban::manga::fandom::FandomCatalog& catalog)
{
    if (catalog.seriesId.isEmpty()) return;

    // Prefer the human-readable seriesTitle from JSON; fall back to seriesId
    // slug if the catalog predates the title field or has it empty.
    const QString title = !catalog.seriesTitle.isEmpty()
                           ? catalog.seriesTitle
                           : catalog.seriesId;

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
