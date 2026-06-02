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
#include "ui/ContextMenuHelper.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QToolButton>
#include <QColor>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLayoutItem>
#include <QLinearGradient>
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
#include <QMenu>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <Qt>

namespace tankoban::manga::comics {

namespace {

// Task 16: kCol* QTableWidget column constants removed -- QTableWidget fully
// replaced by VolumeTile rows. kVolumeThumbSize kept for applyPixmapToVolumeRow
// callers that pass explicit sizes (none remain; kept for documentation).
const QSize kVolumeThumbSize(76, 108);
constexpr int kVolumeRowHeight = 124;
const QSize kHeroCoverSize(90, 135);
// COMICS_WESTERN_ADD 2026-06-02 — Western series view uses a larger, more
// prominent hero cover (the manga shelf keeps its tuned 90x135). Same 2:3 ratio.
const QSize kWesternHeroCoverSize(150, 225);

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
    if (preview.yearStarted > 0)     parts << QString::number(preview.yearStarted);
    for (const QString& genre : preview.genres.mid(0, 2)) {
        const QString trimmed = genre.trimmed();
        if (!trimmed.isEmpty() && !parts.contains(trimmed, Qt::CaseInsensitive))
            parts << trimmed;
    }

    QList<anilist::RankedTag> sortedTags = preview.tags;
    std::sort(sortedTags.begin(), sortedTags.end(),
              [](const anilist::RankedTag& a, const anilist::RankedTag& b) {
                  return a.rank > b.rank;
              });
    const QSet<QString>& demographics = kDemographicTagNames();
    int tagCount = 0;
    for (const auto& tag : sortedTags) {
        if (tag.isSpoiler) continue;
        const QString trimmed = tag.name.trimmed();
        if (trimmed.isEmpty()) continue;
        if (demographics.contains(trimmed.toLower())) continue;
        if (parts.contains(trimmed, Qt::CaseInsensitive)) continue;
        parts << trimmed;
        if (++tagCount >= 2) break;
    }

    const QString lang = humanizeOriginLanguage(preview.countryOfOrigin);
    if (!lang.isEmpty())             parts << lang;
    if (!preview.status.isEmpty())   parts << humanizeStatus(preview.status);
    return parts.join(QStringLiteral(" ") + QChar(0x00B7) + QStringLiteral(" "));
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
    if (detail.preview.yearStarted > 0) {
        parts << QString::number(detail.preview.yearStarted);
    }
    for (const QString& genre : detail.preview.genres.mid(0, 2)) {
        const QString trimmed = genre.trimmed();
        if (!trimmed.isEmpty() && !parts.contains(trimmed, Qt::CaseInsensitive))
            parts << trimmed;
    }

    QList<anilist::RankedTag> sortedTags = detail.preview.tags;
    std::sort(sortedTags.begin(), sortedTags.end(),
              [](const anilist::RankedTag& a, const anilist::RankedTag& b) {
                  return a.rank > b.rank;
              });
    const QSet<QString>& demographics = kDemographicTagNames();
    int tagCount = 0;
    for (const auto& tag : sortedTags) {
        if (tag.isSpoiler) continue;
        const QString trimmed = tag.name.trimmed();
        if (trimmed.isEmpty()) continue;
        if (demographics.contains(trimmed.toLower())) continue;
        if (parts.contains(trimmed, Qt::CaseInsensitive)) continue;
        parts << trimmed;
        if (++tagCount >= 2) break;
    }

    const QString lang = humanizeOriginLanguage(detail.preview.countryOfOrigin);
    if (!lang.isEmpty()) {
        parts << lang;
    }
    if (!detail.preview.status.isEmpty()) {
        parts << humanizeStatus(detail.preview.status);
    }
    return parts.join(QStringLiteral(" ") + QChar(0x00B7) + QStringLiteral(" "));
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
    //   contentRow -- two columns (leftCol stretch=3, rightCol stretch=2)
    //                 leftCol holds title + meta + description + volume table
    //                 rightCol holds m_sourcesPanel full-vertical
    // The prior full-bleed paintEvent wallpaper is GONE -- title text now
    // sits below the banner on a solid dark background. Mockup at
    // .superpowers/brainstorm/1608-1779095122/content/proposed-layout.html.
    // Outer margins (16,8,16,8) match StreamDetailView.cpp:377 — Hemanth
    // 2026-05-27 directive to size Comics sources panel to match Theatre.
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 8, 16, 8);
    outer->setSpacing(18);

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
        // COMICS_WESTERN_RICHNESS 2026-06-01 (Agent 9). Western about-block
        // gray-on-dark QSS — no color, no emoji, matches house style.
        "QLabel#WesternAboutTitle { color: rgba(238,238,238,0.96); font-size: 26px; font-weight: 600; }"
        "QLabel#WesternAboutMeta { color: rgba(238,238,238,0.62); font-size: 13px; }"
        "QLabel#WesternAboutSynopsis { color: rgba(238,238,238,0.82); font-size: 14px; }"
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
        "  background: transparent;"
        "  border: none;"
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
    m_heroBannerLabel->setFixedHeight(170);
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
    heroLayout->setContentsMargins(0, 0, 0, 12);
    heroLayout->setSpacing(22);

    m_heroCoverLabel = new QLabel(m_heroBlock);
    m_heroCoverLabel->setObjectName(QStringLiteral("ComicsSeriesHeroCover"));
    m_heroCoverLabel->setFixedSize(kHeroCoverSize);
    m_heroCoverLabel->setAlignment(Qt::AlignCenter);
    m_heroCoverLabel->setScaledContents(false);
    heroLayout->addWidget(m_heroCoverLabel, 0, Qt::AlignTop);

    auto* heroTextStack = new QVBoxLayout();
    heroTextStack->setContentsMargins(0, 0, 0, 0);
    heroTextStack->setSpacing(12);

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

    // STREAM_PORT 2026-05-18 Task 3: clamped description with
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

    m_tagChipsRow = nullptr;
    m_tagChipsLayout = nullptr;

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
    // No setMinimumWidth — matches StreamDetailView's source panel (no min).
    // Prior 380px floor was Comics-specific and unmatched by Theatre.
    m_sourcesPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    contentRow->addWidget(m_sourcesPanel, /*stretch*/ 2);

    outer->addLayout(contentRow, /*stretch*/ 1);

    // Task 14: loading overlay covers the entire widget during BookWalker
    // resolution; starts hidden. Safety timer forces fallback after 10s.
    m_loadingOverlay = new tankoban::ui::widgets::ComicsSeriesViewLoadingOverlay(this);
    m_loadingOverlay->setMessage(tr("Loading"));
    m_loadingOverlay->hide();

