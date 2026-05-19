// src/ui/pages/comics/ComicsSeriesView.cpp
#include "ComicsSeriesView.h"

#include "core/manga/anilist/AniListCache.h"
#include "core/manga/anilist/AniListClient.h"
#include "core/manga/anilist/AniListVolumeMapper.h"
#include "core/manga/MangaDownloadIndex.h"
#include "core/manga/PremiumCatalog.h"
#include "core/manga/bookwalker/VolumeCoverResolver.h"
#include "ui/widgets/ComicsSeriesViewLoadingOverlay.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QToolButton>
#include <QColor>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMouseEvent>
#include <QPixmap>
#include <QPixmapCache>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSize>
#include <QSizePolicy>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <Qt>

namespace tankoban::manga::comics {

namespace {

// STREAM_PORT 2026-05-18 Task 5 carry-forward: column constants promoted from
// function-local (Task 2 had them inside buildUi()) to file scope so all
// slots in this file can reference them by name. Stream parity at
// StreamDetailView.cpp:1064-1071 uses the same pattern.
constexpr int kColCheckbox = 0;
constexpr int kColIndex    = 1;
constexpr int kColThumb    = 2;
constexpr int kColTitle    = 3;
constexpr int kColProgress = 4;
constexpr int kColStatus   = 5;
constexpr int kColCount    = 6;

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

QJsonObject volumeRowJson(const anilist::VolumeRow& row, int rowIndex,
                          bool selected, const QString& cbzPath,
                          const QString& coverUrl)
{
    QJsonObject obj;
    obj[QStringLiteral("row")] = rowIndex;
    obj[QStringLiteral("volume")] = row.isVolumeX ? QJsonValue::Null : QJsonValue(row.volumeNumber);
    obj[QStringLiteral("volumeLabel")] = row.isVolumeX
        ? QStringLiteral("X")
        : QString::number(row.volumeNumber);
    obj[QStringLiteral("chapterRange")] = formatChapterRange(row);
    obj[QStringLiteral("chapterCount")] = row.chapterCount;
    obj[QStringLiteral("downloaded")] = !cbzPath.isEmpty();
    obj[QStringLiteral("selected")] = selected;
    obj[QStringLiteral("cbzPath")] = cbzPath;
    obj[QStringLiteral("coverUrl")] = coverUrl;

    QJsonArray chapters;
    for (const QString& chapter : row.chapterNumbers)
        chapters.append(chapter);
    obj[QStringLiteral("chapters")] = chapters;
    return obj;
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
    // cellClicked fires for any cell hit (including col-6 chevron) but is
    // mouse-only -- arrow keys do not raise it.
    //
    // F1 (2026-05-18): currentCellChanged fires for both mouse + keyboard
    // selection changes, so the Sources panel populate path is wired there.
    // cellClicked stays for the mouse-tap-to-open-cbz behavior only.
    if (m_volumesTable) {
        connect(m_volumesTable, &QTableWidget::cellClicked,
                this,           &ComicsSeriesView::onVolumeCellClicked);
        connect(m_volumesTable, &QTableWidget::currentCellChanged,
                this,           &ComicsSeriesView::onVolumeCurrentChanged);
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
    // STREAM_PORT 2026-05-18 Task 1: layout shell mirrors StreamDetailView
    // (src/ui/pages/stream/StreamDetailView.cpp:395-487). Shape:
    //   actionRow  -- back link (left) + library button (right)
    //   heroBanner -- 140px solid block holding the series banner image
    //   contentRow -- two columns (leftCol stretch=3, rightCol stretch=1)
    //                 leftCol holds title + meta + description + volume table
    //                 rightCol holds m_sourcesPanel full-vertical
    // The prior full-bleed paintEvent wallpaper is GONE -- title text now
    // sits below the banner on a solid dark background. Mockup at
    // .superpowers/brainstorm/1608-1779095122/content/proposed-layout.html.
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 14, 24, 24);
    outer->setSpacing(12);

    // Root QSS -- no wallpaper, solid dark background, label foreground colors,
    // volume table + sources panel card styling. Selection style is the new
    // 8% white tint (Stream parity at StreamDetailView.cpp:700); the prior
    // 3px gold left-stripe from the 2026-05-17 Sources Sidebar Decision 12
    // is REMOVED per brainstorm 2026-05-18.
    setStyleSheet(QStringLiteral(
        "ComicsSeriesView { background: #0d0d10; }"
        "QLabel#ComicsSeriesHeroBanner {"
        "  background: #101010;"
        "  border-radius: 8px;"
        "}"
        "QLabel#ComicsSeriesTitle {"
        "  color: #ffffff;"
        "  background: transparent;"
        "}"
        "QLabel#ComicsSeriesMetaLine {"
        "  color: rgba(255, 255, 255, 0.62);"
        "  background: transparent;"
        "}"
        "QLabel#ComicsSeriesSynopsis {"
        "  color: rgba(255, 255, 255, 0.55);"
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
        "QTableWidget#ComicsSeriesVolumesTable::item:selected {"
        "  background: rgba(255, 255, 255, 0.08);"
        "}"
        "QTableWidget#ComicsSeriesVolumesTable QHeaderView::section {"
        "  background-color: rgba(20, 20, 24, 0.95);"
        "  color: rgba(255, 255, 255, 0.65);"
        "  border: none;"
        "  padding: 8px 10px;"
        "  font-weight: 600;"
        "}"
        "#ComicsSeriesSourcesPanel {"
        "  background-color: rgba(15, 15, 18, 0.88);"
        "  border: 1px solid rgba(255, 255, 255, 0.08);"
        "  border-radius: 8px;"
        "}"
    ));

    // --- Action row: Back link (left) + Library button (right) ---------
    auto* actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(8);

    m_backButton = new QPushButton(tr("\xe2\x86\x90 Back"), this);
    m_backButton->setObjectName(QStringLiteral("ComicsSeriesBackButton"));
    m_backButton->setFlat(true);
    m_backButton->setCursor(Qt::PointingHandCursor);
    m_backButton->setStyleSheet(QStringLiteral(
        "QPushButton#ComicsSeriesBackButton {"
        "  background: transparent;"
        "  border: none;"
        "  color: rgba(255,255,255,0.7);"
        "  font-size: 13px;"
        "  padding: 4px 8px;"
        "}"
        "QPushButton#ComicsSeriesBackButton:hover {"
        "  color: #fff;"
        "}"));
    connect(m_backButton, &QPushButton::clicked,
            this, &ComicsSeriesView::backRequested);
    actionRow->addWidget(m_backButton, /*stretch*/ 0, Qt::AlignLeft);

    actionRow->addStretch(1);

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
    actionRow->addWidget(m_libraryButton, /*stretch*/ 0, Qt::AlignRight);

    outer->addLayout(actionRow);

    // --- Hero banner: 140px solid block holding the series art ----------
    m_heroBannerLabel = new QLabel(this);
    m_heroBannerLabel->setObjectName(QStringLiteral("ComicsSeriesHeroBanner"));
    m_heroBannerLabel->setFixedHeight(140);
    m_heroBannerLabel->setAlignment(Qt::AlignCenter);
    m_heroBannerLabel->setScaledContents(false);
    m_heroBannerLabel->hide();  // STREAM_PORT 2026-05-18 Task 1 fix: blueprint parity (StreamDetailView.cpp:405) -- reveal only when applyBannerPixmap paints.
    outer->addWidget(m_heroBannerLabel);

    // --- Two-column content row -----------------------------------------
    auto* contentRow = new QHBoxLayout();
    contentRow->setSpacing(16);

    // Left column: title + meta + synopsis + volume table
    auto* leftCol = new QVBoxLayout();
    leftCol->setSpacing(8);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("ComicsSeriesTitle"));
    {
        QFont f = m_title->font();
        f.setPointSize(18);
        f.setBold(true);
        m_title->setFont(f);
    }
    m_title->setWordWrap(true);
    leftCol->addWidget(m_title);

