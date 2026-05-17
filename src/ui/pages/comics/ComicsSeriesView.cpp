// src/ui/pages/comics/ComicsSeriesView.cpp
#include "ComicsSeriesView.h"

#include "core/manga/anilist/AniListCache.h"
#include "core/manga/anilist/AniListClient.h"
#include "core/manga/anilist/AniListVolumeMapper.h"
#include "core/manga/MangaDownloadIndex.h"
#include "core/manga/PremiumCatalog.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMouseEvent>
#include <QPixmap>
#include <QPixmapCache>
#include <QPointer>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSize>
#include <QSizePolicy>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <Qt>

namespace tankoban::manga::comics {

namespace {

constexpr const char* kPremiumSourceId = "tankoyomi_premium";
constexpr const char* kWeebCentralSourceId = "weebcentral";

// Humanize the AniList status enum string into a UI-facing label. AniList
// emits "RELEASING" / "FINISHED" / "HIATUS" / "CANCELLED" / "NOT_YET_RELEASED";
// we render lowercase-friendly equivalents matching the Stream-blueprint
// detail surface vocabulary.
QString humanizeStatus(const QString& raw)
{
    if (raw == QStringLiteral("RELEASING"))         return QStringLiteral("Ongoing");
    if (raw == QStringLiteral("FINISHED"))          return QStringLiteral("Completed");
    if (raw == QStringLiteral("HIATUS"))            return QStringLiteral("On hiatus");
    if (raw == QStringLiteral("CANCELLED"))         return QStringLiteral("Cancelled");
    if (raw == QStringLiteral("NOT_YET_RELEASED"))  return QStringLiteral("Upcoming");
    return raw; // unknown enum -- show the raw token; better than a blank
}

// Humanize the AniList format enum string.
QString humanizeFormat(const QString& raw)
{
    if (raw == QStringLiteral("MANGA"))     return QStringLiteral("Manga");
    if (raw == QStringLiteral("MANHWA"))    return QStringLiteral("Manhwa");
    if (raw == QStringLiteral("MANHUA"))    return QStringLiteral("Manhua");
    if (raw == QStringLiteral("ONE_SHOT"))  return QStringLiteral("One-shot");
    if (raw == QStringLiteral("NOVEL"))     return QStringLiteral("Novel");
    return raw;
}

// Build the small meta-line shown under the title in the hero pane. Uses
// only the fields available on MediaPreview so it can paint instantly
// before the detail fetch returns.
QString buildPreviewMetaLine(const anilist::MediaPreview& preview)
{
    QStringList parts;
    if (!preview.format.isEmpty())   parts << humanizeFormat(preview.format);
    if (preview.yearStarted > 0)     parts << QString::number(preview.yearStarted);
    if (!preview.status.isEmpty())   parts << humanizeStatus(preview.status);
    return parts.join(QStringLiteral("  -  "));
}

// Build the richer meta-line shown after the detail fetch returns. Adds
// the totalVolumes / totalChapters tail when known.
QString buildDetailMetaLine(const anilist::MediaDetail& detail)
{
    QStringList parts;
    if (!detail.preview.format.isEmpty())   parts << humanizeFormat(detail.preview.format);
    if (detail.preview.yearStarted > 0)     parts << QString::number(detail.preview.yearStarted);
    if (!detail.preview.status.isEmpty())   parts << humanizeStatus(detail.preview.status);
    if (detail.totalVolumes > 0)            parts << QString::number(detail.totalVolumes) + QStringLiteral(" volumes");
    if (detail.totalChapters > 0)           parts << QString::number(detail.totalChapters) + QStringLiteral(" chapters");
    return parts.join(QStringLiteral("  -  "));
}

// Format the chapter-range column ("Chs 1-7 (7 ch)"). Defensive against
// zero-range data (which the mapper can emit for empty volumes).
// Bug 3 fix: AniList's `description(asHtml: false)` GraphQL parameter is
// supposed to strip HTML but historically still leaves <br> tags in the
// description text for many series (verified in cached series_30013.json
// for One Piece). QLabel default textFormat=Qt::AutoText does not
// auto-detect bare <br> as RichText markers, so they render as literal
// characters. Strip them client-side before setText.
QString stripDescriptionHtml(const QString& raw)
{
    static const QRegularExpression brRe(
        QStringLiteral("<\\s*br\\s*/?\\s*>"),
        QRegularExpression::CaseInsensitiveOption);
    QString out = raw;
    out.replace(brRe, QStringLiteral("\n"));
    return out;
}

QString formatChapterRange(const anilist::VolumeRow& row)
{
    if (row.chapterCount <= 0) {
        // Bug 4 fix: placeholder Vol X (synthesized when AniList returns
        // null totalChapters/totalVolumes for long-running ongoing series)
        // gets an explanatory subtitle instead of a bare "--".
        if (row.isVolumeX) {
            return QStringLiteral("Listing unavailable");
        }
        return QStringLiteral("--");
    }
    const QString range = (row.chapterRangeStart == row.chapterRangeEnd)
        ? QString::number(row.chapterRangeStart)
        : QString::number(row.chapterRangeStart) + QStringLiteral("-") + QString::number(row.chapterRangeEnd);
    return QStringLiteral("Chs %1 (%2 ch)").arg(range).arg(row.chapterCount);
}

} // namespace

ComicsSeriesView::ComicsSeriesView(anilist::AniListClient*  client,
                                   anilist::AniListCache*   cache,
                                   premium::PremiumCatalog* catalog,
                                   NyaaRuntimeSource*       nyaa,
                                   MangaDownloadIndex*      downloadIndex,
                                   QWidget*                 parent)
    : QWidget(parent)
    , m_client(client)
    , m_cache(cache)
    , m_catalog(catalog)
    , m_nyaa(nyaa)
    , m_downloadIndex(downloadIndex)
{
    setObjectName(QStringLiteral("ComicsSeriesView"));

    buildUi();

    // The client may emit from a different thread under future v2 throttling;
    // use QueuedConnection so the slot always lands on the UI thread.
    if (m_client) {
        connect(m_client, &anilist::AniListClient::seriesSucceeded,
                this,     &ComicsSeriesView::onSeriesSucceeded,
                Qt::QueuedConnection);
        connect(m_client, &anilist::AniListClient::seriesFailed,
                this,     &ComicsSeriesView::onSeriesFailed,
                Qt::QueuedConnection);
    }

    // PHASE 8/B.2: row-click on a not-downloaded volume populates the
    // sources panel; row-click on a downloaded volume opens its cbz path.
    // cellClicked fires for any cell hit, including the col-6 chevron.
    if (m_volumesTable) {
        connect(m_volumesTable, &QTableWidget::cellClicked,
                this,           &ComicsSeriesView::onVolumeCellClicked);
    }

    // PHASE 8: forward the sources panel's downloadRequested verbatim via
    // ComicsSeriesView's own downloadDispatchRequested signal so Phase 9
    // ComicsPage can wire the actual provider dispatch.
    if (m_sourcesPanel) {
        connect(m_sourcesPanel, &ComicsSourcesPanel::downloadRequested,
                this,           &ComicsSeriesView::downloadDispatchRequested);
    }

    if (m_libraryButton) {
        m_libraryButton->installEventFilter(this);
        connect(m_libraryButton, &QPushButton::clicked,
                this, &ComicsSeriesView::onLibraryButtonClicked);
    }
    if (m_cache) {
        connect(m_cache, &anilist::AniListCache::bookmarksChanged,
                this, &ComicsSeriesView::refreshLibraryButton,
                Qt::QueuedConnection);
    }
}

ComicsSeriesView::~ComicsSeriesView() = default;

void ComicsSeriesView::buildUi()
{
    // Stremio-style: the banner is no longer a docked top pane -- it
    // becomes the FULL-VIEWPORT WALLPAPER painted in paintEvent below the
    // child widgets. Outer margins give the wallpaper room to breathe at
    // the top before the title text covers it.
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 32, 24, 24);
    outer->setSpacing(16);

