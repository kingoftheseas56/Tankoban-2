#include "StreamDetailView.h"

#include "core/CoreBridge.h"
#include "core/net/NetSeam.h"
#include "core/stream/MetaAggregator.h"
#include "core/stream/StreamDownloadIndex.h"
#include "core/stream/StreamLibrary.h"
#include "core/stream/StreamProgress.h"
#include "core/torrent/TorrentClient.h"
#include "ui/dialogs/AddTorrentDialog.h"
#include "StreamSourceList.h"

#include <QCheckBox>
#include <QCursor>
#include <QDebug>
#include <QSettings>
#include <QToolButton>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLinearGradient>
#include <QLineEdit>
#include <QIntValidator>
#include <QMenu>
#include <QMessageBox>
#include <QModelIndex>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QTimer>
#include <QPixmap>
#include <QPoint>
#include <QPointer>
#include <QJsonArray>
#include <QJsonObject>
#include <QPushButton>
#include <QSize>
#include <QStandardPaths>
#include <QStackedLayout>
#include <QStyle>
#include <QVBoxLayout>

using tankostream::addon::MetaItem;
using tankostream::stream::MetaAggregator;
using tankostream::stream::StreamEpisode;

namespace {
// STREAM_DOWNLOADS_NETFLIX_OVERHAUL — episode-table columns. Prepended
// checkbox at col 0; appended action icon at col last. Existing columns
// shift +1. All previous references to literal column indices (0..4)
// must use these constants instead. Spec §7.1.
constexpr int kColCheckbox  = 0;
constexpr int kColEpisode   = 1;   // was col 0 (#)
constexpr int kColThumb     = 2;   // was col 1
constexpr int kColTitle     = 3;   // was col 2
constexpr int kColProgress  = 4;   // was col 3
constexpr int kColStatus    = 5;   // was col 4
constexpr int kColAction    = 6;   // NEW
constexpr int kColumnCount  = 7;

// THEATRE_EPISODE_STATE_MODEL (2026-05-30) — the legacy cohort RowState /
// ActionIconSpec / resolveRowState / actionIconForState cluster was removed
// here; episode-row state now derives from the single disk-first
// deriveEpisodeDisplayState (core/stream/EpisodeDisplayState).

// STREAM_DOWNLOADS_NETFLIX_OVERHAUL Task 13 — file-local terminal-state
// predicate for cohort state strings. Mirrors the set used by
// TorrentClient::cancelStreamBulkGroup's cleanup check but avoids
// header-coupling the full predicate into this translation unit.
bool isTerminalCohortState(const QString& state)
{
    static const QSet<QString> kTerminal = {
        QStringLiteral("Published"),      QStringLiteral("Completed"),
        QStringLiteral("Cancelled"),      QStringLiteral("MissingSource"),
        QStringLiteral("MetadataFailed"), QStringLiteral("PublishFailed"),
        QStringLiteral("Failed"),         QStringLiteral("Orphaned"),
    };
    return kTerminal.contains(state);
}

}  // namespace