    m_metaLine = new QLabel(this);
    m_metaLine->setObjectName(QStringLiteral("ComicsSeriesMetaLine"));
    {
        QFont f = m_metaLine->font();
        f.setPointSize(10);
        m_metaLine->setFont(f);
    }
    leftCol->addWidget(m_metaLine);

    m_synopsis = new QLabel(this);
    m_synopsis->setObjectName(QStringLiteral("ComicsSeriesSynopsis"));
    {
        QFont f = m_synopsis->font();
        f.setPointSize(10);
        m_synopsis->setFont(f);
    }
    m_synopsis->setWordWrap(true);
    m_synopsis->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_synopsis->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    leftCol->addWidget(m_synopsis);

    // STREAM_PORT 2026-05-18 Task 3: 3-line clamped description with
    // "Show more / less" toggle. Clamp is computed dynamically from
    // QFontMetrics so short descriptions skip the toggle entirely; long
    // ones reveal the affordance below. Expanded mode removes the maximum
    // height and swaps the label. Mirrors StreamDetailView.cpp:460-472.
    m_descShowMoreBtn = new QPushButton(tr("Show more"), this);
    m_descShowMoreBtn->setObjectName(QStringLiteral("ComicsSeriesDescShowMore"));
    m_descShowMoreBtn->setCursor(Qt::PointingHandCursor);
    m_descShowMoreBtn->setFlat(true);
    m_descShowMoreBtn->setStyleSheet(QStringLiteral(
        "QPushButton#ComicsSeriesDescShowMore {"
        "  background: transparent;"
        "  border: none;"
        "  color: rgba(255,255,255,0.75);"
        "  font-size: 11px;"
        "  padding: 0;"
        "  text-align: left;"
        "}"
        "QPushButton#ComicsSeriesDescShowMore:hover {"
        "  color: #fff;"
        "  text-decoration: underline;"
        "}"));
    m_descShowMoreBtn->hide();
    connect(m_descShowMoreBtn, &QPushButton::clicked,
            this, &ComicsSeriesView::onDescShowMoreClicked);
    leftCol->addWidget(m_descShowMoreBtn, /*stretch*/ 0, Qt::AlignLeft);

    // Future story-arcs slot reserved here per brainstorm Decision 7
    // (2026-05-18). v1 inserts nothing; future v1.x widget mounts before
    // the volume table.

    // --- Volume list table (Task 2: Stream-blueprint 6-column layout). ---
    m_volumesTable = new QTableWidget(this);
    m_volumesTable->setObjectName(QStringLiteral("ComicsSeriesVolumesTable"));

    // STREAM_PORT 2026-05-18 Task 2: 6-column layout (down from 7). Stream
    // parity at StreamDetailView.cpp:661-674. Columns:
    //   [kColCheckbox=0 chk]  -- 32px checkbox (Task 5 wires the toggle)
    //   [kColIndex=1   #]     -- 36px volume index; F1 stash lives here
    //   [kColThumb=2   thmb]  -- 76px (48x64 portrait + padding), MANGA aspect
    //   [kColTitle=3   ttl]   -- stretch, stacked "Vol N" + "Chs A-B" cellWidget
    //   [kColProgress=4 prog] -- 80px progress text ("--" in v1)
    //   [kColStatus=5  stat]  -- 60px status text ("Downloaded" / ...)
    // Col-6 ("Open" chevron) is DROPPED entirely per brainstorm Decision 6.
    // kCol* constants live at file-scope anonymous namespace (Task 5 carry-forward).

    const QStringList headers = {
        QString(),         // checkbox -- no header text
        tr("#"),
        QString(),         // thumb -- no header text
        tr("Title"),
        tr("Progress"),
        tr("Status"),
    };
    m_volumesTable->setColumnCount(kColCount);
    m_volumesTable->setHorizontalHeaderLabels(headers);
    m_volumesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_volumesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_volumesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_volumesTable->setShowGrid(false);
    m_volumesTable->setAlternatingRowColors(true);
    m_volumesTable->verticalHeader()->setVisible(false);
    m_volumesTable->horizontalHeader()->setStretchLastSection(false);

    auto* hdr = m_volumesTable->horizontalHeader();
    hdr->setSectionResizeMode(kColCheckbox, QHeaderView::Fixed);
    hdr->setSectionResizeMode(kColIndex,    QHeaderView::Fixed);
    hdr->setSectionResizeMode(kColThumb,    QHeaderView::Fixed);
    hdr->setSectionResizeMode(kColTitle,    QHeaderView::Stretch);
    hdr->setSectionResizeMode(kColProgress, QHeaderView::Fixed);
    hdr->setSectionResizeMode(kColStatus,   QHeaderView::Fixed);
    m_volumesTable->setColumnWidth(kColCheckbox, 32);
    m_volumesTable->setColumnWidth(kColIndex,    36);
    m_volumesTable->setColumnWidth(kColThumb,    76);
    m_volumesTable->setColumnWidth(kColProgress, 80);
    m_volumesTable->setColumnWidth(kColStatus,   60);
    // STREAM_PORT Bug-4 round-3 fix 2026-05-18: m_volumesTable->setIconSize
    // REMOVED. Was set to QSize(48, 64) in Task 2 for col-2 thumb sizing,
    // but Qt6's QTableWidget appears to leak that iconSize hint into the
    // cellWidget rendering pipeline -- specifically the col-0 QToolButton's
    // SVG was rendering as a "[" (only left-edge visible), clipped to a
    // narrow width while preserving 14px height. Stream's m_episodeTable
    // does NOT set a table-wide iconSize (verified via grep) -- it uses
    // explicit per-cellWidget iconSize on QToolButton (14x14) and
    // QPushButton (16x16) only. Removing the table-wide call decouples
    // every cell's icon sizing. Col-2 thumb migrated to cellWidget+QLabel
    // (same pattern as col-5 status from round-2) -- see populateVolumeRows
    // + applyPixmapToVolumeRow.
    m_volumesTable->verticalHeader()->setDefaultSectionSize(72);

    leftCol->addWidget(m_volumesTable, /*stretch*/ 1);

    // STREAM_PORT 2026-05-18 Task 5: "Download Selected (N)" button. Shown
    // when N >= 1 checked rows; hidden otherwise. Click emits
    // bulkDownloadRequested with the full selection list.
    m_downloadSelectedBtn = new QPushButton(this);
    m_downloadSelectedBtn->setObjectName(QStringLiteral("ComicsSeriesDownloadSelectedBtn"));
    m_downloadSelectedBtn->setCursor(Qt::PointingHandCursor);
    m_downloadSelectedBtn->setFixedHeight(32);
    m_downloadSelectedBtn->setStyleSheet(QStringLiteral(
        "QPushButton#ComicsSeriesDownloadSelectedBtn {"
        "  background: rgba(255,255,255,0.10);"
        "  border: 1px solid rgba(255,255,255,0.20);"
        "  border-radius: 6px;"
        "  color: #fff;"
        "  padding: 6px 14px;"
        "  font-size: 12px;"
        "  font-weight: 500;"
        "}"
        "QPushButton#ComicsSeriesDownloadSelectedBtn:hover {"
        "  background: rgba(255,255,255,0.14);"
        "  border-color: rgba(255,255,255,0.30);"
        "}"));
    m_downloadSelectedBtn->hide();
    connect(m_downloadSelectedBtn, &QPushButton::clicked,
            this, &ComicsSeriesView::onDownloadSelectedClicked);
    leftCol->addWidget(m_downloadSelectedBtn, /*stretch*/ 0, Qt::AlignRight);