    // Stremio-style backdrop QSS:
    //   - Root widget transparent so paintEvent's pixmap+gradient show
    //   - Text labels transparent + light foreground for readability over
    //     the darkened banner
    //   - Volume table + Sources panel get card-style dark backdrops with
    //     subtle borders + rounded corners (mimics Stremio's right-side
    //     episode-list card)
    setStyleSheet(QStringLiteral(
        "ComicsSeriesView { background: transparent; }"
        "QLabel#ComicsSeriesTitle {"
        "  color: #ffffff;"
        "  background: transparent;"
        "}"
        "QLabel#ComicsSeriesMetaLine {"
        "  color: rgba(255, 255, 255, 0.7);"
        "  background: transparent;"
        "}"
        "QLabel#ComicsSeriesSynopsis {"
        "  color: rgba(255, 255, 255, 0.85);"
        "  background: transparent;"
        "}"
        "QTableWidget#ComicsSeriesVolumesTable {"
        "  background-color: rgba(15, 15, 18, 0.88);"
        "  border: 1px solid rgba(255, 255, 255, 0.08);"
        "  border-radius: 8px;"
        "  gridline-color: rgba(255, 255, 255, 0.05);"
        "  color: #e5e7eb;"
        "}"
        "QTableWidget#ComicsSeriesVolumesTable::item {"
        "  background: transparent;"
        "  color: #e5e7eb;"
        "  padding: 4px 8px;"
        "}"
        "QTableWidget#ComicsSeriesVolumesTable::item:alternate {"
        "  background-color: rgba(255, 255, 255, 0.03);"
        "}"
        "QTableWidget#ComicsSeriesVolumesTable QHeaderView::section {"
        "  background-color: rgba(20, 20, 24, 0.95);"
        "  color: rgba(255, 255, 255, 0.65);"
        "  border: none;"
        "  padding: 8px 10px;"
        "  font-weight: 600;"
        "}"
        "ComicsSourcesPanel {"
        "  background-color: rgba(15, 15, 18, 0.88);"
        "  border: 1px solid rgba(255, 255, 255, 0.08);"
        "  border-radius: 8px;"
        "}"
    ));

