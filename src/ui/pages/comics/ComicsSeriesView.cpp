// src/ui/pages/comics/ComicsSeriesView.cpp
#include "ComicsSeriesView.h"
#include "VolumeTile.h"

#include "core/manga/anilist/AniListCache.h"
#include "core/manga/anilist/AniListClient.h"
#include "core/manga/anilist/AniListVolumeMapper.h"
#include "core/manga/MangaDownloadIndex.h"
#include "core/manga/MangaSourceRegistry.h"
#include "core/manga/MangaScraper.h"
#include "core/manga/PremiumCatalog.h"
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
#include <QLayoutItem>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPixmapCache>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSize>
#include <QSizePolicy>
#include <algorithm>
#include <QSet>
#include <QStringList>
#include <QScrollArea>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <Qt>

namespace tankoban::manga::comics {

namespace {

// Task 16: kCol* QTableWidget column constants removed -- QTableWidget fully
// replaced by VolumeTile rows. kVolumeThumbSize kept for applyPixmapToVolumeRow
// callers that pass explicit sizes (none remain; kept for documentation).
const QSize kVolumeThumbSize(110, 150);
constexpr int kVolumeRowHeight = 168;
const QSize kHeroCoverSize(110, 165);

// Tag names that AniList classifies as "Theme-Other-Demographic" — these
// land in the meta strip in a future v1.x extension, not the hero chip
// row. v1 just filters them OUT of the chip row so the chips show only
// genre/theme/setting-flavored tags. Lowercased for compare.
static const QSet<QString>& kDemographicTagNames()
{
    static const QSet<QString> kNames = {
        QStringLiteral("shounen"), QStringLiteral("shoujo"),
        QStringLiteral("seinen"),  QStringLiteral("josei"),
        QStringLiteral("kodomomuke"),
    };
    return kNames;
}

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

// Map AniList ISO 3166-1 alpha-2 country code to a display language name.
// Empty input or unknown code returns empty string (so the meta-strip
// composer can omit it without an "Unknown" token leaking through).
QString humanizeOriginLanguage(const QString& countryCode)
{
    if (countryCode.compare(QStringLiteral("JP"), Qt::CaseInsensitive) == 0) return QStringLiteral("Japanese");
    if (countryCode.compare(QStringLiteral("KR"), Qt::CaseInsensitive) == 0) return QStringLiteral("Korean");
    if (countryCode.compare(QStringLiteral("CN"), Qt::CaseInsensitive) == 0) return QStringLiteral("Chinese");
    if (countryCode.compare(QStringLiteral("TW"), Qt::CaseInsensitive) == 0) return QStringLiteral("Taiwanese");
    return QString();
}

// Build the small meta-line shown under the title in the hero pane. Uses
// only the fields available on MediaPreview so it can paint instantly
// before the detail fetch returns.
QString buildPreviewMetaLine(const anilist::MediaPreview& preview)
{
    QStringList parts;
    if (!preview.genres.isEmpty())   parts << preview.genres.first().toLower();
    if (!preview.status.isEmpty())   parts << humanizeStatus(preview.status);
    if (preview.yearStarted > 0)     parts << QString::number(preview.yearStarted);
    const QString lang = humanizeOriginLanguage(preview.countryOfOrigin);
    if (!lang.isEmpty())             parts << lang;
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
    const QString lang = humanizeOriginLanguage(detail.preview.countryOfOrigin);
    if (!lang.isEmpty())                    parts << lang;
    return parts.join(QStringLiteral("  -  "));
}

// Pick the mangaka byline shown under the title per spec §3.3:
//   "<writer> · <artist>"  if both roles resolve from staff
//   "<writer>" or "<artist>" alone if only one role
//   ""                     if no Story/Art staff at all
// AniList sometimes returns one staff entry with role "Story & Art"
// (rare, e.g. solo-creator series like Berserk); handled as that string
// being the full byline.
QString pickMangakaByline(const QList<anilist::StaffEntry>& staff)
{
    QString writer, artist, both;
    for (const auto& se : staff) {
        const QString role = se.role.trimmed();
        if (role.compare(QStringLiteral("Story & Art"), Qt::CaseInsensitive) == 0
            || role.compare(QStringLiteral("Story and Art"), Qt::CaseInsensitive) == 0) {
            if (both.isEmpty()) both = se.name;
        } else if (role.compare(QStringLiteral("Story"),
                                Qt::CaseInsensitive) == 0
                || role.compare(QStringLiteral("Original Story"),
                                Qt::CaseInsensitive) == 0) {
            if (writer.isEmpty()) writer = se.name;
        } else if (role.compare(QStringLiteral("Art"),
                                Qt::CaseInsensitive) == 0) {
            if (artist.isEmpty()) artist = se.name;
        }
    }

    if (!both.isEmpty()) return QStringLiteral("by %1").arg(both);
    if (!writer.isEmpty() && !artist.isEmpty()) {
        return QStringLiteral("by %1 \xC2\xB7 %2").arg(writer, artist);  // U+00B7 middle dot
    }
    if (!writer.isEmpty()) return QStringLiteral("by %1").arg(writer);
    if (!artist.isEmpty()) return QStringLiteral("by %1").arg(artist);
    return QString();
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

QPixmap makeVolumeThumbPlaceholder()
{
    QPixmap pm(kVolumeThumbSize);
    pm.fill(QColor(QStringLiteral("#1c1c22")));

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(QStringLiteral("#2f2f36")), 1));
    p.drawRoundedRect(pm.rect().adjusted(0, 0, -1, -1), 4, 4);
    return pm;
}

QPixmap makeHeroCoverPlaceholder(const QString& title)
{
    QPixmap pm(kHeroCoverSize);
    pm.fill(QColor(QStringLiteral("#1c1c22")));

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(QStringLiteral("#2f2f36")), 1));
    p.drawRoundedRect(pm.rect().adjusted(0, 0, -1, -1), 5, 5);

    const QString initials = title.split(QRegularExpression(QStringLiteral("\\s+")),
                                         Qt::SkipEmptyParts)
        .mid(0, 2)
        .join(QString())
        .left(2)
        .toUpper();
    if (!initials.isEmpty()) {
        QFont f = p.font();
        f.setPointSize(18);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(QStringLiteral("#8b8b95")));
        p.drawText(pm.rect(), Qt::AlignCenter, initials);
    }

    return pm;
}