    contentRow->addLayout(leftCol, /*stretch*/ 3);

    // Right column: Sources panel full-vertical. PHASE 8: replaces the
    // Phase 7 placeholder QLabel; STREAM_PORT 2026-05-18 Task 1 moves the
    // panel OUT of the prior hero-row top-right slot into the full-height
    // right column beside the volume list. Panel internal logic, cards,
    // skeleton-pulse, auto-pick 300ms beat -- all UNCHANGED.
    m_sourcesPanel = new ComicsSourcesPanel(m_catalog, m_nyaa, this);
    m_sourcesPanel->setObjectName(QStringLiteral("ComicsSeriesSourcesPanel"));
    m_sourcesPanel->setMinimumWidth(240);
    m_sourcesPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    contentRow->addWidget(m_sourcesPanel, /*stretch*/ 1);

    outer->addLayout(contentRow, /*stretch*/ 1);

    // Task 14: loading overlay covers the entire widget during BookWalker
    // resolution; starts hidden. Safety timer forces fallback after 10s.
    m_loadingOverlay = new tankoban::ui::widgets::ComicsSeriesViewLoadingOverlay(this);
    m_loadingOverlay->setMessage(tr("Loading covers..."));
    m_loadingOverlay->hide();

    m_loadingSafetyTimer = new QTimer(this);
    m_loadingSafetyTimer->setSingleShot(true);
    m_loadingSafetyTimer->setInterval(10000);
    connect(m_loadingSafetyTimer, &QTimer::timeout,
            this, &ComicsSeriesView::onCoverResolverSafetyTimeout);
}

void ComicsSeriesView::showSeries(const anilist::MediaPreview& preview)
{
    // STREAM_PORT 2026-05-18 Task 7 (Task 3 carry-forward): reset description
    // expand state at series navigation. Without this, navigating Series A
    // (expanded) -> Back -> Series B leaves m_descExpanded=true in the brief
    // window before renderDetail() lands and resets it.
    m_descExpanded = false;
    if (m_descShowMoreBtn) {
        m_descShowMoreBtn->hide();
        m_descShowMoreBtn->setText(tr("Show more"));
    }

    if (m_synopsis) {
        // STREAM_PORT 2026-05-18 final-review fix: re-apply 3-line clamp on
        // series navigation. Task 7 reset m_descExpanded but did not call
        // setMaximumHeight; the brief window before renderDetail re-clamps
        // could let an expanded synopsis from a prior series flash full-height.
        const QFontMetrics fm(m_synopsis->font());
        m_synopsis->setMaximumHeight(fm.lineSpacing() * m_descClampLines);
    }

    m_currentAnilistId   = preview.anilistId;
    m_currentSeriesTitle = preview.title;
    m_currentVolumeRows.clear();
    if (m_sourcesPanel) m_sourcesPanel->clear();

    // Paint immediately from preview (cheap data, no detail required for hero).
    m_title->setText(preview.title);
    m_synopsis->setText(stripDescriptionHtml(preview.description));
    m_metaLine->setText(buildPreviewMetaLine(preview));
    // STREAM_PORT 2026-05-18 hero-instant-load fix: REMOVED the
    // clear() + hide() on m_heroBannerLabel here. Previous behavior:
    // every series-open cleared the pixmap + hid the label BEFORE
    // loadBannerUrl, so even cache-hit re-opens went through a
    // show()->size()->setPixmap cycle where the label briefly had
    // size (0, 0) -- triggering applyBannerPixmap's "raw pixmap"
    // fallback path that forces Qt to rescale the huge full-res
    // pixmap on every paint until layout settles. That visible
    // rescale was Hemanth's "keeps loading on re-open" perception.
    // Stream's pattern: keep the previous pixmap in the hero label;
    // let the next loadBannerUrl atomically replace it via cache hit
    // (instant on re-open of same series) or async fetch (brief
    // flicker of prior series' banner during series-change, only
    // visible if the new URL is not yet cached).
    m_volumesTable->setRowCount(0);
    refreshLibraryButton();

    // Task 14: kick off BookWalker per-volume cover resolution. Overlay shown
    // here; hidden on resolver signal or safety timeout. populateVolumeRows
    // (called after cache/detail fetch) builds the rows first; resolver
    // callbacks then paint per-volume covers over the AniList thumbs.
    m_currentResolvingAnilistId = preview.anilistId;
    showLoadingOverlay();
    if (m_loadingSafetyTimer) m_loadingSafetyTimer->start();
    if (m_coverResolver) {
        // TASK_8_PENDING: replace with resolveForSeries(seriesKey, englishTitle)
        // once ComicsSeriesView receives the seriesKey from showSeries(). For now
        // the resolver is not invoked; fallback paint runs immediately below.
        // m_coverResolver->resolveForSeries(seriesKey, preview.title);
        qWarning("ComicsSeriesView: resolveForSeries pending Task 8 wire-up; falling back to AniList art");
        paintVolumeCoversAsFallback();
        hideLoadingOverlay();
    } else {
        qWarning("ComicsSeriesView: no cover resolver set; falling back to AniList art");
        paintVolumeCoversAsFallback();
        hideLoadingOverlay();
    }

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
    // STREAM_PORT 2026-05-18 hero-instant-load fix: m_heroBannerLabel
    // pixmap NOT cleared on navigate-away. Stream parity -- the hero
    // pixmap from the most-recently-viewed series persists in the label
    // until the next showSeries replaces it via loadBannerUrl. This is
    // the only way to make re-open of the same series feel INSTANT
    // (cache-hit -> atomic pixmap replace = no visible change). The
    // previous Task-7 clear()+hide() guard caused a noticeable rescale
    // flicker on every re-open; Hemanth flagged it as "the poster keeps
    // loading every time we reopen the series view."
    m_currentResolvingAnilistId = 0;
    m_lastAppliedCoverUrlByVolume.clear();
    hideLoadingOverlay();  // stops the safety timer before clearing rows
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

        // STREAM_PORT 2026-05-18 Task 3: 3-line clamp via QFontMetrics. Mirrors
        // StreamDetailView.cpp:454-472 logic. The "Show more" toggle is hidden
        // when the full description fits within the clamp (i.e. clamping is a
        // no-op).
        if (m_synopsis) {
            m_descExpanded = false;
            const QFontMetrics fm(m_synopsis->font());
            const int clampHeight = fm.lineSpacing() * m_descClampLines;
            m_synopsis->setMaximumHeight(clampHeight);

            // Determine if the description overflows the clamp -- compute
            // natural sizeHint with width set to the synopsis's current width
            // (or 600px fallback if not yet sized).
            const int referenceWidth = (m_synopsis->width() > 0) ? m_synopsis->width() : 600;
            const int needed = m_synopsis->heightForWidth(referenceWidth);
            const bool overflows = (needed > clampHeight);
            if (m_descShowMoreBtn) {
                m_descShowMoreBtn->setText(tr("Show more"));
                m_descShowMoreBtn->setVisible(overflows);
            }
        }
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

    // STREAM_PORT 2026-05-18 Task 5: clear stale selection state on every
    // populateVolumeRows call so navigating between series resets the
    // checkbox set + hides the Download Selected button.
    m_selectedRows.clear();
    if (m_downloadSelectedBtn) m_downloadSelectedBtn->hide();

    m_volumesTable->setRowCount(rows.size());

    for (int i = 0; i < rows.size(); ++i) {
        const anilist::VolumeRow& row = rows.at(i);

        // Col kColCheckbox -- checkbox cellWidget. STREAM_PORT 2026-05-18
        // Task 5: toggled signal now wired to onVolumeCheckboxToggled which
        // updates m_selectedRows and the Download Selected button visibility.
        // STREAM_PORT Bug-4 fix 2026-05-18: was QCheckBox -- invisible at
        // unchecked rest state against the dark theme because Win11's
        // QStyle::PE_IndicatorCheckBox primitive clips the indicator
        // geometry to its own intrinsic 13x13px and ignores QSS sizing
        // (Stream hit the exact same bug 2026-05-12; their fix lives at
        // StreamDetailView.cpp:1000-1035). QToolButton bypasses
        // PE_IndicatorCheckBox entirely -- Qt just paints whatever icon we
        // set at the fixed 18x18px we requested. Tankoban discipline:
        // grayscale, no color, no emoji; the SVG assets at
        // resources/icons/checkbox-{checked,empty}.svg already match.
        // STREAM_PORT Bug-4 round-3 fix 2026-05-18: replaced QToolButton with
        // a clickable QLabel. The QToolButton pattern (verbatim port from
        // StreamDetailView.cpp:1010-1023) rendered as a "[" on Comics --
        // only the left edge of the 14x14 SVG was visible, the right edge
        // was clipped to ~6px width. Live MCP smoke at 0341/0342 confirmed
        // the visual bug. Multiple fix attempts (objectName removal +
        // explicit setChecked + table-wide setIconSize removal + col-2
        // thumb migration to cellWidget) did not resolve it -- the clipping
        // is specific to QToolButton-in-QTableWidget-cellWidget on this
        // Win11 / Qt6 combination. QLabel + setPixmap renders the SVG at
        // its exact dimensions without any clipping (proven by the col-5
        // status icon round-2 fix above). Click handling via the existing
        // eventFilter override -- the cbLabel carries a "rowIdx" + "checked"
        // dynamic property pair the filter reads. Stream's pattern stays
        // valid for Stream's context; Comics needs the QLabel workaround.
        auto* cbLabel = new QLabel(m_volumesTable);
        cbLabel->setObjectName(QStringLiteral("ComicsSeriesVolumeRowCheckbox"));
        // STREAM_PORT Bug-4 round-4 fix 2026-05-18: setFixedSize(32, 32)
        // REMOVED -- it shrunk the QLabel to a tiny 32x32 widget that Qt
        // parked at the top-left of the cell, leaving the checkbox visually
        // higher than the # serial number in the adjacent col-1 (which sits
        // vertically centered as a QTableWidgetItem). Without setFixedSize,
        // the QLabel fills the cell's full 48x108 rect and the pixmap
        // centers vertically + horizontally via setAlignment(AlignCenter).
        cbLabel->setAlignment(Qt::AlignCenter);
        cbLabel->setPixmap(QIcon(QStringLiteral(":/icons/checkbox-empty.svg")).pixmap(14, 14));
        cbLabel->setCursor(Qt::PointingHandCursor);
        cbLabel->setProperty("rowIdx", i);
        cbLabel->setProperty("checked", false);
        cbLabel->setStyleSheet(QStringLiteral(
            "QLabel#ComicsSeriesVolumeRowCheckbox { background: transparent; }"
            "QLabel#ComicsSeriesVolumeRowCheckbox:hover { background: rgba(255,255,255,0.06); border-radius: 2px; }"));
        // STREAM_PORT Bug-4 round-3 fix 2026-05-18 (revision): click handling
        // moved from QLabel eventFilter to QTableWidget::cellClicked. The
        // eventFilter approach was preempted by the table's SelectRows
        // selectionBehavior -- the click was being consumed by row-selection
        // before reaching the QLabel's event chain. cellClicked fires
        // reliably for col-0 cellWidget hits; onVolumeCellClicked branches
        // on column == kColCheckbox to toggle the QLabel's pixmap + property
        // + dispatch to onVolumeCheckboxToggled.
        m_volumesTable->setCellWidget(i, kColCheckbox, cbLabel);

        // Col 1 -- volume index. STREAM_PORT Task 2: F1 stash (chapterNumbers at
        // UserRole, cbzPath at UserRole+1) lives on col-1 now since col-0
        // is a cellWidget. onVolumeCellClicked + populateSourcesForRow updated
        // to read from col-1. Carry-forward B: "chapterNumbers" matches the
        // AniListTypes.h VolumeRow field name.
        //
        // Resolve cbzPath early via the same two-stage lookup used in the
        // original code (catalog hit first, then anilist_ slug fallback for
        // both premium + weebcentral sources). Stash on indexItem so the
        // Status col and F1 open-path can read it synchronously.
        const QString indexText = row.isVolumeX
            ? QStringLiteral("X")
            : QString::number(row.volumeNumber);
        auto* indexItem = new QTableWidgetItem(indexText);
        indexItem->setTextAlignment(Qt::AlignCenter);
        indexItem->setData(Qt::UserRole, row.chapterNumbers);

        if (m_downloadIndex && !row.isVolumeX) {
            std::optional<MangaDownloadIndex::Entry> dlEntry;
            if (m_catalog) {
                if (auto hit = m_catalog->entryForAnilistIdAndVolume(m_currentAnilistId,
                                                                      row.volumeNumber)) {
                    dlEntry = m_downloadIndex->entryForSeriesAndVolume(
                        QString::fromLatin1(kPremiumSourceId),
                        hit->first.seriesId,
                        row.volumeNumber);
                }
            }
            if (!dlEntry) {
                const QString fallbackSeriesId =
                    QStringLiteral("anilist_%1").arg(m_currentAnilistId);
                dlEntry = m_downloadIndex->entryForSeriesAndVolume(
                    QString::fromLatin1(kPremiumSourceId),
                    fallbackSeriesId,
                    row.volumeNumber);
                if (!dlEntry) {
                    dlEntry = m_downloadIndex->entryForSeriesAndVolume(
                        QString::fromLatin1(kWeebCentralSourceId),
                        fallbackSeriesId,
                        row.volumeNumber);
                }
            }
            if (dlEntry && !dlEntry->canonicalPath.isEmpty()) {
                indexItem->setData(Qt::UserRole + 1, dlEntry->canonicalPath);
            }
        }
        m_volumesTable->setItem(i, kColIndex, indexItem);

        // Col 2 -- thumbnail (48x64 portrait). STREAM_PORT Bug-4 round-3
        // fix 2026-05-18: was QTableWidgetItem.setIcon which required the
        // now-removed table-wide setIconSize(48, 64). Migrated to a
        // cellWidget+QLabel-with-pixmap pattern -- explicit pixel size,
        // no table-wide coupling. applyPixmapToVolumeRow targets this
        // QLabel via objectName lookup.
        auto* coverLabel = new QLabel(m_volumesTable);
        coverLabel->setObjectName(QStringLiteral("ComicsSeriesVolumeRowThumb"));
        coverLabel->setAlignment(Qt::AlignCenter);
        coverLabel->setFixedSize(QSize(48, 64));
        coverLabel->setStyleSheet(QStringLiteral("QLabel { background: transparent; }"));
        m_volumesTable->setCellWidget(i, kColThumb, coverLabel);
        const QString coverUrl = detail
            ? (!row.art.thumbnailUrl.isEmpty()
                ? row.art.thumbnailUrl
                : detail->preview.coverThumbUrl)
            : QString();
        if (!coverUrl.isEmpty()) {
            // Race-condition guard: VolumeCoverResolver (BookWalker arc) may emit
            // resolved() with BookWalker CDN URLs BEFORE populateVolumeRows fires
            // (e.g. when MangaUpdates sidecar is warm but AniList detail fetch lags).
            // If a BookWalker URL has already been applied for this volume, don't
            // overwrite it with the AniList fallback per-vol thumbnail. The resolver's
            // BookWalker URLs are publisher-canonical and strictly preferred.
            const QString existing = m_lastAppliedCoverUrlByVolume.value(row.volumeNumber);
            if (!existing.contains(QStringLiteral("rimg.bookwalker.jp"))) {
                loadCoverUrlForVolume(coverUrl, row.volumeNumber);
            }
        }

        // Col 3 -- title cellWidget. STREAM_PORT Bug-5 fix 2026-05-18:
        // dropped the "Chs A-B (N ch)" chapter-range subtitle Hemanth
        // flagged as "kind of bad UI design" -- it read as a stats line
        // rather than content. Stream's title col is title + optional
        // human-readable description (StreamDetailView.cpp:1080-1090); for
        // manga there is no per-volume blurb so a title-only cell is the
        // closest Stream-parity. Chapter-range info preserved as a tooltip
        // on the cell for the power-user who hovers (uses formatChapterRange
        // verbatim including the "(N ch)" tail since tooltips can be more
        // explanatory than a row-line subtitle).
        auto* titleWrap = new QWidget(m_volumesTable);
        auto* titleLay = new QVBoxLayout(titleWrap);
        titleLay->setContentsMargins(8, 6, 8, 6);
        titleLay->setSpacing(0);

        const QString volLabelText = row.isVolumeX
            ? tr("Volume X")
            : tr("Volume %1").arg(row.volumeNumber);
        auto* volLabel = new QLabel(volLabelText, titleWrap);
        volLabel->setStyleSheet(QStringLiteral("color: #e5e7eb; font-size: 13px; font-weight: 500; background: transparent;"));
        titleLay->addWidget(volLabel);

        titleWrap->setToolTip(formatChapterRange(row));

        m_volumesTable->setCellWidget(i, kColTitle, titleWrap);

        // Col 4 -- progress (v1 ships "--" per spec; per-chapter read-state
        // wiring is out of scope for this plan).
        auto* progItem = new QTableWidgetItem(QStringLiteral("--"));
        progItem->setTextAlignment(Qt::AlignCenter);
        progItem->setForeground(QBrush(QColor(255, 255, 255, 128)));
        m_volumesTable->setItem(i, kColProgress, progItem);

        // Col 5 -- status text. Derived from whether cbzPath was resolved above.
        // STREAM_PORT Bug-3 round 2 fix 2026-05-18: was QTableWidgetItem
        // text "Downloaded" / "Not downloaded" which clipped to "Not down..."
        // in the 60px-wide Status column. First-round fix tried
        // QTableWidgetItem::setIcon but the icon rendered at 48x64 (the
        // table-wide setIconSize set in Task 2 for the col-2 thumb applies
        // uniformly to ALL QTableWidgetItem icons -- so the download glyph
        // was "incredibly big" per Hemanth's verdict). Round-2 ports
        // Stream's action-col pattern at StreamDetailView.cpp:1041-1049:
        // cellWidget holds a QLabel-with-pixmap rendered at 16x16, fully
        // decoupled from the table-wide iconSize. Pixmap renders the same
        // SVG asset Stream uses (check.svg / download-arrow.svg). The
        // status text fallback path for transient states ("Downloading...",
        // "Failed") is preserved by re-using the same QLabel for setText
        // in setVolumeStatusText below.
        const bool isDownloaded = (indexItem->data(Qt::UserRole + 1).toString().isEmpty() == false);
        auto* statusLabel = new QLabel(m_volumesTable);
        statusLabel->setObjectName(QStringLiteral("ComicsSeriesVolumeRowStatus"));
        statusLabel->setAlignment(Qt::AlignCenter);
        statusLabel->setStyleSheet(QStringLiteral(
            "QLabel#ComicsSeriesVolumeRowStatus {"
            "  color: rgba(255,255,255,0.55);"
            "  font-size: 11px;"
            "  background: transparent;"
            "}"));
        statusLabel->setPixmap(QIcon(isDownloaded
            ? QStringLiteral(":/icons/check.svg")
            : QStringLiteral(":/icons/download-arrow.svg")).pixmap(16, 16));
        statusLabel->setToolTip(isDownloaded ? tr("Downloaded") : tr("Not downloaded"));
        m_volumesTable->setCellWidget(i, kColStatus, statusLabel);
    }

    // STREAM_PORT 2026-05-18 Task 6: next-unread highlight + auto-scroll.
    // Proxy for "unread": no stashed cbz path. The first such row is the
    // user's next stop. If every row has a cbz (all downloaded),
    // m_nextUnreadRow stays -1 and no scroll fires.
    m_nextUnreadRow = -1;
    for (int i = 0; i < m_volumesTable->rowCount(); ++i) {
        if (auto* item = m_volumesTable->item(i, kColIndex)) {
            const QString cbz = item->data(Qt::UserRole + 1).toString();
            if (cbz.isEmpty()) {
                m_nextUnreadRow = i;
                break;
            }
        }
    }

    if (m_nextUnreadRow >= 0 && m_nextUnreadRow < m_volumesTable->rowCount()) {
        // Scroll the next-unread row into center view on first paint.
        if (auto* item = m_volumesTable->item(m_nextUnreadRow, kColIndex)) {
            m_volumesTable->scrollToItem(item, QAbstractItemView::PositionAtCenter);
        }

        // Apply a subtle 2px left-edge accent to the title-cell wrapper
        // widget at the kColTitle column (the titleWrap cellWidget from
        // Task 2). Re-applying QSS on the wrapper overrides the default
        // transparent background only for the next-unread row.
        if (QWidget* titleWrap = m_volumesTable->cellWidget(m_nextUnreadRow, kColTitle)) {
            // STREAM_PORT 2026-05-18 final-review fix: scope the accent QSS to
            // the titleWrap's objectName so it doesn't propagate to child
            // QLabels (which would render double borders). Per
            // feedback_css_scoping.md.
            titleWrap->setObjectName(QStringLiteral("ComicsSeriesNextUnreadTitle"));
            titleWrap->setStyleSheet(QStringLiteral(
                "#ComicsSeriesNextUnreadTitle { border-left: 2px solid rgba(255,255,255,0.30); }"));
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
    if (m_descShowMoreBtn) m_descShowMoreBtn->hide();
    m_metaLine->clear();
}

void ComicsSeriesView::refreshLibraryButton()
{
    if (!m_libraryButton) return;
    const bool hasSeries  = (m_currentAnilistId > 0);
    const bool bookmarked = (m_cache && hasSeries) ? m_cache->isBookmarked(m_currentAnilistId) : false;
    m_libraryButton->setEnabled(hasSeries && m_cache);
    // STREAM_PORT 2026-05-18 Task 4: Stream-verbatim action labels.
    // Was "In library" (passive) / "Add to library" (action mixed); now
    // "Remove from Library" (when bookmarked) / "Add to Library" (when not).
    m_libraryButton->setText(bookmarked ? tr("Remove from Library") : tr("Add to Library"));
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

// -----------------------------------------------------------------------
// Task 14: BookWalker per-volume cover resolver integration
// -----------------------------------------------------------------------

void ComicsSeriesView::setVolumeCoverResolver(
        tankoban::manga::bookwalker::VolumeCoverResolver* resolver)
{
    if (m_coverResolver)
        disconnect(m_coverResolver.data(), nullptr, this, nullptr);
    m_coverResolver = resolver;
    if (m_coverResolver) {
        connect(m_coverResolver.data(),
                &tankoban::manga::bookwalker::VolumeCoverResolver::resolved,
                this, &ComicsSeriesView::onCoverResolverResolved);
        connect(m_coverResolver.data(),
                &tankoban::manga::bookwalker::VolumeCoverResolver::unresolved,
                this, &ComicsSeriesView::onCoverResolverUnresolved);
        connect(m_coverResolver.data(),
                &tankoban::manga::bookwalker::VolumeCoverResolver::skipped,
                this, &ComicsSeriesView::onCoverResolverSkipped);
    }
}

void ComicsSeriesView::showLoadingOverlay()
{
    if (!m_loadingOverlay) return;
    m_loadingOverlay->setGeometry(rect());
    m_loadingOverlay->raise();
    m_loadingOverlay->show();
}

void ComicsSeriesView::hideLoadingOverlay()
{
    if (m_loadingOverlay) m_loadingOverlay->hide();
    if (m_loadingSafetyTimer) m_loadingSafetyTimer->stop();
}

// TASK_8_NOTE: slot signatures updated from (int anilistId, ...) to
// (const QString& seriesKey, ...) per WEEBCENTRAL_IDENTITY_PIVOT Tasks 6+7.
// The guard comparison (seriesKey vs m_currentResolvingAnilistId) is a stub:
// Task 8 will add a m_currentResolvingSeriesKey member and compare against it.
// For now the slots are no-ops since resolveForSeries is not yet invoked from
// showSeries (see TASK_8_PENDING comment above). The bodies are safe dead code.
void ComicsSeriesView::onCoverResolverResolved(const QString& /*seriesKey*/,
                                               const QMap<int, QString>& volumeToCoverUrl)
{
    // TASK_8_PENDING: guard with seriesKey == m_currentResolvingSeriesKey
    paintVolumeCovers(volumeToCoverUrl);
    hideLoadingOverlay();
}

void ComicsSeriesView::onCoverResolverUnresolved(const QString& /*seriesKey*/,
                                                 const QString& /*reason*/)
{
    // TASK_8_PENDING: guard with seriesKey == m_currentResolvingSeriesKey
    paintVolumeCoversAsFallback();
    hideLoadingOverlay();
}

void ComicsSeriesView::onCoverResolverSkipped(const QString& /*seriesKey*/,
                                              const QString& /*reason*/)
{
    // TASK_8_PENDING: guard with seriesKey == m_currentResolvingSeriesKey
    // Premium short-circuit: PremiumCoverExtractor handles cover paint via the
    // existing pipeline. Just hide the overlay; do not call the paint helpers.
    hideLoadingOverlay();
}

void ComicsSeriesView::onCoverResolverSafetyTimeout()
{
    // Resolver took > 10s -- paint fallback and clear the overlay so the
    // user isn't left staring at a spinner indefinitely.
    paintVolumeCoversAsFallback();
    hideLoadingOverlay();
}

void ComicsSeriesView::paintVolumeCovers(const QMap<int, QString>& volumeToCoverUrl)
{
    // volumeToCoverUrl is keyed by 1-based volumeNumber; m_currentVolumeRows
    // stores the actual row order. Walk via applyPixmapToVolumeRow which
    // already handles the volumeNumber -> row lookup + QLabel painting.
    for (auto it = volumeToCoverUrl.constBegin(); it != volumeToCoverUrl.constEnd(); ++it) {
        if (!it.value().isEmpty())
            loadCoverUrlForVolume(it.value(), it.key());
    }
}

void ComicsSeriesView::paintVolumeCoversAsFallback()
{
    // Fall back to the per-volume AniList art (row.art.thumbnailUrl).
    // loadCoverUrlForVolume guards against empty URLs; it also hits
    // QPixmapCache first so repeated calls on already-loaded rows are free.
    // This is a no-op when m_currentVolumeRows is empty (rows not yet built
    // by populateVolumeRows) -- that path already loads AniList art itself.
    for (const auto& row : m_currentVolumeRows) {
        if (!row.art.thumbnailUrl.isEmpty())
            loadCoverUrlForVolume(row.art.thumbnailUrl, row.volumeNumber);
    }
}

void ComicsSeriesView::loadCoverUrlForVolume(const QString& url, int volumeNumber)
{
    // Task 15: record the URL (including empty) before any guard so the smoke
    // matrix can see what source each volume row was asked to paint.
    if (volumeNumber > 0)
        m_lastAppliedCoverUrlByVolume[volumeNumber] = url;

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
    const QUrl coverUrl(url);
    QNetworkRequest req(coverUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QString::fromLatin1("Mozilla/5.0 (Windows NT 10.0; Win64; x64) Tankoban/1.0"));
    QNetworkReply* reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [self, reply, url, volumeNumber, snapshotAnilistId]() {
        reply->deleteLater();
        if (!self) return;
        // Stale-series guard: discard if user navigated away during the fetch.
        if (self->m_currentAnilistId != snapshotAnilistId) return;
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("loadCoverUrlForVolume: fetch failed url=%s vol=%d error=%s",
                     qUtf8Printable(url), volumeNumber, qUtf8Printable(reply->errorString()));
            return;
        }
        const QByteArray data = reply->readAll();
        QPixmap pm;
        if (!pm.loadFromData(data)) {
            qWarning("loadCoverUrlForVolume: pixmap decode failed url=%s vol=%d bytes=%lld",
                     qUtf8Printable(url), volumeNumber, static_cast<long long>(data.size()));
            return;
        }
        QPixmapCache::insert(url, pm);
        self->applyPixmapToVolumeRow(volumeNumber, pm);
    });
}

void ComicsSeriesView::loadBannerUrl(const QString& url)
{
    // STREAM_PORT 2026-05-18 Task 1: was full-viewport paintEvent wallpaper;
    // now paints onto m_heroBannerLabel at 140px height. Uses
    // KeepAspectRatioByExpanding so the banner image fills the 140px band
    // (horizontal slice, centered). Mirrors StreamDetailView's hero label
    // approach.
    if (url.isEmpty() || !m_heroBannerLabel) return;

    QPixmap cached;
    if (QPixmapCache::find(url, &cached)) {
        applyBannerPixmap(cached);
        return;
    }

    QNetworkAccessManager* nam = m_client ? m_client->networkManager() : nullptr;
    if (!nam) return;

    QPointer<ComicsSeriesView> self(this);
    const int snapshotAnilistId = m_currentAnilistId;
    const QUrl bannerUrl(url);
    QNetworkRequest req(bannerUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QString::fromLatin1("Mozilla/5.0 (Windows NT 10.0; Win64; x64) Tankoban/1.0"));
    QNetworkReply* reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [self, reply, url, snapshotAnilistId]() {
        reply->deleteLater();
        if (!self) return;
        if (self->m_currentAnilistId != snapshotAnilistId) return;
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("loadBannerUrl: fetch failed url=%s error=%s",
                     qUtf8Printable(url), qUtf8Printable(reply->errorString()));
            return;
        }
        const QByteArray data = reply->readAll();
        QPixmap pm;
        if (!pm.loadFromData(data)) {
            qWarning("loadBannerUrl: pixmap decode failed url=%s bytes=%lld",
                     qUtf8Printable(url), static_cast<long long>(data.size()));
            return;
        }
        QPixmapCache::insert(url, pm);
        self->applyBannerPixmap(pm);
    });
}