    // --- Hero + sources row -----------------------------------------------
    auto* heroRow = new QHBoxLayout();
    heroRow->setContentsMargins(16, 0, 16, 0);
    heroRow->setSpacing(16);

    // Hero left column (title / meta / synopsis), ~70% width.
    auto* heroLeft = new QVBoxLayout();
    heroLeft->setSpacing(8);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("ComicsSeriesTitle"));
    {
        QFont f = m_title->font();
        f.setPointSize(24);
        f.setBold(true);
        m_title->setFont(f);
    }
    m_title->setWordWrap(true);

    auto* titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);
    titleRow->addWidget(m_title, /*stretch*/ 1);

    m_libraryButton = new QPushButton(this);
    m_libraryButton->setObjectName(QStringLiteral("ComicsSeriesLibraryButton"));
    m_libraryButton->setAccessibleName(QStringLiteral("ComicsSeriesLibraryButton"));
    m_libraryButton->setAccessibleDescription(QStringLiteral("Add or remove this series from the Comics library."));
    m_libraryButton->setFixedHeight(32);
    m_libraryButton->setCursor(Qt::PointingHandCursor);
    m_libraryButton->setStyleSheet(QStringLiteral(
        "QPushButton#ComicsSeriesLibraryButton {"
        "  background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.18);"
        "  border-radius: 6px;"
        "  color: #ddd;"
        "  padding: 4px 12px;"
        "  font-size: 12px;"
        "}"
        "QPushButton#ComicsSeriesLibraryButton:hover {"
        "  background: rgba(255,255,255,0.12);"
        "  border-color: rgba(255,255,255,0.28);"
        "}"
        "QPushButton#ComicsSeriesLibraryButton:disabled {"
        "  color: #777;"
        "  border-color: rgba(255,255,255,0.10);"
        "}"));
    titleRow->addWidget(m_libraryButton, /*stretch*/ 0, Qt::AlignTop);
    heroLeft->addLayout(titleRow);

    m_metaLine = new QLabel(this);
    m_metaLine->setObjectName(QStringLiteral("ComicsSeriesMetaLine"));
    {
        QFont f = m_metaLine->font();
        f.setPointSize(12);
        m_metaLine->setFont(f);
    }
    // (color is handled by the root QSS sheet -- QLabel#ComicsSeriesMetaLine)
    heroLeft->addWidget(m_metaLine);

    m_synopsis = new QLabel(this);
    m_synopsis->setObjectName(QStringLiteral("ComicsSeriesSynopsis"));
    {
        QFont f = m_synopsis->font();
        f.setPointSize(12);
        m_synopsis->setFont(f);
    }
    m_synopsis->setWordWrap(true);
    m_synopsis->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_synopsis->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
    heroLeft->addWidget(m_synopsis, /*stretch*/ 1);

    auto* heroLeftWrap = new QWidget(this);
    heroLeftWrap->setLayout(heroLeft);
    heroRow->addWidget(heroLeftWrap, /*stretch*/ 7);

    // Sources panel (right column), ~30% width.
    // PHASE 8: replaces the Phase 7 placeholder QLabel. Panel renders its
    // own empty-state "Select a volume to see sources" internally when
    // m_rows is empty.
    m_sourcesPanel = new ComicsSourcesPanel(m_catalog, m_nyaa, this);
    heroRow->addWidget(m_sourcesPanel, /*stretch*/ 3);

    outer->addLayout(heroRow);

    // --- Volume list table -------------------------------------------------
    m_volumesTable = new QTableWidget(this);
    m_volumesTable->setObjectName(QStringLiteral("ComicsSeriesVolumesTable"));
    const QStringList headers = {
        tr("#"), tr("Cover"), tr("Volume"), tr("Chapters"),
        tr("Progress"), tr("Status"), tr("Open")
    };
    m_volumesTable->setColumnCount(headers.size());
    m_volumesTable->setHorizontalHeaderLabels(headers);
    m_volumesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_volumesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_volumesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_volumesTable->setShowGrid(false);
    m_volumesTable->setAlternatingRowColors(true);
    m_volumesTable->verticalHeader()->setVisible(false);
    m_volumesTable->horizontalHeader()->setStretchLastSection(false);
    // Bug 1 fix: explicit pixel column widths matching StreamDetailView's
    // episode-table pattern (StreamDetailView.cpp:626-639). Stretch only the
    // Volume column so the Open affordance stays compact on the right edge.
    auto* hdr = m_volumesTable->horizontalHeader();
    hdr->setSectionResizeMode(0, QHeaderView::Fixed);    // #
    hdr->setSectionResizeMode(1, QHeaderView::Fixed);    // Cover
    hdr->setSectionResizeMode(2, QHeaderView::Stretch);  // Volume (stretch)
    hdr->setSectionResizeMode(3, QHeaderView::Fixed);    // Chapters
    hdr->setSectionResizeMode(4, QHeaderView::Fixed);    // Progress
    hdr->setSectionResizeMode(5, QHeaderView::Fixed);    // Status
    hdr->setSectionResizeMode(6, QHeaderView::Fixed);    // Open
    m_volumesTable->setColumnWidth(0, 36);
    m_volumesTable->setColumnWidth(1, 76);   // 64px cover + 12px padding
    m_volumesTable->setColumnWidth(3, 140);
    m_volumesTable->setColumnWidth(4, 80);
    m_volumesTable->setColumnWidth(5, 120);
    m_volumesTable->setColumnWidth(6, 36);
    // PHASE 12: enlarge the cover-column icon size + row height so the
    // loaded AniList thumbnails (48x64 px) render at intended scale.
    // Row height matches StreamDetailView.cpp:639 (kRowHeight = 64).
    m_volumesTable->setIconSize(QSize(48, 64));
    m_volumesTable->verticalHeader()->setDefaultSectionSize(64);

    outer->addWidget(m_volumesTable, /*stretch*/ 1);
}