StreamDetailView::StreamDetailView(CoreBridge* bridge,
                                   MetaAggregator* meta,
                                   StreamLibrary* library,
                                   QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
    , m_meta(meta)
    , m_library(library)
    , m_nam(tankoban::net::NetSeam::instance()->createManager(this, QStringLiteral("stream-detail-view")))
{
    m_heroCacheDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                   + QStringLiteral("/Tankoban/data/stream_backgrounds");
    QDir().mkpath(m_heroCacheDir);

    m_episodeThumbsCacheDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/Tankoban/data/stream_episode_thumbnails");
    QDir().mkpath(m_episodeThumbsCacheDir);

    buildUI();

    // THEATRE_EPISODE_STATE_MODEL (2026-05-30) — single visibility-scoped 1Hz
    // poll for movie badge + per-episode disk-first repaint + season-header
    // button. Replaces the old m_bulkPollTimer (cohort-driven row paints) which
    // is removed; started in showEvent, stopped in hideEvent.
    m_progressRefreshTimer = new QTimer(this);
    m_progressRefreshTimer->setInterval(1000);
    connect(m_progressRefreshTimer, &QTimer::timeout, this, [this]() {
        refreshMovieDownloadState();
        refreshAllEpisodeRows();
        refreshSeasonHeaderButton();
    });

    if (m_meta) {
        connect(m_meta, &MetaAggregator::seriesMetaReady,
                this, &StreamDetailView::onSeriesMetaReady);
        connect(m_meta, &MetaAggregator::animeCatalogActive,
                this, &StreamDetailView::onAnimeCatalogActive);
        connect(m_meta, &MetaAggregator::metaItemReady,
                this, &StreamDetailView::onMetaItemReady);
    }
}

void StreamDetailView::showEntry(const QString& imdbId,
                                 int            preselectSeason,
                                 int            preselectEpisode,
                                 const std::optional<tankostream::addon::MetaItemPreview>& previewHint)
{
    // THEATRE_DOWNLOAD_OVERHAUL stale-panel-on-show-change fix 2026-05-17 -
    // signal a show-context transition BEFORE mutating m_currentImdb so
    // StreamPage can dismiss the TheatreDownloadPanel (which is StreamPage-
    // owned and otherwise has no way to know the show changed under it).
    // Guarded on m_currentImdb being non-empty AND different from the
    // incoming imdb so same-show re-renders (e.g. season-combo changes
    // routed through showEntry) do not spuriously close the panel.
    if (!m_currentImdb.isEmpty() && m_currentImdb != imdbId)
        emit entryContextChanging();

    m_currentImdb = imdbId;
    m_pendingPreselectSeason  = preselectSeason;
    m_pendingPreselectEpisode = preselectEpisode;
    m_seasons.clear();
    m_episodeTable->setRowCount(0);
    m_episodeTable->hide();
    m_seasonRow->hide();
    // THEATRE_ANIME_CATALOG — reset anime state for the new entry; combo +
    // label default to visible (onAnimeCatalogActive re-hides them if anime).
    m_isAnime = false;
    if (m_animeBadge) m_animeBadge->hide();
    if (m_seasonLabel) m_seasonLabel->show();
    if (m_seasonCombo) m_seasonCombo->show();
    if (m_movieActionRow) m_movieActionRow->hide();
    if (m_movieLocalChip) m_movieLocalChip->hide();
    if (m_movieDownloadChip) m_movieDownloadChip->hide();
    if (m_downloadBtn) m_downloadBtn->hide();
    m_lastChoices.clear();
    m_statusLabel->setText("Loading...");
    m_statusLabel->show();

    m_selectedEpisodes.clear();
    updateDownloadSelectedButton();

    // Resolve the display source for the header paint. Preview hint wins
    // when provided (non-library catalog/home/search tiles); library lookup
    // is the fallback (library-grid / continue-strip / calendar paths).
    // In all cases stash m_lastPreviewHint so the Add-to-Library button
    // always has the full metadata to construct a StreamLibraryEntry from
    // (fixes the edge case where user toggles Remove then Add on a title
    // originally opened via the library path — preview hint was nullopt
    // there, so we reconstruct it from the library entry instead).
    QString displayName, displayYear, displayType, displayRating, displayDesc;
    if (previewHint.has_value()) {
        const auto& p = *previewHint;
        displayName   = p.name;
        displayYear   = p.releaseInfo;
        displayType   = p.type;
        displayRating = p.imdbRating;
        displayDesc   = p.description;
        m_lastPreviewHint = previewHint;
    } else {
        StreamLibraryEntry entry = m_library->get(imdbId);
        displayName   = entry.name;
        displayYear   = entry.year;
        displayType   = entry.type;
        displayRating = entry.imdbRating;
        displayDesc   = entry.description;
        if (!entry.imdb.isEmpty()) {
            tankostream::addon::MetaItemPreview p;
            p.id          = entry.imdb;
            p.type        = entry.type;
            p.name        = entry.name;
            p.releaseInfo = entry.year;
            p.poster      = QUrl(entry.poster);
            p.description = entry.description;
            p.imdbRating  = entry.imdbRating;
            m_lastPreviewHint = p;
        } else {
            m_lastPreviewHint.reset();
        }
    }
    m_currentType = displayType;
    refreshLibraryButton();

    m_titleLabel->setText(displayName);

    // 2026-04-15 — info line removed; chips row below the title conveys
    // year + type + rating. Preserving the chips-populate call via
    // applyChips further down so the visual lands identically.

    setDescription(displayDesc);

    // Phase 3 Batch 3.1 — reset hero + chips on every entry. The preview hint
    // carries `background` only when the tile was a full-meta parse (search
    // / detail re-enter); the common catalog path leaves it empty, so we
    // defer the paint to onMetaItemReady. Chips likewise populate from the
    // preview's partial fields (year, rating, type) and enrich on meta
    // arrival (runtime, genres).
    clearHero();
    // Phase 3 Batch 3.2 (deferred ship) — reset cast/director row on every
    // title change so stale values don't linger while onMetaItemReady is
    // in flight for the new entry.
    if (m_castDirectorLabel) {
        m_castDirectorLabel->clear();
        m_castDirectorLabel->hide();
    }
    // Phase 3 Batch 3.5 (deferred ship) — reset trailer state on every
    // title change; applyTrailerButton re-populates from onMetaItemReady.
    m_currentTrailerDirectUrl = QUrl();
    m_currentTrailerYouTubeId.clear();
    if (m_trailerBtn) m_trailerBtn->setVisible(false);
    if (m_lastPreviewHint.has_value() && m_lastPreviewHint->background.isValid()) {
        applyHeroImage(imdbId, m_lastPreviewHint->background, m_lastPreviewHint->poster);
    } else {
        const QUrl posterUrl = m_lastPreviewHint.has_value()
            ? m_lastPreviewHint->poster
            : QUrl();
        if (posterUrl.isValid()) {
            applyHeroImage(imdbId, QUrl(), posterUrl);
        }
    }
    applyChips(displayYear, /*runtime*/ QString(), /*genres*/ {}, displayRating, displayType);

    // Phase 1 Batch 1.1: kick off the richer meta fetch regardless of path so
    // Phase 3 (detail-view density) has the MetaItem cached by the time it
    // subscribes to metaItemReady. Best-effort — initial paint does not
    // depend on this returning.
    if (m_meta && !displayType.isEmpty()) {
        m_meta->fetchMetaItem(imdbId, displayType);
    }

    if (displayType == "movie") {
        m_statusLabel->hide();
        if (m_movieActionRow) m_movieActionRow->show();
        refreshMovieLocalChip();
        refreshMovieDownloadState();
        // THEATRE_DOWNLOAD_SIMPLIFY P4 — the visible Sources pane was removed,
        // so there is no longer a list to "auto-load on detail open". We still
        // emit playRequested(movie) because StreamPage's streamsReady path calls
        // setStreamSources(), which caches m_lastChoices and enables the movie
        // Download button (a silent top-seeded auto-pick). setStreamSourcesLoading()
        // is kept only for its side effect of disabling the Download button until
        // the choices arrive; its source-list write is now a no-op on the hidden
        // list. This does NOT auto-download — the StreamPage movie path never did.
        setStreamSourcesLoading();
        emit playRequested(imdbId, QStringLiteral("movie"), 0, 0);
    } else if (m_meta) {
        m_meta->fetchSeriesMeta(imdbId);
        // For series the right pane stays in the placeholder state until
        // the user clicks an episode (onEpisodeActivated below).
        setStreamSourcesPlaceholder(tr("Select an episode to see sources"));
    } else {
        m_statusLabel->setText("Meta aggregator unavailable");
        setStreamSourcesPlaceholder(tr("Meta aggregator unavailable"));
    }
}

// ─── UI ──────────────────────────────────────────────────────────────────────

void StreamDetailView::buildUI()
{
    // Phase 3 Batch 3.1 — root is now VBox: a thin top bar (back + library
    // buttons), a full-width hero image with gradient fade, then the
    // existing two-column content (left: metadata/episodes, right: sources).
    // Stream-picker UX rework (pre-3.1) kept the two-column content; Phase 3
    // Batch 3.1 hoists the top bar + hero above both columns so the hero
    // spans the full view width per Stremio parity.
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 8, 16, 8);
    root->setSpacing(8);

    // Back button
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(8);

    m_backBtn = new QPushButton("\u2190 Back", this);
    m_backBtn->setObjectName("SidebarAction");
    m_backBtn->setFixedHeight(30);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setStyleSheet(
        "#SidebarAction { background: transparent; border: none; color: rgba(255,255,255,0.7);"
        "  font-size: 13px; padding: 0 8px; }"
        "#SidebarAction:hover { color: #fff; }");
    connect(m_backBtn, &QPushButton::clicked, this, &StreamDetailView::backRequested);
    topRow->addWidget(m_backBtn);
    topRow->addStretch();

    // Phase 1 Batch 1.2 — Add/Remove Library toggle in the detail header.
    // Grayscale-only per `feedback_no_color_no_emoji`. Label + visual state
    // refreshed on every showEntry + on libraryChanged.
    m_libraryBtn = new QPushButton(tr("Add to Library"), this);
    m_libraryBtn->setObjectName("DetailLibraryBtn");
    m_libraryBtn->setFixedHeight(30);
    m_libraryBtn->setCursor(Qt::PointingHandCursor);
    m_libraryBtn->setStyleSheet(
        "#DetailLibraryBtn {"
        "  background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.18);"
        "  border-radius: 6px; color: #e0e0e0; font-size: 12px; padding: 0 14px; }"
        "#DetailLibraryBtn:hover { background: rgba(255,255,255,0.14);"
        "  border-color: rgba(255,255,255,0.28); }"
        "#DetailLibraryBtn[inLibrary=\"true\"] {"
        "  background: rgba(255,255,255,0.04); color: rgba(255,255,255,0.7); }");
    connect(m_libraryBtn, &QPushButton::clicked, this,
            &StreamDetailView::onLibraryButtonClicked);
    m_libraryBtn->setVisible(false);   // refreshLibraryButton shows it on valid showEntry
    topRow->addWidget(m_libraryBtn);

    // Phase 3 Batch 3.5 (deferred ship) — Watch Trailer button. Same
    // visual weight as Add-to-Library — sits next to it in the top bar.
    // Hidden until applyTrailerButton populates trailer state.
    m_trailerBtn = new QPushButton(tr("Watch Trailer"), this);
    m_trailerBtn->setObjectName("DetailTrailerBtn");
    m_trailerBtn->setFixedHeight(30);
    m_trailerBtn->setCursor(Qt::PointingHandCursor);
    m_trailerBtn->setStyleSheet(
        "#DetailTrailerBtn {"
        "  background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.18);"
        "  border-radius: 6px; color: #e0e0e0; font-size: 12px; padding: 0 14px; }"
        "#DetailTrailerBtn:hover { background: rgba(255,255,255,0.14);"
        "  border-color: rgba(255,255,255,0.28); }");
    connect(m_trailerBtn, &QPushButton::clicked,
            this, &StreamDetailView::onTrailerClicked);
    m_trailerBtn->setVisible(false);
    topRow->addWidget(m_trailerBtn);

    root->addLayout(topRow);

    // Keep the button in sync when an external path (search-widget badge,
    // library grid, continue strip) toggles StreamLibrary. onLibraryButton
    // above also emits libraryChanged — re-entry is cheap since we just
    // rewrite text/property.
    if (m_library) {
        connect(m_library, &StreamLibrary::libraryChanged,
                this, &StreamDetailView::refreshLibraryButton);
    }

    // Phase 3 Batch 3.1 — hero background art (full view width).
    // Hidden until applyHeroImage paints a pixmap; renderHeroPixmap bakes the
    // bottom-fade gradient into the image so content below reads cleanly.
    //
    // 2026-04-15 height reduction — was 240px which dominated the detail
    // view on smaller windows (episodes + sources ended up in a tiny
    // corner). 140px gives the hero visual presence without eating the
    // functional content budget below it.
    m_heroLabel = new QLabel(this);
    m_heroLabel->setFixedHeight(140);
    m_heroLabel->setMinimumWidth(0);
    m_heroLabel->setAlignment(Qt::AlignCenter);
    m_heroLabel->setScaledContents(false);
    m_heroLabel->setObjectName("StreamDetailHero");
    m_heroLabel->setStyleSheet(
        "#StreamDetailHero { background: #101010; border-radius: 8px; }");
    m_heroLabel->hide();
    root->addWidget(m_heroLabel);

    // Two-column content below the hero.
    auto* contentRow = new QHBoxLayout();
    contentRow->setSpacing(16);

    auto* leftCol = new QVBoxLayout();
    leftCol->setSpacing(8);

    // Title
    m_titleLabel = new QLabel(this);
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setStyleSheet("color: #e0e0e0; font-size: 16px; font-weight: bold;");
    leftCol->addWidget(m_titleLabel);

    // THEATRE_ANIME_CATALOG — "ANIME" pill under the title. Hidden by default;
    // shown by onAnimeCatalogActive when the flat Kitsu catalog is in use.
    m_animeBadge = new QLabel(tr("ANIME"), this);
    m_animeBadge->setObjectName(QStringLiteral("StreamDetailAnimeBadge"));
    m_animeBadge->setStyleSheet(
        "#StreamDetailAnimeBadge {"
        "  background: rgba(139,92,246,0.22);"
        "  color: #c4b5fd;"
        "  border: 1px solid rgba(139,92,246,0.45);"
        "  border-radius: 4px; padding: 1px 7px;"
        "  font-size: 10px; font-weight: 600; letter-spacing: 0.5px; }");
    m_animeBadge->hide();
    {
        auto* badgeRow = new QHBoxLayout();
        badgeRow->setContentsMargins(0, 0, 0, 0);
        badgeRow->addWidget(m_animeBadge);
        badgeRow->addStretch(1);
        leftCol->addLayout(badgeRow);
    }

    // STREAM_DETAIL_METADATA_POLISH 2026-05-06 — single inline metadata
    // line (Stremio parity). Replaces the earlier 5-chip row whose QSS
    // (padding 2px 10px, 11px font, 10px border-radius) intended small
    // pills but visually rendered as ~100px chunky blocks once stretched
    // by the parent layout. Stremio's detail view shows year · runtime ·
    // genres · type · IMDb rating as a single muted-gray inline string —
    // less visual noise, more vertical space for description + episodes.
    m_metaLine = new QLabel(this);
    m_metaLine->setObjectName(QStringLiteral("StreamDetailMetaLine"));
    m_metaLine->setWordWrap(true);
    m_metaLine->setStyleSheet(
        "QLabel#StreamDetailMetaLine {"
        "  background: transparent;"
        "  border: none;"
        "  color: rgba(255,255,255,0.62);"
        "  font-size: 12px;"
        "  font-weight: 400;"
        "  padding: 0;"
        "  margin: 4px 0 0 0;"
        "}");
    m_metaLine->hide();   // hidden until applyChips lands first non-empty paint
    leftCol->addWidget(m_metaLine);

    // 2026-04-15 — m_infoLabel removed. Chips row above already shows
    // year + type + rating from the first-paint preview hint; the info
    // line was redundant and ate 20px of vertical space the episode
    // table needs.

    // Description
    // Phase 3 Batch 3.3 — 3-line clamped description with "Show more / less"
    // toggle. Clamp is computed dynamically from QFontMetrics so short
    // descriptions skip the toggle entirely; long ones reveal the affordance
    // below. Expanded mode removes the maximum height and swaps the label.
    m_descLabel = new QLabel(this);
    m_descLabel->setWordWrap(true);
    m_descLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_descLabel->setStyleSheet("color: rgba(255,255,255,0.4); font-size: 11px;");
    leftCol->addWidget(m_descLabel);

    m_descShowMoreBtn = new QPushButton(tr("Show more"), this);
    m_descShowMoreBtn->setObjectName("DescShowMoreBtn");
    m_descShowMoreBtn->setCursor(Qt::PointingHandCursor);
    m_descShowMoreBtn->setFlat(true);
    m_descShowMoreBtn->setStyleSheet(
        "#DescShowMoreBtn { background: transparent; border: none;"
        "  color: rgba(255,255,255,0.75); font-size: 11px; padding: 0;"
        "  text-align: left; }"
        "#DescShowMoreBtn:hover { color: #fff; text-decoration: underline; }");
    m_descShowMoreBtn->hide();
    connect(m_descShowMoreBtn, &QPushButton::clicked,
            this, &StreamDetailView::onDescShowMoreClicked);
    leftCol->addWidget(m_descShowMoreBtn, 0, Qt::AlignLeft);

    // Phase 3 Batch 3.2 (deferred ship) — director + cast row. Hidden
    // until applyCastDirector populates it from MetaItem.preview.links.
    m_castDirectorLabel = new QLabel(this);
    m_castDirectorLabel->setWordWrap(true);
    m_castDirectorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_castDirectorLabel->setMaximumHeight(40);
    m_castDirectorLabel->setStyleSheet(
        "color: rgba(255,255,255,0.55); font-size: 11px;");
    m_castDirectorLabel->hide();
    leftCol->addWidget(m_castDirectorLabel);

    m_movieActionRow = new QWidget(this);
    auto* movieActionLayout = new QHBoxLayout(m_movieActionRow);
    movieActionLayout->setContentsMargins(0, 4, 0, 4);
    movieActionLayout->setSpacing(8);

    m_movieDownloadBtn = new QPushButton(tr("Download"), m_movieActionRow);
    m_movieDownloadBtn->setObjectName(QStringLiteral("DetailMovieDownloadBtn"));
    m_movieDownloadBtn->setFixedHeight(30);
    m_movieDownloadBtn->setCursor(Qt::PointingHandCursor);
    m_movieDownloadBtn->setIcon(QIcon(QStringLiteral(":/icons/download-arrow.svg")));
    m_movieDownloadBtn->setStyleSheet(
        "#DetailMovieDownloadBtn { background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.14); border-radius: 6px;"
        "  color: #ddd; padding: 0 12px; font-size: 12px; }"
        "#DetailMovieDownloadBtn:hover { background: rgba(255,255,255,0.12);"
        "  border-color: rgba(255,255,255,0.22); }");
    m_movieDownloadBtn->setEnabled(false);
    connect(m_movieDownloadBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentImdb.isEmpty()) return;
        if (m_lastChoices.isEmpty()) {
            qWarning() << "StreamDetailView::movieDownloadBtn click: no loaded choices for"
                       << m_currentImdb << "- ignoring click";
            return;
        }
        // m_lastChoices is pre-sorted by buildPickerChoices: magnets-with-seeders
        // first (descending by seeder count). The first magnet-kind entry is the
        // top-seeded auto-pick. See StreamSourceChoice.h:62-65 for sort contract.
        QString topHash;
        QString topMagnet;
        for (const auto& choice : m_lastChoices) {
            if (choice.sourceKind == QLatin1String("magnet")) {
                topHash = choice.infoHash;
                topMagnet = choice.magnetUri;
                break;
            }
        }
        if (topHash.isEmpty() && topMagnet.isEmpty()) {
            qWarning() << "StreamDetailView::movieDownloadBtn click: no magnet sources for"
                       << m_currentImdb << "- ignoring click";
            return;
        }
        emit theatreTopSeededDownloadRequested(m_currentImdb,
                                                currentTitle(),
                                                topHash,
                                                topMagnet);
        // THEATRE_EPISODE_STATE_MODEL (2026-05-30) — the visibility-scoped 1Hz
        // m_progressRefreshTimer picks up the dispatch; no grace-window timer.
    });
    movieActionLayout->addWidget(m_movieDownloadBtn);

    m_movieLocalChip = new QLabel(tr("LOCAL"), m_movieActionRow);
    m_movieLocalChip->setObjectName(QStringLiteral("DetailMovieLocalChip"));
    m_movieLocalChip->setFixedHeight(20);
    m_movieLocalChip->setAlignment(Qt::AlignCenter);
    m_movieLocalChip->setStyleSheet(
        "#DetailMovieLocalChip { background: rgba(255,255,255,0.10);"
        "  border: 1px solid rgba(255,255,255,0.22); border-radius: 2px;"
        "  color: #e6e6e6; padding: 1px 6px; font-size: 9px;"
        "  font-weight: 600; }");
    m_movieLocalChip->hide();
    movieActionLayout->addWidget(m_movieLocalChip);

    m_movieDownloadChip = new QLabel(tr("DOWNLOADING"), m_movieActionRow);
    m_movieDownloadChip->setObjectName(QStringLiteral("DetailMovieDownloadChip"));
    m_movieDownloadChip->setFixedHeight(20);
    m_movieDownloadChip->setAlignment(Qt::AlignCenter);
    m_movieDownloadChip->setStyleSheet(
        "#DetailMovieDownloadChip { background: rgba(255,255,255,0.10);"
        "  border: 1px solid rgba(255,255,255,0.22); border-radius: 2px;"
        "  color: #e6e6e6; padding: 1px 6px; font-size: 9px;"
        "  font-weight: 600; }");
    m_movieDownloadChip->hide();
    movieActionLayout->addWidget(m_movieDownloadChip);
    movieActionLayout->addStretch();
    m_movieActionRow->hide();
    leftCol->addWidget(m_movieActionRow);

    // Season selector row (hidden for movies)
    m_seasonRow = new QWidget(this);
    auto* seasonLayout = new QHBoxLayout(m_seasonRow);
    seasonLayout->setContentsMargins(0, 4, 0, 4);
    seasonLayout->setSpacing(8);

    m_seasonLabel = new QLabel("Season:", m_seasonRow);
    m_seasonLabel->setStyleSheet("color: rgba(255,255,255,0.6); font-size: 13px;");
    seasonLayout->addWidget(m_seasonLabel);

    m_seasonCombo = new QComboBox(m_seasonRow);
    m_seasonCombo->setFixedWidth(120);
    m_seasonCombo->setStyleSheet(
        "QComboBox { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12);"
        "  border-radius: 6px; color: #ccc; padding: 2px 8px; font-size: 12px; }"
        "QComboBox:hover { border-color: rgba(255,255,255,0.2); }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView {"
        "  background: #1e1e1e; color: #ccc; selection-background-color: rgba(255,255,255,0.1);"
        "  border: 1px solid rgba(255,255,255,0.12); }");
    connect(m_seasonCombo, &QComboBox::currentIndexChanged,
            this, &StreamDetailView::onSeasonChanged);
    seasonLayout->addWidget(m_seasonCombo);

    // Episode-number jump/filter (2026-06-02, Hemanth ask) — for long anime like
    // One Piece (1000+ eps) where scrolling the list is impractical. Typing an
    // episode number collapses the list to matching rows + scrolls to the first;
    // clearing restores the full list.
    m_episodeSearch = new QLineEdit(m_seasonRow);
    m_episodeSearch->setObjectName(QStringLiteral("EpisodeSearch"));
    m_episodeSearch->setPlaceholderText(tr("Go to episode #"));
    m_episodeSearch->setClearButtonEnabled(true);
    m_episodeSearch->setFixedWidth(150);
    m_episodeSearch->setFixedHeight(28);
    m_episodeSearch->setValidator(new QIntValidator(0, 1000000, m_episodeSearch));
    m_episodeSearch->setStyleSheet(
        "QLineEdit { background: rgba(255,255,255,0.07);"
        "  border: 1px solid rgba(255,255,255,0.12); border-radius: 6px;"
        "  color: #ccc; padding: 2px 8px; font-size: 12px; }"
        "QLineEdit:focus { border-color: rgba(255,255,255,0.25); }");
    connect(m_episodeSearch, &QLineEdit::textChanged,
            this, &StreamDetailView::filterEpisodesByNumber);
    seasonLayout->addWidget(m_episodeSearch);

    m_downloadBtn = new QPushButton(tr("Download"), m_seasonRow);
    m_downloadBtn->setObjectName(QStringLiteral("DetailDownloadBtn"));
    m_downloadBtn->setFixedHeight(30);
    m_downloadBtn->setCursor(Qt::PointingHandCursor);
    m_downloadBtn->setIcon(QIcon(QStringLiteral(":/icons/download-arrow.svg")));
    m_downloadBtn->setStyleSheet(
        "#DetailDownloadBtn { background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.14); border-radius: 6px;"
        "  color: #ddd; padding: 0 12px; font-size: 12px; }"
        "#DetailDownloadBtn:hover { background: rgba(255,255,255,0.12);"
        "  border-color: rgba(255,255,255,0.22); }");
    // THEATRE_DOWNLOAD_OVERHAUL Phase E: unified show/movie Download entry.
    connect(m_downloadBtn, &QPushButton::clicked,
            this, &StreamDetailView::onDownloadSeasonClicked);

    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL Task 13 — "Download Selected (N)"
    // secondary button; visible only when m_selectedEpisodes is non-empty.
    m_downloadSelectedBtn = new QPushButton(this);
    m_downloadSelectedBtn->setCursor(Qt::PointingHandCursor);
    m_downloadSelectedBtn->setIcon(QIcon(QStringLiteral(":/icons/download-arrow.svg")));
    m_downloadSelectedBtn->setVisible(false);
    m_downloadSelectedBtn->setFixedHeight(30);
    m_downloadSelectedBtn->setObjectName(QStringLiteral("DetailDownloadSelectedBtn"));
    m_downloadSelectedBtn->setStyleSheet(
        "#DetailDownloadSelectedBtn { background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.14); border-radius: 6px;"
        "  color: #ddd; padding: 0 12px; font-size: 12px; }"
        "#DetailDownloadSelectedBtn:hover { background: rgba(255,255,255,0.12);"
        "  border-color: rgba(255,255,255,0.22); }");
    connect(m_downloadSelectedBtn, &QPushButton::clicked,
            this, &StreamDetailView::onDownloadSelectedClicked);

    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL Task 13 — right-click on the season
    // combo also opens the "Cancel Season" context menu.
    m_seasonCombo->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_seasonCombo, &QComboBox::customContextMenuRequested,
            this, &StreamDetailView::onSeasonHeaderRightClick);

    seasonLayout->addStretch();
    seasonLayout->addWidget(m_downloadSelectedBtn);
    seasonLayout->addWidget(m_downloadBtn);

    // THEATRE_DOWNLOAD_SIMPLIFY P4.2 (2026-05-30) — the "Pack downloads"
    // (layers-3) button that opened the Tankorent pack picker (TheatreDownloadPanel)
    // is removed from the series view per the download-simplify direction. The
    // Tankorent SEARCH engine (UnifiedPackSearchEngine / TankorentSearchService /
    // TheatreDownloadPanel class) is left fully intact for the future streaming
    // revival — only this UI trigger is gone. The theatreDownloadRequested signal
    // is retained (declared in the header; StreamPage still connects it) so the
    // panel can be re-surfaced later without re-plumbing.

    m_seasonRow->hide();
    leftCol->addWidget(m_seasonRow);

    // Episode table — single-click triggers source load per UX rework.
    // The legacy Play Movie button is gone: movie-mode detail view auto-
    // loads its sources on showEntry and the user clicks a source card
    // directly (no middle-column button).
    m_episodeTable = new QTableWidget(this);
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — 7-column layout. Prepended checkbox
    // at col 0; appended action icon at col 6. Existing columns shift +1.
    //   [0 ☐] [1 #] [2 Thumb 64x36] [3 Title+Overview stacked] [4 Progress] [5 Status] [6 ⋮]
    m_episodeTable->setColumnCount(kColumnCount);
    m_episodeTable->setHorizontalHeaderLabels({
        QString(),                   // checkbox — no header text
        QStringLiteral("#"),
        QString(),                   // thumbnail — no header text
        tr("Title"),
        tr("Progress"),
        tr("Status"),
        QString(),                   // action — no header text
    });
    m_episodeTable->horizontalHeader()->setSectionResizeMode(kColCheckbox,  QHeaderView::Fixed);
    m_episodeTable->horizontalHeader()->setSectionResizeMode(kColEpisode,   QHeaderView::Fixed);
    m_episodeTable->horizontalHeader()->setSectionResizeMode(kColThumb,     QHeaderView::Fixed);
    m_episodeTable->horizontalHeader()->setSectionResizeMode(kColTitle,     QHeaderView::Stretch);
    m_episodeTable->horizontalHeader()->setSectionResizeMode(kColProgress,  QHeaderView::Fixed);
    m_episodeTable->horizontalHeader()->setSectionResizeMode(kColStatus,    QHeaderView::Fixed);
    m_episodeTable->horizontalHeader()->setSectionResizeMode(kColAction,    QHeaderView::Fixed);
    m_episodeTable->setColumnWidth(kColCheckbox, 32);
    m_episodeTable->setColumnWidth(kColEpisode,  36);
    m_episodeTable->setColumnWidth(kColThumb,    118);  // 102px thumb (~1.6x) + 16px padding
    m_episodeTable->setColumnWidth(kColProgress, 80);
    m_episodeTable->setColumnWidth(kColStatus,   60);
    m_episodeTable->setColumnWidth(kColAction,   64);  // holds the "Play" text button (downloaded) + icon glyphs
    m_episodeTable->verticalHeader()->setDefaultSectionSize(84);
    m_episodeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_episodeTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_episodeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_episodeTable->setShowGrid(false);
    m_episodeTable->setAlternatingRowColors(true);
    m_episodeTable->setSortingEnabled(false);   // Batch 3.4 — see populateEpisodeTable note.
    m_episodeTable->verticalHeader()->hide();
    m_episodeTable->setStyleSheet(
        "QTableWidget { background: transparent; border: none; color: #ccc;"
        "  alternate-background-color: rgba(255,255,255,0.03); }"
        "QTableWidget::item { padding: 4px; }"
        "QTableWidget::item:selected { background: rgba(255,255,255,0.08); }"
        "QHeaderView::section { background: rgba(255,255,255,0.05); color: rgba(255,255,255,0.5);"
        "  border: none; font-size: 11px; padding: 4px; }");

    connect(m_episodeTable, &QTableWidget::cellClicked,
            this, &StreamDetailView::onEpisodeActivated);

    // STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) — right-click on a row
    // opens the "Show alternate streams" menu; routes to the existing
    // source-pick flow regardless of disk-presence. Spec §6.3.
    m_episodeTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_episodeTable, &QWidget::customContextMenuRequested,
            this, &StreamDetailView::onEpisodeContextMenu);
    m_episodeTable->hide();
    leftCol->addWidget(m_episodeTable, 1);

    // Status
    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("color: rgba(255,255,255,0.4); font-size: 11px; padding: 20px;");
    leftCol->addWidget(m_statusLabel);

    // MOVIE_DETAIL_LEFT_COL_TIDY_FIX 2026-05-18 (Agent 5) -- terminate the
    // left column with a stretch so leftover vertical space (forced on us
    // by the tall right-pane Sources list) lands at the bottom instead of
    // being distributed across the title / meta / synopsis / cast / Download
    // labels. Without this, movie mode (where m_episodeTable is hidden and
    // its stretch=1 doesn't apply) gives every wordWrap QLabel a Preferred
    // policy that grows to soak up extra height -- producing the ~150px
    // gaps Hemanth flagged on the Fight Club detail view. Series mode
    // unaffected: m_episodeTable's stretch=1 outranks this spacer's
    // stretch=0 in the Expanding-policy fight for leftover space.
    leftCol->addStretch();

    // THEATRE_DOWNLOAD_SIMPLIFY P4.1 — right pane (Tankorent season-pack UI)
    // removed; episode list takes the full window width. m_rightPaneStack /
    // m_sourcesPanel stay null; rightPaneStack() returns null so StreamPage's
    // null-guarded panel wiring cleanly no-ops. The Tankorent SEARCH engine
    // (m_unifiedPackSearchEngine / TankorentSearchService / TheatreDownloadPanel)
    // is untouched.
    contentRow->addLayout(leftCol, 1);

    // m_sourcesHeader is retained (constructed off-layout, never shown) so the
    // member stays non-null for devSnapshot() and the header decl is harmless.
    m_sourcesHeader = new QLabel(tr("Sources"), this);
    m_sourcesHeader->hide();

    // m_sourcesList is retained (constructed off-layout, never shown) so the
    // setStreamSources*/placeholder/toast passthroughs and the four source
    // signal re-emits stay wired without a visible pane. Parented to this (not
    // m_sourcesPanel, which is now null) so it is owned + destroyed with the view.
    m_sourcesList = new tankostream::stream::StreamSourceList(this);
    m_sourcesList->hide();
    connect(m_sourcesList, &tankostream::stream::StreamSourceList::sourceActivated,
            this, &StreamDetailView::sourceActivated);
    connect(m_sourcesList, &tankostream::stream::StreamSourceList::addToTankorentRequested,
            this, &StreamDetailView::addToTankorentRequested);
    connect(m_sourcesList, &tankostream::stream::StreamSourceList::directDownloadRequested,
            this, &StreamDetailView::directDownloadRequested);
    connect(m_sourcesList, &tankostream::stream::StreamSourceList::autoLaunchCancelRequested,
            this, &StreamDetailView::autoLaunchCancelRequested);

    root->addLayout(contentRow, 1);
}