void ComicsSeriesView::applyBannerPixmap(const QPixmap& pm)
{
    if (!m_heroBannerLabel || pm.isNull()) return;
    m_heroBannerLabel->show();  // STREAM_PORT Task 1 fix: reveal banner when a pixmap actually lands.
    const QSize target = m_heroBannerLabel->size();
    if (target.width() <= 0 || target.height() <= 0) {
        // Label hasn't been sized yet (first paint); store and re-apply on
        // resize via setPixmap with the raw pixmap -- Qt will scale on paint.
        m_heroBannerLabel->setPixmap(pm);
        return;
    }
    const QPixmap scaled = pm.scaled(target,
                                     Qt::KeepAspectRatioByExpanding,
                                     Qt::SmoothTransformation);
    m_heroBannerLabel->setPixmap(scaled);
}

void ComicsSeriesView::applyPixmapToVolumeRow(int volumeNumber, const QPixmap& pm)
{
    if (!m_volumesTable || pm.isNull()) {
        qWarning("applyPixmapToVolumeRow: precondition fail vol=%d table=%p null=%d",
                 volumeNumber, static_cast<void*>(m_volumesTable), pm.isNull());
        return;
    }
    // STREAM_PORT Bug-4 round-3 fix 2026-05-18: writes to the cellWidget
    // QLabel#ComicsSeriesVolumeRowThumb (was QTableWidgetItem.setIcon via
    // DecorationRole). Same 48x64 KeepAspectRatio scale; same per-row
    // volumeNumber match. The migration was forced by removing the
    // table-wide setIconSize that was clipping the col-0 QToolButton icon.
    for (int i = 0; i < m_currentVolumeRows.size() && i < m_volumesTable->rowCount(); ++i) {
        if (m_currentVolumeRows.at(i).volumeNumber == volumeNumber) {
            auto* coverLabel = qobject_cast<QLabel*>(m_volumesTable->cellWidget(i, kColThumb));
            if (!coverLabel) {
                qWarning("applyPixmapToVolumeRow: cellWidget for vol=%d row=%d col=%d is not QLabel (got %p)",
                         volumeNumber, i, kColThumb,
                         static_cast<void*>(m_volumesTable->cellWidget(i, kColThumb)));
                return;
            }
            const QSize thumb(48, 64);
            const QPixmap scaled = pm.scaled(thumb, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
            coverLabel->setPixmap(scaled);
            return;
        }
    }
    qWarning("applyPixmapToVolumeRow: no row matched volumeNumber=%d (m_currentVolumeRows.size=%lld rowCount=%d)",
             volumeNumber,
             static_cast<long long>(m_currentVolumeRows.size()),
             m_volumesTable->rowCount());
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
        if (auto* indexItem = m_volumesTable->item(i, kColIndex)) {
            indexItem->setData(Qt::UserRole + 1, downloaded ? cbzPath : QString());
        }
        // STREAM_PORT Bug-3 round 2 fix 2026-05-18: write to the cellWidget
        // QLabel (16x16 pixmap) instead of the now-removed QTableWidgetItem
        // icon. populateVolumeRows installed a QLabel#ComicsSeriesVolumeRowStatus
        // at this column for every row -- look it up and setPixmap.
        if (auto* statusLabel = qobject_cast<QLabel*>(m_volumesTable->cellWidget(i, kColStatus))) {
            statusLabel->setPixmap(QIcon(downloaded
                ? QStringLiteral(":/icons/check.svg")
                : QStringLiteral(":/icons/download-arrow.svg")).pixmap(16, 16));
            statusLabel->setText(QString());
            statusLabel->setToolTip(downloaded ? tr("Downloaded") : tr("Not downloaded"));
        }
        return;
    }
}