    // m_loadingSafetyTimer removed: cover loading is now direct-URL via
    // loadCoverUrlForVolume; m_pendingMediaLoads drives overlay hide timing.
}

// COMICS_OPEN_TRACE (Agent 1, 2026-05-24 evening debug session). Timestamped
// file trace at the key phase boundaries of a series open. Writes to
// %TEMP%/comics_open_trace.log so we can measure WHERE the perceived loading
// time is spent. Will be stripped after the bottleneck is identified + fixed.
namespace {
void comicsOpenTrace(const QString& event)
{
    static const QString path = QDir::temp().absoluteFilePath(QStringLiteral("comics_open_trace.log"));
    QFile f(path);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << QDateTime::currentMSecsSinceEpoch() << '\t' << event << '\n';
    }
}
} // namespace

void ComicsSeriesView::showSeries(const anilist::MediaPreview& preview)
{
    comicsOpenTrace(QStringLiteral("CSV::showSeries(MediaPreview) ENTRY anilistId=%1 title=\"%2\"")
                        .arg(preview.anilistId).arg(preview.title));
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
        comicsOpenTrace(QStringLiteral("CSV::showSeries(MediaPreview) -> seriesById fire reqId=%1")
                            .arg(m_pendingSeriesReqId));
        m_client->seriesById(preview.anilistId, m_pendingSeriesReqId);
    }
    comicsOpenTrace(QStringLiteral("CSV::showSeries(MediaPreview) EXIT"));
}

void ComicsSeriesView::showSearchResultLoading()
{
    clearView();
    m_pendingMediaLoads = 0;
    if (m_heroBannerLabel) {
        m_heroBannerLabel->clear();
        m_heroBannerLabel->hide();
    }
    showLoadingOverlay();
}

// WEEBCENTRAL_IDENTITY_PIVOT Task 8: WeebCentral-sourced series entry point.
// Mirrors showSeries(MediaPreview) but drives the cover resolver via the
// seriesKey composite and fetches chapter/volume detail via MangaSourceRegistry.
void ComicsSeriesView::showSeries(const MangaResult& wc)
{
    showSeries(wc, true);
}

void ComicsSeriesView::showSeries(const MangaResult& wc, bool requestEnrichment)
{
    comicsOpenTrace(QStringLiteral("CSV::showSeries(MangaResult) ENTRY source=%1 id=%2 title=\"%3\" enrich=%4")
                        .arg(wc.source).arg(wc.id).arg(wc.title)
                        .arg(requestEnrichment ? 1 : 0));
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

    // COMICS_WC_AUTOENRICH 2026-05-24 (Agent 1). Search-opened series start
    // with anilistId=0 and no AniList cache hit — which leaves the hero
    // banner, poster, synopsis, and tags blank until the user explicitly
    // clicks Add to Library. Fire the AniList enrichment automatically so
    // every series view paints the same Stream-style hero block regardless
    // of bookmark status. ComicsPage handles the search + cache seed +
    // re-show; bookmark is NOT added (that stays opt-in via the button).
    if (requestEnrichment && m_currentAnilistId <= 0 && !wc.title.trimmed().isEmpty()) {
        emit enrichSeriesByTitleRequested(wc.title.trimmed());
    }
}