QString buildHeroMetaLine(const anilist::MediaDetail& detail)
{
    QStringList parts;
    if (detail.totalVolumes > 0) {
        parts << QStringLiteral("%1 volumes").arg(detail.totalVolumes);
    }
    if (!detail.preview.genres.isEmpty()) {
        parts << detail.preview.genres.first().toLower();
    }
    if (!detail.preview.status.isEmpty()) {
        parts << humanizeStatus(detail.preview.status).toLower();
    }
    if (detail.preview.yearStarted > 0) {
        parts << QString::number(detail.preview.yearStarted);
    }
    const QString lang = humanizeOriginLanguage(detail.preview.countryOfOrigin);
    if (!lang.isEmpty()) {
        parts << lang;
    }
    return parts.join(QStringLiteral("  -  "));
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

    // Task 13: cellClicked + currentCellChanged connects removed (m_volumesTable
    // no longer exists). Task 16: onVolumeCellClicked + onVolumeCurrentChanged
    // are now no-op stubs; VolumeTile owns its own click/toggle signals.

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
        "QWidget#ComicsSeriesHeroBlock {"
        "  background: transparent;"
        "}"
        "QLabel#ComicsSeriesHeroCover {"
        "  background: #1c1c22;"
        "  border: 1px solid #2f2f36;"
        "  border-radius: 5px;"
        "}"
        "QLabel#ComicsSeriesTitle {"
        "  color: #ffffff;"
        "  font-size: 24px;"
        "  font-weight: 700;"
        "  background: transparent;"
        "}"
        "QLabel#ComicsSeriesMangakaByline {"
        "  color: #c0a0ff;"
        "  font-size: 13px;"
        "  background: transparent;"
        "}"
        "QLabel#ComicsSeriesMetaLine {"
        "  color: #8b8b95;"
        "  font-size: 12px;"
        "  background: transparent;"
        "}"
        "QLabel#ComicsSeriesSynopsis {"
        "  color: #c8c8d0;"
        "  font-size: 13px;"
        "  background: transparent;"
        "}"
        "QWidget#ComicsSeriesHeroTagsRow {"
        "  background: transparent;"
        "}"
        "QLabel#ComicsSeriesHeroTagChip {"
        "  background: #1c1c22;"
        "  border: 1px solid #2a2a32;"
        "  border-radius: 999px;"
        "  color: #a8a8b4;"
        "  font-size: 11px;"
        "  padding: 2px 9px;"
        "}"
        // Task 16: QTableWidget#ComicsSeriesVolumesTable QSS removed --
        // volume rows are now VolumeTile QFrame widgets with their own QSS.
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

    // Force-refresh icon button. Unicode "↻" (U+21BB CLOCKWISE OPEN CIRCLE ARROW)
    // keeps the affordance icon-only without needing a new SVG asset; matches the
    // m_backButton "←" pattern. Click emits forceRefreshRequested, which
    // ComicsPage handles by re-loading from data/mangafire_catalog/.
    m_forceRefreshButton = new QPushButton(QStringLiteral("\xe2\x86\xbb"), this);
    m_forceRefreshButton->setObjectName(QStringLiteral("ComicsSeriesForceRefreshButton"));
    m_forceRefreshButton->setAccessibleName(QStringLiteral("ComicsSeriesForceRefreshButton"));
    m_forceRefreshButton->setAccessibleDescription(
        QStringLiteral("Force-refresh the catalog for this series."));
    m_forceRefreshButton->setToolTip(tr("Refresh wiki catalog"));
    m_forceRefreshButton->setFixedSize(32, 32);
    m_forceRefreshButton->setCursor(Qt::PointingHandCursor);
    m_forceRefreshButton->setStyleSheet(QStringLiteral(
        "QPushButton#ComicsSeriesForceRefreshButton {"
        "  background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.18);"
        "  border-radius: 6px;"
        "  color: #ddd;"
        "  font-size: 16px;"
        "}"
        "QPushButton#ComicsSeriesForceRefreshButton:hover {"
        "  background: rgba(255,255,255,0.12);"
        "  border-color: rgba(255,255,255,0.28);"
        "}"));
    connect(m_forceRefreshButton, &QPushButton::clicked,
            this, &ComicsSeriesView::forceRefreshRequested);
    actionRow->addWidget(m_forceRefreshButton, /*stretch*/ 0, Qt::AlignRight);

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

    // Left column: hero block + volume table
    auto* leftCol = new QVBoxLayout();
    leftCol->setSpacing(8);

    m_heroBlock = new QWidget(this);
    m_heroBlock->setObjectName(QStringLiteral("ComicsSeriesHeroBlock"));
    auto* heroLayout = new QHBoxLayout(m_heroBlock);
    heroLayout->setContentsMargins(0, 4, 0, 18);
    heroLayout->setSpacing(22);

    m_heroCoverLabel = new QLabel(m_heroBlock);
    m_heroCoverLabel->setObjectName(QStringLiteral("ComicsSeriesHeroCover"));
    m_heroCoverLabel->setFixedSize(kHeroCoverSize);
    m_heroCoverLabel->setAlignment(Qt::AlignCenter);
    m_heroCoverLabel->setScaledContents(false);
    heroLayout->addWidget(m_heroCoverLabel, 0, Qt::AlignTop);

    auto* heroTextStack = new QVBoxLayout();
    heroTextStack->setContentsMargins(0, 0, 0, 0);
    heroTextStack->setSpacing(8);

    m_title = new QLabel(m_heroBlock);
    m_title->setObjectName(QStringLiteral("ComicsSeriesTitle"));
    m_title->setWordWrap(true);
    heroTextStack->addWidget(m_title);

    m_mangakaByline = new QLabel(m_heroBlock);
    m_mangakaByline->setObjectName(QStringLiteral("ComicsSeriesMangakaByline"));
    m_mangakaByline->hide();
    heroTextStack->addWidget(m_mangakaByline);

    m_metaLine = new QLabel(m_heroBlock);
    m_metaLine->setObjectName(QStringLiteral("ComicsSeriesMetaLine"));
    heroTextStack->addWidget(m_metaLine);

    m_synopsis = new QLabel(m_heroBlock);
    m_synopsis->setObjectName(QStringLiteral("ComicsSeriesSynopsis"));
    m_synopsis->setWordWrap(true);
    m_synopsis->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_synopsis->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_synopsis->setMaximumWidth(720);
    heroTextStack->addWidget(m_synopsis);

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
    heroTextStack->addWidget(m_descShowMoreBtn, /*stretch*/ 0, Qt::AlignLeft);

    m_tagChipsRow = new QWidget(m_heroBlock);
    m_tagChipsRow->setObjectName(QStringLiteral("ComicsSeriesHeroTagsRow"));
    m_tagChipsLayout = new QHBoxLayout(m_tagChipsRow);
    m_tagChipsLayout->setContentsMargins(0, 0, 0, 0);
    m_tagChipsLayout->setSpacing(6);
    heroTextStack->addWidget(m_tagChipsRow);

    heroTextStack->addStretch(1);
    heroLayout->addLayout(heroTextStack, 1);
    leftCol->addWidget(m_heroBlock);

    // Future story-arcs slot reserved here per brainstorm Decision 7
    // (2026-05-18). v1 inserts nothing; future v1.x widget mounts before
    // the volume table.

    // --- Volume scroll surface (Task 13: VolumeTile container replaces QTableWidget). ---
    // QScrollArea wraps a QWidget host that owns a QVBoxLayout of VolumeTile rows.
    // New tiles are inserted at layout index count()-1 (before the trailing stretch).
    m_volumesScroll = new QScrollArea(this);
    m_volumesScroll->setObjectName(QStringLiteral("ComicsSeriesVolumesScroll"));
    m_volumesScroll->setWidgetResizable(true);
    m_volumesScroll->setFrameShape(QFrame::NoFrame);
    m_volumesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_volumesScroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));

    m_volumesHost = new QWidget(m_volumesScroll);
    m_volumesLayout = new QVBoxLayout(m_volumesHost);
    m_volumesLayout->setContentsMargins(0, 0, 0, 0);
    m_volumesLayout->setSpacing(0);
    m_volumesLayout->addStretch(1);   // pushes rows up; new tiles inserted at index count()-1

    m_volumesScroll->setWidget(m_volumesHost);

    leftCol->addWidget(m_volumesScroll, /*stretch*/ 1);

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
    m_loadingOverlay->setMessage(tr("Loading"));
    m_loadingOverlay->hide();

    // m_loadingSafetyTimer removed: cover loading is now direct-URL via
    // loadCoverUrlForVolume; m_pendingMediaLoads drives overlay hide timing.
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
    m_lastAppliedCoverUrlByVolume.clear();  // Fix 2: clear stale cover map at every series-change
    m_pendingMediaLoads = 0;   // Pass 3: reset overlay-hide counter for new series
    if (m_sourcesPanel) m_sourcesPanel->clear();

    // Paint immediately from preview (cheap data, no detail required for hero).
    m_title->setText(preview.title);
    m_synopsis->setText(stripDescriptionHtml(preview.description));
    m_metaLine->setText(buildPreviewMetaLine(preview));
    if (m_mangakaByline) {
        const QString byline = pickMangakaByline(preview.staff);
        if (byline.isEmpty()) {
            m_mangakaByline->clear();
            m_mangakaByline->hide();
        } else {
            m_mangakaByline->setText(byline);
            m_mangakaByline->show();
        }
    }
    populateHeroTags(preview.tags);
    if (m_heroCoverLabel) {
        m_heroCoverLabel->setPixmap(makeHeroCoverPlaceholder(preview.title));
    }
    loadHeroCoverUrl(preview.coverFullUrl);
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
    // Task 16: clear VolumeTile rows instead of QTableWidget setRowCount(0).
    qDeleteAll(m_volumeTiles);
    m_volumeTiles.clear();
    m_volumeTilesByVolumeNumber.clear();
    refreshLibraryButton();

    // Task 14 / Task 8 (WEEBCENTRAL_IDENTITY_PIVOT): kick off BookWalker
    // per-volume cover resolution. Overlay shown here; hidden on resolver
    // signal or safety timeout. populateVolumeRows (called after cache/detail
    // fetch) builds the rows first; resolver callbacks then paint per-volume
    // covers over the AniList thumbs.
    //
    // AniList-only path: synthesize a "anilist:<id>" key so the stale-guard
    // comparison in the resolver slots uses the same seriesKey idiom as the
    // WeebCentral path.
    // Fix 3: honor a catalog-slug key if showCatalogSeries() pre-set one.
    // This prevents zero-AniList catalog tiles from collapsing to "anilist:0".
    if (!m_pendingCatalogSeriesKey.isEmpty()) {
        m_currentSeriesKey = m_pendingCatalogSeriesKey;
        m_pendingCatalogSeriesKey.clear();  // one-shot consume
    } else {
        m_currentSeriesKey = QStringLiteral("anilist:%1").arg(preview.anilistId);
    }
    showLoadingOverlay();
    // Covers are painted by loadCoverUrlForVolume calls in populateVolumeRows /
    // populateVolumeRowsFromCatalog. hideLoadingOverlay is called when
    // m_pendingMediaLoads reaches 0 after all QNAM fetches complete.

    // PHASE 12: kick off banner async-load from preview (renderDetail will
    // re-fire with detail.preview.bannerUrl after the cache hit / refetch
    // lands; the URL identity makes QPixmapCache a synchronous-hit second time).
    loadBannerUrl(preview.bannerUrl);

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