void ComicsSeriesView::setVolumeStatusText(int volumeNumber, const QString& statusText)
{
    if (!m_volumesTable) return;
    for (int i = 0; i < m_currentVolumeRows.size() && i < m_volumesTable->rowCount(); ++i) {
        if (m_currentVolumeRows.at(i).volumeNumber != volumeNumber) continue;
        // STREAM_PORT Bug-3 round 2 fix 2026-05-18: same cellWidget QLabel
        // that holds the steady-state pixmap now holds transient text
        // ("Downloading...", "Failed"). Clear the pixmap so it doesn't
        // overlap the text inside the same QLabel.
        if (auto* statusLabel = qobject_cast<QLabel*>(m_volumesTable->cellWidget(i, kColStatus))) {
            statusLabel->setPixmap(QPixmap());
            statusLabel->setText(statusText);
        }
        return;
    }
}

void ComicsSeriesView::onVolumeCellClicked(int row, int column)
{
    if (row < 0 || row >= m_currentVolumeRows.size()) {
        return;
    }

    // STREAM_PORT Bug-4 round-3 fix 2026-05-18: col-0 checkbox click toggles
    // the QLabel pixmap + dynamic "checked" property + dispatches to
    // onVolumeCheckboxToggled. cellWidget eventFilter routing was preempted
    // by the table's SelectRows behavior; the cellClicked signal fires
    // reliably regardless. Early-return after toggling so the cbz-open path
    // below doesn't ALSO trigger.
    if (column == kColCheckbox) {
        if (auto* lbl = qobject_cast<QLabel*>(m_volumesTable->cellWidget(row, kColCheckbox))) {
            const bool newChecked = !lbl->property("checked").toBool();
            lbl->setProperty("checked", newChecked);
            lbl->setPixmap(QIcon(newChecked
                ? QStringLiteral(":/icons/checkbox-checked.svg")
                : QStringLiteral(":/icons/checkbox-empty.svg")).pixmap(14, 14));
            onVolumeCheckboxToggled(row, newChecked);
        }
        return;
    }

    // F1 (2026-05-18): mouse-tap-to-open path only. If the row has a stashed
    // cbz path, emit openVolume so ComicsPage can route to the reader. The
    // Sources panel populate side now lives on onVolumeCurrentChanged, which
    // fires from this same click *before* cellClicked, so by the time we get
    // here the panel is already up to date.
    if (auto* item = m_volumesTable->item(row, kColIndex)) {
        const QString cbzPath = item->data(Qt::UserRole + 1).toString();
        if (!cbzPath.isEmpty()) {
            const anilist::VolumeRow& volRow = m_currentVolumeRows.at(row);
            emit openVolume(volRow.volumeNumber, cbzPath);
        }
    }
}