// Fix 3: catalog-tile entry point that sets a slug-based key when AniList id
// is unavailable, preventing all zero-id catalog series from sharing "anilist:0".
void ComicsSeriesView::showCatalogSeries(const QString& seriesId,
                                          const QString& title,
                                          int            anilistId)
{
    // Pre-set the one-shot override so showSeries(MediaPreview) picks it up.
    // Always tag as "mangafire:<slug>" regardless of anilistId — this lets
    // renderDetail (the AniList async response handler) skip its populateVolumeRows
    // call when the series was opened from the local catalog, so AniList's
    // chapter-range estimate + series-cover-for-every-row don't overwrite the
    // accurate per-volume MangaFire data already loaded by populateVolumeRowsFromCatalog.
    m_pendingCatalogSeriesKey = QStringLiteral("mangafire:%1").arg(seriesId);

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
    m_currentMangaCatalog = tankoban::manga::MangaCatalog{};
    m_westernOnShelf = false;  // COMICS_WESTERN_ADD 2026-06-01: reset per-series

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
    m_lastBulkAnchorVolume = -1;
    if (m_downloadSelectedBtn) m_downloadSelectedBtn->hide();
    if (m_sourcesPanel) {
        m_sourcesPanel->clear();
        m_sourcesPanel->setVisible(true);  // re-show; Western-editionless hides it per-populate
    }
    // Reset the hero cover to the manga size; populateVolumeRowsFromCatalog bumps
    // it for Western. Keeps the manga AniList path (no populate) at 90x135.
    if (m_heroCoverLabel) m_heroCoverLabel->setFixedSize(kHeroCoverSize);
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
    if (!detail.preview.title.isEmpty()) {
        emit detailResolvedForCatalog(detail.preview.anilistId,
                                      detail.preview.title);
    }

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
    comicsOpenTrace(QStringLiteral("CSV::renderDetail ENTRY anilistId=%1 title=\"%2\"")
                        .arg(detail.preview.anilistId).arg(detail.preview.title));
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

    // Skip AniList volume-row population when MangaFire catalog already populated
    // this series via populateVolumeRowsFromCatalog (catalog-tile entry path).
    // AniList's per-volume data is an estimate (chapters/volumeCount even split,
    // single series cover for every row) which would clobber the accurate per-volume
    // covers + chapter ranges loaded from the catalog JSON.
    if (!m_currentSeriesKey.startsWith(QStringLiteral("mangafire:"))) {
        populateVolumeRows(anilist::AniListVolumeMapper::map(detail), &detail);
    }

    // Phase 8a Wave 2: banner slot exists only for a real AniList banner.
    // Never stretch the portrait cover into this landscape band.
    loadBannerUrl(detail.preview.bannerUrl);

    // COMICS_SERIES_OPEN_OVERLAY_CLEAR_FIX (Agent 1, 2026-05-25).
    // Hero metadata is now visible as soon as renderDetail completes; volume
    // covers continue loading in the background and still use the
    // m_pendingMediaLoads safety path below.
    hideLoadingOverlay();
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
    m_lastBulkAnchorVolume = -1;
    if (m_downloadSelectedBtn) m_downloadSelectedBtn->hide();

    // AniList path: synthesize stable identity keys.
    // Two-stage lookup mirrors the old QTableWidget cbzPath resolution:
    //   1. If the fandom catalog knows this AniList ID, use its
    //      (kPremiumSourceId, catalog.seriesId) key so downloads landed via
    //      Fandom-aware paths are found.
    //   2. Otherwise fall back to ("anilist_N") slug used by the premium +
    //      weebcentral download paths.
    const QString fallbackSeriesId = QStringLiteral("anilist_%1").arg(m_currentAnilistId);

    int rowIndex = -1;
    for (const auto& row : rows) {
        ++rowIndex;
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

        Q_UNUSED(rowIndex);
        connect(tile, &tankoban::ui::comics::VolumeTile::rowClicked,
                this, &ComicsSeriesView::onVolumeRowActivated);
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

        tile->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tile, &QWidget::customContextMenuRequested, this,
                [this, tile](const QPoint& pos) { showVolumeTileMenu(tile, pos); });

        // Phase B regression fix 2026-05-23: populateVolumeRows used to rely on
        // paintVolumeCovers / paintVolumeCoversAsFallback (BookWalker-driven)
        // to trigger the actual cover fetch. Phase B deleted those paths along
        // with the BookWalker resolver; without this line the AniList-path
        // VolumeTiles never get their cover labels painted. Mirrors line ~1322
        // in populateVolumeRowsFromCatalog.
        if (!resolvedCoverUrl.isEmpty()) {
            loadCoverUrlForVolume(resolvedCoverUrl, row.volumeNumber);
        }
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
    // COMICS_MANGAFIRE_ON_DEMAND_FETCH 2026-05-23 (Agent 1). Tag the current
    // series with a "mangafire:<slug>" key BEFORE inserting tiles. The
    // catalog-tile entry path already pre-tags this in showCatalogSeries,
    // but the search-open path (showSeries(MangaResult)) sets the key to
    // "weebcentral:<id>", which leaves us exposed to late AniList replies
    // clobbering the rows via renderDetail's populateVolumeRows call. This
    // assignment closes that hole for the on-demand fetch path without
    // changing the catalog-tile path (idempotent — same value either way).
    if (!catalog.seriesId.isEmpty()) {
        m_currentSeriesKey = QStringLiteral("mangafire:%1").arg(catalog.seriesId);
    }
    m_currentMangaCatalog = catalog;

    // COMICS_WESTERN_ADD 2026-06-02 (Agent 1). Western series view uses a larger,
    // more prominent hero cover than the manga shelf. Set BEFORE the hero paint
    // below so the async applyHeroCoverPixmap scales to this size; clearView
    // resets it to the manga size for the next series.
    if (m_heroCoverLabel) {
        m_heroCoverLabel->setFixedSize(catalog.source == QLatin1String("rco")
                                           ? kWesternHeroCoverSize
                                           : kHeroCoverSize);
    }

    // COMICS_WESTERN_ADD 2026-06-01 (Agent 1). Reset on-shelf flag for each
    // catalog open so the button defaults to "Add to Library" until ComicsPage
    // calls setWesternOnShelf(true) for already-shelved series.
    m_westernOnShelf = false;
    refreshLibraryButton();

    // COMICS_WESTERN_RICHNESS 2026-06-01 (Agent 9). Render the about-block header
    // (synopsis + "author · publisher · year · genre") DIRECTLY — never routed
    // through showSeries / dispatchCatalogResolve (Guard #3 no-auto-enrich).
    updateAboutBlock(catalog);

    // Tear down existing tiles.
    qDeleteAll(m_volumeTiles);
    m_volumeTiles.clear();
    m_volumeTilesByVolumeNumber.clear();

    // Pre-population cleanup (mirrors prior function head).
    m_currentVolumeRows.clear();
    m_selectedRows.clear();
    m_lastBulkAnchorVolume = -1;
    if (m_downloadSelectedBtn) m_downloadSelectedBtn->hide();

    // Clear meta-line — source-name leaking into view has no user value.
    if (m_metaLine) {
        m_metaLine->clear();
    }

    // VOLUME_X_DOWNLOAD: a real tankobon volume carries a MangaFire cover.
    // Cover-less volumes are auto-bucket fakes (e.g. One Piece "vol 116/117")
    // whose chapters belong under Volume X, not their own tile — skip them.
    // Fallback: render all when the catalog has no covers at all.
    bool catalogHasAnyCover = false;
    for (const auto& vol : catalog.volumes) {
        if (!vol.coverUrlJapanese.isEmpty() || !vol.coverUrlEnglish.isEmpty()) {
            catalogHasAnyCover = true;
            break;
        }
    }

    // Insert each volume as a VolumeTile row, just before the trailing
    // stretch in m_volumesLayout (index = count() - 1).
    for (const auto& vol : catalog.volumes) {
        if (catalogHasAnyCover
            && vol.coverUrlJapanese.isEmpty() && vol.coverUrlEnglish.isEmpty()) {
            continue;  // fake auto-bucket volume -> folded into Volume X
        }
        tankoban::ui::comics::VolumeTileData data;
        // COMICS_WC_SOURCE_LABEL_FIX 2026-05-26 (Agent 9).
        // VolumeTile needs to find download index entries registered under
        // "weebcentral" (not "mangafire_catalog"). The download index stores
        // WeebCentral-packed volumes with sourceId="weebcentral" regardless
        // of whether the catalog/series identity was resolved via MangaFire.
        // Using the catalog sourceId here would prevent the tile from ever
        // detecting its Complete state via onIndexEntriesChanged.
        data.sourceId     = QString::fromLatin1(kWeebCentralSourceId);
        data.seriesId     = catalog.seriesId;
        data.volumeNumber = vol.volumeNumber;
        data.title        = !vol.titleEnglish.isEmpty() ? vol.titleEnglish : vol.titleJapanese;
        data.synopsis     = vol.synopsis;
        if (vol.releaseDateEn.isValid()) {
            data.publishDate = vol.releaseDateEn.toString(Qt::ISODate);
        }
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
        // Per-edition cover (Task 5, 2026-06-02): prefer edition-specific cover
        // when present (GetComics post image); empty => existing shared-series path.
        data.coverUrl     = !vol.coverUrlEdition.isEmpty()
                              ? vol.coverUrlEdition
                              : (!vol.coverUrlJapanese.isEmpty()
                                   ? vol.coverUrlJapanese
                                   : vol.coverUrlEnglish);

        // VOLUME_X_QUALITY 2026-05-28 (Agent 1, DeepSeek V4-Pro).
        // Tag magazine-sourced volumes with the RAW SCAN badge.
        if (auto it = m_classifiedByVolume.constFind(vol.volumeNumber);
            it != m_classifiedByVolume.constEnd()) {
            data.isRawScan = (it->quality == tankoban::manga::VolumeQuality::Magazine);
        }

        const int rowIndex = m_currentVolumeRows.size();
        Q_UNUSED(rowIndex);
        anilist::VolumeRow mappedRow;
        mappedRow.volumeNumber      = vol.volumeNumber;
        mappedRow.chapterRangeStart = vol.chapterRangeStart;
        mappedRow.chapterRangeEnd   = vol.chapterRangeEnd;
        mappedRow.chapterNumbers    = vol.chapterList;
        if (mappedRow.chapterNumbers.isEmpty()) {
            if (!vol.chapterStartRaw.isEmpty()) mappedRow.chapterNumbers.append(vol.chapterStartRaw);
            if (!vol.chapterEndRaw.isEmpty() && vol.chapterEndRaw != vol.chapterStartRaw) {
                mappedRow.chapterNumbers.append(vol.chapterEndRaw);
            }
        }
        mappedRow.chapterCount      = mappedRow.chapterNumbers.size();
        mappedRow.art.thumbnailUrl  = data.coverUrl;
        mappedRow.art.fullUrl       = data.coverUrl;
        m_currentVolumeRows.append(mappedRow);

        auto* tile = new tankoban::ui::comics::VolumeTile(data, m_volumesHost);

        tankoban::ui::comics::VolumeTileState state;
        state.provenance = QStringLiteral("MangaFire");
        tile->setVolumeState(state);
        tile->setMangaDownloadIndex(m_downloadIndex);

        connect(tile, &tankoban::ui::comics::VolumeTile::rowClicked,
                this, &ComicsSeriesView::onVolumeRowActivated);
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

        // VOLUME_X_QUALITY 2026-05-28 (Agent 1, DeepSeek V4-Pro).
        // Upgrade click opens the Sources panel so the user can re-dispatch
        // the download. The new dispatch picks up the Clean verdict via
        // isVolumeMagazineSourced → needsChapterPairing=false.
        connect(tile, &tankoban::ui::comics::VolumeTile::upgradeRequested,
                this, &ComicsSeriesView::onVolumeRowActivated);

        m_volumeTiles.append(tile);
        m_volumeTilesByVolumeNumber.insert(vol.volumeNumber, tile);
        m_volumesLayout->insertWidget(m_volumesLayout->count() - 1, tile);

        tile->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tile, &QWidget::customContextMenuRequested, this,
                [this, tile](const QPoint& pos) { showVolumeTileMenu(tile, pos); });

        // Paint the catalog's own CDN cover URL for this volume. The async fetch
        // has a stale-series guard (m_currentSeriesKey) which correctly differentiates
        // catalog slugs ("mangafire:<slug>") from AniList/WeebCentral series.
        if (!data.coverUrl.isEmpty()) {
            loadCoverUrlForVolume(data.coverUrl, vol.volumeNumber);
        }
    }

    // VOLUME_X_QUALITY 2026-05-28 (Agent 1, DeepSeek V4-Pro).
    // Append a Volume X row for the bleeding-edge tail bucket. This row
    // carries the isRawScan badge (Volume X is always magazine quality)
    // and a distinct "Volume X" title so the user can distinguish it from
    // bound catalog volumes.
    for (auto it = m_classifiedByVolume.constBegin();
         it != m_classifiedByVolume.constEnd(); ++it) {
        if (!it->isVolumeX) continue;

        tankoban::ui::comics::VolumeTileData xData;
        xData.sourceId     = QString::fromLatin1(kWeebCentralSourceId);
        xData.seriesId     = catalog.seriesId;
        xData.volumeNumber = tankoban::manga::anilist::kVolumeXNumber;
        xData.title        = QStringLiteral("Volume X");
        xData.chapterRange = QStringLiteral("ch %1-%2")
                                 .arg(static_cast<int>(it->chapterNumbers.first()))
                                 .arg(static_cast<int>(it->chapterNumbers.last()));
        xData.isRawScan = true;  // Volume X is always magazine

        auto* xTile = new tankoban::ui::comics::VolumeTile(xData, m_volumesHost);
        tankoban::ui::comics::VolumeTileState xState;
        xState.provenance = QStringLiteral("MangaFire");
        xTile->setVolumeState(xState);
        xTile->setMangaDownloadIndex(m_downloadIndex);

        connect(xTile, &tankoban::ui::comics::VolumeTile::rowClicked,
                this, &ComicsSeriesView::onVolumeRowActivated);
        connect(xTile, &tankoban::ui::comics::VolumeTile::toggledShift,
                this, [this](bool checked, bool shiftHeld) {
                    auto* src = qobject_cast<tankoban::ui::comics::VolumeTile*>(sender());
                    if (!src) return;
                    const int vn = src->volumeNumber();
                    if (checked) m_selectedRows.insert(vn);
                    else         m_selectedRows.remove(vn);
                    if (m_downloadSelectedBtn) {
                        m_downloadSelectedBtn->setText(
                            QStringLiteral("Download Selected (%1)").arg(m_selectedRows.size()));
                        m_downloadSelectedBtn->setVisible(!m_selectedRows.isEmpty());
                    }
                });

        m_volumeTiles.append(xTile);
        m_volumeTilesByVolumeNumber.insert(tankoban::manga::anilist::kVolumeXNumber, xTile);
        m_volumesLayout->insertWidget(m_volumesLayout->count() - 1, xTile);

        xTile->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(xTile, &QWidget::customContextMenuRequested, this,
                [this, xTile](const QPoint& pos) { showVolumeTileMenu(xTile, pos); });

        break;  // at most one Volume X
    }

    // Cache current identity for downstream slots (existing behavior).
    m_currentSeriesTitle = !catalog.seriesTitle.isEmpty()
                            ? catalog.seriesTitle
                            : catalog.seriesId;

    // COMICS_CANONICAL_COVER 2026-05-26 (Agent 9) — update the hero poster
    // to MangaFire Volume 1's cover URL when available. Volume rows still
    // show their own per-volume covers (set in the loop above); this only
    // touches the top-of-page hero block.
    bool heroPainted = false;
    for (const auto& vol : catalog.volumes) {
        if (vol.volumeNumber == 1 && !vol.coverUrlJapanese.isEmpty()) {
            loadHeroCoverUrl(vol.coverUrlJapanese);
            heroPainted = true;
            break;
        }
    }
    // COMICS_WESTERN_ADD 2026-06-02 (Agent 1). An editionless live Western series
    // has no volume #1 to carry the hero cover, so fall back to the series-level
    // cover (WesternCatalogLoader sets it from the RCO page cover / search
    // thumbnail). Manga catalogs leave seriesCover empty, so this no-ops for them
    // (loadHeroCoverUrl also guards empty URLs); the volume-1 branch above keeps
    // owning the manga + edition-bearing hero unchanged.
    if (!heroPainted && !catalog.seriesCover.isEmpty()) {
        loadHeroCoverUrl(catalog.seriesCover);
    }

    // COMICS_WESTERN_ADD 2026-06-02 (Agent 1). Editionless Western series (marquee
    // titles whose collected editions live under sibling slugs we don't yet
    // discover) get an explicit empty-state in the volumes column so the page
    // reads as intentional rather than unfinished (spec §8). Lazily created,
    // shown only for an rco series with zero edition tiles, hidden otherwise. It
    // is NOT in m_volumeTiles, so the qDeleteAll teardown above never frees it.
    const bool westernNoEditions =
        (catalog.source == QLatin1String("rco")) && m_volumeTiles.isEmpty();
    if (westernNoEditions && !m_westernNoEditionsLabel && m_volumesLayout && m_volumesHost) {
        m_westernNoEditionsLabel =
            new QLabel(tr("No collected editions found yet."), m_volumesHost);
        m_westernNoEditionsLabel->setObjectName(QStringLiteral("WesternNoEditions"));
        m_westernNoEditionsLabel->setWordWrap(true);
        m_westernNoEditionsLabel->setStyleSheet(
            QStringLiteral("color: rgba(255,255,255,0.5); padding: 12px 2px;"));
        m_volumesLayout->insertWidget(m_volumesLayout->count() - 1, m_westernNoEditionsLabel);
    }
    if (m_westernNoEditionsLabel) {
        m_westernNoEditionsLabel->setVisible(westernNoEditions);
    }
    // COMICS_WESTERN_ADD 2026-06-02 (Agent 1). For an editionless Western series
    // the right-hand Sources panel is dead weight (no volume to source) and left
    // the page looking half-empty. Hide it so the left column reclaims the full
    // width; clearView re-shows it for the next series. Shown for manga + any
    // Western series that DOES have editions.
    if (m_sourcesPanel) {
        m_sourcesPanel->setVisible(!westernNoEditions);
    }

    // No next-unread highlight on the MangaFire catalog path in v1 — that
    // hinges on cbzPath stash which the catalog doesn't carry. The Tankoyomi-
    // download-linkage refresh in a future task will overlay highlight +
    // status icons on top of the rendered tiles.
    m_nextUnreadRow = -1;

    // COMICS_WESTERN_ADD 2026-06-02 (Agent 1). Dismiss the "Loading" overlay once
    // rows are built when NO async cover fetch is in flight. showSearchResultLoading
    // raises the overlay and the per-cover finished handler hides it when the
    // counter drains to 0 — but a series that queues ZERO cover loads (a live
    // Western series with no collected editions => no volumes, or an all-covers-
    // cached catalog) never runs that handler, so the overlay would hang on
    // "Loading" forever. Hiding here closes that gap; when covers ARE in flight
    // (m_pendingMediaLoads > 0) the async path still owns the hide (unchanged).
    if (m_pendingMediaLoads == 0) hideLoadingOverlay();
}