void StreamDetailView::setStreamSourcesLoading()
{
    if (m_sourcesList) m_sourcesList->setLoading();
    if (m_movieDownloadBtn) m_movieDownloadBtn->setEnabled(false);
    refreshMovieDownloadState();
}

void StreamDetailView::setStreamSources(
    const QList<tankostream::stream::StreamPickerChoice>& choices,
    const QString&                                        savedChoiceKey)
{
    // THEATRE_DOWNLOAD_OVERHAUL E1 UX refinement 2026-05-17 — cache the
    // sorted choice list so the movie-row Download button can pick the
    // top-seeded magnet without re-running the aggregator. buildPickerChoices
    // already sorts magnets-with-seeders first (StreamSourceChoice.h:62-65).
    m_lastChoices = choices;
    if (m_sourcesList) m_sourcesList->setSources(choices, savedChoiceKey);
    // STREAM_ASYNC_RACE_FIXES 2026-05-18 Task A - enable the movie Download
    // button only when at least one magnet source is present. Pre-fix the
    // button was clickable from the moment the movie-action-row showed,
    // silently no-op-ing when m_lastChoices was empty (Hemanth's 2026-05-18
    // smoke). Walking the choices for a magnet matches the click handler's
    // own filter at line 513-519.
    if (m_movieDownloadBtn) {
        bool hasMagnet = false;
        for (const auto& choice : m_lastChoices) {
            if (choice.sourceKind == QLatin1String("magnet")) {
                hasMagnet = true;
                break;
            }
        }
        m_movieDownloadBtn->setEnabled(hasMagnet);
    }
    refreshMovieDownloadState();
}

void StreamDetailView::setStreamSourcesError(const QString& message)
{
    // THEATRE_DOWNLOAD_SIMPLIFY P4 — the Sources pane is gone, so route the
    // error text (8 StreamPage callers use this as the generic "show an error"
    // surface for auto-download / bulk failures) to the always-present status
    // label instead of the now-hidden source list. The list write is kept
    // (guarded, harmless) so devSnapshot + any residual reads stay consistent.
    if (m_statusLabel && !message.isEmpty()) {
        m_statusLabel->setText(message);
        m_statusLabel->show();
    }
    if (m_sourcesList) m_sourcesList->setError(message);
    if (m_movieDownloadBtn) m_movieDownloadBtn->setEnabled(false);
    refreshMovieDownloadState();
}

void StreamDetailView::setStreamSourcesPlaceholder(const QString& message)
{
    if (m_sourcesList) m_sourcesList->setPlaceholder(message);
    if (m_movieDownloadBtn) m_movieDownloadBtn->setEnabled(false);
    refreshMovieDownloadState();
}

void StreamDetailView::showAutoLaunchToast(const QString& label)
{
    if (m_sourcesList) m_sourcesList->showAutoLaunchToast(label);
}

void StreamDetailView::hideAutoLaunchToast()
{
    if (m_sourcesList) m_sourcesList->hideAutoLaunchToast();
}

QString StreamDetailView::currentTitle() const
{
    if (m_lastPreviewHint.has_value() && !m_lastPreviewHint->name.isEmpty())
        return m_lastPreviewHint->name;
    return m_titleLabel ? m_titleLabel->text() : QString();
}

QString StreamDetailView::currentYear() const
{
    if (m_lastPreviewHint.has_value())
        return m_lastPreviewHint->releaseInfo;
    return {};
}

QList<StreamEpisode> StreamDetailView::episodesForSeason(int season) const
{
    return m_seasons.value(season);
}

QMap<int, int> StreamDetailView::episodeCountsBySeason() const
{
    QMap<int, int> out;
    for (auto it = m_seasons.constBegin(); it != m_seasons.constEnd(); ++it)
        out.insert(it.key(), it.value().size());
    return out;
}

QJsonObject StreamDetailView::devSnapshot() const
{
    QJsonObject snap;
    snap[QStringLiteral("currentImdb")] = m_currentImdb;
    snap[QStringLiteral("currentType")] = m_currentType;
    snap[QStringLiteral("currentTitle")] = currentTitle();
    snap[QStringLiteral("currentYear")] = currentYear();
    snap[QStringLiteral("currentSeason")] = currentSeason();
    snap[QStringLiteral("selectedEpisodes")] = m_selectedEpisodes.size();
    snap[QStringLiteral("movieDownloadButtonEnabled")] =
        m_movieDownloadBtn && m_movieDownloadBtn->isEnabled();
    snap[QStringLiteral("movieDownloadChipVisible")] =
        m_movieDownloadChip && m_movieDownloadChip->isVisible();

    QJsonArray rows;
    if (m_episodeTable) {
        const int season = currentSeason();
        const auto cohortSnap = (m_torrentClient && !m_currentImdb.isEmpty() && season > 0)
            ? m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, season)
            : QHash<int, QPair<QString, int>>();
        for (int row = 0; row < m_episodeTable->rowCount(); ++row) {
            auto* epItem = m_episodeTable->item(row, kColEpisode);
            if (!epItem)
                continue;
            const int ep = epItem->data(Qt::UserRole).toInt();
            QJsonObject r;
            r[QStringLiteral("ep")] = ep;
            r[QStringLiteral("season")] = epItem->data(Qt::UserRole + 1).toInt();
            r[QStringLiteral("status")] =
                m_episodeTable->item(row, kColStatus)
                    ? m_episodeTable->item(row, kColStatus)->text()
                    : QString();
            r[QStringLiteral("progress")] =
                m_episodeTable->item(row, kColProgress)
                    ? m_episodeTable->item(row, kColProgress)->text()
                    : QString();
            r[QStringLiteral("hasDownloadIcon")] =
                m_episodeTable->cellWidget(row, kColAction) != nullptr;
            r[QStringLiteral("selected")] = m_selectedEpisodes.contains(ep);
            const auto cohortIt = cohortSnap.constFind(ep);
            if (cohortIt != cohortSnap.constEnd()) {
                r[QStringLiteral("bulkState")] = cohortIt->first;
                r[QStringLiteral("bulkProgressPct")] = cohortIt->second;
            }
            rows.append(r);
        }
    }
    snap[QStringLiteral("episodeRows")] = rows;

    QJsonObject sources;
    sources[QStringLiteral("visible")] = m_sourcesPanel && m_sourcesPanel->isVisible();
    sources[QStringLiteral("sourceCount")] = m_lastChoices.size();
    sources[QStringLiteral("header")] = m_sourcesHeader ? m_sourcesHeader->text() : QString();
    snap[QStringLiteral("sourcesPanel")] = sources;
    return snap;
}