void ComicsSeriesView::onVolumeCurrentChanged(int currentRow, int currentColumn,
                                              int previousRow, int previousColumn)
{
    Q_UNUSED(currentColumn);
    Q_UNUSED(previousRow);
    Q_UNUSED(previousColumn);
    // F1 (2026-05-18): fires on both mouse + keyboard selection changes, so
    // arrow-key nav across the volumes table updates the Sources panel.
    populateSourcesForRow(currentRow);
}

void ComicsSeriesView::populateSourcesForRow(int row)
{
    if (row < 0 || row >= m_currentVolumeRows.size()) {
        return;
    }
    if (!m_sourcesPanel) return;

    const anilist::VolumeRow& volRow = m_currentVolumeRows.at(row);

    // Pull chapterNumbers back out of the col-1 item's UserRole stash. This is
    // the QStringList we set during renderDetail (VolumeRow::chapterNumbers field).
    // STREAM_PORT Task 2: col 0 is now checkbox; stash lives on col 1.
    // Carry-forward B: renamed from chapterIds to chapterNumbers to match
    // AniListTypes.h VolumeRow field naming. Panel API parameter stays chapterIds.
    QStringList chapterNumbers;
    if (auto* item = m_volumesTable->item(row, kColIndex)) {
        chapterNumbers = item->data(Qt::UserRole).toStringList();
    }

    m_sourcesPanel->populate(m_currentSeriesTitle,
                             m_currentAnilistId,
                             volRow,
                             chapterNumbers);
}