void ComicsSeriesView::showSeries(const anilist::MediaPreview& preview)
{
    m_currentAnilistId   = preview.anilistId;
    m_currentSeriesTitle = preview.title;
    m_currentVolumeRows.clear();
    if (m_sourcesPanel) m_sourcesPanel->clear();

    // Paint immediately from preview (cheap data, no detail required for hero).
    m_title->setText(preview.title);
    m_synopsis->setText(stripDescriptionHtml(preview.description));
    m_metaLine->setText(buildPreviewMetaLine(preview));
    m_bannerPixmap = QPixmap();
    update();  // clear stale banner wallpaper from prior series
    m_volumesTable->setRowCount(0);
    refreshLibraryButton();

    // PHASE 12: kick off banner async-load from preview (renderDetail will
    // re-fire with detail.preview.bannerUrl after the cache hit / refetch
    // lands; the URL identity makes QPixmapCache a synchronous-hit second time).
    const QString previewBannerUrl = !preview.bannerUrl.isEmpty()
        ? preview.bannerUrl
        : preview.coverFullUrl;
    if (!previewBannerUrl.isEmpty()) {
        loadBannerUrl(previewBannerUrl);
    }

    // Cache first: render whatever we have on disk, even if stale.
    if (m_cache) {
        if (auto cached = m_cache->get(preview.anilistId); cached.has_value()) {
            renderDetail(*cached);
        }
    }

    // Background refetch always fires; staleness check is the cache layer's
    // job in v2. v1 keeps the contract simple: refetch every showSeries().
    if (m_client) {
        m_pendingSeriesReqId = m_nextRequestId++;
        m_client->seriesById(preview.anilistId, m_pendingSeriesReqId);
    }
}

void ComicsSeriesView::clearView()
{
    m_currentAnilistId   = 0;
    m_pendingSeriesReqId = -1;
    m_currentSeriesTitle.clear();
    m_currentVolumeRows.clear();

    m_title->clear();
    m_metaLine->clear();
    m_synopsis->clear();
    m_bannerPixmap = QPixmap();
    update();  // clear stale banner wallpaper
    m_volumesTable->setRowCount(0);
    if (m_sourcesPanel) m_sourcesPanel->clear();
    refreshLibraryButton();
}

void ComicsSeriesView::onSeriesSucceeded(int requestId,
                                         const anilist::MediaDetail& detail)
{
    // Stale-request guard: only honor the most-recent pending fetch. Older
    // in-flight requests from a previous showSeries() call get dropped.
    if (requestId != m_pendingSeriesReqId) {
        return;
    }
    m_pendingSeriesReqId = -1;

    // Defense against the AniList API returning a detail for a different
    // anilistId than the one we are currently displaying (shouldn't happen,
    // but the requestId tag is the only stable correlation).
    if (detail.preview.anilistId != 0 &&
        detail.preview.anilistId != m_currentAnilistId) {
        return;
    }

    renderDetail(detail);

    // Persist for next app launch (Phase 2 cache integration).
    if (m_cache) {
        m_cache->put(detail);
    }
}