void StreamDetailView::updateBulkDownloadButton()
{
    if (!m_downloadBtn || !m_seasonCombo)
        return;
    const int season = m_seasonCombo->currentData().toInt();
    const bool visible = m_currentType == QLatin1String("series")
        && season > 0
        && !m_seasons.value(season).isEmpty();
    m_downloadBtn->setVisible(visible);
}

void StreamDetailView::updateDownloadSelectedButton()
{
    if (!m_downloadSelectedBtn)
        return;
    const int n = m_selectedEpisodes.size();
    if (n == 0) {
        m_downloadSelectedBtn->setVisible(false);
    } else {
        m_downloadSelectedBtn->setVisible(true);
        m_downloadSelectedBtn->setText(tr("Download Selected (%1)").arg(n));
    }
}

// ─── Series metadata ─────────────────────────────────────────────────────────

void StreamDetailView::onAnimeCatalogActive(const QString& imdbId, bool isAnime)
{
    if (imdbId != m_currentImdb)
        return;
    m_isAnime = isAnime;
    if (m_animeBadge)
        m_animeBadge->setVisible(isAnime);
    // Anime uses one flat absolute-numbered list, so the season combo is
    // meaningless — hide it (the episode table already shows the flat list).
    // Download controls in the same row stay visible.
    if (m_seasonLabel) m_seasonLabel->setVisible(!isAnime);
    if (m_seasonCombo) m_seasonCombo->setVisible(!isAnime);
}

void StreamDetailView::onSeriesMetaReady(
    const QString& imdbId,
    const QMap<int, QList<StreamEpisode>>& seasons)
{
    if (imdbId != m_currentImdb)
        return;

    m_seasons = seasons;
    m_statusLabel->hide();

    if (m_seasons.isEmpty()) {
        m_statusLabel->setText("No episodes found");
        m_statusLabel->show();
        updateBulkDownloadButton();
        return;
    }

    // Populate season combo
    m_seasonCombo->blockSignals(true);
    m_seasonCombo->clear();
    int preselectComboIdx = -1;
    int idx = 0;
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL 2026-05-12 post-smoke v2 — read
    // last-viewed season from QSettings (set by onSeasonChanged below).
    // Explicit preselect (from a calendar/back-stack/etc. nav) wins over
    // the remembered last-viewed; the persisted value is the fallback
    // for the library-tile → detail-view default path.
    int rememberedSeason = -1;
    {
        QSettings s;
        const QString key = QStringLiteral("stream/lastSeason/") + m_currentImdb;
        const QVariant v = s.value(key);
        if (v.isValid()) rememberedSeason = v.toInt();
    }
    for (auto it = m_seasons.begin(); it != m_seasons.end(); ++it) {
        m_seasonCombo->addItem("Season " + QString::number(it.key()), it.key());
        if (m_pendingPreselectSeason >= 0 && it.key() == m_pendingPreselectSeason)
            preselectComboIdx = idx;
        else if (m_pendingPreselectSeason < 0
                 && preselectComboIdx < 0
                 && rememberedSeason > 0
                 && it.key() == rememberedSeason)
            preselectComboIdx = idx;
        ++idx;
    }
    m_seasonCombo->blockSignals(false);

    m_seasonRow->show();
    // THEATRE_ANIME_CATALOG — the flat Kitsu list has a single meaningless
    // "Season 1"; hide the combo (the episode table already shows the flat
    // list). Download controls in the same row stay visible.
    if (m_seasonLabel) m_seasonLabel->setVisible(!m_isAnime);
    if (m_seasonCombo) m_seasonCombo->setVisible(!m_isAnime);
    updateBulkDownloadButton();

    // Batch 6.2 — Calendar navigation: if a season/episode was staged by
    // the caller, switch the combo and focus the matching episode row.
    // Consume-once semantics: clear the pending values so a later showEntry
    // without preselection doesn't re-apply.
    if (preselectComboIdx >= 0) {
        m_seasonCombo->setCurrentIndex(preselectComboIdx);
        onSeasonChanged(preselectComboIdx);

        if (m_pendingPreselectEpisode >= 0) {
            for (int r = 0; r < m_episodeTable->rowCount(); ++r) {
                auto* cell = m_episodeTable->item(r, kColEpisode);
                if (cell && cell->data(Qt::UserRole).toInt() == m_pendingPreselectEpisode) {
                    m_episodeTable->selectRow(r);
                    m_episodeTable->scrollToItem(cell, QAbstractItemView::PositionAtCenter);
                    break;
                }
            }
        }
    } else {
        onSeasonChanged(0);
    }

    m_pendingPreselectSeason  = -1;
    m_pendingPreselectEpisode = -1;
    updateBulkDownloadButton();
}

void StreamDetailView::onSeasonChanged(int comboIndex)
{
    if (comboIndex < 0 || comboIndex >= m_seasonCombo->count())
        return;

    m_selectedEpisodes.clear();
    updateDownloadSelectedButton();

    int season = m_seasonCombo->itemData(comboIndex).toInt();
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL 2026-05-12 post-smoke v2 —
    // persist per-show last-viewed season so the next time the user
    // opens this show, the same season auto-loads instead of always
    // defaulting to the first season.
    if (!m_currentImdb.isEmpty() && season > 0) {
        QSettings s;
        s.setValue(QStringLiteral("stream/lastSeason/") + m_currentImdb, season);
    }
    populateEpisodeTable(season);
    updateBulkDownloadButton();
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL Task 13 — re-evaluate the
    // season-header morphing button for the newly-selected season.
    // (populateEpisodeTable already repainted the rows disk-first.)
    refreshSeasonHeaderButton();
}

void StreamDetailView::populateEpisodeTable(int season)
{
    // Sorting is off by default (set in buildUI) — see the trailing note
    // on why. Clear rows before rebuild.
    m_episodeTable->setRowCount(0);

    // Reset the episode-number filter on (re)populate so a new season shows all
    // rows and the search box doesn't lie about what's visible.
    if (m_episodeSearch && !m_episodeSearch->text().isEmpty())
        m_episodeSearch->clear();

    auto episodes = m_seasons.value(season);
    QJsonObject allProgress = m_bridge->allProgress("stream");

    for (const auto& ep : episodes) {
        const int row = m_episodeTable->rowCount();
        m_episodeTable->insertRow(row);
        m_episodeTable->setRowHeight(row, 84);

        // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — col 0 checkbox cell. State synced
        // to m_selectedEpisodes via stateChanged slot below. Default unchecked
        // unless m_selectedEpisodes already holds this episode (i.e., the
        // season was just re-populated and selection survives).
        auto* cbHolder = new QWidget(m_episodeTable);
        auto* cbLayout = new QHBoxLayout(cbHolder);
        cbLayout->setContentsMargins(8, 0, 8, 0);
        cbLayout->setAlignment(Qt::AlignCenter);
        // STREAM_DOWNLOADS_NETFLIX_OVERHAUL 2026-05-12 post-smoke v2 —
        // QToolButton with swappable SVG icon replaces QCheckBox. Two
        // prior attempts (QSS border on indicator, QSS image: url on
        // indicator) both rendered as a partial "[" bracket because
        // Win11's native QStyle::PE_IndicatorCheckBox primitive clips
        // QCheckBox::indicator geometry to its own intrinsic size,
        // ignoring the QSS width/height directive. QToolButton bypasses
        // PE_IndicatorCheckBox entirely — Qt just draws the icon we
        // provide at the fixed size we set. Tankoban discipline:
        // grayscale, no color, no emoji.
        auto* checkbox = new QToolButton(cbHolder);
        checkbox->setCheckable(true);
        checkbox->setFixedSize(18, 18);
        checkbox->setIconSize(QSize(14, 14));
        checkbox->setAutoRaise(true);
        checkbox->setCursor(Qt::PointingHandCursor);
        checkbox->setStyleSheet(QStringLiteral(
            "QToolButton { border: none; background: transparent; padding: 0; }"
            "QToolButton:hover { background: rgba(255,255,255,0.06); border-radius: 2px; }"));
        const bool initiallyChecked = m_selectedEpisodes.contains(ep.episode);
        checkbox->setChecked(initiallyChecked);
        checkbox->setIcon(QIcon(initiallyChecked
            ? QStringLiteral(":/icons/checkbox-checked.svg")
            : QStringLiteral(":/icons/checkbox-empty.svg")));
        checkbox->setProperty("episodeNum", ep.episode);
        connect(checkbox, &QToolButton::toggled, this,
                [this, checkbox, episode = ep.episode](bool checked) {
                    checkbox->setIcon(QIcon(checked
                        ? QStringLiteral(":/icons/checkbox-checked.svg")
                        : QStringLiteral(":/icons/checkbox-empty.svg")));
                    if (checked) m_selectedEpisodes.insert(episode);
                    else         m_selectedEpisodes.remove(episode);
                    updateDownloadSelectedButton();
                });
        cbLayout->addWidget(checkbox);
        m_episodeTable->setCellWidget(row, kColCheckbox, cbHolder);

        // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — col 6 action-icon cell.
        // Initial glyph chosen by resolveRowState; updated in
        // refreshEpisodeMarkers + refreshEpisodeBulkProgress on subsequent
        // ticks (Task 12). Click routes through onActionIconClicked.
        auto* iconHolder = new QWidget(m_episodeTable);
        auto* iconLayout = new QHBoxLayout(iconHolder);
        iconLayout->setContentsMargins(4, 0, 4, 0);
        iconLayout->setAlignment(Qt::AlignCenter);
        auto* btn = new QPushButton(iconHolder);
        btn->setFlat(true);
        btn->setFixedSize(24, 24);
        btn->setIconSize(QSize(16, 16));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setProperty("episodeNum", ep.episode);
        connect(btn, &QPushButton::clicked, this, [this, btn, episode = ep.episode]() {
            onActionIconClicked(episode, btn->mapToGlobal(btn->rect().center()));
        });
        iconLayout->addWidget(btn);
        m_episodeTable->setCellWidget(row, kColAction, iconHolder);

        // Column 0 — episode number (also the carrier of UserRole data for
        // the Calendar preselect lookup + onEpisodeActivated dispatch).
        auto* numItem = new QTableWidgetItem(QString::number(ep.episode));
        numItem->setTextAlignment(Qt::AlignCenter);
        numItem->setData(Qt::UserRole, ep.episode);
        numItem->setData(Qt::UserRole + 1, season);
        m_episodeTable->setItem(row, kColEpisode, numItem);

        // Column 1 — thumbnail (102x58, ~1.6x of the prior 64x36). QLabel as
        // cell widget: disk-cache hit paints synchronously; miss kicks an async
        // download via NAM.
        auto* thumbHolder = new QWidget(m_episodeTable);
        auto* thumbLayout = new QHBoxLayout(thumbHolder);
        thumbLayout->setContentsMargins(6, 0, 6, 0);
        thumbLayout->setSpacing(0);
        auto* thumbLabel = new QLabel(thumbHolder);
        thumbLabel->setFixedSize(102, 58);
        thumbLabel->setAlignment(Qt::AlignCenter);
        thumbLabel->setStyleSheet(
            "QLabel { background: rgba(255,255,255,0.05);"
            "  border: 1px solid rgba(255,255,255,0.08); border-radius: 4px; }");
        thumbLayout->addWidget(thumbLabel);
        thumbLayout->addStretch();

        const QString cachedThumb = episodeThumbPath(m_currentImdb, season, ep.episode);
        if (QFile::exists(cachedThumb)) {
            applyEpisodeThumbnail(thumbLabel, cachedThumb);
        } else if (ep.thumbnail.isValid() && !ep.thumbnail.isEmpty()) {
            fetchEpisodeThumbnail(m_currentImdb, season, ep.episode,
                                  ep.thumbnail, thumbLabel);
        }
        m_episodeTable->setCellWidget(row, kColThumb, thumbHolder);

        // Column 2 — stacked title (primary) + overview (italic gray, 2-line
        // clamp). QLabel with Qt::TextWordWrap + fixed line count achieves
        // the clamp via maximumHeight.
        auto* titleCell = new QWidget(m_episodeTable);
        auto* titleLayout = new QVBoxLayout(titleCell);
        titleLayout->setContentsMargins(4, 6, 4, 6);
        titleLayout->setSpacing(2);

        auto* tLabel = new QLabel(ep.title, titleCell);
        tLabel->setWordWrap(false);
        tLabel->setStyleSheet(
            "color: #e0e0e0; font-size: 13px; font-weight: 500; background: transparent;");
        tLabel->setTextFormat(Qt::PlainText);
        titleLayout->addWidget(tLabel);

        if (!ep.overview.isEmpty()) {
            auto* oLabel = new QLabel(ep.overview, titleCell);
            oLabel->setWordWrap(true);
            oLabel->setStyleSheet(
                "color: rgba(255,255,255,0.45); font-size: 11px;"
                "  font-style: italic; background: transparent;");
            oLabel->setTextFormat(Qt::PlainText);
            // THEATRE_DOWNLOAD_SIMPLIFY P4 — synopsis bumped to 11px and the
            // clamp raised from 2 to 3 wrapped lines now that the taller (84px)
            // rows have the vertical room. QFontMetrics reads the (larger)
            // label font, so the 3-line height tracks the new size. Overflow is
            // silently clipped (no toggle; episode rows aren't interactive
            // beyond click-to-play).
            const QFontMetrics fm(oLabel->font());
            oLabel->setMaximumHeight(fm.lineSpacing() * 3);
            titleLayout->addWidget(oLabel);
        } else {
            titleLayout->addStretch();
        }
        m_episodeTable->setCellWidget(row, kColTitle, titleCell);

        // Column 3 — progress %.
        const QString epKey = StreamProgress::episodeKey(m_currentImdb, season, ep.episode);
        const QJsonObject state = allProgress.value(epKey).toObject();
        const double pct = StreamProgress::percent(state);
        const bool finished = StreamProgress::isFinished(state);

        auto* progItem = new QTableWidgetItem();
        progItem->setTextAlignment(Qt::AlignCenter);
        if (finished)
            progItem->setText("100%");
        else if (pct > 0)
            progItem->setText(QString::number(static_cast<int>(pct)) + "%");
        else
            progItem->setText("-");
        m_episodeTable->setItem(row, kColProgress, progItem);

        // Column 4 — status checkmark.
        auto* statusItem = new QTableWidgetItem();
        statusItem->setTextAlignment(Qt::AlignCenter);
        if (finished)
            statusItem->setText("\u2713");
        m_episodeTable->setItem(row, kColStatus, statusItem);
    }

    // Phase 3 Batch 3.4 — sorting disabled. Cell widgets in cols 1/2 (thumb,
    // stacked title+overview) don't carry sortable QTableWidgetItem data, so
    // a header-click sort would produce unpredictable order. Episodes are
    // already sorted numerically by parseSeriesEpisodes — the visible order
    // is the authoritative one and doesn't need runtime re-sort.
    m_episodeTable->setSortingEnabled(false);

    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL Task 13 — initial paint of the
    // season-header morphing button for the newly-populated season.
    refreshSeasonHeaderButton();

    // THEATRE_EPISODE_STATE_MODEL (2026-05-30) — authoritative disk-first paint
    // of every row's status text + action control. Replaces the legacy trio
    // (refreshEpisodeMarkers + the repaintActionIconForRow loop +
    // refreshSubstrateStatesForActiveSeason). Called LAST so disk wins.
    refreshAllEpisodeRows();

    m_episodeTable->show();
}