void ComicsSeriesView::onDescShowMoreClicked()
{
    if (!m_synopsis || !m_descShowMoreBtn) return;
    m_descExpanded = !m_descExpanded;
    if (m_descExpanded) {
        m_synopsis->setMaximumHeight(QWIDGETSIZE_MAX);
        m_descShowMoreBtn->setText(tr("Show less"));
    } else {
        const QFontMetrics fm(m_synopsis->font());
        m_synopsis->setMaximumHeight(fm.lineSpacing() * m_descClampLines);
        m_descShowMoreBtn->setText(tr("Show more"));
    }
}

void ComicsSeriesView::onVolumeCheckboxToggled(int row, bool checked)
{
    // STREAM_PORT 2026-05-18 Task 7 (Task 5 carry-forward): defensive bounds
    // guard. The current code path is reachable-safe (Qt destroys cellWidgets
    // synchronously on setRowCount, killing stale toggled signal connects),
    // but explicit bounds-check matches the same defensive pattern in
    // onDownloadSelectedClicked.
    if (row < 0 || row >= m_currentVolumeRows.size()) return;

    // STREAM_PORT 2026-05-18 Task 5: tracks per-row selection in
    // m_selectedRows. The Download Selected button shows when at least one
    // row is checked; its label updates with the count.
    if (checked) m_selectedRows.insert(row);
    else         m_selectedRows.remove(row);

    if (!m_downloadSelectedBtn) return;
    const int n = m_selectedRows.size();
    m_downloadSelectedBtn->setText(tr("Download Selected (%1)").arg(n));
    m_downloadSelectedBtn->setVisible(n > 0);
}