// WEEBCENTRAL_IDENTITY_PIVOT Task 8: WeebCentral-sourced series entry point.
// Mirrors showSeries(MediaPreview) but drives the cover resolver via the
// seriesKey composite and fetches chapter/volume detail via MangaSourceRegistry.
void ComicsSeriesView::showSeries(const MangaResult& wc)
{
    if (wc.id.isEmpty() || wc.source.isEmpty()) {
        qWarning("ComicsSeriesView::showSeries(MangaResult): empty id or source — ignoring");
        return;
    }

    // Reset description expand state on series navigation (mirrors MediaPreview path).
    m_descExpanded = false;
    if (m_descShowMoreBtn) {
        m_descShowMoreBtn->hide();
        m_descShowMoreBtn->setText(tr("Show more"));
    }
    if (m_synopsis) {
        const QFontMetrics fm(m_synopsis->font());
        m_synopsis->setMaximumHeight(fm.lineSpacing() * m_descClampLines);
    }

    m_currentSeriesKey   = wc.source + QStringLiteral(":") + wc.id;
    m_currentSeriesTitle = wc.title;
    m_currentVolumeRows.clear();
    m_lastAppliedCoverUrlByVolume.clear();  // Fix 2: clear stale cover map at every series-change
    m_pendingMediaLoads = 0;   // Pass 3: reset overlay-hide counter for new series
    if (m_sourcesPanel) m_sourcesPanel->clear();

    // Paint immediately from available preview data.
    m_title->setText(wc.title);
    m_synopsis->clear();
    // FANDOM_LOCAL_LOADER_INTEGRATION 2026-05-22 (Agent 1, Hemanth-flagged) --
    // Meta-line cleared. The previous source-name label ("WeebCentral",
    // "Tankoyomi", etc.) was metadata-source-implementation-detail leaking
    // into the view; the only time it mattered was when it lied
    // (Fandom-derived volume rows under a WeebCentral banner). Hemanth's
    // call: drop entirely, not flip.
    m_metaLine->clear();
    if (m_mangakaByline) {
        m_mangakaByline->clear();
        m_mangakaByline->hide();
    }
    populateHeroTags(QStringList());
    if (m_heroCoverLabel) {
        m_heroCoverLabel->setPixmap(makeHeroCoverPlaceholder(wc.title));
    }
    loadHeroCoverUrl(wc.thumbnailUrl);
    // Task 16: clear VolumeTile rows instead of QTableWidget setRowCount(0).
    qDeleteAll(m_volumeTiles);
    m_volumeTiles.clear();
    m_volumeTilesByVolumeNumber.clear();
    refreshLibraryButton();

    // Banner: use thumbnailUrl from MangaResult as the hero image until an
    // AniList augmentation lookup overrides it (v1.x decoration; out of scope).
    if (!wc.thumbnailUrl.isEmpty()) {
        loadBannerUrl(wc.thumbnailUrl);
    }

    showLoadingOverlay();
    // Covers are painted by loadCoverUrlForVolume calls in populateVolumeRows /
    // populateVolumeRowsFromCatalog. hideLoadingOverlay is called when
    // m_pendingMediaLoads reaches 0 after all QNAM fetches complete.

    // Trigger detail fetch via the WeebCentral scraper (delivers chapter/volume
    // list via detailReady signal — caller must connect scraper::detailReady to
    // this view's population slots, or route via ComicsPage).
    if (m_sourceRegistry) {
        if (MangaScraper* scraper = m_sourceRegistry->find(wc.source)) {
            scraper->fetchDetail(wc);
        } else {
            qWarning("ComicsSeriesView::showSeries(MangaResult): no scraper for source '%s'",
                     qUtf8Printable(wc.source));
        }
    } else {
        qWarning("ComicsSeriesView::showSeries(MangaResult): no source registry set");
    }
}

// Fix 3: catalog-tile entry point that sets a slug-based key when AniList id
// is unavailable, preventing all zero-id catalog series from sharing "anilist:0".
void ComicsSeriesView::showCatalogSeries(const QString& seriesId,
                                          const QString& title,
                                          int            anilistId)
{
    // Pre-set the one-shot override so showSeries(MediaPreview) picks it up.
    if (anilistId <= 0) {
        m_pendingCatalogSeriesKey = QStringLiteral("mangafire:%1").arg(seriesId);
    } else {
        m_pendingCatalogSeriesKey.clear();
    }

    tankoban::manga::anilist::MediaPreview preview;
    preview.anilistId = anilistId;
    preview.title     = title;
    showSeries(preview);
}

void ComicsSeriesView::clearView()
{
    m_currentAnilistId   = 0;
    m_pendingSeriesReqId = -1;
    m_currentSeriesTitle.clear();
    m_currentVolumeRows.clear();

    m_title->clear();
    if (m_heroCoverLabel) m_heroCoverLabel->clear();
    if (m_mangakaByline) {
        m_mangakaByline->clear();
        m_mangakaByline->hide();
    }
    populateHeroTags(QStringList());
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

    // Task 8 (WEEBCENTRAL_IDENTITY_PIVOT): reset seriesKey-based guards.
    m_currentSeriesKey.clear();

    m_lastAppliedCoverUrlByVolume.clear();
    hideLoadingOverlay();  // stops the safety timer before clearing rows
    // Task 16: clear VolumeTile rows instead of QTableWidget setRowCount(0).
    qDeleteAll(m_volumeTiles);
    m_volumeTiles.clear();
    m_volumeTilesByVolumeNumber.clear();
    m_currentVolumeRows.clear();
    m_selectedRows.clear();
    if (m_downloadSelectedBtn) m_downloadSelectedBtn->hide();
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

    // Task 16: no QTableWidget rowCount; use tile list instead.
    if (m_volumeTiles.isEmpty()) {
        renderEmpty(reason);
    }
}