void ComicsSeriesView::onSeriesFailed(int requestId, const QString& reason)
{
    if (requestId != m_pendingSeriesReqId) {
        return;
    }
    m_pendingSeriesReqId = -1;

    // If we already painted a cached detail, leave the view in place. If
    // not, surface the failure reason in the synopsis pane as the cheapest
    // feedback channel.
    qWarning("ComicsSeriesView: seriesFailed(reqId=%d) reason=%s",
             requestId, reason.toUtf8().constData());

    if (m_volumesTable->rowCount() == 0) {
        renderEmpty(reason);
    }
}

void ComicsSeriesView::renderDetail(const anilist::MediaDetail& detail)
{
    // Refresh the hero pane with the richer detail data.
    if (!detail.preview.title.isEmpty()) {
        m_title->setText(detail.preview.title);
    }
    if (!detail.preview.description.isEmpty()) {
        m_synopsis->setText(stripDescriptionHtml(detail.preview.description));
    }
    m_metaLine->setText(buildDetailMetaLine(detail));

    populateVolumeRows(anilist::AniListVolumeMapper::map(detail), &detail);

    // PHASE 12: async-load banner. Prefer preview.bannerUrl; fall back to
    // coverFullUrl (stretched) when the AniList Media has no banner.
    const QString bannerUrl = !detail.preview.bannerUrl.isEmpty()
        ? detail.preview.bannerUrl
        : detail.preview.coverFullUrl;
    if (!bannerUrl.isEmpty()) {
        loadBannerUrl(bannerUrl);
    }
}

void ComicsSeriesView::populateVolumeRows(const QList<anilist::VolumeRow>& rows,
                                          const anilist::MediaDetail* detail)
{
    // PHASE 8: cache the mapped rows so onVolumeCellClicked can hand the
    // full VolumeRow to the sources panel without rerunning the mapper.
    m_currentVolumeRows = rows;
    // Also stash the canonical title for the panel populate() call.
    if (detail && !detail->preview.title.isEmpty()) {
        m_currentSeriesTitle = detail->preview.title;
    }

    m_volumesTable->setRowCount(rows.size());

    for (int i = 0; i < rows.size(); ++i) {
        const auto& row = rows.at(i);

        // Col 0 -- #
        QString indexText;
        if (row.isVolumeX) {
            indexText = QStringLiteral("X");
        } else {
            indexText = QString::number(row.volumeNumber);
        }
        auto* indexItem = new QTableWidgetItem(indexText);
        indexItem->setTextAlignment(Qt::AlignCenter);
        // PHASE 8: stash chapterIds (mapped row's chapterNumbers) per row in
        // the col-0 item via UserRole so onVolumeCellClicked can retrieve
        // them when populating the sources panel. The chapterNumbers
        // QStringList preserves "12.5"-style half-chapters per
        // AniListTypes.h:64.
        indexItem->setData(Qt::UserRole, row.chapterNumbers);
        m_volumesTable->setItem(i, 0, indexItem);

        // Col 1 -- Cover. PHASE 12 three-stage resolution:
        //   Stage 1: row.art.thumbnailUrl when non-empty (per-vol AniList art)
        //   Stage 2: detail.preview.coverThumbUrl when no per-vol art
        //   Stage 3: setVolumeCoverFromDisk replaces with cbz-extracted cover
        //            once Phase 10 extractor fires (provider/packer signal).
        auto* coverItem = new QTableWidgetItem();
        m_volumesTable->setItem(i, 1, coverItem);
        const QString coverUrl = detail
            ? (!row.art.thumbnailUrl.isEmpty()
                ? row.art.thumbnailUrl
                : detail->preview.coverThumbUrl)
            : QString();
        if (!coverUrl.isEmpty()) {
            loadCoverUrlForVolume(coverUrl, row.volumeNumber);
        }

        // Col 2 -- Volume label
        const QString volLabel = row.isVolumeX
            ? QStringLiteral("Volume X")
            : QStringLiteral("Volume %1").arg(row.volumeNumber);
        m_volumesTable->setItem(i, 2, new QTableWidgetItem(volLabel));

        // Col 3 -- Chapters
        m_volumesTable->setItem(i, 3, new QTableWidgetItem(formatChapterRange(row)));

        // Col 4 -- Progress (empty placeholder)
        // PHASE 10: pull per-volume progress from MangaDownloadIndex.
        m_volumesTable->setItem(i, 4, new QTableWidgetItem(QString()));

        // Col 5 -- Status (hardcoded "Not downloaded")
        // PHASE 10: MangaDownloadIndex lookup determines per-volume status.
        auto* statusItem = new QTableWidgetItem(tr("Not downloaded"));
        statusItem->setForeground(QBrush(QColor(0x88, 0x88, 0x88)));
        m_volumesTable->setItem(i, 5, statusItem);

        // Col 6 -- downloaded-state open affordance. Empty while the volume
        // is not on disk; setVolumeDownloadState/render-time index lookup
        // swaps in a chevron when a cbz path is available.
        m_volumesTable->setItem(i, 6, new QTableWidgetItem());
        setRowOpenIndicator(i, false);

        std::optional<MangaDownloadIndex::Entry> downloaded;
        if (m_downloadIndex && !row.isVolumeX) {
            if (m_catalog) {
                if (auto hit = m_catalog->entryForAnilistIdAndVolume(m_currentAnilistId,
                                                                      row.volumeNumber)) {
                    downloaded = m_downloadIndex->entryForSeriesAndVolume(
                        QString::fromLatin1(kPremiumSourceId),
                        hit->first.seriesId,
                        row.volumeNumber);
                }
            }
            if (!downloaded) {
                const QString fallbackSeriesId =
                    QStringLiteral("anilist_%1").arg(m_currentAnilistId);
                downloaded = m_downloadIndex->entryForSeriesAndVolume(
                    QString::fromLatin1(kPremiumSourceId),
                    fallbackSeriesId,
                    row.volumeNumber);
                if (!downloaded) {
                    downloaded = m_downloadIndex->entryForSeriesAndVolume(
                        QString::fromLatin1(kWeebCentralSourceId),
                        fallbackSeriesId,
                        row.volumeNumber);
                }
            }
        }
        if (downloaded) {
            setVolumeDownloadState(row.volumeNumber, downloaded->canonicalPath, true);
        }
    }
}