// ─── Play triggers ───────────────────────────────────────────────────────────

void StreamDetailView::onEpisodeActivated(int row, int /*col*/)
{
    auto* numItem = m_episodeTable->item(row, kColEpisode);
    if (!numItem) return;

    int episode = numItem->data(Qt::UserRole).toInt();
    int season  = numItem->data(Qt::UserRole + 1).toInt();

    // STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) — branch on disk
    // presence. If the file is on disk + still exists, fire local-file
    // play. Otherwise fall through to the source-pick flow. Spec §6.2.
    if (m_downloadIndex && !m_currentImdb.isEmpty() && season > 0 && episode > 0) {
        const auto pathOpt = m_downloadIndex->filePathFor(m_currentImdb, season, episode);
        if (pathOpt.has_value()) {
            if (QFileInfo::exists(*pathOpt)) {
                emit playLocalFileFromStreamRequested(
                    *pathOpt, m_currentImdb, currentTitle(), season, episode);
                return;  // skip source-pick
            } else {
                // Lazy-stat eviction: file was deleted manually since the
                // index was last refreshed. Evict + fall through to streams.
                m_downloadIndex->evictByPath(
                    StreamDownloadIndex::computeCanonicalKey(*pathOpt));
                if (m_statusLabel)
                    m_statusLabel->setText(tr("File missing — falling back to streams."));
            }
        }
    }

    // Stream-picker UX rework — single-click on an episode row now means
    // "load sources for this episode". Flip the right pane to its loading
    // state immediately so the user gets instant visual feedback without
    // waiting for StreamPage's aggregator round-trip.
    setStreamSourcesLoading();
    emit playRequested(m_currentImdb, "series", season, episode);
}

// ─── THEATRE_EPISODE_STATE_MODEL (2026-05-30) — disk-first episode state ──────
//
// One derivation feeds both the status cell and the action control. Disk is the
// single source of truth (a file on disk is Downloaded, full stop); the engine
// snapshot is consulted ONLY for not-on-disk episodes. Replaces the legacy
// cohort RowState cluster (deleted in P1.T5).

tankostream::stream::EpisodeDisplayState
StreamDetailView::episodeDisplayState(int season, int episode) const
{
    using tankostream::stream::EpisodeStateInputs;
    EpisodeStateInputs in;

    // Authoritative index entry: the real on-disk path + a completion hint. The
    // index `state` field is only a HINT — it is rebuilt each launch and can lag
    // reality (a finished torrent can leave a Pending/Downloading record behind,
    // e.g. a ghost re-dispatch stuck at 0% — the Invincible S4E01 bug). We take
    // onDisk + the path from it, but the ACTIVE-TRANSFER decision is corroborated
    // against the LIVE engine, never the cached state alone.
    if (m_downloadIndex && !m_currentImdb.isEmpty()) {
        const auto best = m_downloadIndex->bestEntryForEpisode(m_currentImdb, season, episode);
        if (best.has_value()) {
            in.onDisk      = QFileInfo::exists(best->canonicalPath);
            in.complete    = (best->state == StreamDownloadIndex::Entry::Complete);
            in.progressPct = best->progressPct;
            if (best->state == StreamDownloadIndex::Entry::Pending
             || best->state == StreamDownloadIndex::Entry::Downloading) {
                // Corroborate the cached in-progress state against the live torrent
                // backing this entry. Only a torrent genuinely still downloading
                // counts as an active transfer; a seeding/completed (or session-
                // absent) torrent means the cached state is stale, so we let the
                // on-disk file speak for itself (deriveEpisodeDisplayState treats a
                // bare on-disk file as Downloaded).
                const QString hash =
                    best->sourceGroupId.startsWith(QLatin1String("tankorent:"))
                        ? best->sourceGroupId.mid(QStringLiteral("tankorent:").size())
                        : QString();
                if (m_torrentClient) {
                    for (const auto& ti : m_torrentClient->listActive()) {
                        if (!hash.isEmpty()) {
                            if (ti.infoHash.compare(hash, Qt::CaseInsensitive) != 0)
                                continue;
                        } else if (ti.imdbId.compare(m_currentImdb,
                                                     Qt::CaseInsensitive) != 0) {
                            continue;
                        }
                        const QString s = ti.stateString;
                        if (s == QLatin1String("seeding")
                         || s == QLatin1String("completed")) {
                            in.complete = true;
                        } else if (ti.progress < 1.0f
                                && (s == QLatin1String("downloading")
                                 || s == QLatin1String("checking")
                                 || s == QLatin1String("metadata")
                                 || s == QLatin1String("allocating")
                                 || s == QLatin1String("paused"))) {
                            in.hasTransfer = true;
                        }
                    }
                }
            }
        }
    }
    // ALWAYS consult the live engine snapshot (it carries the Paused flag + the
    // live %, and an active transfer must beat a pre-allocated on-disk file).
    if (m_torrentClient && !m_currentImdb.isEmpty()) {
        const auto snap = m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, season);
        const auto it = snap.constFind(episode);
        if (it != snap.constEnd()) {
            const QString st = it.value().first;            // cohort state string
            const int pct    = qMax(0, it.value().second);  // 0..100 (-1 terminal -> 0)
            if (st == QLatin1String("Published") || st == QLatin1String("Completed")) {
                in.complete = true;                          // engine confirms terminal success
                in.onDisk   = true;                          // FIX: a Published season's files
                                                             // are on disk by definition — do
                                                             // not also demand a per-file index
                                                             // entry (One Piece: snapshot
                                                             // Published but index entry absent
                                                             // this launch → was NotDownloaded).
            } else if (st == QLatin1String("Paused")) {
                in.hasTransfer = true; in.paused = true; in.progressPct = pct;
            } else if (st == QLatin1String("Failed")
                    || st == QLatin1String("MetadataFailed")
                    || st == QLatin1String("PublishFailed")
                    || st == QLatin1String("MissingSource")) {
                in.hasTransfer = true; in.failed = true; in.progressPct = pct;
            } else if (st == QLatin1String("Downloading")
                    || st == QLatin1String("Pending")
                    || st == QLatin1String("Publishing")) {
                in.hasTransfer = true; in.progressPct = pct;
            }
            // Cancelled/Orphaned + other terminal-non-success: no active transfer
            // (falls through to the on-disk check).
        }
    }
    return tankostream::stream::deriveEpisodeDisplayState(in);
}

void StreamDetailView::refreshEpisodeRow(int row, int season, int episode)
{
    if (!m_episodeTable || row < 0 || row >= m_episodeTable->rowCount())
        return;
    using S = tankostream::stream::EpisodeDisplayState;
    const S state = episodeDisplayState(season, episode);

    // Progress % for in-progress states (cheap re-read of the in-process snapshot;
    // clamped so a terminal -1 never leaks into the label).
    int pct = 0;
    if ((state == S::Downloading || state == S::Paused)
        && m_torrentClient && !m_currentImdb.isEmpty()) {
        const auto snap = m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, season);
        const auto it = snap.constFind(episode);
        if (it != snap.constEnd())
            pct = qMax(0, it.value().second);
    }

    // --- Status text cell (kColStatus) ---
    if (auto* statusItem = m_episodeTable->item(row, kColStatus)) {
        QString text;
        switch (state) {
        // Just the percent — the action icon (pause vs resume) shows which it is,
        // and the Status column is too narrow for the word + percent.
        case S::Downloading:   text = QStringLiteral("%1%").arg(pct); break;
        case S::Paused:        text = QStringLiteral("%1%").arg(pct); break;
        case S::Failed:        text = tr("Failed");                   break;
        case S::Downloaded:    text = QString();                      break;  // Play button carries it
        case S::NotDownloaded: text = QString();                      break;
        }
        statusItem->setText(text);
        statusItem->setTextAlignment(Qt::AlignCenter);
    }

    // --- Action control cell (kColAction) ---
    auto* holder = m_episodeTable->cellWidget(row, kColAction);
    if (!holder) return;
    auto* btn = holder->findChild<QPushButton*>();
    if (!btn) return;

    btn->setProperty("episodeNum", episode);
    if (state == S::Downloaded) {
        // Play affordance: a text button. Relax the construction-time
        // setFixedSize(24,24) so the label fits (kColAction is widened to hold it).
        btn->setIcon(QIcon());
        btn->setText(tr("Play"));
        btn->setToolTip(tr("Play downloaded episode"));
        btn->setMinimumSize(44, 24);
        btn->setMaximumSize(QWIDGETSIZE_MAX, 24);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background: rgba(255,255,255,0.10);"
            "  border: 1px solid rgba(255,255,255,0.18); border-radius: 4px;"
            "  color: #e0e0e0; font-size: 11px; padding: 2px 8px; }"
            "QPushButton:hover { background: rgba(255,255,255,0.16); }"));
    } else {
        // Icon affordance: restore the compact fixed glyph button.
        btn->setText(QString());
        btn->setFixedSize(24, 24);
        btn->setStyleSheet(QStringLiteral("QPushButton { border: none; background: transparent; }"));
        QString icon, tip;
        switch (state) {
        case S::NotDownloaded: icon = QStringLiteral(":/icons/download-arrow.svg"); tip = tr("Download episode"); break;
        case S::Downloading:   icon = QStringLiteral(":/icons/pause-circle.svg");   tip = tr("Pause download");   break;
        case S::Paused:        icon = QStringLiteral(":/icons/play-circle.svg");    tip = tr("Resume download");  break;
        case S::Failed:        icon = QStringLiteral(":/icons/retry-arrow.svg");    tip = tr("Retry download");   break;
        case S::Downloaded:    break;  // handled above
        }
        btn->setIcon(QIcon(icon));
        btn->setToolTip(tip);
    }
}

void StreamDetailView::refreshAllEpisodeRows()
{
    if (!m_episodeTable) return;
    const int season = currentSeason();
    if (season <= 0) return;
    for (int row = 0; row < m_episodeTable->rowCount(); ++row) {
        auto* numItem = m_episodeTable->item(row, kColEpisode);
        if (!numItem) continue;
        const int episode = numItem->data(Qt::UserRole).toInt();
        if (episode > 0) refreshEpisodeRow(row, season, episode);
    }
}

void StreamDetailView::filterEpisodesByNumber(const QString& text)
{
    if (!m_episodeTable) return;
    const QString needle = text.trimmed();
    int firstVisible = -1;
    for (int row = 0; row < m_episodeTable->rowCount(); ++row) {
        bool show = true;
        if (!needle.isEmpty()) {
            const auto* numItem = m_episodeTable->item(row, kColEpisode);
            const QString epStr = numItem ? numItem->text() : QString();
            // Prefix match so typing narrows toward the target
            // ("116" -> 116, 1160-1169, 1164; "1164" -> just 1164).
            show = epStr.startsWith(needle);
        }
        m_episodeTable->setRowHidden(row, !show);
        if (show && firstVisible < 0) firstVisible = row;
    }
    if (firstVisible >= 0) {
        if (auto* it = m_episodeTable->item(firstVisible, kColEpisode))
            m_episodeTable->scrollToItem(it, QAbstractItemView::PositionAtTop);
    }
}

// STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) — wire the download index
// in. We connect to entriesChanged so a bulk-completion (Phase 2) in another
// pane lights up the rows here in place. QueuedConnection because the index
// may emit from a worker thread (validateAll runs via QtConcurrent on home
// open per spec §10.4).
void StreamDetailView::setStreamDownloadIndex(StreamDownloadIndex* idx)
{
    if (m_downloadIndex == idx) return;
    if (m_downloadIndex) {
        // This is the single wiring site for index->this connections.
        disconnect(m_downloadIndex, nullptr, this, nullptr);
    }
    m_downloadIndex = idx;
    if (m_downloadIndex) {
        // THEATRE_EPISODE_STATE_MODEL (2026-05-30) — any index change re-derives
        // the visible rows disk-first + refreshes the movie chips. QueuedConnection:
        // the index may emit from a worker thread (validateAll runs via
        // QtConcurrent on home open, spec §10.4).
        connect(m_downloadIndex, &StreamDownloadIndex::entriesChanged, this,
                [this]() {
                    refreshAllEpisodeRows();
                    refreshMovieLocalChip();
                    refreshMovieDownloadState();
                },
                Qt::QueuedConnection);
        connect(m_downloadIndex, &StreamDownloadIndex::entryStateChanged, this,
                [this](const QString& imdbId, int season, int episode) {
                    if (imdbId != m_currentImdb) return;
                    if (season == 0 && episode == 0) { refreshMovieDownloadState(); return; }
                    if (season == currentSeason()) refreshAllEpisodeRows();
                },
                Qt::QueuedConnection);
        // Repaint immediately if the table already has rows from a prior
        // showEntry call (the wiring may land late-in-MainWindow ctor).
        refreshAllEpisodeRows();
        refreshMovieLocalChip();
        refreshMovieDownloadState();
    }
    if (!m_downloadIndex) {
        refreshMovieLocalChip();
        refreshMovieDownloadState();
    }
}