void ComicsSeriesView::renderDetail(const anilist::MediaDetail& detail)
{
    // Refresh the hero pane with the richer detail data.
    if (!detail.preview.title.isEmpty()) {
        m_title->setText(detail.preview.title);
    }
    if (m_mangakaByline) {
        const QString byline = pickMangakaByline(detail.preview.staff);
        if (byline.isEmpty()) {
            m_mangakaByline->clear();
            m_mangakaByline->hide();
        } else {
            m_mangakaByline->setText(byline);
            m_mangakaByline->show();
        }
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
    m_metaLine->setText(buildHeroMetaLine(detail));
    populateHeroTags(detail.preview.tags);
    if (m_heroCoverLabel && !detail.preview.title.isEmpty()
        && m_heroCoverLabel->pixmap(Qt::ReturnByValue).isNull()) {
        m_heroCoverLabel->setPixmap(makeHeroCoverPlaceholder(detail.preview.title));
    }
    loadHeroCoverUrl(detail.preview.coverFullUrl);

    populateVolumeRows(anilist::AniListVolumeMapper::map(detail), &detail);

    // Phase 8a Wave 2: banner slot exists only for a real AniList banner.
    // Never stretch the portrait cover into this landscape band.
    loadBannerUrl(detail.preview.bannerUrl);
}

void ComicsSeriesView::populateVolumeRows(const QList<anilist::VolumeRow>& rows,
                                          const anilist::MediaDetail* detail)
{
    // Tear down existing tiles.
    qDeleteAll(m_volumeTiles);
    m_volumeTiles.clear();
    m_volumeTilesByVolumeNumber.clear();

    // PHASE 8: cache the mapped rows so downstream slots (e.g. onVolumeCellClicked
    // legacy path, populateSourcesForRow) can hand the full VolumeRow to the
    // sources panel without rerunning the mapper.
    m_currentVolumeRows = rows;
    // Also stash the canonical title for the panel populate() call.
    if (detail && !detail->preview.title.isEmpty()) {
        m_currentSeriesTitle = detail->preview.title;
    }

    // Clear stale selection state on every call so navigating between series
    // resets the checkbox set + hides the Download Selected button.
    m_selectedRows.clear();
    if (m_downloadSelectedBtn) m_downloadSelectedBtn->hide();

    // AniList path: synthesize stable identity keys.
    // Two-stage lookup mirrors the old QTableWidget cbzPath resolution:
    //   1. If the fandom catalog knows this AniList ID, use its
    //      (kPremiumSourceId, catalog.seriesId) key so downloads landed via
    //      Fandom-aware paths are found.
    //   2. Otherwise fall back to ("anilist_N") slug used by the premium +
    //      weebcentral download paths.
    const QString fallbackSeriesId = QStringLiteral("anilist_%1").arg(m_currentAnilistId);

    for (const auto& row : rows) {
        // Resolve the download-index entry for this volume (same two-stage
        // logic the old code did inline per row).
        QString resolvedCbzPath;
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
                dlEntry = m_downloadIndex->entryForSeriesAndVolume(
                    QString::fromLatin1(kPremiumSourceId),
                    fallbackSeriesId,
                    row.volumeNumber);
            }
            if (!dlEntry) {
                dlEntry = m_downloadIndex->entryForSeriesAndVolume(
                    QString::fromLatin1(kWeebCentralSourceId),
                    fallbackSeriesId,
                    row.volumeNumber);
            }
            if (dlEntry && !dlEntry->canonicalPath.isEmpty()) {
                resolvedCbzPath = dlEntry->canonicalPath;
            }
        }

        // Synthesize display title: VolumeRow has no per-volume title field
        // (AniList doesn't expose one). Mirror the old volLabelText pattern.
        const QString synthesizedTitle = row.isVolumeX
            ? tr("Volume X")
            : tr("Volume %1").arg(row.volumeNumber);

        // Synthesize chapter range string from the numeric fields VolumeRow
        // carries. Mirror the formatChapterRange tooltip from the old path.
        const QString synthesizedChapterRange =
            (row.chapterRangeStart > 0 && row.chapterRangeEnd > 0)
            ? QStringLiteral("ch %1-%2").arg(row.chapterRangeStart).arg(row.chapterRangeEnd)
            : QString();

        // Cover URL: prefer per-volume art thumbnail; fall back to series
        // coverThumbUrl from detail.
        const QString resolvedCoverUrl = detail
            ? (!row.art.thumbnailUrl.isEmpty()
                ? row.art.thumbnailUrl
                : detail->preview.coverThumbUrl)
            : row.art.thumbnailUrl;

        tankoban::ui::comics::VolumeTileData data;
        data.sourceId     = QStringLiteral("anilist");
        data.seriesId     = fallbackSeriesId;
        data.volumeNumber = row.volumeNumber;
        data.title        = synthesizedTitle;
        data.chapterRange = synthesizedChapterRange;
        data.coverUrl     = resolvedCoverUrl;

        auto* tile = new tankoban::ui::comics::VolumeTile(data, m_volumesHost);

        tankoban::ui::comics::VolumeTileState state;
        state.provenance = QString();  // AniList-only path: no source badge
        if (!resolvedCbzPath.isEmpty()) {
            state.state   = tankoban::ui::comics::VolumeTileState::Complete;
            state.cbzPath = resolvedCbzPath;
        }
        tile->setVolumeState(state);
        tile->setMangaDownloadIndex(m_downloadIndex);

        const QString seriesIdCapture = fallbackSeriesId;
        connect(tile, &tankoban::ui::comics::VolumeTile::openRequested,
                this, [this, seriesIdCapture](int vn) {
                    const auto entry = m_downloadIndex
                        ? m_downloadIndex->entryForSeriesAndVolume(
                              QStringLiteral("anilist"),
                              seriesIdCapture, vn)
                        : std::nullopt;
                    emit openVolume(vn, entry ? entry->canonicalPath : QString());
                });
        connect(tile, &tankoban::ui::comics::VolumeTile::downloadRequested,
                this, [this](int /*vn*/) {
                    populateSourcesForRow(-1);
                });
        connect(tile, &tankoban::ui::comics::VolumeTile::toggledShift,
                this, [this](bool checked, bool shiftHeld) {
                    auto* src = qobject_cast<tankoban::ui::comics::VolumeTile*>(sender());
                    if (!src) return;
                    const int vn = src->volumeNumber();

                    if (shiftHeld && m_lastBulkAnchorVolume > 0) {
                        const int lo = std::min(m_lastBulkAnchorVolume, vn);
                        const int hi = std::max(m_lastBulkAnchorVolume, vn);
                        for (int v = lo; v <= hi; ++v) {
                            auto* t = m_volumeTilesByVolumeNumber.value(v, nullptr);
                            if (!t) continue;
                            t->setCheckedQuiet(checked);
                            if (checked) m_selectedRows.insert(v);
                            else         m_selectedRows.remove(v);
                        }
                    } else {
                        if (checked) m_selectedRows.insert(vn);
                        else         m_selectedRows.remove(vn);
                    }
                    m_lastBulkAnchorVolume = vn;

                    if (m_downloadSelectedBtn) {
                        m_downloadSelectedBtn->setText(
                            QStringLiteral("Download Selected (%1)").arg(m_selectedRows.size()));
                        m_downloadSelectedBtn->setVisible(!m_selectedRows.isEmpty());
                    }
                });

        m_volumeTiles.append(tile);
        m_volumeTilesByVolumeNumber.insert(row.volumeNumber, tile);
        m_volumesLayout->insertWidget(m_volumesLayout->count() - 1, tile);
    }

    // next-unread: proxy for "unread" is no resolved cbz path. Walk the
    // freshly-built tile list (no QTableWidget rows to scan any more).
    // The highlight + scroll-into-view wiring on the VolumeTile widget
    // itself lands in a later task; for now we track the index so callers
    // of m_nextUnreadRow still get a sane value.
    m_nextUnreadRow = -1;
    for (int i = 0; i < m_volumeTiles.size(); ++i) {
        const auto* t = m_volumeTiles.at(i);
        if (t && t->volumeState().cbzPath.isEmpty()) {
            m_nextUnreadRow = i;
            break;
        }
    }

    Q_UNUSED(detail);
}