void ComicsSeriesView::setVolumeRows(const QList<anilist::VolumeRow>& rows)
{
    std::optional<anilist::MediaDetail> detail;
    if (m_cache && m_currentAnilistId > 0) {
        detail = m_cache->get(m_currentAnilistId);
    }
    populateVolumeRows(rows, detail ? &(*detail) : nullptr);
}

void ComicsSeriesView::renderEmpty(const QString& reason)
{
    m_volumesTable->setRowCount(0);
    m_currentVolumeRows.clear();
    if (m_sourcesPanel) m_sourcesPanel->clear();
    if (!reason.isEmpty()) {
        m_synopsis->setText(tr("No data available: %1").arg(reason));
    } else {
        m_synopsis->setText(tr("No data available"));
    }
    m_metaLine->clear();
}

void ComicsSeriesView::refreshLibraryButton()
{
    if (!m_libraryButton) return;
    const bool hasSeries = (m_currentAnilistId > 0);
    const bool bookmarked = hasSeries && m_cache && m_cache->isBookmarked(m_currentAnilistId);
    m_libraryButton->setEnabled(hasSeries && m_cache);
    m_libraryButton->setText(bookmarked ? tr("In library") : tr("Add to library"));
}

void ComicsSeriesView::onLibraryButtonClicked()
{
    if (!m_cache || m_currentAnilistId <= 0) return;
    if (m_cache->isBookmarked(m_currentAnilistId)) {
        m_cache->removeBookmark(m_currentAnilistId);
    } else {
        m_cache->addBookmark(m_currentAnilistId);
    }
    refreshLibraryButton();
}

