#include "StreamDetailView.h"

#include "core/CoreBridge.h"
#include "core/stream/MetaAggregator.h"
#include "core/stream/StreamDownloadIndex.h"
#include "core/stream/StreamLibrary.h"
#include "core/stream/StreamProgress.h"
#include "core/torrent/TorrentClient.h"
#include "StreamSourceList.h"

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
#include <QMenu>
#include <QMessageBox>
#include <QModelIndex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QTimer>
#include <QPixmap>
#include <QPoint>
#include <QPointer>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyle>
#include <QVBoxLayout>

using tankostream::addon::MetaItem;
using tankostream::stream::MetaAggregator;
using tankostream::stream::StreamEpisode;

StreamDetailView::StreamDetailView(CoreBridge* bridge,
                                   MetaAggregator* meta,
                                   StreamLibrary* library,
                                   QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
    , m_meta(meta)
    , m_library(library)
    , m_nam(new QNetworkAccessManager(this))
{
    m_heroCacheDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                   + QStringLiteral("/Tankoban/data/stream_backgrounds");
    QDir().mkpath(m_heroCacheDir);

    m_episodeThumbsCacheDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/Tankoban/data/stream_episode_thumbnails");
    QDir().mkpath(m_episodeThumbsCacheDir);

    buildUI();

    // STREAM_BULK_DOWNLOAD_V2 Phase 3 — 1Hz poll for the per-episode
    // download-state column. Started lazily by populateEpisodeTable when
    // bulk activity is detected for the show+season; refreshEpisodeBulkProgress
    // self-stops the timer when no bulk activity remains.
    m_bulkPollTimer = new QTimer(this);
    m_bulkPollTimer->setInterval(1000);
    connect(m_bulkPollTimer, &QTimer::timeout,
            this, &StreamDetailView::refreshEpisodeBulkProgress);

    if (m_meta) {
        connect(m_meta, &MetaAggregator::seriesMetaReady,
                this, &StreamDetailView::onSeriesMetaReady);
        connect(m_meta, &MetaAggregator::metaItemReady,
                this, &StreamDetailView::onMetaItemReady);
    }
}

void StreamDetailView::showEntry(const QString& imdbId,
                                 int            preselectSeason,
                                 int            preselectEpisode,
                                 const std::optional<tankostream::addon::MetaItemPreview>& previewHint)
{
    m_currentImdb = imdbId;
    m_pendingPreselectSeason  = preselectSeason;
    m_pendingPreselectEpisode = preselectEpisode;
    m_seasons.clear();
    m_episodeTable->setRowCount(0);
    m_episodeTable->hide();
    m_seasonRow->hide();
    if (m_downloadSeasonBtn) m_downloadSeasonBtn->hide();
    m_statusLabel->setText("Loading...");
    m_statusLabel->show();

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
        // Stream-picker UX rework — for movies, auto-trigger the source load
        // on detail open (matches Stremio). StreamPage listens on
        // playRequested, runs StreamAggregator::load, and backfills the
        // right pane via setStreamSources.
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

    // Season selector row (hidden for movies)
    m_seasonRow = new QWidget(this);
    auto* seasonLayout = new QHBoxLayout(m_seasonRow);
    seasonLayout->setContentsMargins(0, 4, 0, 4);
    seasonLayout->setSpacing(8);

    auto* seasonLabel = new QLabel("Season:", m_seasonRow);
    seasonLabel->setStyleSheet("color: rgba(255,255,255,0.6); font-size: 13px;");
    seasonLayout->addWidget(seasonLabel);

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

    m_downloadSeasonBtn = new QPushButton(tr("Download season"), m_seasonRow);
    m_downloadSeasonBtn->setObjectName(QStringLiteral("DetailDownloadSeasonBtn"));
    m_downloadSeasonBtn->setFixedHeight(30);
    m_downloadSeasonBtn->setCursor(Qt::PointingHandCursor);
    m_downloadSeasonBtn->setStyleSheet(
        "#DetailDownloadSeasonBtn { background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.14); border-radius: 6px;"
        "  color: #ddd; padding: 0 12px; font-size: 12px; }"
        "#DetailDownloadSeasonBtn:hover { background: rgba(255,255,255,0.12);"
        "  border-color: rgba(255,255,255,0.22); }");
    connect(m_downloadSeasonBtn, &QPushButton::clicked, this, [this]() {
        if (!m_seasonCombo) return;
        const int season = m_seasonCombo->currentData().toInt();
        if (m_currentType != QLatin1String("series") || m_seasons.value(season).isEmpty())
            return;
        emit bulkDownloadRequested(season);
    });
    seasonLayout->addStretch();
    seasonLayout->addWidget(m_downloadSeasonBtn);

    m_seasonRow->hide();
    leftCol->addWidget(m_seasonRow);

    // Episode table — single-click triggers source load per UX rework.
    // The legacy Play Movie button is gone: movie-mode detail view auto-
    // loads its sources on showEntry and the user clicks a source card
    // directly (no middle-column button).
    m_episodeTable = new QTableWidget(this);
    // Phase 3 Batch 3.4 — 5-column layout with left-side thumbnail + stacked
    // title/overview cell:
    //   [0 #] [1 Thumb 64x36] [2 Title+Overview stacked] [3 Progress] [4 ✓]
    m_episodeTable->setColumnCount(5);
    m_episodeTable->setHorizontalHeaderLabels({"#", "", "Title", "Progress", "Status"});
    m_episodeTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_episodeTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_episodeTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_episodeTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_episodeTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_episodeTable->setColumnWidth(0, 36);
    m_episodeTable->setColumnWidth(1, 76);   // 64px thumb + 12px padding
    m_episodeTable->setColumnWidth(3, 80);
    m_episodeTable->setColumnWidth(4, 60);
    m_episodeTable->verticalHeader()->setDefaultSectionSize(64);
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

    contentRow->addLayout(leftCol, 3);

    // ── Right column: Sources pane ────────────────────────────────────────
    auto* rightCol = new QVBoxLayout();
    rightCol->setSpacing(6);

    m_sourcesHeader = new QLabel(tr("Sources"), this);
    m_sourcesHeader->setStyleSheet(
        "color: #e5e7eb; font-size: 13px; font-weight: 600; padding: 0 2px;");
    rightCol->addWidget(m_sourcesHeader);

    m_sourcesList = new tankostream::stream::StreamSourceList(this);
    connect(m_sourcesList, &tankostream::stream::StreamSourceList::sourceActivated,
            this, &StreamDetailView::sourceActivated);
    connect(m_sourcesList, &tankostream::stream::StreamSourceList::addToTankorentRequested,
            this, &StreamDetailView::addToTankorentRequested);
    connect(m_sourcesList, &tankostream::stream::StreamSourceList::autoLaunchCancelRequested,
            this, &StreamDetailView::autoLaunchCancelRequested);
    rightCol->addWidget(m_sourcesList, 1);

    contentRow->addLayout(rightCol, 2);

    root->addLayout(contentRow, 1);
}

void StreamDetailView::setStreamSourcesLoading()
{
    if (m_sourcesList) m_sourcesList->setLoading();
}

void StreamDetailView::setStreamSources(
    const QList<tankostream::stream::StreamPickerChoice>& choices,
    const QString&                                        savedChoiceKey)
{
    if (m_sourcesList) m_sourcesList->setSources(choices, savedChoiceKey);
}

void StreamDetailView::setStreamSourcesError(const QString& message)
{
    if (m_sourcesList) m_sourcesList->setError(message);
}

void StreamDetailView::setStreamSourcesPlaceholder(const QString& message)
{
    if (m_sourcesList) m_sourcesList->setPlaceholder(message);
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

void StreamDetailView::updateBulkDownloadButton()
{
    if (!m_downloadSeasonBtn || !m_seasonCombo)
        return;
    const int season = m_seasonCombo->currentData().toInt();
    const bool visible = m_currentType == QLatin1String("series")
        && season > 0
        && !m_seasons.value(season).isEmpty();
    m_downloadSeasonBtn->setVisible(visible);
}

// ─── Series metadata ─────────────────────────────────────────────────────────

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
    for (auto it = m_seasons.begin(); it != m_seasons.end(); ++it) {
        m_seasonCombo->addItem("Season " + QString::number(it.key()), it.key());
        if (m_pendingPreselectSeason >= 0 && it.key() == m_pendingPreselectSeason)
            preselectComboIdx = idx;
        ++idx;
    }
    m_seasonCombo->blockSignals(false);

    m_seasonRow->show();
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
                auto* cell = m_episodeTable->item(r, 0);
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

    int season = m_seasonCombo->itemData(comboIndex).toInt();
    populateEpisodeTable(season);
    updateBulkDownloadButton();
}

void StreamDetailView::populateEpisodeTable(int season)
{
    // Sorting is off by default (set in buildUI) — see the trailing note
    // on why. Clear rows before rebuild.
    m_episodeTable->setRowCount(0);

    auto episodes = m_seasons.value(season);
    QJsonObject allProgress = m_bridge->allProgress("stream");

    for (const auto& ep : episodes) {
        const int row = m_episodeTable->rowCount();
        m_episodeTable->insertRow(row);
        m_episodeTable->setRowHeight(row, 64);

        // Column 0 — episode number (also the carrier of UserRole data for
        // the Calendar preselect lookup + onEpisodeActivated dispatch).
        auto* numItem = new QTableWidgetItem(QString::number(ep.episode));
        numItem->setTextAlignment(Qt::AlignCenter);
        numItem->setData(Qt::UserRole, ep.episode);
        numItem->setData(Qt::UserRole + 1, season);
        m_episodeTable->setItem(row, 0, numItem);

        // Column 1 — thumbnail (64x36). QLabel as cell widget: disk-cache
        // hit paints synchronously; miss kicks an async download via NAM.
        auto* thumbHolder = new QWidget(m_episodeTable);
        auto* thumbLayout = new QHBoxLayout(thumbHolder);
        thumbLayout->setContentsMargins(6, 0, 6, 0);
        thumbLayout->setSpacing(0);
        auto* thumbLabel = new QLabel(thumbHolder);
        thumbLabel->setFixedSize(64, 36);
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
        m_episodeTable->setCellWidget(row, 1, thumbHolder);

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
            "color: #e0e0e0; font-size: 12px; font-weight: 500; background: transparent;");
        tLabel->setTextFormat(Qt::PlainText);
        titleLayout->addWidget(tLabel);

        if (!ep.overview.isEmpty()) {
            auto* oLabel = new QLabel(ep.overview, titleCell);
            oLabel->setWordWrap(true);
            oLabel->setStyleSheet(
                "color: rgba(255,255,255,0.45); font-size: 10px;"
                "  font-style: italic; background: transparent;");
            oLabel->setTextFormat(Qt::PlainText);
            // 2-line clamp via QFontMetrics — consistent with Batch 3.3's
            // description approach. Overflow is silently clipped (no toggle
            // here; episode rows aren't interactive beyond click-to-play).
            const QFontMetrics fm(oLabel->font());
            oLabel->setMaximumHeight(fm.lineSpacing() * 2);
            titleLayout->addWidget(oLabel);
        } else {
            titleLayout->addStretch();
        }
        m_episodeTable->setCellWidget(row, 2, titleCell);

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
        m_episodeTable->setItem(row, 3, progItem);

        // Column 4 — status checkmark.
        auto* statusItem = new QTableWidgetItem();
        statusItem->setTextAlignment(Qt::AlignCenter);
        if (finished)
            statusItem->setText("\u2713");
        m_episodeTable->setItem(row, 4, statusItem);
    }

    // Phase 3 Batch 3.4 — sorting disabled. Cell widgets in cols 1/2 (thumb,
    // stacked title+overview) don't carry sortable QTableWidgetItem data, so
    // a header-click sort would produce unpredictable order. Episodes are
    // already sorted numerically by parseSeriesEpisodes — the visible order
    // is the authoritative one and doesn't need runtime re-sort.
    m_episodeTable->setSortingEnabled(false);

    // STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) — paint per-row "on
    // disk" icons after the rows are built. Cheap (single mutex-guarded
    // hash lookup per row); safe pre-show.
    refreshEpisodeMarkers();

    // STREAM_BULK_DOWNLOAD_V2 Phase 3 — paint per-row download-state from
    // the TorrentClient bulk snapshot, and start the 1Hz poll timer if
    // any bulk activity exists for this show+season. Self-stops in the
    // slot when activity drains.
    refreshEpisodeBulkProgress();

    m_episodeTable->show();
}