// -----------------------------------------------------------------------
// -----------------------------------------------------------------------
// populateVolumeRowsFromCatalog (COMICS_MANGAFIRE_PIVOT Phase B.2, 2026-05-23)
// -----------------------------------------------------------------------
// Consumes MangaCatalog::volumes (MangaFire schema) and emits one VolumeTile
// per volume. Covers load via direct CDN URL (MangaVolume::coverUrlJapanese)
// via loadCoverUrlForVolume — no BookWalker resolver needed.
namespace {
QString clipSynopsisSnippet(const QString& synopsis, int maxChars = 120)
{
    if (synopsis.size() <= maxChars) return synopsis;
    return synopsis.left(maxChars - 1).trimmed() + QStringLiteral("…");
}
} // namespace

void ComicsSeriesView::populateVolumeRowsFromCatalog(
    const tankoban::manga::MangaCatalog& catalog)
{
    // Tear down existing tiles.
    qDeleteAll(m_volumeTiles);
    m_volumeTiles.clear();
    m_volumeTilesByVolumeNumber.clear();

    // Pre-population cleanup (mirrors prior function head).
    m_currentVolumeRows.clear();
    m_selectedRows.clear();
    if (m_downloadSelectedBtn) m_downloadSelectedBtn->hide();

    // Clear meta-line — source-name leaking into view has no user value.
    if (m_metaLine) {
        m_metaLine->clear();
    }

    // Insert each volume as a VolumeTile row, just before the trailing
    // stretch in m_volumesLayout (index = count() - 1).
    for (const auto& vol : catalog.volumes) {
        tankoban::ui::comics::VolumeTileData data;
        data.sourceId     = QStringLiteral("mangafire_catalog");
        data.seriesId     = catalog.seriesId;
        data.volumeNumber = vol.volumeNumber;
        data.title        = !vol.titleEnglish.isEmpty() ? vol.titleEnglish : vol.titleJapanese;
        // chapterStartRaw/chapterEndRaw carry the raw MangaFire strings (e.g. "0.01", "5.5");
        // fall back to integer range if raws are absent.
        if (!vol.chapterStartRaw.isEmpty() || !vol.chapterEndRaw.isEmpty()) {
            data.chapterRange = QStringLiteral("ch %1-%2")
                                    .arg(vol.chapterStartRaw)
                                    .arg(vol.chapterEndRaw);
        } else {
            data.chapterRange = QStringLiteral("ch %1-%2")
                                    .arg(vol.chapterRangeStart)
                                    .arg(vol.chapterRangeEnd);
        }
        // MangaFire volumes carry a direct CDN cover URL in coverUrlJapanese
        // (mapped from JSON "coverUrl"); fall back to English cover if present.
        data.coverUrl     = !vol.coverUrlJapanese.isEmpty()
                              ? vol.coverUrlJapanese
                              : vol.coverUrlEnglish;

        auto* tile = new tankoban::ui::comics::VolumeTile(data, m_volumesHost);

        tankoban::ui::comics::VolumeTileState state;
        state.provenance = QStringLiteral("MangaFire");
        tile->setVolumeState(state);
        tile->setMangaDownloadIndex(m_downloadIndex);

        const QString seriesIdSnapshot = catalog.seriesId;
        connect(tile, &tankoban::ui::comics::VolumeTile::openRequested,
                this, [this, seriesIdSnapshot](int vn) {
                    const auto entry = m_downloadIndex
                        ? m_downloadIndex->entryForSeriesAndVolume(
                              QStringLiteral("mangafire_catalog"),
                              seriesIdSnapshot, vn)
                        : std::nullopt;
                    emit openVolume(vn, entry ? entry->canonicalPath : QString());
                });
        connect(tile, &tankoban::ui::comics::VolumeTile::downloadRequested,
                this, [this](int /*vn*/) {
                    // Route through existing source-panel dispatch path.
                    // populateSourcesForRow takes a table-row index from the
                    // old QTableWidget world. For VolumeTile world the source
                    // panel is driven by volume-number via the sources panel
                    // directly — Task 16 fully rewires this path.
                    populateSourcesForRow(-1);
                });
        connect(tile, &tankoban::ui::comics::VolumeTile::toggledShift,
                this, [this](bool checked, bool shiftHeld) {
                    auto* src = qobject_cast<tankoban::ui::comics::VolumeTile*>(sender());
                    if (!src) return;
                    const int vn = src->volumeNumber();

                    if (shiftHeld && m_lastBulkAnchorVolume > 0) {
                        const int lo = std::min(m_lastBulkAnchorVolume, vn);
                        const int hi = std::max(m_lastBulkAnchorVolume, vn);
                        for (int v = lo; v <= hi; ++v) {
                            auto* t = m_volumeTilesByVolumeNumber.value(v, nullptr);
                            if (!t) continue;
                            t->setCheckedQuiet(checked);
                            if (checked) m_selectedRows.insert(v);
                            else         m_selectedRows.remove(v);
                        }
                    } else {
                        if (checked) m_selectedRows.insert(vn);
                        else         m_selectedRows.remove(vn);
                    }
                    m_lastBulkAnchorVolume = vn;

                    if (m_downloadSelectedBtn) {
                        m_downloadSelectedBtn->setText(
                            QStringLiteral("Download Selected (%1)").arg(m_selectedRows.size()));
                        m_downloadSelectedBtn->setVisible(!m_selectedRows.isEmpty());
                    }
                });

        m_volumeTiles.append(tile);
        m_volumeTilesByVolumeNumber.insert(vol.volumeNumber, tile);
        m_volumesLayout->insertWidget(m_volumesLayout->count() - 1, tile);

        // Paint the catalog's own CDN cover URL for this volume. The async fetch
        // has a stale-series guard (m_currentSeriesKey) which correctly differentiates
        // catalog slugs ("mangafire:<slug>") from AniList/WeebCentral series.
        if (!data.coverUrl.isEmpty()) {
            loadCoverUrlForVolume(data.coverUrl, vol.volumeNumber);
        }
    }

    // Cache current identity for downstream slots (existing behavior).
    m_currentSeriesTitle = !catalog.seriesTitle.isEmpty()
                            ? catalog.seriesTitle
                            : catalog.seriesId;

    // No next-unread highlight on the MangaFire catalog path in v1 — that
    // hinges on cbzPath stash which the catalog doesn't carry. The Tankoyomi-
    // download-linkage refresh in a future task will overlay highlight +
    // status icons on top of the rendered tiles.
    m_nextUnreadRow = -1;
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
    // Task 16: clear VolumeTile rows instead of QTableWidget setRowCount(0).
    qDeleteAll(m_volumeTiles);
    m_volumeTiles.clear();
    m_volumeTilesByVolumeNumber.clear();
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


void ComicsSeriesView::showLoadingOverlay()
{
    if (!m_loadingOverlay) return;
    m_loadingOverlay->setGeometry(rect());
    m_loadingOverlay->raise();
    m_loadingOverlay->show();
    // Pass 3-followup 2026-05-19: re-sync geometry on the next event-loop
    // tick. showLoadingOverlay is typically called from inside showSeries
    // (right after the QStackedWidget switches to this view); at that moment
    // the view has just become current but the layout pass hasn't run yet,
    // so rect() may still report the widget's pre-layout default size and
    // the overlay paints in a tiny upper-left strip. Hemanth flagged this
    // 2026-05-19: "loading covers toast went to the top left corner and
    // melted into the back button." Deferring one tick lets the layout
    // settle; resizeEvent below handles any later parent resizes.
    QPointer<ComicsSeriesView> self(this);
    QTimer::singleShot(0, this, [self]() {
        if (!self || !self->m_loadingOverlay) return;
        if (self->m_loadingOverlay->isVisible()) {
            self->m_loadingOverlay->setGeometry(self->rect());
        }
    });
}

void ComicsSeriesView::hideLoadingOverlay()
{
    if (m_loadingOverlay) m_loadingOverlay->hide();
}

void ComicsSeriesView::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    // Pass 3-followup 2026-05-19: keep the loading overlay covering the
    // full view on any parent resize (window resize, QStackedWidget settling
    // after a tile click). Without this, the overlay only gets sized once
    // in showLoadingOverlay and stays at that geometry forever.
    if (m_loadingOverlay && m_loadingOverlay->isVisible()) {
        m_loadingOverlay->setGeometry(rect());
    }
}