// ---------------------------------------------------------------------------
// VOLUME_X_QUALITY 2026-05-28 (Agent 1, DeepSeek V4-Pro).
// ---------------------------------------------------------------------------

void ComicsSeriesView::onSeriesClassified(
    const QString& mangaFireSeriesId,
    QList<tankoban::manga::ClassifiedVolume> classified)
{
    // Guard: stale classification for a different series.
    if (mangaFireSeriesId != m_currentMangaCatalog.seriesId) return;

    m_classifiedByVolume.clear();
    for (const auto& cv : classified) {
        m_classifiedByVolume.insert(cv.volumeNumber, cv);
    }

    // Re-render rows with classification applied. populateVolumeRowsFromCatalog
    // reads m_classifiedByVolume to set RAW tags and append the Volume X row.
    populateVolumeRowsFromCatalog(m_currentMangaCatalog);

    // VOLUME_X_QUALITY 2026-05-28 (Agent 1, DeepSeek V4-Pro); backfill added
    // 2026-05-29 (Agent 1). After re-rendering, reconcile each downloaded
    // volume's ".volx" pairing sidecar against the authoritative classification.
    for (auto* tile : m_volumeTiles) {
        if (!tile) continue;
        const auto& state = tile->volumeState();
        if (state.cbzPath.isEmpty()) continue;

        auto it = m_classifiedByVolume.constFind(tile->volumeNumber());
        if (it == m_classifiedByVolume.constEnd()) continue;

        const QString volxPath = state.cbzPath + QStringLiteral(".volx");
        const bool needsPairing =
            it->isVolumeX
            || it->quality == tankoban::manga::VolumeQuality::Magazine;

        if (needsPairing) {
            // Backfill: a magazine / Volume X volume downloaded before the
            // quality-aware .volx wiring (or via a path that never wrote it)
            // has no sidecar, so the reader never applied chapter-boundary
            // pairing or the book-mode default. Write it now from the
            // authoritative classification — existing downloads are fixed
            // without a re-download. Add-only (clean volumes are never given a
            // sidecar here), so a stale/incomplete classification can never
            // strip pairing off a correctly-packed volume. Quality is the right
            // discriminator: clean volume scans are ALSO chapter-split packs, so
            // a page-naming heuristic would wrongly break them.
            if (!QFile::exists(volxPath)) {
                QFile marker(volxPath);
                if (marker.open(QIODevice::WriteOnly)) marker.close();
            }
        } else if (it->quality == tankoban::manga::VolumeQuality::Clean
                   && QFile::exists(volxPath)) {
            // Clean volume carrying a stale .volx (packed as Magazine before a
            // reclassify) → offer a re-download without the pairing sidecar.
            tile->setUpgradeAvailable(true);
        }
    }
}