// ─── Play triggers ───────────────────────────────────────────────────────────

void StreamDetailView::onEpisodeActivated(int row, int /*col*/)
{
    auto* numItem = m_episodeTable->item(row, 0);
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

// STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) — wire the download index
// in. We connect to entriesChanged so a bulk-completion (Phase 2) in another
// pane lights up the rows here in place. QueuedConnection because the index
// may emit from a worker thread (validateAll runs via QtConcurrent on home
// open per spec §10.4).
void StreamDetailView::setStreamDownloadIndex(StreamDownloadIndex* idx)
{
    if (m_downloadIndex == idx) return;
    if (m_downloadIndex) {
        disconnect(m_downloadIndex, &StreamDownloadIndex::entriesChanged,
                   this, &StreamDetailView::refreshEpisodeMarkers);
    }
    m_downloadIndex = idx;
    if (m_downloadIndex) {
        connect(m_downloadIndex, &StreamDownloadIndex::entriesChanged,
                this, &StreamDetailView::refreshEpisodeMarkers,
                Qt::QueuedConnection);
        // Repaint immediately if the table already has rows from a prior
        // showEntry call (the wiring may land late-in-MainWindow ctor).
        refreshEpisodeMarkers();
    }
}

// STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) — repaint per-row on-disk
// markers. The icon is mounted on the column-0 QTableWidgetItem (the "#"
// cell), not on column 2's stacked title+overview cell-widget — col 0 is
// a plain QTableWidgetItem (setIcon works directly), whereas col 2 is a
// custom QWidget where icon mounting would mean rebuilding the layout.
void StreamDetailView::refreshEpisodeMarkers()
{
    if (!m_episodeTable || !m_downloadIndex || m_currentImdb.isEmpty())
        return;
    if (m_currentType != QLatin1String("series"))
        return;

    static const QIcon dlIcon(QStringLiteral(":/icons/downloaded.svg"));

    for (int row = 0; row < m_episodeTable->rowCount(); ++row) {
        QTableWidgetItem* numItem = m_episodeTable->item(row, 0);
        if (!numItem) continue;
        const int episode = numItem->data(Qt::UserRole).toInt();
        const int season  = numItem->data(Qt::UserRole + 1).toInt();
        if (season <= 0 || episode <= 0) continue;

        const auto pathOpt = m_downloadIndex->filePathFor(m_currentImdb, season, episode);
        if (pathOpt.has_value()) {
            numItem->setIcon(dlIcon);
            numItem->setToolTip(tr("On disk: %1").arg(QFileInfo(*pathOpt).fileName()));
        } else {
            numItem->setIcon(QIcon());
            numItem->setToolTip(QString());
        }
    }
}

// STREAM_BULK_DOWNLOAD_V2 Phase 3 — repaint per-row download state in the
// Status column (col 4). Sourced from TorrentClient::streamBulkSnapshotForImdbSeason
// which walks m_streamBulkGroups for matching imdbId+season. Per row, the
// state string drives a short label:
//   "Pending"      → "Queued"           (paused-queued in cohort)
//   "Downloading"  → "<N>%"             (live progress from listActive)
//   "Publishing"   → "Done"             (file rename in flight, sub-second)
//   "Published"    → "✓"                (terminal success; Phase 4 also
//                                        lights the col-0 download icon)
//   any failed     → "Failed"           (terminal; cohort advanced past it)
//   no row in snap → existing watched-checkmark logic (preserved)
// The 1Hz poll timer started in populateEpisodeTable runs only while the
// snapshot is non-empty; this slot stops the timer when bulk activity
// drains so an idle detail view doesn't burn CPU on no-op polls.
void StreamDetailView::refreshEpisodeBulkProgress()
{
    if (!m_episodeTable || !m_torrentClient || m_currentImdb.isEmpty()) {
        if (m_bulkPollTimer && m_bulkPollTimer->isActive())
            m_bulkPollTimer->stop();
        return;
    }
    if (m_currentType != QLatin1String("series")) {
        if (m_bulkPollTimer && m_bulkPollTimer->isActive())
            m_bulkPollTimer->stop();
        return;
    }

    int activeSeason = 1;
    if (m_seasonCombo) {
        const int idx = m_seasonCombo->currentIndex();
        if (idx >= 0)
            activeSeason = m_seasonCombo->itemData(idx).toInt();
    }
    if (activeSeason <= 0) {
        if (m_bulkPollTimer && m_bulkPollTimer->isActive())
            m_bulkPollTimer->stop();
        return;
    }

    const QHash<int, QPair<QString, int>> snapshot =
        m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, activeSeason);

    if (snapshot.isEmpty()) {
        if (m_bulkPollTimer && m_bulkPollTimer->isActive())
            m_bulkPollTimer->stop();
        return;
    }

    bool anyActive = false;  // any non-terminal row → keep polling
    for (int row = 0; row < m_episodeTable->rowCount(); ++row) {
        QTableWidgetItem* numItem = m_episodeTable->item(row, 0);
        QTableWidgetItem* statusItem = m_episodeTable->item(row, 4);
        if (!numItem || !statusItem) continue;
        const int episode = numItem->data(Qt::UserRole).toInt();
        if (episode <= 0) continue;

        const auto it = snapshot.constFind(episode);
        if (it == snapshot.cend())
            continue;  // no bulk row → leave existing watched-checkmark intact

        const QString& state = it->first;
        const int pct = it->second;

        // STREAM_BULK_DOWNLOAD_V2 hotfix 2026-05-10 — only paint the three
        // active in-flight states. Terminal states (Published, Failed,
        // MissingSource, MetadataFailed, PublishFailed, Cancelled, Orphaned)
        // intentionally fall through and leave the existing watched-checkmark
        // text untouched. Reasoning: m_streamBulkGroups never garbage-
        // collects historical bulk-dispatch records, so a prior Cancelled-
        // by-user group (e.g. Daredevil S01 from earlier today) would
        // otherwise pollute this column with stale "Failed" labels for
        // episodes the user has long since moved past. Tankorent's group
        // row is the canonical historical-failures surface; this column
        // is for "what's downloading right now."
        if (state == QLatin1String("Pending")) {
            statusItem->setText(tr("Queued"));
            anyActive = true;
        } else if (state == QLatin1String("Downloading")) {
            statusItem->setText(pct > 0
                ? QStringLiteral("%1%").arg(pct)
                : tr("Downloading"));
            anyActive = true;
        } else if (state == QLatin1String("Publishing")) {
            statusItem->setText(tr("Done"));
            anyActive = true;
        }
    }

    if (anyActive) {
        if (m_bulkPollTimer && !m_bulkPollTimer->isActive())
            m_bulkPollTimer->start();
    } else {
        if (m_bulkPollTimer && m_bulkPollTimer->isActive())
            m_bulkPollTimer->stop();
    }
}

// STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) — right-click context
// menu on the episode table. Always offers "Show alternate streams" — for
// downloaded episodes this is the explicit escape from the local-file
// shortcut; for undownloaded episodes it's just an alternate name for the
// default click action. StreamPage routes the signal to the same
// source-pick flow either way. Spec §6.3.
void StreamDetailView::onEpisodeContextMenu(const QPoint& pos)
{
    if (!m_episodeTable) return;
    const QModelIndex idx = m_episodeTable->indexAt(pos);
    if (!idx.isValid()) return;

    QTableWidgetItem* numItem = m_episodeTable->item(idx.row(), 0);
    if (!numItem) return;
    const int episode = numItem->data(Qt::UserRole).toInt();
    const int season  = numItem->data(Qt::UserRole + 1).toInt();
    if (episode <= 0 || season <= 0) return;

    QMenu menu(this);
    QAction* altAct = menu.addAction(tr("Show alternate streams"));
    QAction* picked = menu.exec(m_episodeTable->viewport()->mapToGlobal(pos));
    if (picked == altAct) {
        emit alternateStreamRequested(season, episode);
    }
}

void StreamDetailView::updateProgressColumn()
{
    if (m_currentType != "series" || m_seasonCombo->count() == 0)
        return;

    int season = m_seasonCombo->currentData().toInt();
    populateEpisodeTable(season);
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
            QMessageBox box(this);
            box.setWindowTitle(tr("Cancel active bulk download?"));
            box.setText(tr("This show has an active bulk download in progress.\n"
                           "Cancel the download first, then Remove from Library?"));
            QPushButton* doIt = box.addButton(tr("Cancel download + Remove"),
                                              QMessageBox::DestructiveRole);
            QPushButton* abort = box.addButton(tr("Keep downloading"),
                                                QMessageBox::RejectRole);
            box.setDefaultButton(abort);
            box.exec();
            if (box.clickedButton() != doIt)
                return;  // user kept downloading; no Remove

            // Cancel all active bulk groups for this imdb. Atomic-ish: we
            // iterate the snapshot of group IDs taken just before the
            // dialog fired; any group that completes between dialog-open
            // and click is no longer "contains"-d in m_streamBulkGroups
            // and cancelStreamBulkGroup short-circuits.
            const QStringList ids =
                m_torrentClient->streamBulkGroupIdsForImdb(m_currentImdb);
            for (const QString& gid : ids)
                m_torrentClient->cancelStreamBulkGroup(gid);
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

    // Scale-to-fill with smooth transform and center-crop to 64x36 so
    // landscape thumbnails don't distort. Target size matches the fixed
    // QLabel size set during row construction.
    const QPixmap scaled = pm.scaled(64, 36, Qt::KeepAspectRatioByExpanding,
                                     Qt::SmoothTransformation);
    if (scaled.width() == 64 && scaled.height() == 36) {
        target->setPixmap(scaled);
        return;
    }
    const int cx = (scaled.width()  - 64) / 2;
    const int cy = (scaled.height() - 36) / 2;
    target->setPixmap(scaled.copy(cx, cy, 64, 36));
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