// Resolver slots removed in COMICS_MANGAFIRE_PIVOT Phase B:
// onCoverResolverResolved / onCoverResolverUnresolved / onCoverResolverSkipped /
// onCoverResolverSafetyTimeout / paintVolumeCovers / paintVolumeCoversAsFallback
// — MangaFire volumes carry direct CDN URLs; covers load via
// loadCoverUrlForVolume inside populateVolumeRowsFromCatalog.

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

    // Pass 3 fix 2026-05-19: inc counter for in-flight async media fetches;
    // decremented in the finished lambda below. Drives the loading overlay
    // hide-timing — overlay stays visible until counter reaches 0.
    m_pendingMediaLoads++;

    QPointer<ComicsSeriesView> self(this);
    // WEEBCENTRAL_IDENTITY_PIVOT post-pivot fix 2026-05-19: stale-guard re-keyed
    // from m_currentAnilistId (int) to m_currentSeriesKey (QString). The
    // anilistId-keyed guard was a no-op on the WeebCentral path (anilistId
    // stays 0 across all WC series), so prior series' covers leaked onto the
    // newly-opened series until the new fetch landed. seriesKey is the
    // unified identity for both AniList ("anilist:<id>") and WeebCentral
    // ("<source>:<id>") paths; see showSeries(MediaPreview):617 and
    // showSeries(MangaResult):677.
    const QString snapshotSeriesKey = m_currentSeriesKey;
    const QUrl coverUrl(url);
    QNetworkRequest req(coverUrl);
    // FANDOM_LOCAL_LOADER hotfix 2026-05-22 (Agent 1): Cloudflare-friendly UA.
    // Previous "Tankoban/1.0" suffix triggered Fandom's Cloudflare bot heuristics
    // -> 403 on static.wikia.nocookie.net cover URLs from our local catalog.
    // Mirror of the UA we used in scripts/fandom_scraper/backfill_covers.py.
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QString::fromLatin1(
                      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                      "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"));
    QNetworkReply* reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [self, reply, url, volumeNumber, snapshotSeriesKey]() {
        reply->deleteLater();
        if (!self) return;
        // Pass 3: decrement counter unconditionally (even for stale replies
        // — the counter belongs to the series the fetch was initiated for,
        // and a fresh showSeries would have reset the counter anyway, so
        // the decrement is only meaningful if the series didn't change).
        if (self->m_pendingMediaLoads > 0) self->m_pendingMediaLoads--;
        const bool stillCurrentSeries = (self->m_currentSeriesKey == snapshotSeriesKey);
        auto maybeHideOverlay = [self]() {
            if (self->m_pendingMediaLoads == 0) self->hideLoadingOverlay();
        };
        // Stale-series guard: discard the pixmap if user navigated away
        // during the fetch (still let the overlay-hide check run for the
        // current series — the counter is shared).
        if (!stillCurrentSeries) {
            maybeHideOverlay();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("loadCoverUrlForVolume: fetch failed url=%s vol=%d error=%s",
                     qUtf8Printable(url), volumeNumber, qUtf8Printable(reply->errorString()));
            maybeHideOverlay();
            return;
        }
        const QByteArray data = reply->readAll();
        QPixmap pm;
        if (!pm.loadFromData(data)) {
            qWarning("loadCoverUrlForVolume: pixmap decode failed url=%s vol=%d bytes=%lld",
                     qUtf8Printable(url), volumeNumber, static_cast<long long>(data.size()));
            maybeHideOverlay();
            return;
        }
        QPixmapCache::insert(url, pm);
        self->applyPixmapToVolumeRow(volumeNumber, pm);
        maybeHideOverlay();
    });
}

void ComicsSeriesView::loadHeroCoverUrl(const QString& url)
{
    if (!m_heroCoverLabel || url.trimmed().isEmpty()) return;

    QPixmap cached;
    if (QPixmapCache::find(url, &cached)) {
        applyHeroCoverPixmap(cached);
        return;
    }

    if (url.startsWith(QStringLiteral("file:///"), Qt::CaseInsensitive)) {
        QPixmap pm(QUrl(url).toLocalFile());
        if (!pm.isNull()) {
            QPixmapCache::insert(url, pm);
            applyHeroCoverPixmap(pm);
            return;
        }
    }

    QNetworkAccessManager* nam = m_client ? m_client->networkManager() : nullptr;
    if (!nam) return;

    QPointer<ComicsSeriesView> self(this);
    const QString snapshotSeriesKey = m_currentSeriesKey;
    QNetworkRequest req{QUrl(url)};
    // FANDOM_LOCAL_LOADER hotfix 2026-05-22 (Agent 1): Cloudflare-friendly UA.
    // Previous "Tankoban/1.0" suffix triggered Fandom's Cloudflare bot heuristics
    // -> 403 on static.wikia.nocookie.net cover URLs from our local catalog.
    // Mirror of the UA we used in scripts/fandom_scraper/backfill_covers.py.
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QString::fromLatin1(
                      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                      "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"));
    QNetworkReply* reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [self, reply, url, snapshotSeriesKey]() {
        reply->deleteLater();
        if (!self || self->m_currentSeriesKey != snapshotSeriesKey) return;
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("loadHeroCoverUrl: fetch failed url=%s error=%s",
                     qUtf8Printable(url), qUtf8Printable(reply->errorString()));
            return;
        }
        const QByteArray data = reply->readAll();
        QPixmap pm;
        if (!pm.loadFromData(data)) {
            qWarning("loadHeroCoverUrl: pixmap decode failed url=%s bytes=%lld",
                     qUtf8Printable(url), static_cast<long long>(data.size()));
            return;
        }
        QPixmapCache::insert(url, pm);
        self->applyHeroCoverPixmap(pm);
    });
}