// THEATRE_EPISODE_STATE_MODEL (2026-05-30) — disk-first right-click menu on the
// episode table. In-progress rows offer "Cancel download"; downloaded rows offer
// "Delete from disk" (destructive, confirmed) + "Show in folder". Menu items are
// driven by the single derived state — no cohort/substrate reads.
void StreamDetailView::onEpisodeContextMenu(const QPoint& pos)
{
    if (!m_episodeTable) return;
    const QModelIndex idx = m_episodeTable->indexAt(pos);
    if (!idx.isValid()) return;
    const int row = idx.row();
    QTableWidgetItem* numItem = m_episodeTable->item(row, kColEpisode);
    if (!numItem) return;
    const int episode = numItem->data(Qt::UserRole).toInt();
    const int season  = numItem->data(Qt::UserRole + 1).toInt();
    if (episode <= 0 || season <= 0 || m_currentImdb.isEmpty()) return;

    using S = tankostream::stream::EpisodeDisplayState;
    const S state = episodeDisplayState(season, episode);

    QMenu menu(this);
    QAction* cancelAct = nullptr;
    QAction* deleteAct = nullptr;
    QAction* showAct   = nullptr;
    if (state == S::Downloading || state == S::Paused || state == S::Failed)
        cancelAct = menu.addAction(tr("Cancel download"));
    if (state == S::Downloaded) {
        deleteAct = menu.addAction(tr("Delete from disk"));
        showAct   = menu.addAction(tr("Show in folder"));
    }
    if (menu.isEmpty()) return;

    QAction* chosen = menu.exec(m_episodeTable->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    // Authoritative on-disk path (duplicate-safe), resolved once.
    const auto best = m_downloadIndex
        ? m_downloadIndex->bestEntryForEpisode(m_currentImdb, season, episode)
        : std::nullopt;

    if (chosen == cancelAct) {
        const QString hash = findInfoHashForEpisode(season, episode);
        if (!hash.isEmpty() && m_torrentClient)
            m_torrentClient->deleteTorrent(hash, /*deleteFiles=*/true);
        // Disk-first: drop the record (+ any pre-allocated partial via the engine
        // above) so the row returns to NotDownloaded immediately, not on the next
        // async tick. Also covers an orphaned record with no live torrent.
        if (best.has_value() && m_downloadIndex)
            m_downloadIndex->evictByPath(
                StreamDownloadIndex::computeCanonicalKey(best->canonicalPath));
    } else if (chosen == deleteAct) {
        if (!best.has_value()) return;
        const QString path = best->canonicalPath;
        const auto reply = QMessageBox::warning(
            this, tr("Delete from disk"),
            tr("Permanently delete this episode's file from disk?\n\n%1").arg(path),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
        QFile::remove(path);
        if (m_downloadIndex)
            m_downloadIndex->evictByPath(StreamDownloadIndex::computeCanonicalKey(path));
    } else if (chosen == showAct) {
        if (best.has_value())
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(QFileInfo(best->canonicalPath).absolutePath()));
    }
    refreshEpisodeRow(row, season, episode);
}

void StreamDetailView::refreshMovieLocalChip()
{
    if (!m_movieActionRow || !m_movieLocalChip)
        return;

    const bool isMovie = m_currentType == QLatin1String("movie")
        && !m_currentImdb.isEmpty();
    m_movieActionRow->setVisible(isMovie);
    if (!isMovie) {
        m_movieLocalChip->hide();
        return;
    }

    const bool hasLocal = m_downloadIndex
        && m_downloadIndex->filePathForMovie(m_currentImdb).has_value();
    m_movieLocalChip->setVisible(hasLocal);
}

void StreamDetailView::refreshMovieDownloadState()
{
    if (!m_movieDownloadBtn || !m_movieDownloadChip)
        return;

    const bool isMovie = m_currentType == QLatin1String("movie")
        && !m_currentImdb.isEmpty();
    if (!isMovie) {
        m_movieDownloadChip->hide();
        m_movieDownloadBtn->setText(tr("Download"));
        return;
    }

    // Layer 1: existing addon-bulk snapshot (Codex Trigger D #5, 2026-05-18).
    const QPair<QString, int> snapshot = m_torrentClient
        ? m_torrentClient->streamMovieDownloadSnapshot(m_currentImdb)
        : QPair<QString, int>();

    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 Task 16 — Layer 2: consult
    // StreamDownloadIndex for the Tankorent-side movie entry. Index Pending/
    // Downloading/Failed take precedence over snapshot for visible chip text;
    // Index Complete falls through to snapshot if snapshot is active.
    auto indexState = StreamDownloadIndex::Entry::Complete;
    int indexPct = 100;
    bool indexHasEntry = false;
    if (m_downloadIndex) {
        const auto entries = m_downloadIndex->entriesForImdb(m_currentImdb);
        for (const auto& e : entries) {
            if (e.type == QStringLiteral("movie")) {
                indexState = e.state;
                indexPct = e.progressPct;
                indexHasEntry = true;
                break;
            }
        }
    }
    // Provenance read for amber-tint application — deferred to Task 20.

    if (indexHasEntry && indexState == StreamDownloadIndex::Entry::Pending) {
        m_movieDownloadChip->setText(QStringLiteral("QUEUED"));
        m_movieDownloadChip->show();
        m_movieDownloadBtn->setText(tr("Queued"));
        m_movieDownloadBtn->setEnabled(false);
        return;
    }
    if (indexHasEntry && indexState == StreamDownloadIndex::Entry::Downloading) {
        m_movieDownloadChip->setText(QStringLiteral("DOWNLOADING"));
        m_movieDownloadChip->show();
        m_movieDownloadBtn->setText(tr("Downloading %1%").arg(indexPct));
        m_movieDownloadBtn->setEnabled(false);
        return;
    }
    if (indexHasEntry && indexState == StreamDownloadIndex::Entry::Failed) {
        m_movieDownloadChip->setText(QStringLiteral("FAILED"));
        m_movieDownloadChip->show();
        m_movieDownloadBtn->setText(tr("Retry"));
        m_movieDownloadBtn->setEnabled(true);
        return;
    }

    // Fall through: index says Complete or no entry. Snapshot path handles
    // the addon-bulk live-progress case.
    if (!snapshot.first.isEmpty()) {
        // PHASE3_CHIP_VISIBILITY_FIX 2026-05-19 F8 — always show a numeric
        // percent (even 0%) instead of suppressing the suffix. A stuck
        // download (e.g. high-seeder torrent that never attached) reads as
        // 0% indefinitely; suppressing the number hides that fact. Showing
        // "0%" surfaces the stuck state immediately to the user.
        m_movieDownloadChip->setText(QStringLiteral("DOWNLOADING"));
        m_movieDownloadChip->show();
        m_movieDownloadBtn->setText(tr("Downloading %1%").arg(snapshot.second));
        m_movieDownloadBtn->setEnabled(false);
        return;
    }

    // Index Complete + snapshot empty → explicit "DOWNLOADED" badge.
    if (indexHasEntry && indexState == StreamDownloadIndex::Entry::Complete) {
        m_movieDownloadChip->setText(QStringLiteral("DOWNLOADED"));
        m_movieDownloadChip->show();
    } else if (m_torrentClient
               && m_torrentClient->streamMovieIsLegacyNoMagnet(m_currentImdb)) {
        // TORRENT_PERSISTENCE_COLLAPSE Phase 4.2 (2026-05-20) — a movie row
        // for this imdb survived the Phase 1.6 migration without a magnetUri
        // (audit D10). Cannot be auto-resumed. Surface the recovery state
        // via the chip; the existing Download button already creates a
        // fresh download from m_lastChoices when the user clicks it, which
        // is exactly the re-add semantics here. No button-handler change
        // required.
        m_movieDownloadChip->setText(QStringLiteral("NEEDS RE-ADD"));
        m_movieDownloadChip->show();
    } else {
        m_movieDownloadChip->hide();
    }
    m_movieDownloadBtn->setText(tr("Download"));

    bool hasMagnet = false;
    for (const auto& choice : m_lastChoices) {
        if (choice.sourceKind == QLatin1String("magnet")) {
            hasMagnet = true;
            break;
        }
    }
    m_movieDownloadBtn->setEnabled(hasMagnet);
}

void StreamDetailView::updateProgressColumn()
{
    if (m_currentType != "series" || m_seasonCombo->count() == 0)
        return;

    int season = m_seasonCombo->currentData().toInt();
    populateEpisodeTable(season);
}

// ─── STREAM_DOWNLOADS_NETFLIX_OVERHAUL — action-icon helpers + slot ──────────

void StreamDetailView::setTorrentClient(TorrentClient* client)
{
    m_torrentClient = client;

    // Repaint immediately on every cohort mutation + torrent lifecycle event
    // rather than waiting up to 1s for the next poll tick.
    if (m_torrentClient) {
        connect(m_torrentClient, &TorrentClient::streamBulkGroupsChanged,
                this, [this](const QString& /*groupId*/) {
                    refreshAllEpisodeRows();
                    refreshMovieDownloadState();
                }, Qt::QueuedConnection);
        connect(m_torrentClient, &TorrentClient::torrentAdded,
                this, [this](const QString& /*infoHash*/) {
                    refreshMovieDownloadState();
                }, Qt::QueuedConnection);
        connect(m_torrentClient, &TorrentClient::torrentUpdated,
                this, [this](const QString& /*infoHash*/) {
                    refreshMovieDownloadState();
                }, Qt::QueuedConnection);
        connect(m_torrentClient, &TorrentClient::torrentRemoved,
                this, [this](const QString& /*infoHash*/) {
                    refreshMovieDownloadState();
                }, Qt::QueuedConnection);
        connect(m_torrentClient, &TorrentClient::torrentCompleted,
                this, [this](const QString& /*infoHash*/) {
                    // F13 fix 2026-05-19: immediate refresh on completion —
                    // don't wait up to 1s for the next timer tick to flip the
                    // badge from 'Downloading 99%' to 'Downloaded'.
                    refreshMovieDownloadState();
                    refreshAllEpisodeRows();
                }, Qt::QueuedConnection);
    }
    refreshMovieDownloadState();
}

QString StreamDetailView::findInfoHashForEpisode(int season, int episode) const
{
    if (!m_torrentClient || m_currentImdb.isEmpty())
        return {};
    const QString prefix =
        QStringLiteral("stream:") + m_currentImdb + QLatin1Char(':');
    const QJsonObject groups = m_torrentClient->streamBulkGroupsSnapshot();
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        if (!it.key().startsWith(prefix)) continue;
        const QJsonObject g = it.value().toObject();
        if (g.value(QStringLiteral("sourceIds")).toObject()
              .value(QStringLiteral("season")).toInt(-1) != season) {
            continue;
        }
        for (const auto& v : g.value(QStringLiteral("items")).toArray()) {
            const QJsonObject item = v.toObject();
            const QString itemKey = item.value(QStringLiteral("itemKey")).toString();
            const int eIdx = itemKey.lastIndexOf(QLatin1Char('E'));
            if (eIdx <= 0) continue;
            if (itemKey.mid(eIdx + 1).toInt() != episode) continue;
            return item.value(QStringLiteral("infoHash")).toString();
        }
    }
    return {};
}

QString StreamDetailView::findGroupIdForCohort(int season) const
{
    if (!m_torrentClient || m_currentImdb.isEmpty())
        return {};
    const QString prefix =
        QStringLiteral("stream:") + m_currentImdb + QLatin1Char(':');
    const QJsonObject groups = m_torrentClient->streamBulkGroupsSnapshot();
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        if (!it.key().startsWith(prefix)) continue;
        const QJsonObject g = it.value().toObject();
        if (g.value(QStringLiteral("sourceIds")).toObject()
              .value(QStringLiteral("season")).toInt(-1) != season) {
            continue;
        }
        return it.key();
    }
    return {};
}

int StreamDetailView::rowForEpisode(int episode) const
{
    if (!m_episodeTable) return -1;
    for (int row = 0; row < m_episodeTable->rowCount(); ++row) {
        auto* numItem = m_episodeTable->item(row, kColEpisode);
        if (numItem && numItem->data(Qt::UserRole).toInt() == episode)
            return row;
    }
    return -1;
}

// THEATRE_EPISODE_STATE_MODEL (2026-05-30) — action-control click routed by the
// single disk-first derived state. globalAnchorPos is unused now (the old
// Published actions-menu moved entirely to the right-click context menu, P1.T4).
void StreamDetailView::onActionIconClicked(int episode, const QPoint& /*globalAnchorPos*/)
{
    const int season = currentSeason();
    if (season <= 0 || m_currentImdb.isEmpty() || episode <= 0) return;

    using S = tankostream::stream::EpisodeDisplayState;
    switch (episodeDisplayState(season, episode)) {
    case S::Downloaded:
        // Play from disk — same path as a row click (re-checks disk + evicts
        // a vanished file before falling back to streams).
        onEpisodeActivated(rowForEpisode(episode), 0);
        break;
    case S::NotDownloaded:
        emit singleEpisodeDownloadRequested(season, episode);
        break;
    case S::Downloading: {
        const QString hash = findInfoHashForEpisode(season, episode);
        if (!hash.isEmpty() && m_torrentClient) {
            m_torrentClient->pauseTorrent(hash);
            // Keep the cohort snapshot's Paused flag in sync — episodeDisplayState
            // reads it to derive the Paused state.
            m_torrentClient->setStreamBulkItemPaused(hash, /*paused=*/true);
        }
        break;
    }
    case S::Paused: {
        const QString hash = findInfoHashForEpisode(season, episode);
        if (!hash.isEmpty() && m_torrentClient) {
            m_torrentClient->resumeTorrent(hash);
            m_torrentClient->setStreamBulkItemPaused(hash, /*paused=*/false);
        }
        break;
    }
    case S::Failed:
        // Retry = fresh re-dispatch (the simplified disk-first model drops the
        // cohort-internal retry machinery; Phase 2 handles source-tiering).
        emit singleEpisodeDownloadRequested(season, episode);
        break;
    }
    refreshEpisodeRow(rowForEpisode(episode), season, episode);
}

// ─── STREAM_DOWNLOADS_NETFLIX_OVERHAUL Task 13 — season-header morphing slots ─