bool ComicsSeriesView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_libraryButton && m_libraryButton) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_libraryButtonSawPress = true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                const bool sawPress = m_libraryButtonSawPress;
                m_libraryButtonSawPress = false;
                if (!sawPress
                    && m_libraryButton->isEnabled()
                    && m_libraryButton->rect().contains(mouse->position().toPoint())) {
                    m_libraryButton->click();
                    event->accept();
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

// -----------------------------------------------------------------------
// PHASE 12 -- Three-stage cover resolution.
// -----------------------------------------------------------------------
// Stage 1: AniList per-vol art (volumeRow.art.thumbnailUrl) when non-empty.
// Stage 2: Series cover fallback (detail.preview.coverThumbUrl) when no
//          per-vol art.
// Stage 3: Post-download cbz-extracted cover via setVolumeCoverFromDisk
//          replaces the AniList-loaded thumb.
//
// Lazy-load via QNetworkAccessManager (borrowed from AniListClient) +
// QPixmapCache keyed by URL. Cache hits paint synchronously inside
// loadCoverUrlForVolume; cache misses fire a GET and paint in the
// finished-lambda when the reply lands.
// -----------------------------------------------------------------------

void ComicsSeriesView::loadCoverUrlForVolume(const QString& url, int volumeNumber)
{
    if (url.isEmpty() || volumeNumber == 0) return;

    QPixmap cached;
    if (QPixmapCache::find(url, &cached)) {
        applyPixmapToVolumeRow(volumeNumber, cached);
        return;
    }

    QNetworkAccessManager* nam = m_client ? m_client->networkManager() : nullptr;
    if (!nam) return;

    QPointer<ComicsSeriesView> self(this);
    const int snapshotAnilistId = m_currentAnilistId;
    QNetworkReply* reply = nam->get(QNetworkRequest(QUrl(url)));
    connect(reply, &QNetworkReply::finished, this,
            [self, reply, url, volumeNumber, snapshotAnilistId]() {
        reply->deleteLater();
        if (!self) return;
        // Stale-series guard: discard if user navigated away during the fetch.
        if (self->m_currentAnilistId != snapshotAnilistId) return;
        if (reply->error() != QNetworkReply::NoError) return;
        const QByteArray data = reply->readAll();
        QPixmap pm;
        if (!pm.loadFromData(data)) return;
        QPixmapCache::insert(url, pm);
        self->applyPixmapToVolumeRow(volumeNumber, pm);
    });
}

void ComicsSeriesView::loadBannerUrl(const QString& url)
{
    if (url.isEmpty()) return;

    QPixmap cached;
    if (QPixmapCache::find(url, &cached)) {
        m_bannerPixmap = cached;
        update();
        return;
    }

    QNetworkAccessManager* nam = m_client ? m_client->networkManager() : nullptr;
    if (!nam) return;

    QPointer<ComicsSeriesView> self(this);
    const int snapshotAnilistId = m_currentAnilistId;
    QNetworkReply* reply = nam->get(QNetworkRequest(QUrl(url)));
    connect(reply, &QNetworkReply::finished, this,
            [self, reply, url, snapshotAnilistId]() {
        reply->deleteLater();
        if (!self) return;
        if (self->m_currentAnilistId != snapshotAnilistId) return;
        if (reply->error() != QNetworkReply::NoError) return;
        const QByteArray data = reply->readAll();
        QPixmap pm;
        if (!pm.loadFromData(data)) return;
        QPixmapCache::insert(url, pm);
        self->m_bannerPixmap = pm;
        self->update();
    });
}

// Stremio-style background painter.
// Stage 1: dark base fill (always visible while banner loads or for series
//          with no banner art).
// Stage 2: stretched banner pixmap, center-cropped via KeepAspectRatioByExpanding.
// Stage 3: vertical gradient overlay -- lighter at the top so banner art is
//          visible, near-solid dark at the bottom where text + table sit.
void ComicsSeriesView::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    const QRect r = rect();

    p.fillRect(r, QColor(18, 18, 22));

    if (!m_bannerPixmap.isNull()) {
        const QPixmap scaled = m_bannerPixmap.scaled(
            r.size(),
            Qt::KeepAspectRatioByExpanding,
            Qt::SmoothTransformation);
        const int xOff = (r.width()  - scaled.width())  / 2;
        const int yOff = (r.height() - scaled.height()) / 2;
        p.drawPixmap(xOff, yOff, scaled);
    }

    QLinearGradient grad(0, 0, 0, r.height());
    grad.setColorAt(0.0, QColor(0, 0, 0, 60));
    grad.setColorAt(0.3, QColor(0, 0, 0, 130));
    grad.setColorAt(0.6, QColor(0, 0, 0, 200));
    grad.setColorAt(1.0, QColor(18, 18, 22, 245));
    p.fillRect(r, grad);
}

void ComicsSeriesView::applyPixmapToVolumeRow(int volumeNumber, const QPixmap& pm)
{
    if (!m_volumesTable || pm.isNull()) return;
    // Walk m_currentVolumeRows to find the matching row index, then mutate
    // the Cover cell's DecorationRole. We match by volumeNumber (incl.
    // sentinel kVolumeXNumber for Vol X).
    for (int i = 0; i < m_currentVolumeRows.size() && i < m_volumesTable->rowCount(); ++i) {
        if (m_currentVolumeRows.at(i).volumeNumber == volumeNumber) {
            if (QTableWidgetItem* coverItem = m_volumesTable->item(i, /*col=*/1)) {
                const QSize thumb(48, 64);
                const QPixmap scaled = pm.scaled(thumb, Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation);
                coverItem->setData(Qt::DecorationRole, QIcon(scaled));
            }
            // First match wins (volumeNumber is unique within m_currentVolumeRows).
            break;
        }
    }
}