void ComicsSeriesView::loadBannerUrl(const QString& url)
{
    // STREAM_PORT 2026-05-18 Task 1: was full-viewport paintEvent wallpaper;
    // now paints onto m_heroBannerLabel at 140px height. Uses
    // KeepAspectRatioByExpanding so the banner image fills the 140px band
    // (horizontal slice, centered). Mirrors StreamDetailView's hero label
    // approach.
    if (!m_heroBannerLabel) return;
    if (url.trimmed().isEmpty()) {
        m_heroBannerLabel->clear();
        m_heroBannerLabel->hide();
        m_lastBannerUrl.clear();
        return;
    }

    // WEEBCENTRAL_IDENTITY_PIVOT post-pivot Pass 1 fix 2026-05-19: on
    // series-switch (new URL differs from what's currently painted), wipe
    // the label pixmap before kicking off the load so the previous series'
    // banner does not leak into the new view. clearView() intentionally
    // does NOT wipe the banner pixmap (2026-05-18 hero-instant-load fix at
    // line 732+); that contract only makes sense for same-URL re-opens.
    // For different URLs the prior pixmap is a stale leak — Hemanth flagged
    // this 2026-05-19: "Death Note's banner shows Berserk's cover."
    // For same-URL re-opens we skip the wipe so the QPixmapCache hit can
    // replace atomically with no flicker (instant-load contract preserved).
    if (url != m_lastBannerUrl) {
        m_heroBannerLabel->clear();   // wipe pixmap; widget stays sized + visible
        m_lastBannerUrl = url;
    }

    QPixmap cached;
    if (QPixmapCache::find(url, &cached)) {
        applyBannerPixmap(cached);
        return;
    }

    // WEEBCENTRAL_IDENTITY_PIVOT post-pivot Pass 2 fix 2026-05-19: local-file
    // URLs (library records: "file:///C:/.../cover.jpg") load synchronously
    // via QPixmap::load, skipping the QNetworkAccessManager round-trip. QNAM
    // *does* handle file:/// scheme, but the request is queued onto the event
    // loop so even a local file takes ~50-200ms to land — long enough for
    // Hemanth to see a blank banner gap on every library open ("Death Note's
    // banner takes its sweet time to load"). Direct QPixmap::load is single-
    // digit ms file IO. We still insert into QPixmapCache so subsequent re-
    // opens of the same library series hit instantly via the cache path
    // above. Failure falls through to the async path as a safety net.
    if (url.startsWith(QStringLiteral("file:///"), Qt::CaseInsensitive)) {
        const QString localPath = QUrl(url).toLocalFile();
        QPixmap pm;
        if (pm.load(localPath)) {
            QPixmapCache::insert(url, pm);
            applyBannerPixmap(pm);
            return;
        }
        qWarning("loadBannerUrl: local-file load failed url=%s path=%s",
                 qUtf8Printable(url), qUtf8Printable(localPath));
        // Fall through to async path below.
    }

    QNetworkAccessManager* nam = m_client ? m_client->networkManager() : nullptr;
    if (!nam) return;

    // Pass 3 fix 2026-05-19: inc counter for in-flight async media fetches;
    // decremented in the finished lambda below. Drives the loading overlay
    // hide-timing — overlay stays visible until counter reaches 0.
    m_pendingMediaLoads++;

    QPointer<ComicsSeriesView> self(this);
    // WEEBCENTRAL_IDENTITY_PIVOT post-pivot fix 2026-05-19: stale-guard re-keyed
    // from m_currentAnilistId (int) to m_currentSeriesKey (QString). Same bug
    // class as loadCoverUrlForVolume — banner from a prior WeebCentral series
    // would paint onto the newly-opened series because both shared anilistId=0
    // and the guard never fired. See the longer comment at the cover variant.
    const QString snapshotSeriesKey = m_currentSeriesKey;
    const QUrl bannerUrl(url);
    QNetworkRequest req(bannerUrl);
    // FANDOM_LOCAL_LOADER hotfix 2026-05-22 (Agent 1): Cloudflare-friendly UA.
    // Previous "Tankoban/1.0" suffix triggered Fandom's Cloudflare bot heuristics
    // -> 403 on static.wikia.nocookie.net cover URLs from our local catalog.
    // Mirror of the UA we used in scripts/fandom_scraper/backfill_covers.py.
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QString::fromLatin1(
                      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                      "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"));
    QNetworkReply* reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [self, reply, url, snapshotSeriesKey]() {
        reply->deleteLater();
        if (!self) return;
        // Pass 3: decrement counter unconditionally (see loadCoverUrlForVolume
        // for the rationale around stale replies + the shared counter).
        if (self->m_pendingMediaLoads > 0) self->m_pendingMediaLoads--;
        const bool stillCurrentSeries = (self->m_currentSeriesKey == snapshotSeriesKey);
        auto maybeHideOverlay = [self]() {
            if (self->m_pendingMediaLoads == 0) self->hideLoadingOverlay();
        };
        if (!stillCurrentSeries) {
            maybeHideOverlay();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("loadBannerUrl: fetch failed url=%s error=%s",
                     qUtf8Printable(url), qUtf8Printable(reply->errorString()));
            maybeHideOverlay();
            return;
        }
        const QByteArray data = reply->readAll();
        QPixmap pm;
        if (!pm.loadFromData(data)) {
            qWarning("loadBannerUrl: pixmap decode failed url=%s bytes=%lld",
                     qUtf8Printable(url), static_cast<long long>(data.size()));
            maybeHideOverlay();
            return;
        }
        QPixmapCache::insert(url, pm);
        self->applyBannerPixmap(pm);
        maybeHideOverlay();
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

void ComicsSeriesView::applyHeroCoverPixmap(const QPixmap& pm)
{
    if (!m_heroCoverLabel || pm.isNull()) return;
    const QPixmap scaled = pm.scaled(kHeroCoverSize,
                                     Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
    m_heroCoverLabel->setPixmap(scaled);
}

void ComicsSeriesView::populateHeroTags(const QStringList& genres)
{
    if (!m_tagChipsRow || !m_tagChipsLayout) return;

    while (QLayoutItem* item = m_tagChipsLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    QStringList chips;
    for (const QString& genre : genres) {
        const QString trimmed = genre.trimmed();
        if (trimmed.isEmpty() || chips.contains(trimmed, Qt::CaseInsensitive)) continue;
        chips << trimmed;
        if (chips.size() >= 5) break;
    }

    for (const QString& chipText : chips) {
        auto* chip = new QLabel(chipText.toLower(), m_tagChipsRow);
        chip->setObjectName(QStringLiteral("ComicsSeriesHeroTagChip"));
        chip->setTextFormat(Qt::PlainText);
        chip->setAlignment(Qt::AlignCenter);
        m_tagChipsLayout->addWidget(chip, 0, Qt::AlignLeft);
    }
    m_tagChipsLayout->addStretch(1);
    m_tagChipsRow->setVisible(!chips.isEmpty());
}

void ComicsSeriesView::populateHeroTags(const QList<anilist::RankedTag>& tags)
{
    if (!m_tagChipsRow || !m_tagChipsLayout) return;

    while (QLayoutItem* item = m_tagChipsLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    // Sort descending by rank, copy because input is const&.
    QList<anilist::RankedTag> sorted = tags;
    std::sort(sorted.begin(), sorted.end(),
        [](const anilist::RankedTag& a, const anilist::RankedTag& b) {
            return a.rank > b.rank;
        });

    QStringList chips;
    const QSet<QString>& demographics = kDemographicTagNames();
    for (const auto& t : sorted) {
        if (t.isSpoiler) continue;
        const QString trimmed = t.name.trimmed();
        const QString lower = trimmed.toLower();
        if (trimmed.isEmpty()) continue;
        if (demographics.contains(lower)) continue;
        if (chips.contains(trimmed, Qt::CaseInsensitive)) continue;
        chips << trimmed;
        if (chips.size() >= 5) break;
    }

    for (const QString& chipText : chips) {
        auto* chip = new QLabel(chipText.toLower(), m_tagChipsRow);
        chip->setObjectName(QStringLiteral("ComicsSeriesHeroTagChip"));
        chip->setTextFormat(Qt::PlainText);
        chip->setAlignment(Qt::AlignCenter);
        m_tagChipsLayout->addWidget(chip, 0, Qt::AlignLeft);
    }
    m_tagChipsLayout->addStretch(1);
    m_tagChipsRow->setVisible(!chips.isEmpty());
}

void ComicsSeriesView::applyPixmapToVolumeRow(int volumeNumber, const QPixmap& pm)
{
    // Task 16: route through VolumeTile::setCoverFromPixmap.
    // The old QTableWidget cellWidget lookup is replaced by a hash lookup.
    if (pm.isNull()) {
        qWarning("applyPixmapToVolumeRow: null pixmap for vol=%d", volumeNumber);
        return;
    }
    auto* tile = m_volumeTilesByVolumeNumber.value(volumeNumber, nullptr);
    if (!tile) {
        qWarning("applyPixmapToVolumeRow: no tile for volumeNumber=%d (tileCount=%d)",
                 volumeNumber, m_volumeTilesByVolumeNumber.size());
        return;
    }
    tile->setCoverFromPixmap(pm);
}

void ComicsSeriesView::setVolumeCoverFromDisk(const QString& seriesId, int volumeNumber,
                                              const QString& coverPath)
{
    // Task 16: stale-series guard preserved. seriesId may be "anilist_<N>"
    // or a catalog slug (e.g. "death-note") or m_currentSeriesKey format
    // ("anilist:<N>" / "weebcentral:<id>"). Check both formats.
    const QString expectedAnilist    = QStringLiteral("anilist_%1").arg(m_currentAnilistId);
    const QString expectedAnilistKey = QStringLiteral("anilist:%1").arg(m_currentAnilistId);
    if (seriesId != expectedAnilist
     && seriesId != expectedAnilistKey
     && seriesId != m_currentSeriesKey) {
        // Not an exact match — also allow the legacy anilist_ prefix parse.
        if (seriesId.startsWith(QStringLiteral("anilist_"))) {
            bool ok = false;
            const int parsed = QStringView(seriesId).mid(8).toInt(&ok);
            if (ok && parsed != m_currentAnilistId) {
                return;  // stale event for a different series
            }
        } else if (m_currentSeriesKey.startsWith(QStringLiteral("fandom_catalog:"))) {
            // Fix 5: catalog-keyed series — compare incoming slug against the
            // current catalog slug so late provider events from a prior series
            // don't paint over the currently displayed catalog series.
            const QString currentSlug = m_currentSeriesKey.mid(
                QStringLiteral("fandom_catalog:").size());
            if (!seriesId.isEmpty() && seriesId != currentSlug) {
                return;  // stale disk-cover event for a different catalog series
            }
        }
        // Other unknown slug formats: apply unconditionally (v1 behaviour for
        // non-catalog, non-anilist slug spaces).
    }

    if (coverPath.isEmpty() || !QFileInfo(coverPath).exists()) return;

    // Task 16: route through VolumeTile::setCoverFromDisk (reads disk file
    // itself) rather than applyPixmapToVolumeRow (which takes a QPixmap).
    // This avoids a redundant decode: setCoverFromDisk already handles scale.
    auto* tile = m_volumeTilesByVolumeNumber.value(volumeNumber, nullptr);
    if (!tile) return;
    tile->setCoverFromDisk(coverPath);
}

void ComicsSeriesView::setVolumeDownloadState(int volumeNumber, const QString& cbzPath,
                                              bool downloaded)
{
    // Task 16: route through VolumeTile state setter.
    auto* tile = m_volumeTilesByVolumeNumber.value(volumeNumber, nullptr);
    if (!tile) return;
    auto state = tile->volumeState();
    state.cbzPath = downloaded ? cbzPath : QString();
    state.state   = downloaded
        ? tankoban::ui::comics::VolumeTileState::Complete
        : tankoban::ui::comics::VolumeTileState::NotStarted;
    tile->setVolumeState(state);
}


void ComicsSeriesView::setVolumeStatusText(int volumeNumber, const QString& statusText)
{
    // Task 16: route through VolumeTile::setStatusText + recompute state.
    auto* tile = m_volumeTilesByVolumeNumber.value(volumeNumber, nullptr);
    if (!tile) return;
    tile->setStatusText(statusText);
    // Re-derive state from (index-presence, statusText). The index-presence
    // check mirrors what VolumeTile::onIndexEntriesChanged does, but that
    // fires on the index signal — this slot handles transient push-state
    // updates (Queued/Downloading/Failed) from the download manager.
    auto state = tile->volumeState();
    const bool hasEntry = m_downloadIndex
        && m_downloadIndex->entryForSeriesAndVolume(
               QStringLiteral("mangafire_catalog"),
               m_currentSeriesKey, volumeNumber).has_value();
    state.state      = tankoban::ui::comics::VolumeTile::computeState(hasEntry, statusText);
    state.statusText = statusText;
    tile->setVolumeState(state);
}

void ComicsSeriesView::onVolumeCellClicked(int /*row*/, int /*column*/)
{
    // Task 16: deprecated. VolumeTile owns its click handling (openRequested /
    // downloadRequested / toggled signals). This slot is kept as a no-op so
    // any residual signal-slot connect (if any) does not break at link time.
}


void ComicsSeriesView::onVolumeCurrentChanged(int /*currentRow*/, int /*currentColumn*/,
                                              int /*previousRow*/, int /*previousColumn*/)
{
    // Task 16: deprecated. VolumeTile keyboard/click nav drives the Sources
    // panel via downloadRequested signal wiring. No-op stub.
}

void ComicsSeriesView::populateSourcesForRow(int row)
{
    if (row < 0 || row >= m_currentVolumeRows.size()) {
        return;
    }
    if (!m_sourcesPanel) return;

    const anilist::VolumeRow& volRow = m_currentVolumeRows.at(row);

    // Task 16: chapterNumbers come directly from VolumeRow (no QTableWidget
    // UserRole stash needed). The UserRole stash was a QTableWidget-era hack
    // to shuttle data back out of a cellWidget; VolumeRow already carries it.
    const QStringList& chapterNumbers = volRow.chapterNumbers;

    // Task 16: volume title synthesized from VolumeRow fields (mirrors the
    // title synthesis in populateVolumeRows). No UserRole+2 stash needed.
    const QString volumeTitle = volRow.isVolumeX
        ? tr("Volume X")
        : tr("Volume %1").arg(volRow.volumeNumber);

    m_sourcesPanel->setContext(volRow.volumeNumber, volumeTitle);

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
    // Task 16: no QTableWidget currentRow; report tile count instead.
    snap[QStringLiteral("tileCount")] = m_volumeTiles.size();
    snap[QStringLiteral("nextUnreadRow")] = m_nextUnreadRow;

    QJsonArray selectedRows;
    for (int row : m_selectedRows)
        selectedRows.append(row);
    snap[QStringLiteral("selectedRows")] = selectedRows;

    QJsonArray rows;
    for (int i = 0; i < m_currentVolumeRows.size(); ++i) {
        // Task 16: cbzPath now lives on the VolumeTileState instead of a
        // QTableWidgetItem UserRole stash.
        const int volNum = m_currentVolumeRows.at(i).volumeNumber;
        const auto* tile = m_volumeTilesByVolumeNumber.value(volNum, nullptr);
        const QString cbzPath = tile ? tile->volumeState().cbzPath : QString();
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
    // Task 16: no QTableWidget; guard against out-of-range row only.
    if (row < 0 || row >= m_currentVolumeRows.size()) {
        out[QStringLiteral("status")] = QStringLiteral("error");
        out[QStringLiteral("message")] = QStringLiteral("row out of range");
        return out;
    }

    // Task 16: no setCurrentCell; drive the sources panel directly.
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

    // Task 16: no setCurrentCell; drive the sources panel directly.
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