void StreamDetailView::onDownloadSeasonClicked()
{
    if (m_currentImdb.isEmpty() || m_currentType != QLatin1String("series"))
        return;
    int season = m_seasonCombo ? m_seasonCombo->currentData().toInt() : 0;
    if (season <= 0) return;

    if (!m_torrentClient) {
        emit seasonDownloadRequested(season);
        return;
    }
    const auto snap = m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, season);
    bool anyActive = false;
    bool allPaused = !snap.isEmpty();
    for (auto it = snap.cbegin(); it != snap.cend(); ++it) {
        const QString& s = it->first;
        if (!isTerminalCohortState(s)) {
            anyActive = true;
            if (s != QLatin1String("Paused")) allPaused = false;
        } else {
            allPaused = false;
        }
    }
    const QString groupId = findGroupIdForCohort(season);

    if (allPaused && !groupId.isEmpty()) {
        // All non-terminal items are Paused → Continue Season.
        for (auto it = snap.cbegin(); it != snap.cend(); ++it) {
            if (it->first != QLatin1String("Paused")) continue;
            const QString ih = findInfoHashForEpisode(season, it.key());
            if (ih.isEmpty()) continue;
            m_torrentClient->resumeTorrent(ih);
            m_torrentClient->setStreamBulkItemPaused(ih, /*paused=*/false);
        }
    } else if (anyActive && !groupId.isEmpty()) {
        // Some items still actively Downloading → Pause Season.
        for (auto it = snap.cbegin(); it != snap.cend(); ++it) {
            if (it->first != QLatin1String("Downloading")) continue;
            const QString ih = findInfoHashForEpisode(season, it.key());
            if (ih.isEmpty()) continue;
            m_torrentClient->pauseTorrent(ih);
            m_torrentClient->setStreamBulkItemPaused(ih, /*paused=*/true);
        }
    } else {
        // Idle (no cohort, or all terminal) → Download Season.
        emit seasonDownloadRequested(season);
    }
}

void StreamDetailView::onDownloadSelectedClicked()
{
    if (m_currentImdb.isEmpty()) return;
    int season = m_seasonCombo ? m_seasonCombo->currentData().toInt() : 0;
    if (season <= 0) return;
    QList<int> eps(m_selectedEpisodes.cbegin(), m_selectedEpisodes.cend());
    std::sort(eps.begin(), eps.end());
    if (eps.isEmpty()) return;
    emit selectedEpisodesDownloadRequested(season, eps);
    m_selectedEpisodes.clear();
    updateDownloadSelectedButton();
    // Uncheck all checkboxes in the current table (QToolButton from
    // post-smoke v2 — see populateEpisodeTable for rationale).
    for (int row = 0; row < m_episodeTable->rowCount(); ++row) {
        auto* holder = qobject_cast<QWidget*>(m_episodeTable->cellWidget(row, kColCheckbox));
        if (!holder) continue;
        auto* cb = holder->findChild<QToolButton*>();
        if (cb) cb->setChecked(false);
    }
}


void StreamDetailView::onSeasonHeaderRightClick(const QPoint& pos)
{
    int season = m_seasonCombo ? m_seasonCombo->currentData().toInt() : 0;
    if (season <= 0 || m_currentImdb.isEmpty()) return;
    QWidget* origin = qobject_cast<QWidget*>(sender());
    if (!origin) return;

    QMenu menu(this);
    QAction* cancelAct = menu.addAction(tr("Cancel Season"));
    QAction* chosen = menu.exec(origin->mapToGlobal(pos));
    if (chosen == cancelAct) {
        const QString groupId = findGroupIdForCohort(season);
        if (groupId.isEmpty()) return;
        const auto reply = QMessageBox::question(this, tr("Cancel Season?"),
            tr("Cancel and delete all files for this season? "
               "This cannot be undone."),
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes && m_torrentClient) {
            m_torrentClient->cancelStreamBulkGroup(groupId, /*deleteFiles=*/true);
        }
    }
}

void StreamDetailView::refreshSeasonHeaderButton()
{
    if (!m_downloadBtn) return;
    if (m_currentImdb.isEmpty() || m_currentType != QLatin1String("series")) {
        m_downloadBtn->setVisible(false);
        return;
    }
    int season = m_seasonCombo ? m_seasonCombo->currentData().toInt() : 0;
    m_downloadBtn->setText(tr("Download"));
    m_downloadBtn->setIcon(QIcon(QStringLiteral(":/icons/download-arrow.svg")));
    m_downloadBtn->setVisible(season > 0);
}

// ─── F13 fix 2026-05-19: visibility-scoped progress refresh timer ────────────

void StreamDetailView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_progressRefreshTimer && !m_progressRefreshTimer->isActive())
        m_progressRefreshTimer->start();
}

void StreamDetailView::hideEvent(QHideEvent* event)
{
    if (m_progressRefreshTimer && m_progressRefreshTimer->isActive())
        m_progressRefreshTimer->stop();
    QWidget::hideEvent(event);
}

// ─── Library toggle (Phase 1 Batch 1.2) ─────────────────────────────────────

void StreamDetailView::refreshLibraryButton()
{
    if (!m_libraryBtn) return;

    if (m_currentImdb.isEmpty() || !m_library) {
        m_libraryBtn->setVisible(false);
        return;
    }
    m_libraryBtn->setVisible(true);

    const bool inLibrary = m_library->has(m_currentImdb);
    m_libraryBtn->setText(inLibrary ? tr("Remove from Library")
                                    : tr("Add to Library"));
    // Drives the `#DetailLibraryBtn[inLibrary="true"]` selector for the
    // muted visual of the already-in-library state.
    m_libraryBtn->setProperty("inLibrary", inLibrary);
    m_libraryBtn->style()->unpolish(m_libraryBtn);
    m_libraryBtn->style()->polish(m_libraryBtn);
}

// STREAM_CONTINUE_LIBRARY_AND_HUD_AUTOFIRE 2026-05-06 — Bug 2 entry point.
// Called from StreamPage's progressUpdated lambda on the FIRST successful
// save in a session (gated by m_session.autoLibraryAdded). Mirrors the
// add-side of onLibraryButtonClicked but is strictly add-only — never
// toggles off an existing entry. Idempotent: m_library->has(imdbId)
// short-circuits when the user has already pinned this show manually.
void StreamDetailView::autoAddToLibrary()
{
    if (m_currentImdb.isEmpty() || !m_library) return;
    if (m_library->has(m_currentImdb)) return;
    if (!m_lastPreviewHint.has_value()) return;

    const auto& p = *m_lastPreviewHint;
    StreamLibraryEntry entry;
    entry.imdb        = p.id;
    entry.type        = p.type;
    entry.name        = p.name;
    entry.year        = p.releaseInfo;
    entry.poster      = p.poster.toString();
    entry.description = p.description;
    entry.imdbRating  = p.imdbRating;
    m_library->add(entry);
    // StreamLibrary::add emits libraryChanged → refreshLibraryButton runs
    // via the connection wired in buildUI; if the user is currently on
    // the detail view of this show, the button will flip from "Add to
    // Library" → "Remove from Library" reactively.
}

void StreamDetailView::onLibraryButtonClicked()
{
    if (m_currentImdb.isEmpty() || !m_library) return;

    if (m_library->has(m_currentImdb)) {
        // STREAM_DOWNLOADED_LIBRARY Phase 7 (2026-05-10) — bulk-in-flight
        // confirmation. If a stream-bulk for this show is still downloading,
        // require explicit user consent to cancel-and-remove atomically.
        // Spec §10.10. When TorrentClient isn't wired (defensive), fall
        // through to the unguarded Remove path (preserves prior behavior).
        if (m_torrentClient
            && m_torrentClient->hasActiveStreamBulkGroupsForImdb(m_currentImdb)) {
            // STREAM_DOWNLOADS_NETFLIX_OVERHAUL 2026-05-12 Phase 7 — dialog
            // text updated to surface the file-deletion side effect (spec
            // §9.3 + §P8 cancel-deletes-files). The actual delete-with-
            // cancel happens both here (visible-to-the-user immediate
            // path) AND inside StreamLibrary::remove (engine-level
            // belt-and-suspenders for any caller that bypasses this UI).
            QMessageBox box(this);
            box.setWindowTitle(tr("Cancel active bulk download?"));
            box.setText(tr("This show has an active bulk download in progress.\n"
                           "Cancel the download and delete partial files, then\n"
                           "Remove from Library? This cannot be undone."));
            QPushButton* doIt = box.addButton(tr("Cancel + Remove"),
                                              QMessageBox::DestructiveRole);
            QPushButton* abort = box.addButton(tr("Keep downloading"),
                                                QMessageBox::RejectRole);
            box.setDefaultButton(abort);
            box.exec();
            if (box.clickedButton() != doIt)
                return;  // user kept downloading; no Remove

            // Cancel all active bulk groups for this imdb WITH file-delete.
            // Atomic-ish: iterate the snapshot of group IDs taken just
            // before the dialog fired; any group that completes between
            // dialog-open and click is no longer "contains"-d in
            // m_streamBulkGroups and cancelStreamBulkGroup short-circuits.
            const QStringList ids =
                m_torrentClient->streamBulkGroupIdsForImdb(m_currentImdb);
            for (const QString& gid : ids)
                m_torrentClient->cancelStreamBulkGroup(gid, /*deleteFiles=*/true);
        }

        m_library->remove(m_currentImdb);
    } else if (m_lastPreviewHint.has_value()) {
        // showEntry always stashes a preview — either the hint from the
        // catalog/home/search path, or a preview reconstructed from the
        // library entry on the library-path. No metadata loss on re-add.
        const auto& p = *m_lastPreviewHint;
        StreamLibraryEntry entry;
        entry.imdb        = p.id;
        entry.type        = p.type;
        entry.name        = p.name;
        entry.year        = p.releaseInfo;
        entry.poster      = p.poster.toString();
        entry.description = p.description;
        entry.imdbRating  = p.imdbRating;
        m_library->add(entry);
    }
    // StreamLibrary::add/remove emits libraryChanged → refreshLibraryButton
    // runs via the connection wired in buildUI. Still call explicitly in
    // case a caller invokes the slot directly (e.g. a keyboard shortcut).
    refreshLibraryButton();
}

// ─── Phase 3 Batch 3.1 — MetaItem hero + chip enrichment ─────────────────────

void StreamDetailView::onMetaItemReady(const MetaItem& item)
{
    // Stale-callback guard: fetchMetaItem is one-shot per imdbId but replays
    // cached hits on each showEntry, so a previous title's cached emit can
    // arrive after navigation. Ignore anything that doesn't match the
    // currently-displayed title.
    if (item.preview.id != m_currentImdb) return;

    const auto& p = item.preview;

    // Stash the richer preview so later Add-to-Library calls capture
    // background / runtime / genres (Phase 1 Batch 1.2's entry construction
    // only had the basic fields).
    m_lastPreviewHint = p;

    // Hero paint: background preferred, poster fallback. Only re-apply if
    // the preview-hint paint didn't already cover it (cheap check: if the
    // hero is currently hidden, we haven't painted yet).
    if (p.background.isValid() || p.poster.isValid()) {
        if (!m_heroLabel->isVisible()
            || m_heroLabel->property("bgSource").toString() != p.background.toString())
        {
            applyHeroImage(p.id, p.background, p.poster);
            m_heroLabel->setProperty("bgSource", p.background.toString());
        }
    }

    // Chip enrichment: layer runtime + genres on top of year/rating/type
    // that showEntry already populated from the partial preview.
    const QString year   = !p.releaseInfo.isEmpty() ? p.releaseInfo : QString();
    const QString rating = p.imdbRating;
    const QString type   = p.type;
    applyChips(year, p.runtime, p.genres, rating, type);

    // Movie-action-row resilience (2026-05-18 - Smoke Test 3a fix per Task 1
    // of docs/superpowers/plans/2026-05-18-three-small-fixes.md). showEntry's
    // type-resolution at line 209-238 falls back to m_library->get(imdbId)
    // when previewHint is absent OR carries an empty type - for a movie that
    // is NOT yet in the library, that fallback returns an empty entry, so
    // m_currentType ends up empty and the `if (displayType == "movie")`
    // branch (line 289) never fires -> m_movieActionRow stays hidden and the
    // Download button is invisible. Backfill from the full MetaItem here:
    // when we now learn the type is "movie" and the movie-action-row hasn't
    // been shown yet, promote into the movie branch behavior - show the row,
    // refresh the LOCAL chip, and kick the stream-source load so the auto-
    // Download / source list lands the same as the in-library path.
    if (!type.isEmpty() && type != m_currentType) {
        m_currentType = type;
        if (type == QLatin1String("movie")) {
            m_statusLabel->hide();
            if (m_movieActionRow) m_movieActionRow->show();
            refreshMovieLocalChip();
            refreshMovieDownloadState();
            if (m_lastChoices.isEmpty()) {
                setStreamSourcesLoading();
                emit playRequested(m_currentImdb, QStringLiteral("movie"), 0, 0);
            }
        }
    }

    // Phase 3 Batch 3.3 — description may arrive richer via the full-meta
    // fetch than what the partial preview carried. Overwrite only if the
    // new one is non-empty and differs; otherwise keep the first-paint text.
    if (!p.description.isEmpty() && p.description != m_descLabel->text()) {
        setDescription(p.description);
    }

    // Phase 3 Batch 3.2 (deferred ship) — director + cast.
    applyCastDirector(p.links);

    // Phase 3 Batch 3.5 (deferred ship) — trailer button.
    applyTrailerButton(p.trailerStreams);
}

QString StreamDetailView::heroCachePath(const QString& imdbId) const
{
    return m_heroCacheDir + QStringLiteral("/") + imdbId + QStringLiteral(".jpg");
}

void StreamDetailView::clearHero()
{
    if (!m_heroLabel) return;
    m_heroLabel->clear();
    m_heroLabel->hide();
    m_heroLabel->setProperty("bgSource", QVariant());
}

void StreamDetailView::applyHeroImage(const QString& imdbId,
                                       const QUrl& backgroundUrl,
                                       const QUrl& posterFallbackUrl)
{
    // 1. Disk cache hit (background previously downloaded) → paint now.
    const QString cachePath = heroCachePath(imdbId);
    if (QFile::exists(cachePath)) {
        renderHeroPixmap(cachePath);
        return;
    }

    // 2. Background URL available → kick off async download; on completion
    //    paint from disk. Poster fallback runs on failure.
    if (backgroundUrl.isValid() && !backgroundUrl.isEmpty()) {
        downloadBackgroundArt(imdbId, backgroundUrl, /*usePosterFallback*/ true);
        // While the download runs, paint the poster (if any) as an interim
        // hero so the layout doesn't collapse. Later renderHeroPixmap call
        // replaces it.
        if (posterFallbackUrl.isValid()) {
            const QString posterCache =
                QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                + QStringLiteral("/Tankoban/data/stream_posters/") + imdbId
                + QStringLiteral(".jpg");
            if (QFile::exists(posterCache)) renderHeroPixmap(posterCache);
        }
        return;
    }

    // 3. No background URL — use poster (disk-cached by StreamLibraryLayout
    //    / StreamSearchWidget). If poster isn't on disk either, hero stays
    //    hidden; absent is better than broken.
    if (posterFallbackUrl.isValid()) {
        const QString posterCache =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + QStringLiteral("/Tankoban/data/stream_posters/") + imdbId
            + QStringLiteral(".jpg");
        if (QFile::exists(posterCache)) {
            renderHeroPixmap(posterCache);
        }
    }
}