bool ComicsSeriesView::isVolumeMagazineSourced(int volumeNumber) const
{
    auto it = m_classifiedByVolume.constFind(volumeNumber);
    if (it == m_classifiedByVolume.constEnd()) return false;
    return it->quality == tankoban::manga::VolumeQuality::Magazine;
}

// ---------------------------------------------------------------------------

void ComicsSeriesView::setVolumeRows(const QList<anilist::VolumeRow>& rows)
{
    // Same guard as renderDetail: catalog-source series (mangafire:<slug>) have
    // their volumes populated by populateVolumeRowsFromCatalog with accurate
    // per-volume MangaFire data. The AniList-derived rows (estimated chapter
    // ranges, series-cover-for-every-row) would clobber that. The clobber is
    // most visible on ongoing series with many volumes (One Piece 117, Berserk
    // 44, Dandadan 24) because their AniList metadata resolver fires delayed
    // after the catalog has already painted.
    if (m_currentSeriesKey.startsWith(QStringLiteral("mangafire:"))) {
        return;
    }
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

void ComicsSeriesView::setWesternOnShelf(bool onShelf)
{
    // COMICS_WESTERN_ADD 2026-06-01 (Agent 1). Called by ComicsPage after
    // westernSeriesReady fires; tells the view whether the series is already
    // on the shelf so the button can be pre-set correctly before the user
    // interacts. Also called from onLibraryButtonClicked() after a successful
    // add to flip the button to "On shelf" immediately.
    m_westernOnShelf = onShelf;
    refreshLibraryButton();
}

void ComicsSeriesView::refreshLibraryButton()
{
    if (!m_libraryButton) return;

    // COMICS_WESTERN_ADD 2026-06-01 (Agent 1). Western (RCO) series bypass:
    // the shelf state is tracked by m_westernOnShelf (set externally by
    // ComicsPage), not by AniListCache bookmarks. "On shelf" is inert;
    // "Add to Library" is enabled. This branch fires whenever the currently
    // loaded catalog is a Western one, regardless of AniList state.
    if (m_currentMangaCatalog.source == QLatin1String("rco")) {
        m_libraryButton->setText(m_westernOnShelf ? tr("On shelf") : tr("Add to Library"));
        m_libraryButton->setEnabled(!m_westernOnShelf);
        return;
    }

    const bool hasSeries  = (m_currentAnilistId > 0);
    const bool bookmarked = (m_cache && hasSeries) ? m_cache->isBookmarked(m_currentAnilistId) : false;
    // COMICS_WC_LIBRARY_ENRICH 2026-05-24 (Agent 1). MangaFire-catalog-only
    // series (anilistId=0 because MangaFire couldn't resolve an AniList
    // match) used to disable this button entirely. Now we ALSO enable it
    // when we have a series title, and the click handler routes through a
    // best-effort AniList-search-by-title enrichment flow owned by
    // ComicsPage (via addToLibraryByTitleRequested signal). On match the
    // series gets bookmarked under the discovered anilistId; on no match
    // the button silently re-enables for retry.
    const bool hasTitle = !m_currentSeriesTitle.isEmpty();
    m_libraryButton->setEnabled((hasSeries || hasTitle) && m_cache);
    // STREAM_PORT 2026-05-18 Task 4: Stream-verbatim action labels.
    // Was "In library" (passive) / "Add to library" (action mixed); now
    // "Remove from Library" (when bookmarked) / "Add to Library" (when not).
    m_libraryButton->setText(bookmarked ? tr("Remove from Library") : tr("Add to Library"));
}

void ComicsSeriesView::onLibraryButtonClicked()
{
    // COMICS_WESTERN_ADD 2026-06-01 (Agent 1). Western (RCO) series branch —
    // fires BEFORE the AniList path so it never touches AniListCache for
    // series that have no AniList backing. The button is already disabled
    // when m_westernOnShelf is true (see refreshLibraryButton), so this
    // branch only runs for the "Add to Library" case.
    if (m_currentMangaCatalog.source == QLatin1String("rco")) {
        if (!m_westernOnShelf) {
            emit addWesternToLibraryRequested();
            m_westernOnShelf = true;
            refreshLibraryButton();
        }
        return;
    }

    if (!m_cache) return;
    // Existing AniList-keyed bookmark toggle path.
    if (m_currentAnilistId > 0) {
        if (m_cache->isBookmarked(m_currentAnilistId)) {
            // Remove from Library. A series may be BOOKMARKED and/or
            // DOWNLOADED; the landing DOWNLOADED strip is driven by
            // MangaDownloadIndex, so dropping the bookmark alone left a
            // downloaded series stuck on the landing (bug 2026-05-30). Gather
            // the series' download entries across sources, offer the same
            // 3-way remove dialog as the per-volume delete, evict them, drop
            // the bookmark, then navigate back. The landing refreshes via
            // entriesChanged + bookmarksChanged (both wired in ComicsPage).
            QStringList wantSeriesIds;
            wantSeriesIds << QStringLiteral("anilist_%1").arg(m_currentAnilistId);
            if (!m_currentMangaCatalog.seriesId.isEmpty())
                wantSeriesIds << m_currentMangaCatalog.seriesId;

            QList<MangaDownloadIndex::Entry> seriesEntries;
            if (m_downloadIndex) {
                QSet<QString> seenBuckets;
                for (const auto& rep : m_downloadIndex->entriesForAllSeries()) {
                    if (!wantSeriesIds.contains(rep.seriesId)) continue;
                    const QString bucket = rep.sourceId + QLatin1Char(':') + rep.seriesId;
                    if (seenBuckets.contains(bucket)) continue;
                    seenBuckets.insert(bucket);
                    seriesEntries += m_downloadIndex->entriesForSeries(rep.sourceId, rep.seriesId);
                }
            }

            bool deleteFiles = false;
            if (!seriesEntries.isEmpty()) {
                const auto choice = ContextMenuHelper::confirmRemoveWithFile(
                    this, tr("Remove from Library"),
                    tr("Remove \"%1\" (%2 downloaded volume%3) from your library?")
                        .arg(m_currentSeriesTitle)
                        .arg(seriesEntries.size())
                        .arg(seriesEntries.size() == 1 ? QString() : QStringLiteral("s")));
                if (choice == ContextMenuHelper::RemoveChoice::Cancel)
                    return;
                deleteFiles = (choice == ContextMenuHelper::RemoveChoice::DeleteFile);
            }

            m_cache->removeBookmark(m_currentAnilistId);

            if (m_downloadIndex) {
                QSet<QString> evicted;
                for (const auto& e : seriesEntries) {
                    if (deleteFiles && !e.canonicalPath.isEmpty()) {
                        QFile::remove(e.canonicalPath);
                        QFile::remove(e.canonicalPath + QStringLiteral(".volx"));
                    }
                    const QString bucket = e.sourceId + QLatin1Char(':') + e.seriesId;
                    if (!evicted.contains(bucket)) {
                        evicted.insert(bucket);
                        m_downloadIndex->evictBySeries(e.sourceId, e.seriesId);
                    }
                }
            }

            emit backRequested();
            return;
        }
        m_cache->addBookmark(m_currentAnilistId);
        refreshLibraryButton();
        return;
    }
    // COMICS_WC_LIBRARY_ENRICH 2026-05-24 (Agent 1). MangaFire-catalog-only
    // series (no AniList id resolved): hand off to ComicsPage for the
    // best-effort search-by-title enrichment. ComicsPage emits an updated
    // showSeries on the matched preview, which re-flows m_currentAnilistId
    // and triggers refreshLibraryButton via the bookmarksChanged signal.
    if (m_currentSeriesTitle.isEmpty()) return;
    m_libraryButton->setEnabled(false);
    m_libraryButton->setText(tr("Searching AniList…"));
    emit addToLibraryByTitleRequested(m_currentSeriesTitle);
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
    comicsOpenTrace(QStringLiteral("CSV::showLoadingOverlay"));
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
    comicsOpenTrace(QStringLiteral("CSV::hideLoadingOverlay pendingMediaLoads=%1")
                        .arg(m_pendingMediaLoads));
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
    const int targetW = qMax(m_heroBannerLabel->width(), 800);
    const int targetH = m_heroBannerLabel->height() > 0 ? m_heroBannerLabel->height() : 170;

    const QImage src = pm.toImage();
    QImage scaled = src.scaled(targetW, targetH,
                               Qt::KeepAspectRatioByExpanding,
                               Qt::SmoothTransformation);
    QImage canvas(targetW, targetH, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    QPainter p(&canvas);
    const int sx = (scaled.width() - targetW) / 2;
    const int sy = (scaled.height() - targetH) / 2;
    p.drawImage(QPoint(0, 0), scaled, QRect(sx, sy, targetW, targetH));

    QLinearGradient fade(0, qMax(0, targetH - 80), 0, targetH);
    fade.setColorAt(0.0, QColor(0, 0, 0, 0));
    fade.setColorAt(1.0, QColor(13, 13, 16, 235));
    p.fillRect(canvas.rect(), fade);
    p.end();

    m_heroBannerLabel->setPixmap(QPixmap::fromImage(canvas));
}

void ComicsSeriesView::applyHeroCoverPixmap(const QPixmap& pm)
{
    if (!m_heroCoverLabel || pm.isNull()) return;
    // Scale to the label's CURRENT fixed size (not the hardcoded manga size) so
    // the Western shelf's larger hero cover fills correctly. Manga keeps 90x135.
    const QPixmap scaled = pm.scaled(m_heroCoverLabel->size(),
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
    auto* tile = m_volumeTilesByVolumeNumber.value(volumeNumber, nullptr);
    if (!tile) return;
    tile->setStatusText(statusText);
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

void ComicsSeriesView::onVolumeRowActivated(int volumeNumber)
{
    auto* tile = m_volumeTilesByVolumeNumber.value(volumeNumber, nullptr);
    if (!tile) return;

    for (auto* other : std::as_const(m_volumeTiles)) {
        if (other) other->setSelected(other == tile);
    }

    const auto state = tile->volumeState();
    if (state.state == tankoban::ui::comics::VolumeTileState::Complete
        && !state.cbzPath.isEmpty()) {
        emit openVolume(volumeNumber, state.cbzPath);
        return;
    }

    populateSourcesForVolume(volumeNumber);
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

void ComicsSeriesView::populateSourcesForVolume(int volumeNumber)
{
    if (volumeNumber <= 0 || !m_sourcesPanel) {
        return;
    }

    // COMICS_WESTERN_DOWNLOAD 2026-06-02 (Agent 1). Western (RCO) editions now
    // download via WesternVolumeDownloader (GetComics resolve + magnet/DDL).
    // Emit the signal so ComicsPage routes to the provider; keep the
    // tile-selection highlight that already ran in onVolumeRowActivated.
    // Volumes already downloaded / in progress are handled by the existing
    // Complete-state path at the top of onVolumeRowActivated (cbzPath non-empty
    // => openVolume emitted, never reaching here).
    if (m_currentMangaCatalog.source == QLatin1String("rco")) {
        // Look up the matching catalog volume for its edition metadata.
        // Fall back to empty strings when the volume has no record (graceful).
        QString editionTitle;
        QString tierLabel;
        for (const tankoban::manga::MangaVolume& vol : m_currentMangaCatalog.volumes) {
            if (vol.volumeNumber == volumeNumber) {
                editionTitle = vol.titleEnglish;
                tierLabel    = vol.groupingLabel;
                break;
            }
        }
        emit downloadWesternEditionRequested(volumeNumber, editionTitle, tierLabel);
        return;
    }

    // VOLUME_X_DOWNLOAD: the synthetic Volume X tile is not in m_currentVolumeRows
    // or the catalog, so resolve its sources from the classified tail bucket.
    if (volumeNumber == tankoban::manga::anilist::kVolumeXNumber) {
        const auto it = m_classifiedByVolume.constFind(volumeNumber);
        if (it == m_classifiedByVolume.constEnd() || it->chapterNumbers.isEmpty()) {
            return;
        }
        const auto span = tankoban::manga::volumeXChapterSpan(it->chapterNumbers);
        if (span.first <= 0 || span.second < span.first) {
            return;
        }

        tankoban::manga::anilist::VolumeRow stub;
        stub.volumeNumber = volumeNumber;
        stub.isVolumeX = true;
        stub.chapterRangeStart = span.first;
        stub.chapterRangeEnd = span.second;
        for (double c : it->chapterNumbers) {
            stub.chapterNumbers.append(QString::number(c, 'g', 12));
        }

        m_currentVolumeRows.append(stub);
        const int row = m_currentVolumeRows.size() - 1;
        populateSourcesForRow(row);
        m_currentVolumeRows.removeLast();

        const QString mangaFireSeriesId = m_currentMangaCatalog.isValid()
            ? m_currentMangaCatalog.seriesId
            : QString();
        emit weebCentralResolveRangeRequested(mangaFireSeriesId, volumeNumber,
                                              span.first, span.second);
        return;
    }

    for (int i = 0; i < m_currentVolumeRows.size(); ++i) {
        if (m_currentVolumeRows.at(i).volumeNumber != volumeNumber) {
            continue;
        }

        populateSourcesForRow(i);
        const QString mangaFireSeriesId = m_currentMangaCatalog.isValid()
            ? m_currentMangaCatalog.seriesId
            : QString();
        emit weebCentralResolveRequested(mangaFireSeriesId, volumeNumber);
        return;
    }

    if (!m_currentMangaCatalog.isValid()) {
        return;
    }

    for (const tankoban::manga::MangaVolume& vol : m_currentMangaCatalog.volumes) {
        if (vol.volumeNumber != volumeNumber) {
            continue;
        }

        tankoban::manga::anilist::VolumeRow stub;
        stub.volumeNumber = vol.volumeNumber;
        stub.chapterRangeStart = vol.chapterRangeStart;
        stub.chapterRangeEnd = vol.chapterRangeEnd;
        stub.chapterNumbers = vol.chapterList;
        stub.art.thumbnailUrl = !vol.coverUrlJapanese.isEmpty()
            ? vol.coverUrlJapanese
            : vol.coverUrlEnglish;

        m_currentVolumeRows.append(stub);
        const int row = m_currentVolumeRows.size() - 1;
        populateSourcesForRow(row);
        m_currentVolumeRows.removeLast();
        emit weebCentralResolveRequested(m_currentMangaCatalog.seriesId, volumeNumber);
        return;
    }
}

void ComicsSeriesView::onWeebCentralViable(int volumeNumber, const QStringList& chapterIds)
{
    if (!m_sourcesPanel) {
        return;
    }
    m_sourcesPanel->appendWeebCentralRow(volumeNumber, chapterIds);
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
    QList<anilist::VolumeRow> selectedVols;
    selectedVols.reserve(m_selectedRows.size());
    for (const auto& row : std::as_const(m_currentVolumeRows)) {
        if (m_selectedRows.contains(row.volumeNumber)) {
            selectedVols.append(row);
        }
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
    populateSourcesForVolume(m_currentVolumeRows.at(row).volumeNumber);
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

const tankoban::ui::comics::VolumeTile*
ComicsSeriesView::tileForVolume(int volumeNumber) const
{
    return m_volumeTilesByVolumeNumber.value(volumeNumber, nullptr);
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
    populateSourcesForVolume(volumeNumber);

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

void ComicsSeriesView::showVolumeTileMenu(tankoban::ui::comics::VolumeTile* tile,
                                          const QPoint& pos)
{
    if (!tile) return;
    const auto st = tile->volumeState();
    const bool downloaded =
        (st.state == tankoban::ui::comics::VolumeTileState::Complete)
        && !st.cbzPath.isEmpty();
    if (!downloaded) return;

    QMenu* menu = ContextMenuHelper::createMenu(this);
    QAction* del    = ContextMenuHelper::addDangerAction(menu, tr("Delete…"));
    QAction* reveal = menu->addAction(tr("Reveal in File Explorer"));
    QAction* copy   = menu->addAction(tr("Copy path"));
    const bool fileExists = QFile::exists(st.cbzPath);
    reveal->setEnabled(fileExists);
    copy->setEnabled(fileExists);

    QAction* chosen = menu->exec(tile->mapToGlobal(pos));
    if (chosen == del)         deleteVolumeDownload(tile->volumeNumber(), st.cbzPath);
    else if (chosen == reveal) ContextMenuHelper::revealInExplorer(st.cbzPath);
    else if (chosen == copy)   ContextMenuHelper::copyToClipboard(st.cbzPath);
    menu->deleteLater();
}

void ComicsSeriesView::deleteVolumeDownload(int volumeNumber, const QString& cbzPath)
{
    Q_UNUSED(volumeNumber);
    if (!m_downloadIndex) return;

    const auto choice = ContextMenuHelper::confirmRemoveWithFile(
        this, tr("Delete volume"),
        tr("Remove this volume from your library?"));
    if (choice == ContextMenuHelper::RemoveChoice::Cancel) return;

    // Evict by the exact file path, not a guessed source. A volume's index
    // entry can be sourced from mangafire_catalog / weebcentral / premium; the
    // old hardcoded-weebcentral evictByVolume missed mangafire-packed volumes
    // (e.g. One Piece Vol 114), so the entry stayed registered and the tile
    // never reverted to undownloaded.
    if (!cbzPath.isEmpty())
        m_downloadIndex->evictByPath(cbzPath);

    if (choice == ContextMenuHelper::RemoveChoice::DeleteFile && !cbzPath.isEmpty()) {
        QFile::remove(cbzPath);
        QFile::remove(cbzPath + QStringLiteral(".volx"));
    }

    // Re-render so the just-deleted volume's tile reverts to undownloaded. The
    // download state is re-read per volume from m_downloadIndex during render,
    // so the now-evicted entry drops its cbzPath -> tile loses the completed
    // tick and a click re-opens the Sources panel for re-download. Without this
    // the tile kept its stale Complete state and the volume couldn't be
    // re-downloaded (bug reported 2026-05-29).
    populateVolumeRowsFromCatalog(m_currentMangaCatalog);
}

// COMICS_WESTERN_RICHNESS 2026-06-01 (Agent 9). Western catalogue about-block
// header — synopsis + "author · publisher · year · genre" meta line rendered
// DIRECTLY from populateVolumeRowsFromCatalog (never routed through showSeries /
// dispatchCatalogResolve — Guard #3 no-auto-enrich).

void ComicsSeriesView::buildAboutBlock()
{
    if (m_aboutBlock) return;
    m_aboutBlock = new QWidget(this);
    m_aboutBlock->setObjectName(QStringLiteral("WesternAboutBlock"));
    auto* v = new QVBoxLayout(m_aboutBlock);
    v->setContentsMargins(0, 0, 0, 12);
    v->setSpacing(6);
    m_aboutTitle = new QLabel(m_aboutBlock);
    m_aboutTitle->setObjectName(QStringLiteral("WesternAboutTitle"));
    m_aboutTitle->setWordWrap(true);
    v->addWidget(m_aboutTitle);
    m_aboutMeta = new QLabel(m_aboutBlock);
    m_aboutMeta->setObjectName(QStringLiteral("WesternAboutMeta"));
    m_aboutMeta->setWordWrap(true);
    m_aboutSynopsis = new QLabel(m_aboutBlock);
    m_aboutSynopsis->setObjectName(QStringLiteral("WesternAboutSynopsis"));
    m_aboutSynopsis->setWordWrap(true);
    v->addWidget(m_aboutMeta);
    v->addWidget(m_aboutSynopsis);
    // Insert at the top of the volumes column, above the edition rows.
    if (m_volumesLayout)
        m_volumesLayout->insertWidget(0, m_aboutBlock);
}

void ComicsSeriesView::updateAboutBlock(const tankoban::manga::MangaCatalog& catalog)
{
    buildAboutBlock();
    // Western-only: this path (populateVolumeRowsFromCatalog) is shared with the
    // manga catalog-tile flow, and manga already renders its synopsis in the hero
    // block (m_synopsis) — showing the about-block for manga would duplicate it.
    // Western catalogue records carry source=="rco" (COMICS_WESTERN_RICHNESS).
    if (catalog.source != QStringLiteral("rco")) {
        if (m_aboutBlock) m_aboutBlock->setVisible(false);
        return;
    }
    m_aboutTitle->setText(catalog.seriesTitle);
    m_aboutTitle->setVisible(!catalog.seriesTitle.isEmpty());
    QStringList meta;
    if (!catalog.author.isEmpty())  meta << catalog.author;
    if (!catalog.studio.isEmpty())  meta << catalog.studio;       // publisher
    if (catalog.publishedYearStart) meta << QString::number(catalog.publishedYearStart);
    if (!catalog.genres.isEmpty())  meta << catalog.genres.join(QStringLiteral(", "));
    m_aboutMeta->setText(meta.join(QStringLiteral("  \xC2\xB7  ")));
    m_aboutMeta->setVisible(!meta.isEmpty());
    m_aboutSynopsis->setText(catalog.seriesSynopsis);
    m_aboutSynopsis->setVisible(!catalog.seriesSynopsis.isEmpty());
    m_aboutBlock->setVisible(!meta.isEmpty() || !catalog.seriesSynopsis.isEmpty());
}

} // namespace tankoban::manga::comics