void ComicsSeriesView::setVolumeCoverFromDisk(const QString& seriesId, int volumeNumber,
                                              const QString& coverPath)
{
    // Stale-series guard: seriesId may be a real catalog seriesId (e.g.
    // "death-note") OR a synthesized "anilist_<N>" slug. Parse the slug
    // prefix and compare against m_currentAnilistId; if seriesId is not a
    // recognized slug shape, fall through and apply unconditionally (cheaper
    // than maintaining a per-view catalog-id map; the worst case is a stale
    // event painting a stray row, which is recoverable on next renderDetail).
    if (seriesId.startsWith(QStringLiteral("anilist_"))) {
        bool ok = false;
        const int parsed = QStringView(seriesId).mid(8).toInt(&ok);
        if (ok && parsed != m_currentAnilistId) {
            return;
        }
    }

    if (coverPath.isEmpty() || !QFileInfo(coverPath).exists()) return;
    QPixmap pm(coverPath);
    if (pm.isNull()) return;

    // Stage 3: replace the AniList thumb with the cbz-extracted cover. We
    // intentionally do NOT cache disk-loaded paths in QPixmapCache (URL keys
    // only; the URL cache is for network-fetched images). Re-renders pick up
    // the disk file directly when this slot fires again.
    applyPixmapToVolumeRow(volumeNumber, pm);
}

void ComicsSeriesView::setVolumeDownloadState(int volumeNumber, const QString& cbzPath,
                                              bool downloaded)
{
    if (!m_volumesTable) return;
    for (int i = 0; i < m_currentVolumeRows.size() && i < m_volumesTable->rowCount(); ++i) {
        if (m_currentVolumeRows.at(i).volumeNumber != volumeNumber) continue;
        if (auto* indexItem = m_volumesTable->item(i, 0)) {
            indexItem->setData(Qt::UserRole + 1, downloaded ? cbzPath : QString());
        }
        if (auto* statusItem = m_volumesTable->item(i, 5)) {
            statusItem->setText(downloaded ? tr("Downloaded") : tr("Not downloaded"));
            statusItem->setForeground(downloaded
                ? QBrush(QColor(0xcc, 0xcc, 0xcc))
                : QBrush(QColor(0x88, 0x88, 0x88)));
        }
        m_volumesTable->removeCellWidget(i, 6);
        setRowOpenIndicator(i, downloaded);
        return;
    }
}

void ComicsSeriesView::setRowOpenIndicator(int tableRow, bool downloaded)
{
    if (!m_volumesTable || tableRow < 0 || tableRow >= m_volumesTable->rowCount()) return;
    auto* item = m_volumesTable->item(tableRow, 6);
    if (!item) {
        item = new QTableWidgetItem();
        m_volumesTable->setItem(tableRow, 6, item);
    }
    item->setText(QString());
    item->setTextAlignment(Qt::AlignCenter);
    item->setToolTip(downloaded ? tr("Open") : QString());
    item->setIcon(downloaded ? QIcon(QStringLiteral(":/icons/chevron_right.svg")) : QIcon());
}

void ComicsSeriesView::setVolumeStatusText(int volumeNumber, const QString& statusText)
{
    if (!m_volumesTable) return;
    for (int i = 0; i < m_currentVolumeRows.size() && i < m_volumesTable->rowCount(); ++i) {
        if (m_currentVolumeRows.at(i).volumeNumber != volumeNumber) continue;
        if (auto* statusItem = m_volumesTable->item(i, 5)) {
            statusItem->setText(statusText);
            statusItem->setForeground(QBrush(QColor(0x88, 0x88, 0x88)));
        }
        return;
    }
}

void ComicsSeriesView::onVolumeCellClicked(int row, int column)
{
    Q_UNUSED(column);
    if (row < 0 || row >= m_currentVolumeRows.size()) {
        return;
    }

    const anilist::VolumeRow& volRow = m_currentVolumeRows.at(row);

    // Pull chapterIds back out of the col-0 item's UserRole stash. This is
    // the QStringList we set during renderDetail.
    QStringList chapterIds;
    if (auto* item = m_volumesTable->item(row, 0)) {
        chapterIds = item->data(Qt::UserRole).toStringList();
        const QString cbzPath = item->data(Qt::UserRole + 1).toString();
        if (!cbzPath.isEmpty()) {
            emit openVolume(volRow.volumeNumber, cbzPath);
            return;
        }
    }

    if (!m_sourcesPanel) return;
    m_sourcesPanel->populate(m_currentSeriesTitle,
                             m_currentAnilistId,
                             volRow,
                             chapterIds);
}

} // namespace tankoban::manga::comics