void StreamDetailView::downloadBackgroundArt(const QString& imdbId,
                                              const QUrl& url,
                                              bool usePosterFallback)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
    req.setTransferTimeout(10000);

    auto* reply = m_nam->get(req);
    QPointer<StreamDetailView> guard(this);
    connect(reply, &QNetworkReply::finished, this,
        [this, reply, imdbId, guard, usePosterFallback]() {
            reply->deleteLater();
            if (!guard) return;
            if (reply->error() != QNetworkReply::NoError) return;
            const QByteArray data = reply->readAll();
            if (data.isEmpty()) return;

            const QString path = heroCachePath(imdbId);
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly)) return;
            file.write(data);
            file.close();

            // Only paint if the user is still on this title. Stale arrival
            // after navigation → file is cached for next open, no UI update.
            if (imdbId == m_currentImdb) {
                renderHeroPixmap(path);
            }
            Q_UNUSED(usePosterFallback);
        });
}

void StreamDetailView::renderHeroPixmap(const QString& imagePath)
{
    if (!m_heroLabel) return;

    QImage src(imagePath);
    if (src.isNull()) return;

    // Target size: full current view width × the hero's configured height
    // (140px post-2026-04-15 layout rebalance; pre-change was 240px).
    // width() may be 0 before first show; fall back to a reasonable
    // default the layout can stretch from.
    const int targetW = qMax(m_heroLabel->width(), 800);
    const int targetH = m_heroLabel->height() > 0 ? m_heroLabel->height() : 140;

    // Scale to fill, crop-center horizontally to avoid distortion.
    QImage scaled = src.scaled(targetW, targetH,
                               Qt::KeepAspectRatioByExpanding,
                               Qt::SmoothTransformation);
    QImage canvas(targetW, targetH, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    const int sx = (scaled.width()  - targetW) / 2;
    const int sy = (scaled.height() - targetH) / 2;
    p.drawImage(QPoint(0, 0), scaled, QRect(sx, sy, targetW, targetH));

    // Dark-gradient overlay: transparent at top, ~85% black at bottom so
    // the title + chip row below read cleanly against the fade. Baked into
    // the pixmap (not a QGraphicsEffect) for zero extra paint cost per frame.
    QLinearGradient grad(0, 0, 0, targetH);
    grad.setColorAt(0.0,  QColor(0, 0, 0,   0));
    grad.setColorAt(0.55, QColor(0, 0, 0, 110));
    grad.setColorAt(1.0,  QColor(0, 0, 0, 220));
    p.fillRect(canvas.rect(), grad);
    p.end();

    m_heroLabel->setPixmap(QPixmap::fromImage(canvas));
    m_heroLabel->show();
}

void StreamDetailView::applyChips(const QString& year,
                                   const QString& runtime,
                                   const QStringList& genres,
                                   const QString& rating,
                                   const QString& type)
{
    // STREAM_DETAIL_METADATA_POLISH 2026-05-06 — compose ONE muted-gray
    // inline string from the present fields. Empty-field branches just
    // skip the QStringList append so " · " separators never bracket
    // missing data (no leading/trailing dots, no doubled separators).
    // Genres now use comma-separation INSIDE the genre section; middle-
    // dot " · " is reserved between sections (Stremio idiom).
    if (!m_metaLine) return;

    QStringList parts;

    if (!year.trimmed().isEmpty()) {
        // Preserve the Stremio ongoing-series normalization: trailing
        // en-dash reads as a dangling separator inline; replace with
        // "<year>-present". Same fix applied in StreamLibraryLayout
        // tile subtitles for consistency.
        QString y = year.trimmed();
        if (y.endsWith(QChar(0x2013)) || y.endsWith(QChar('-'))) {
            y.chop(1);
            y += QStringLiteral("\u2013present");
        }
        parts << y;
    }

    if (!runtime.trimmed().isEmpty())
        parts << runtime.trimmed();

    if (!genres.isEmpty()) {
        const QStringList firstThree = genres.mid(0, 3);
        parts << firstThree.join(QStringLiteral(", "));
    }

    if (type == QStringLiteral("series"))
        parts << QStringLiteral("Series");
    else if (type == QStringLiteral("movie"))
        parts << QStringLiteral("Movie");

    if (!rating.trimmed().isEmpty()) {
        // Plain "IMDb 7.5" \u2014 no star glyph per
        // feedback_no_color_no_emoji.md (grayscale-only, no emoji,
        // SVG icons only). U+2605 star would render as a colored
        // emoji in some font fallbacks; literal text is the safe
        // parity move with the prior chip's behavior.
        parts << QStringLiteral("IMDb ") + rating.trimmed();
    }

    const QString text = parts.join(QStringLiteral(" \u00B7 "));

    if (text.isEmpty()) {
        m_metaLine->hide();
    } else {
        m_metaLine->setText(text);
        m_metaLine->show();
    }
}

// ─── Phase 3 Batch 3.3 — description clamp + show-more toggle ────────────────

namespace {
// 3 lines × font line-spacing — the "clamped" height target.
constexpr int kDescClampLines = 3;
}

void StreamDetailView::setDescription(const QString& text)
{
    if (!m_descLabel) return;

    const QString trimmed = text.trimmed();
    m_descLabel->setText(trimmed);
    m_descLabel->setVisible(!trimmed.isEmpty());

    // Reset to collapsed state on every content change so a new title
    // doesn't inherit the previous title's expanded view.
    m_descExpanded = false;
    if (m_descShowMoreBtn) {
        m_descShowMoreBtn->setText(tr("Show more"));
    }
    updateDescriptionClamp();
}

void StreamDetailView::updateDescriptionClamp()
{
    if (!m_descLabel || !m_descShowMoreBtn) return;

    const QString text = m_descLabel->text();
    if (text.trimmed().isEmpty()) {
        m_descLabel->setMaximumHeight(QWIDGETSIZE_MAX);
        m_descShowMoreBtn->hide();
        return;
    }

    // Compute the natural height of the full text at the label's available
    // width. Fall back to a reasonable default when the layout hasn't yet
    // sized the widget (first paint on showEntry). 480px is a conservative
    // left-column estimate for the 3:2 root split at typical window widths.
    const int availWidth = m_descLabel->width() > 40 ? m_descLabel->width() : 480;
    const QFontMetrics fm(m_descLabel->font());
    const QRect fullRect = fm.boundingRect(QRect(0, 0, availWidth, 0),
                                           Qt::TextWordWrap, text);
    const int lineHeight = fm.lineSpacing();
    const int clampHeight = lineHeight * kDescClampLines;
    const int fullHeight  = fullRect.height();

    if (fullHeight <= clampHeight + 2 /* small tolerance */) {
        // Fits inside the clamp — no need for the toggle.
        m_descLabel->setMaximumHeight(QWIDGETSIZE_MAX);
        m_descShowMoreBtn->hide();
        return;
    }

    // Overflows — apply the clamp and surface the toggle. If the user has
    // already expanded, honor that state across resize / re-run.
    if (m_descExpanded) {
        m_descLabel->setMaximumHeight(QWIDGETSIZE_MAX);
        m_descShowMoreBtn->setText(tr("Show less"));
    } else {
        m_descLabel->setMaximumHeight(clampHeight);
        m_descShowMoreBtn->setText(tr("Show more"));
    }
    m_descShowMoreBtn->show();
}

void StreamDetailView::onDescShowMoreClicked()
{
    m_descExpanded = !m_descExpanded;
    updateDescriptionClamp();
}

// ─── Phase 3 Batch 3.2 (deferred ship) — director + cast row ─────────────────

void StreamDetailView::applyCastDirector(
    const QList<tankostream::addon::MetaLink>& links)
{
    if (!m_castDirectorLabel) return;

    QStringList directors;
    QStringList cast;
    for (const auto& link : links) {
        // Stremio convention: category is a free-form string; canonical
        // values are "Director", "Cast" (also "Writer", "Genre", etc — we
        // ignore those here). Match case-insensitive for safety.
        if (link.category.compare(QStringLiteral("Director"),
                                   Qt::CaseInsensitive) == 0) {
            if (!link.name.trimmed().isEmpty()) directors.append(link.name.trimmed());
        } else if (link.category.compare(QStringLiteral("Cast"),
                                          Qt::CaseInsensitive) == 0) {
            if (!link.name.trimmed().isEmpty()) cast.append(link.name.trimmed());
        }
    }

    if (directors.isEmpty() && cast.isEmpty()) {
        m_castDirectorLabel->hide();
        m_castDirectorLabel->clear();
        return;
    }

    QStringList parts;
    if (!directors.isEmpty()) {
        parts << tr("Director: ") + directors.join(QStringLiteral(", "));
    }
    if (!cast.isEmpty()) {
        // Elide the cast list at the label's width × 2 lines (the label's
        // maximumHeight of 40 allows ~2 lines of 11px type). Qt's QLabel
        // word-wrap + maximumHeight combination handles the vertical
        // clip, but for a single-line result we also hard-cap the cast
        // string to avoid pathological all-caps wrapping. 200 chars is
        // generous — typical 6-8 top-billed names fit well under that.
        QString castJoined = cast.join(QStringLiteral(", "));
        if (castJoined.length() > 200) {
            castJoined = castJoined.left(200).trimmed() + QStringLiteral("\u2026");
        }
        parts << tr("Cast: ") + castJoined;
    }

    m_castDirectorLabel->setText(parts.join(QStringLiteral(" \u00B7 ")));
    m_castDirectorLabel->show();
}

// ─── Phase 3 Batch 3.5 (deferred ship) — trailer button ──────────────────────

void StreamDetailView::applyTrailerButton(
    const QList<tankostream::addon::Stream>& trailerStreams)
{
    m_currentTrailerDirectUrl = QUrl();
    m_currentTrailerYouTubeId.clear();

    // Prefer direct playable URL (Url or Http kind). Matches Peerflix +
    // Stremio convention — if the addon offers a direct trailer, play
    // it in the in-app player rather than punting to a browser.
    for (const auto& t : trailerStreams) {
        const auto kind = t.source.kind;
        if ((kind == tankostream::addon::StreamSource::Kind::Url
             || kind == tankostream::addon::StreamSource::Kind::Http)
            && t.source.url.isValid() && !t.source.url.isEmpty())
        {
            m_currentTrailerDirectUrl = t.source.url;
            break;
        }
    }

    // Fallback: first YouTube-kind. Our StreamEngine rejects YouTube
    // (youtube-dl/yt-dlp isn't shipped in-app), so these route to the
    // default browser via QDesktopServices instead of an in-app embed.
    if (!m_currentTrailerDirectUrl.isValid()) {
        for (const auto& t : trailerStreams) {
            if (t.source.kind == tankostream::addon::StreamSource::Kind::YouTube
                && !t.source.youtubeId.isEmpty())
            {
                m_currentTrailerYouTubeId = t.source.youtubeId;
                break;
            }
        }
    }

    if (!m_trailerBtn) return;
    const bool hasTrailer = m_currentTrailerDirectUrl.isValid()
                          || !m_currentTrailerYouTubeId.isEmpty();
    m_trailerBtn->setVisible(hasTrailer);
}

void StreamDetailView::onTrailerClicked()
{
    if (m_currentTrailerDirectUrl.isValid()) {
        // Direct-URL trailer — emit signal; StreamPage wraps as ad-hoc
        // play through StreamPlayerController (same pattern as Batch 4.3
        // URL-paste handling).
        emit trailerDirectPlayRequested(m_currentTrailerDirectUrl);
        return;
    }
    if (!m_currentTrailerYouTubeId.isEmpty()) {
        // YouTube-only — punt to default browser per TODO.
        QDesktopServices::openUrl(QUrl(
            QStringLiteral("https://www.youtube.com/watch?v=") + m_currentTrailerYouTubeId));
    }
}

// ─── Phase 3 Batch 3.4 — episode thumbnail fetch + paint ─────────────────────

QString StreamDetailView::episodeThumbPath(const QString& imdbId,
                                            int season,
                                            int episode) const
{
    return m_episodeThumbsCacheDir + QStringLiteral("/") + imdbId
         + QStringLiteral("_") + QString::number(season)
         + QStringLiteral("_") + QString::number(episode)
         + QStringLiteral(".jpg");
}

void StreamDetailView::applyEpisodeThumbnail(QLabel* target, const QString& imagePath)
{
    if (!target) return;
    QPixmap pm(imagePath);
    if (pm.isNull()) return;

    // Scale-to-fill with smooth transform and center-crop to 102x58 so
    // landscape thumbnails don't distort. Target size matches the fixed
    // QLabel size set during row construction (~1.6x of the prior 64x36).
    const QPixmap scaled = pm.scaled(102, 58, Qt::KeepAspectRatioByExpanding,
                                     Qt::SmoothTransformation);
    if (scaled.width() == 102 && scaled.height() == 58) {
        target->setPixmap(scaled);
        return;
    }
    const int cx = (scaled.width()  - 102) / 2;
    const int cy = (scaled.height() - 58) / 2;
    target->setPixmap(scaled.copy(cx, cy, 102, 58));
}

void StreamDetailView::fetchEpisodeThumbnail(const QString& imdbId,
                                              int season, int episode,
                                              const QUrl& url, QLabel* target)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
    req.setTransferTimeout(10000);

    auto* reply = m_nam->get(req);
    // Two QPointer guards: view (self) + target label. Either gone → no-op.
    // Stale-title guard via imdbId == m_currentImdb so a download completing
    // after the user navigated away still lands on disk for next open but
    // doesn't paint into a recycled cell widget showing a different title.
    QPointer<StreamDetailView> selfGuard(this);
    QPointer<QLabel>           labelGuard(target);
    const QString cachePath = episodeThumbPath(imdbId, season, episode);

    connect(reply, &QNetworkReply::finished, this,
        [this, reply, selfGuard, labelGuard, cachePath, imdbId]() {
            reply->deleteLater();
            if (!selfGuard) return;
            if (reply->error() != QNetworkReply::NoError) return;
            const QByteArray data = reply->readAll();
            if (data.isEmpty()) return;

            QFile f(cachePath);
            if (!f.open(QIODevice::WriteOnly)) return;
            f.write(data);
            f.close();

            if (imdbId != m_currentImdb) return;   // user navigated away
            if (!labelGuard) return;               // cell widget replaced
            applyEpisodeThumbnail(labelGuard, cachePath);
        });
}