void ComicsSeriesView::onDownloadSelectedClicked()
{
    // STREAM_PORT 2026-05-18 Task 5: bulk-download dispatch (Option A).
    // Emits bulkDownloadRequested once with the full selection list.
    // Option A chosen because bulk path has no per-source-picked context
    // (unlike downloadDispatchRequested which carries a UnifiedSourceRow);
    // fabricating a dummy UnifiedSourceRow would be wrong. ComicsPage v1.x
    // wires this signal to the default provider once bulk routing lands.
    if (m_selectedRows.isEmpty()) return;

    // Snapshot the set since the dispatch may indirectly clear it (e.g. if
    // the receiver triggers a re-populate of the table).
    const QList<int> rows = QList<int>(m_selectedRows.cbegin(), m_selectedRows.cend());
    QList<anilist::VolumeRow> selectedVols;
    selectedVols.reserve(rows.size());
    for (int row : rows) {
        if (row < 0 || row >= m_currentVolumeRows.size()) continue;
        selectedVols.append(m_currentVolumeRows.at(row));
    }
    if (!selectedVols.isEmpty()) {
        emit bulkDownloadRequested(m_currentAnilistId, selectedVols);
    }
}

// -----------------------------------------------------------------------
// dev-control bridge
// -----------------------------------------------------------------------

QJsonObject ComicsSeriesView::devSnapshot() const
{
    QJsonObject snap;
    snap[QStringLiteral("active")] = isVisible();
    snap[QStringLiteral("currentAnilistId")] = m_currentAnilistId;
    snap[QStringLiteral("currentSeriesTitle")] = m_currentSeriesTitle;
    snap[QStringLiteral("pendingSeriesReqId")] = m_pendingSeriesReqId;
    snap[QStringLiteral("bannerVisible")] = m_heroBannerLabel && m_heroBannerLabel->isVisible();
    snap[QStringLiteral("bannerHasPixmap")] =
        m_heroBannerLabel && !m_heroBannerLabel->pixmap(Qt::ReturnByValue).isNull();
    snap[QStringLiteral("libraryButtonText")] =
        m_libraryButton ? m_libraryButton->text() : QString();
    snap[QStringLiteral("selectedVolumeCount")] = m_selectedRows.size();
    snap[QStringLiteral("currentRow")] = m_volumesTable ? m_volumesTable->currentRow() : -1;
    snap[QStringLiteral("nextUnreadRow")] = m_nextUnreadRow;

    QJsonArray selectedRows;
    for (int row : m_selectedRows)
        selectedRows.append(row);
    snap[QStringLiteral("selectedRows")] = selectedRows;

    QJsonArray rows;
    for (int i = 0; i < m_currentVolumeRows.size(); ++i) {
        QString cbzPath;
        if (m_volumesTable) {
            if (auto* item = m_volumesTable->item(i, kColIndex))
                cbzPath = item->data(Qt::UserRole + 1).toString();
        }
        const int volNum = m_currentVolumeRows.at(i).volumeNumber;
        const QString coverUrl = m_lastAppliedCoverUrlByVolume.value(volNum);
        rows.append(volumeRowJson(m_currentVolumeRows.at(i), i,
                                  m_selectedRows.contains(i), cbzPath, coverUrl));
    }
    snap[QStringLiteral("volumes")] = rows;
    snap[QStringLiteral("sourcesPanel")] = devSourcesSnapshot();
    return snap;
}

QJsonObject ComicsSeriesView::devSelectVolume(int row)
{
    QJsonObject out;
    if (!m_volumesTable || row < 0 || row >= m_currentVolumeRows.size()) {
        out[QStringLiteral("status")] = QStringLiteral("error");
        out[QStringLiteral("message")] = QStringLiteral("row out of range");
        return out;
    }

    m_volumesTable->setCurrentCell(row, kColIndex);
    populateSourcesForRow(row);
    out[QStringLiteral("status")] = QStringLiteral("ok");
    out[QStringLiteral("row")] = row;
    out[QStringLiteral("volume")] = m_currentVolumeRows.at(row).volumeNumber;
    out[QStringLiteral("snapshot")] = devSnapshot();
    return out;
}

QJsonObject ComicsSeriesView::devSourcesSnapshot() const
{
    return m_sourcesPanel ? m_sourcesPanel->devSnapshot() : QJsonObject{};
}

QJsonObject ComicsSeriesView::devDispatchVolume(int volumeNumber, const QString& source)
{
    QJsonObject out;
    int row = -1;
    for (int i = 0; i < m_currentVolumeRows.size(); ++i) {
        if (m_currentVolumeRows.at(i).volumeNumber == volumeNumber) {
            row = i;
            break;
        }
    }
    if (row < 0) {
        out[QStringLiteral("status")] = QStringLiteral("error");
        out[QStringLiteral("message")] = QStringLiteral("volume not found");
        return out;
    }

    m_volumesTable->setCurrentCell(row, kColIndex);
    populateSourcesForRow(row);

    QString err;
    if (!m_sourcesPanel || !m_sourcesPanel->devDispatchSource(source, &err)) {
        out[QStringLiteral("status")] = QStringLiteral("error");
        out[QStringLiteral("message")] = err.isEmpty() ? QStringLiteral("source dispatch failed") : err;
        out[QStringLiteral("sourcesPanel")] = devSourcesSnapshot();
        return out;
    }

    out[QStringLiteral("status")] = QStringLiteral("ok");
    out[QStringLiteral("volume")] = volumeNumber;
    out[QStringLiteral("source")] = source;
    out[QStringLiteral("sourcesPanel")] = devSourcesSnapshot();
    return out;
}

} // namespace tankoban::manga::comics
