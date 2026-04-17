#include "StreamPage.h"

#include "core/CoreBridge.h"
#include "core/stream/MetaAggregator.h"
#include "core/stream/addon/AddonRegistry.h"
#include "ui/pages/stream/AddonManagerScreen.h"
#include "core/stream/StreamEngine.h"
#include "core/stream/StreamLibrary.h"
#include "core/torrent/TorrentEngine.h"
#include "stream/StreamLibraryLayout.h"
#include "stream/StreamSearchWidget.h"
#include "stream/StreamDetailView.h"
#include "stream/StreamSourceChoice.h"
#include "core/stream/StreamAggregator.h"
#include "core/stream/SubtitlesAggregator.h"
#include "core/stream/CalendarEngine.h"
#include "stream/CalendarScreen.h"
#include "stream/StreamPlayerController.h"
#include "stream/StreamContinueStrip.h"
#include "stream/StreamHomeBoard.h"
#include "stream/CatalogBrowseScreen.h"
#include "core/stream/StreamProgress.h"

#include "ui/player/VideoPlayer.h"
#include "ui/dialogs/AddAddonDialog.h"
#include "core/stream/addon/StreamSource.h"

#include <QDebug>
#include <QFrame>
#include <QEvent>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QProgressBar>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QUrl>

#include <functional>
#include <memory>

StreamPage::StreamPage(CoreBridge* bridge, TorrentEngine* torrentEngine,
                       QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
    , m_torrentEngine(torrentEngine)
{
    setObjectName("stream");

    // Core services
    m_addonRegistry = new tankostream::addon::AddonRegistry(bridge->dataDir(), nullptr, this);
    m_streamAggregator = new tankostream::stream::StreamAggregator(m_addonRegistry, this);
    m_metaAggregator = new tankostream::stream::MetaAggregator(m_addonRegistry, this);
    m_subtitlesAggregator = new tankostream::stream::SubtitlesAggregator(m_addonRegistry, this);

    // Phase 4 Batch 4.1 — drive the search-bar spinner off the aggregator's
    // catalog signals. StreamSearchWidget is the primary result consumer;
    // these connects only toggle the busy UI state. Lambdas capture `this`
    // safely — StreamPage owns m_metaAggregator so lifetimes are coupled.
    connect(m_metaAggregator, &tankostream::stream::MetaAggregator::catalogResults,
            this, [this](const QList<tankostream::addon::MetaItemPreview>&) {
                setSearchBusy(false);
            });
    connect(m_metaAggregator, &tankostream::stream::MetaAggregator::catalogError,
            this, [this](const QString&) {
                setSearchBusy(false);
            });

    // Batch 5.3 — route subtitle aggregator results to the VideoPlayer's
    // SubtitleMenu. Player is created by MainWindow and reachable via
    // findChild; connection is persistent for the StreamPage lifetime.
    connect(m_subtitlesAggregator, &tankostream::stream::SubtitlesAggregator::subtitlesReady,
        this, [this](const QList<tankostream::addon::SubtitleTrack>& tracks,
                     const QHash<QString, QString>& originByTrackKey) {
            auto* mainWin = window();
            if (!mainWin) return;
            auto* player = mainWin->findChild<VideoPlayer*>();
            if (!player) return;
            player->setExternalSubtitleTracks(tracks, originByTrackKey);
        });
    m_library   = new StreamLibrary(&bridge->store(), this);

    // Batch 6.1 — Calendar backend. Needs AddonRegistry (meta fan-out) +
    // StreamLibrary (series source) + dataDir (cache path). No UI hookup
    // in 6.1; Batch 6.2 wires CalendarScreen to this engine's signals and
    // triggers loadUpcoming() on the calendar entry button.
    m_calendarEngine = new tankostream::stream::CalendarEngine(
        m_addonRegistry, m_library, bridge->dataDir(), this);

    QString cacheDir = bridge->dataDir() + "/stream_cache";
    m_streamEngine = new StreamEngine(torrentEngine, cacheDir, this);
    m_streamEngine->start();
    m_streamEngine->cleanupOrphans();
    m_streamEngine->startPeriodicCleanup();

    // Player controller
    m_playerController = new StreamPlayerController(bridge, m_streamEngine, this);
    connect(m_playerController, &StreamPlayerController::bufferUpdate,
            this, &StreamPage::onBufferUpdate);
    connect(m_playerController, &StreamPlayerController::readyToPlay,
            this, &StreamPage::onReadyToPlay);
    connect(m_playerController, &StreamPlayerController::streamFailed,
            this, &StreamPage::onStreamFailed);
    connect(m_playerController, &StreamPlayerController::streamStopped,
            this, &StreamPage::onStreamStopped);

    buildUI();
}

void StreamPage::activate()
{
    if (m_homeBoard)
        m_homeBoard->refresh();
    if (m_libraryLayout)
        m_libraryLayout->refresh();
}

// ─── UI construction ─────────────────────────────────────────────────────────

void StreamPage::buildUI()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    buildSearchBar();

    m_mainStack = new QStackedWidget(this);

    buildBrowseLayer();
    m_mainStack->addWidget(m_browseLayer);   // index 0: browse

    // Detail layer
    m_detailView = new StreamDetailView(m_bridge, m_metaAggregator, m_library, this);
    m_mainStack->addWidget(m_detailView); // index 1: detail

    connect(m_detailView, &StreamDetailView::backRequested, this, &StreamPage::showBrowse);
    connect(m_detailView, &StreamDetailView::playRequested, this, &StreamPage::onPlayRequested);
    connect(m_detailView, &StreamDetailView::sourceActivated,
            this, &StreamPage::onSourceActivated);
    // Phase 3 Batch 3.5 (deferred ship) — direct-URL trailer playback.
    // Routes through the same ad-hoc-stream pattern as Batch 4.3's URL
    // paste handler: synthesize a httpSource Stream, set m_session.pending
    // with an "adhoc-trailer:" imdbId prefix (namespaced so progress
    // persistence doesn't collide with real library entries), dispatch
    // through StreamPlayerController.
    connect(m_detailView, &StreamDetailView::trailerDirectPlayRequested,
            this, [this](const QUrl& trailerUrl) {
                if (!m_playerController || !trailerUrl.isValid()) return;

                tankostream::addon::Stream stream;
                stream.source = tankostream::addon::StreamSource::httpSource(trailerUrl);
                stream.name   = QStringLiteral("Trailer");

                PendingPlay p;
                p.imdbId    = QStringLiteral("adhoc-trailer:")
                                  + trailerUrl.toString().left(40);
                p.mediaType = QStringLiteral("movie");
                p.season    = 0;
                p.episode   = 0;
                p.epKey     = QStringLiteral("stream:") + p.imdbId;
                p.valid     = true;
                beginSession(p.epKey, p, QStringLiteral("trailer-paste"));

                m_mainStack->setCurrentIndex(2);
                m_bufferLabel->setText(tr("Loading trailer..."));
                m_bufferOverlay->show();

                m_playerController->startStream(p.imdbId, p.mediaType,
                                                p.season, p.episode, stream);
            });
    // Phase 2 Batch 2.4 — Pick-different button in the toast; aborts the
    // auto-launch timer and leaves the picker open so the user can pick a
    // different source manually.
    connect(m_detailView, &StreamDetailView::autoLaunchCancelRequested,
            this, &StreamPage::cancelAutoLaunch);

    // Auto-launch timer — single-shot, 2s window (enough for the user to
    // notice the "Resuming with last-used source" toast and cancel).
    m_autoLaunchTimer = new QTimer(this);
    m_autoLaunchTimer->setSingleShot(true);
    m_autoLaunchTimer->setInterval(2000);
    connect(m_autoLaunchTimer, &QTimer::timeout,
            this, &StreamPage::onAutoLaunchFire);

    // Wire library grid single+double-click → show detail. Explicit lambda
    // because showDetail is overloaded — pointer-to-member would be ambiguous
    // between the (QString) and (MetaItemPreview) overloads.
    connect(m_libraryLayout, &StreamLibraryLayout::showClicked, this,
        [this](const QString& imdbId) { showDetail(imdbId); });

    // Player layer — buffer overlay only (VideoPlayer handles its own controls)
    auto* playerLayer = new QWidget(this);
    auto* playerLayerLayout = new QVBoxLayout(playerLayer);
    playerLayerLayout->setContentsMargins(0, 0, 0, 0);
    playerLayerLayout->setAlignment(Qt::AlignCenter);

    m_bufferOverlay = new QWidget(playerLayer);
    auto* bufLayout = new QVBoxLayout(m_bufferOverlay);
    bufLayout->setAlignment(Qt::AlignCenter);
    bufLayout->setSpacing(12);

    m_bufferLabel = new QLabel("Connecting...", m_bufferOverlay);
    m_bufferLabel->setObjectName("StreamBufferOverlay");
    m_bufferLabel->setAlignment(Qt::AlignCenter);
    m_bufferLabel->setStyleSheet(
        "#StreamBufferOverlay { background: rgba(0,0,0,180); color: white;"
        "  font-size: 15px; padding: 12px 24px; border-radius: 8px; }");
    bufLayout->addWidget(m_bufferLabel, 0, Qt::AlignCenter);

    m_bufferCancelBtn = new QPushButton("Cancel", m_bufferOverlay);
    m_bufferCancelBtn->setFixedSize(100, 30);
    m_bufferCancelBtn->setCursor(Qt::PointingHandCursor);
    m_bufferCancelBtn->setStyleSheet(
        "QPushButton { background: rgba(255,255,255,0.15); border: none;"
        "  border-radius: 6px; color: rgba(255,255,255,0.7); font-size: 12px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.25); }");
    connect(m_bufferCancelBtn, &QPushButton::clicked, this, [this]() {
        m_playerController->stopStream();
    });
    bufLayout->addWidget(m_bufferCancelBtn, 0, Qt::AlignCenter);

    playerLayerLayout->addWidget(m_bufferOverlay);

    // Phase 2 Batch 2.5 — next-episode overlay lives on the same player
    // layer as the buffer overlay. Hidden by default; becomes visible after
    // the user closes a series episode whose playback crossed 95% AND a
    // next-episode pre-fetch completed with a matched source. 10s countdown
    // auto-plays; Play Now skips the countdown; Cancel returns to browse.
    m_nextEpisodeOverlay = new QFrame(playerLayer);
    m_nextEpisodeOverlay->setObjectName(QStringLiteral("StreamNextEpisodeOverlay"));
    m_nextEpisodeOverlay->setStyleSheet(QStringLiteral(
        "#StreamNextEpisodeOverlay {"
        "  background: rgba(0,0,0,200); border-radius: 10px; padding: 20px; }"
        "#StreamNextEpisodeTitle {"
        "  color: #f0f0f0; font-size: 15px; font-weight: 600; }"
        "#StreamNextEpisodeCountdown {"
        "  color: rgba(255,255,255,0.75); font-size: 12px; }"
        "#StreamNextEpisodePlayNow, #StreamNextEpisodeCancel {"
        "  background: rgba(255,255,255,0.14); border: 1px solid rgba(255,255,255,0.22);"
        "  border-radius: 6px; color: #e8e8e8; font-size: 12px; padding: 6px 18px; }"
        "#StreamNextEpisodePlayNow:hover, #StreamNextEpisodeCancel:hover {"
        "  background: rgba(255,255,255,0.22); }"));
    m_nextEpisodeOverlay->setFixedWidth(420);
    auto* nextLayout = new QVBoxLayout(m_nextEpisodeOverlay);
    nextLayout->setContentsMargins(20, 16, 20, 16);
    nextLayout->setSpacing(8);
    nextLayout->setAlignment(Qt::AlignCenter);

    m_nextEpisodeTitleLabel = new QLabel(m_nextEpisodeOverlay);
    m_nextEpisodeTitleLabel->setObjectName(QStringLiteral("StreamNextEpisodeTitle"));
    m_nextEpisodeTitleLabel->setAlignment(Qt::AlignCenter);
    m_nextEpisodeTitleLabel->setWordWrap(true);
    nextLayout->addWidget(m_nextEpisodeTitleLabel);

    m_nextEpisodeCountdownLabel = new QLabel(m_nextEpisodeOverlay);
    m_nextEpisodeCountdownLabel->setObjectName(QStringLiteral("StreamNextEpisodeCountdown"));
    m_nextEpisodeCountdownLabel->setAlignment(Qt::AlignCenter);
    nextLayout->addWidget(m_nextEpisodeCountdownLabel);

    auto* nextBtnRow = new QHBoxLayout();
    nextBtnRow->setSpacing(10);
    nextBtnRow->setAlignment(Qt::AlignCenter);

    m_nextEpisodePlayNowBtn = new QPushButton(tr("Play now"), m_nextEpisodeOverlay);
    m_nextEpisodePlayNowBtn->setObjectName(QStringLiteral("StreamNextEpisodePlayNow"));
    m_nextEpisodePlayNowBtn->setCursor(Qt::PointingHandCursor);
    connect(m_nextEpisodePlayNowBtn, &QPushButton::clicked,
            this, &StreamPage::onNextEpisodePlayNow);
    nextBtnRow->addWidget(m_nextEpisodePlayNowBtn);

    m_nextEpisodeCancelBtn = new QPushButton(tr("Cancel"), m_nextEpisodeOverlay);
    m_nextEpisodeCancelBtn->setObjectName(QStringLiteral("StreamNextEpisodeCancel"));
    m_nextEpisodeCancelBtn->setCursor(Qt::PointingHandCursor);
    connect(m_nextEpisodeCancelBtn, &QPushButton::clicked,
            this, &StreamPage::onNextEpisodeCancel);
    nextBtnRow->addWidget(m_nextEpisodeCancelBtn);

    nextLayout->addLayout(nextBtnRow);

    m_nextEpisodeOverlay->hide();
    playerLayerLayout->addWidget(m_nextEpisodeOverlay, 0, Qt::AlignCenter);

    // Countdown timer — 1s tick. Decrements m_nextEpisodeCountdownSec; fires
    // Play Now at zero.
    m_nextEpisodeCountdownTimer = new QTimer(this);
    m_nextEpisodeCountdownTimer->setInterval(1000);
    connect(m_nextEpisodeCountdownTimer, &QTimer::timeout,
            this, &StreamPage::onNextEpisodeCountdownTick);

    m_mainStack->addWidget(playerLayer); // index 2: player

    // Addon manager layer (Phase 2 Batch 2.1)
    m_addonManager = new AddonManagerScreen(m_addonRegistry, this);
    m_mainStack->addWidget(m_addonManager); // index 3: addons

    connect(m_addonManager, &AddonManagerScreen::backRequested,
            this, &StreamPage::showBrowse);
    // addAddonRequested stays unwired until Batch 2.2 ships AddAddonDialog.

    // Catalog browse layer (Phase 3 Batch 3.3)
    m_catalogBrowse = new tankostream::stream::CatalogBrowseScreen(m_addonRegistry, this);
    m_mainStack->addWidget(m_catalogBrowse); // index 4: catalog browse

    connect(m_catalogBrowse, &tankostream::stream::CatalogBrowseScreen::backRequested,
            this, &StreamPage::showBrowse);
    connect(m_catalogBrowse, &tankostream::stream::CatalogBrowseScreen::metaActivated, this,
        [this](const tankostream::addon::MetaItemPreview& preview) {
            showDetail(preview);
        });
    // Stream library UX rework 2026-04-15 — StreamHomeBoard no longer emits
    // browseCatalogRequested (featured rows deleted). Users reach the
    // catalog browser via the Catalog button in the search bar instead.

    // Calendar layer (Phase 6 Batch 6.2)
    m_calendarScreen = new tankostream::stream::CalendarScreen(this);
    m_mainStack->addWidget(m_calendarScreen); // index 5: calendar

    // Engine → screen dataflow. Prefer grouped signal (pre-bucketed + date
    // math already done engine-side); the flat signal fires too but the
    // screen's setItems regroups harmlessly if grouped hasn't arrived yet.
    connect(m_calendarEngine, &tankostream::stream::CalendarEngine::calendarGroupedReady,
            m_calendarScreen, &tankostream::stream::CalendarScreen::setGroupedItems);
    connect(m_calendarEngine, &tankostream::stream::CalendarEngine::calendarError,
            m_calendarScreen, &tankostream::stream::CalendarScreen::setError);

    // Screen → navigation.
    connect(m_calendarScreen, &tankostream::stream::CalendarScreen::backRequested,
            this, &StreamPage::showBrowse);
    connect(m_calendarScreen, &tankostream::stream::CalendarScreen::refreshRequested,
            this, [this]() {
                if (m_calendarEngine && m_calendarScreen) {
                    m_calendarScreen->setLoading(true);
                    m_calendarEngine->loadUpcoming();
                }
            });
    connect(m_calendarScreen, &tankostream::stream::CalendarScreen::seriesEpisodeActivated,
            this, [this](const QString& imdbId, int season, int episode) {
                if (!m_detailView) return;
                // Route into the detail view with preselection staged. Detail
                // view consumes once in onSeriesMetaReady and clears, so a
                // subsequent showDetail click without preselection won't
                // re-apply these values.
                m_detailView->showEntry(imdbId, season, episode);
                m_mainStack->setCurrentIndex(1);
            });

    rootLayout->addWidget(m_mainStack, 1);

    m_mainStack->setCurrentIndex(0);
}

void StreamPage::buildSearchBar()
{
    m_searchBarFrame = new QFrame(this);
    m_searchBarFrame->setObjectName("streamSearchBar");

    // Margins are 0 left/right because the search bar now lives inside
    // m_scrollLayout (20,0,20,20), which already provides the page-edge
    // inset. Top=20 preserves the original breathing room from the page top.
    auto* layout = new QHBoxLayout(m_searchBarFrame);
    layout->setContentsMargins(0, 20, 0, 0);
    layout->setSpacing(8);

    m_searchInput = new QLineEdit(m_searchBarFrame);
    m_searchInput->setPlaceholderText("Search movies & TV shows...");
    // 36px input height to match VideosPage.cpp:119 search bar. Was 32.
    m_searchInput->setFixedHeight(36);
    m_searchInput->setObjectName("StreamSearchInput");
    m_searchInput->setClearButtonEnabled(true);
    layout->addWidget(m_searchInput, 1);

    // Phase 4 Batch 4.1 — indeterminate spinner. Busy mode QProgressBar
    // (range 0..0) animates natively; 16x16 sits between input + Search btn.
    auto* busy = new QProgressBar(m_searchBarFrame);
    busy->setRange(0, 0);
    busy->setTextVisible(false);
    busy->setFixedSize(16, 16);
    busy->setObjectName("StreamSearchBusy");
    busy->setStyleSheet(
        "#StreamSearchBusy { background: transparent; border: none; }"
        "#StreamSearchBusy::chunk { background: rgba(255,255,255,0.5); }");
    busy->hide();
    layout->addWidget(busy);
    m_searchBusy = busy;

    m_searchBtn = new QPushButton("Search", m_searchBarFrame);
    m_searchBtn->setFixedHeight(36);
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    m_searchBtn->setObjectName("StreamSearchBtn");
    layout->addWidget(m_searchBtn);

    m_addonsBtn = new QPushButton("Addons", m_searchBarFrame);
    m_addonsBtn->setFixedHeight(36);
    m_addonsBtn->setCursor(Qt::PointingHandCursor);
    m_addonsBtn->setObjectName("StreamAddonsBtn");
    m_addonsBtn->setToolTip("Manage installed addons");
    layout->addWidget(m_addonsBtn);

    // Batch 6.2 — Calendar entry button. Placed after Addons; opens the
    // calendar stack layer and triggers an engine load on each click
    // (CalendarEngine has its own 12h TTL cache + signature guard so
    // repeated clicks don't re-fan-out unnecessarily).
    m_calendarBtn = new QPushButton("Calendar", m_searchBarFrame);
    m_calendarBtn->setFixedHeight(36);
    m_calendarBtn->setCursor(Qt::PointingHandCursor);
    m_calendarBtn->setObjectName("StreamCalendarBtn");
    m_calendarBtn->setToolTip("Upcoming episodes from your library");
    layout->addWidget(m_calendarBtn);

    // Stream library UX rework 2026-04-15 — Catalog button replaces the
    // deleted home-board featured rows. Opens the existing
    // CatalogBrowseScreen; the screen's own addon/catalog combos handle
    // selection (persists across sessions).
    m_catalogBtn = new QPushButton("Catalog", m_searchBarFrame);
    m_catalogBtn->setFixedHeight(36);
    m_catalogBtn->setCursor(Qt::PointingHandCursor);
    m_catalogBtn->setObjectName("StreamCatalogBtn");
    m_catalogBtn->setToolTip("Browse all addon catalogs");
    layout->addWidget(m_catalogBtn);

    connect(m_searchInput, &QLineEdit::returnPressed,
            this, &StreamPage::onSearchSubmit);
    connect(m_searchBtn, &QPushButton::clicked,
            this, &StreamPage::onSearchSubmit);
    connect(m_addonsBtn, &QPushButton::clicked,
            this, &StreamPage::showAddonManager);
    connect(m_calendarBtn, &QPushButton::clicked,
            this, &StreamPage::showCalendar);
    connect(m_catalogBtn, &QPushButton::clicked,
            this, &StreamPage::onCatalogBtnClicked);

    // Phase 4 Batch 4.1 — live-search debounce. 300ms after the last
    // textChanged event, onSearchDebounceFired runs the deferred logic.
    // Enter / Search button path stays as-is for instant fire.
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(300);
    connect(m_searchDebounce, &QTimer::timeout,
            this, &StreamPage::onSearchDebounceFired);
    connect(m_searchInput, &QLineEdit::textChanged,
            this, &StreamPage::onSearchTextChanged);

    // Phase 4 Batch 4.2 — search history. Load persisted list, build
    // dropdown frame, install event filter on input for focus tracking.
    loadSearchHistory();
    buildSearchHistoryDropdown();
    m_searchInput->installEventFilter(this);
}

void StreamPage::buildBrowseLayer()
{
    m_browseLayer = new QWidget(this);
    auto* layerLayout = new QVBoxLayout(m_browseLayer);
    layerLayout->setContentsMargins(0, 0, 0, 0);
    layerLayout->setSpacing(0);

    m_browseScroll = new QScrollArea(m_browseLayer);
    m_browseScroll->setFrameShape(QFrame::NoFrame);
    m_browseScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_browseScroll->setWidgetResizable(true);

    m_scrollHome = new QWidget();
    m_scrollLayout = new QVBoxLayout(m_scrollHome);
    m_scrollLayout->setContentsMargins(20, 0, 20, 20);
    m_scrollLayout->setSpacing(24);

    m_scrollLayout->addWidget(m_searchBarFrame);

    // Home board: continue-watching strip + N catalog rows (Phase 3 Batch 3.2).
    // Phase 2 Batch 2.2 — m_metaAggregator plumbed through so the
    // StreamContinueStrip can resolve next-unwatched episodes for series
    // whose most-recent progress entry is finished.
    m_homeBoard = new tankostream::stream::StreamHomeBoard(
        m_bridge, m_library, m_addonRegistry, m_metaAggregator, m_scrollHome);
    m_scrollLayout->addWidget(m_homeBoard);

    m_continueStrip = m_homeBoard->continueStrip();
    connect(m_continueStrip, &StreamContinueStrip::playRequested, this,
        [this](const QString& imdbId, int season, int episode) {
            QString mediaType = (season == 0 && episode == 0) ? "movie" : "series";
            // Continue-strip clicks used to open a modal dialog with sources;
            // under the inline-source-pane UX we have to navigate to the
            // detail view so the right-pane source list is visible when the
            // streams populate. Without this, clicking continue-watching
            // fires onPlayRequested → loads streams → populates m_detailView
            // which is not currently shown → user sees nothing.
            if (m_detailView && m_detailView->currentImdb() != imdbId) {
                m_detailView->showEntry(imdbId, season, episode);
            }
            m_mainStack->setCurrentIndex(1);
            // For movies, showEntry's movie-branch already emits playRequested
            // back into us — skip to avoid double-loading streams. For series,
            // showEntry waits on episode-click, so we explicitly fire with the
            // specific episode context from the continue-strip click.
            if (mediaType != QLatin1String("movie")) {
                onPlayRequested(imdbId, mediaType, season, episode);
            }
        });

    // Stream library UX rework 2026-04-15 — StreamHomeBoard no longer emits
    // metaActivated (featured-row tile clicks deleted). The only entry
    // points into showDetail for non-library titles are now:
    //   - StreamSearchWidget::metaActivated (search results)
    //   - CatalogBrowseScreen::metaActivated (explicit catalog browse)
    //   - StreamLibraryLayout / Continue strip for library-resident titles.

    m_homeBoard->refresh();

    // Library grid
    m_libraryLayout = new StreamLibraryLayout(m_bridge, m_library, m_scrollHome);
    m_libraryLayout->refresh();
    m_scrollLayout->addWidget(m_libraryLayout, 1);

    connect(m_library, &StreamLibrary::libraryChanged, m_libraryLayout, &StreamLibraryLayout::refresh);

    m_browseScroll->setWidget(m_scrollHome);
    layerLayout->addWidget(m_browseScroll, 1);

    // Search results overlay
    m_searchWidget = new StreamSearchWidget(m_metaAggregator, m_library, m_browseLayer);
    m_searchWidget->hide();
    layerLayout->addWidget(m_searchWidget, 1);

    connect(m_searchWidget, &StreamSearchWidget::backRequested, this, &StreamPage::showBrowse);
    connect(m_searchWidget, &StreamSearchWidget::libraryChanged, m_libraryLayout, &StreamLibraryLayout::refresh);
    // Phase 1 Batch 1.2 — search-tile click opens the detail view with the
    // result's MetaItemPreview. Previously the click toggled library add/
    // remove; that moved into the detail view's Add-to-Library button.
    connect(m_searchWidget, &StreamSearchWidget::metaActivated, this,
        [this](const tankostream::addon::MetaItemPreview& preview) {
            showDetail(preview);
        });
}

// ─── Search ──────────────────────────────────────────────────────────────────

void StreamPage::onSearchSubmit()
{
    QString query = m_searchInput->text().trimmed();
    if (query.isEmpty())
        return;

    if (m_searchDebounce) m_searchDebounce->stop();
    hideSearchHistoryDropdown();

    // Phase 4 Batch 4.3 — route URL-paste kinds to their action instead
    // of the search path. PasteKind is refreshed on every textChanged,
    // so by the time Enter / Search button fires it reflects the full
    // current input. Fresh re-detect here as a defensive sync in case
    // textChanged was skipped (clipboard paste + immediate Enter).
    const PasteKind kind = detectPasteKind(query);
    if (kind != PasteKind::None) {
        handlePasteAction(kind, query);
        return;
    }

    pushSearchHistory(query);
    m_browseScroll->hide();
    setSearchBusy(true);
    m_searchWidget->search(query);
}

// Phase 4 Batch 4.1 — live-search debounce + spinner handlers.

void StreamPage::onSearchTextChanged(const QString& text)
{
    const QString trimmed = text.trimmed();

    // Phase 4 Batch 4.3 — re-detect paste kind on every keystroke. The
    // Search button label reflects the intended action; Enter uses it.
    const PasteKind newKind = detectPasteKind(trimmed);
    if (newKind != m_pasteKind) {
        m_pasteKind = newKind;
        applyPasteKindToSearchButton(m_pasteKind);
    }

    if (trimmed.isEmpty()) {
        // Clearing restores the home/browse layer. Stop any pending debounce
        // so a stale deferred fire doesn't kick a search after the user
        // cleared the field intentionally. If the input is focused, show
        // the history dropdown (Batch 4.2).
        if (m_searchDebounce) m_searchDebounce->stop();
        setSearchBusy(false);
        if (m_searchWidget && m_searchWidget->isVisible()) {
            showBrowse();
        }
        if (m_searchInput && m_searchInput->hasFocus()) {
            showSearchHistoryDropdown();
        }
        return;
    }

    // Any non-empty text hides the history dropdown (Batch 4.2) — the user
    // is typing a new query, not browsing history.
    hideSearchHistoryDropdown();

    // Phase 4 Batch 4.3 — URL-paste kinds stop the live-search debounce.
    // The user's intent is an action (play/install), not a text search.
    // Enter or the repurposed Search button fires the action.
    if (m_pasteKind != PasteKind::None) {
        if (m_searchDebounce) m_searchDebounce->stop();
        return;
    }

    // <2 chars: don't fire yet, but don't revert either — user is mid-type.
    if (trimmed.length() < 2) {
        if (m_searchDebounce) m_searchDebounce->stop();
        return;
    }

    // Restart the debounce so only the latest pause in typing survives.
    if (m_searchDebounce) m_searchDebounce->start();
}

void StreamPage::onSearchDebounceFired()
{
    const QString query = m_searchInput ? m_searchInput->text().trimmed() : QString();
    if (query.length() < 2) return;   // defensive; user may have deleted
                                       // chars between textChanged and fire

    hideSearchHistoryDropdown();
    pushSearchHistory(query);
    m_browseScroll->hide();
    setSearchBusy(true);
    m_searchWidget->search(query);
}

void StreamPage::setSearchBusy(bool busy)
{
    if (!m_searchBusy) return;
    m_searchBusy->setVisible(busy);
}

// ─── Phase 4 Batch 4.3 — URL / magnet paste handling ─────────────────────────

StreamPage::PasteKind StreamPage::detectPasteKind(const QString& input) const
{
    const QString s = input.trimmed();
    if (s.isEmpty()) return PasteKind::None;

    // Magnet URI — regex anchored per RFC 2056 style: magnet:?xt=urn:btih:HASH
    static const QRegularExpression kMagnetRe(
        QStringLiteral("^magnet:\\?xt=urn:btih:[A-Za-z0-9]{32,}"),
        QRegularExpression::CaseInsensitiveOption);
    if (kMagnetRe.match(s).hasMatch()) return PasteKind::Magnet;

    // Stremio addon deep link.
    if (s.startsWith(QStringLiteral("stremio://"), Qt::CaseInsensitive))
        return PasteKind::AddonManifest;

    // HTTP / HTTPS — branch further by path shape.
    const QUrl url = QUrl::fromUserInput(s);
    if (!url.isValid() || url.scheme().isEmpty()) return PasteKind::None;
    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))
        return PasteKind::None;

    const QString path = url.path().toLower();

    // Addon manifest convention: URL ends in /manifest.json. Stremio
    // documents this explicitly for published addons.
    if (path.endsWith(QStringLiteral("/manifest.json")))
        return PasteKind::AddonManifest;

    // Direct-video: common container extensions. Matches the TODO's
    // enumeration (mp4, mkv, m3u8) plus a few common siblings.
    static const QStringList kVideoExts = {
        QStringLiteral(".mp4"), QStringLiteral(".m4v"),
        QStringLiteral(".mkv"), QStringLiteral(".webm"),
        QStringLiteral(".mov"), QStringLiteral(".avi"),
        QStringLiteral(".m3u8"), QStringLiteral(".ts"),
    };
    for (const QString& ext : kVideoExts) {
        if (path.endsWith(ext)) return PasteKind::DirectVideo;
    }

    // Plain HTTP without a recognized path shape — ambiguous between
    // "arbitrary search query that happens to look like a URL" and
    // "an unknown addon / media endpoint". Per the TODO's "only if the
    // full value is parseable as a URL — regex guarded" — we stay
    // conservative and treat unrecognized HTTP as a text search.
    return PasteKind::None;
}

void StreamPage::applyPasteKindToSearchButton(PasteKind kind)
{
    if (!m_searchBtn) return;
    switch (kind) {
        case PasteKind::Magnet:
        case PasteKind::DirectVideo:
            m_searchBtn->setText(tr("Play this stream"));
            break;
        case PasteKind::AddonManifest:
            m_searchBtn->setText(tr("Install this addon"));
            break;
        case PasteKind::None:
        default:
            m_searchBtn->setText(tr("Search"));
            break;
    }
}

void StreamPage::handlePasteAction(PasteKind kind, const QString& input)
{
    const QString s = input.trimmed();
    if (s.isEmpty()) return;

    switch (kind) {
        case PasteKind::AddonManifest: {
            // Pre-fill the URL into AddAddonDialog and exec(). Reusing the
            // dialog's existing install flow — install signals still fire
            // through AddonRegistry as in the regular "Addons → Add" path.
            if (!m_addonRegistry) return;
            AddAddonDialog dlg(m_addonRegistry, this);
            dlg.setPrefilledUrl(s);
            dlg.exec();
            // Clear the search field so the button label resets; also
            // clears the paste-kind state via the textChanged chain.
            if (m_searchInput) m_searchInput->clear();
            return;
        }
        case PasteKind::Magnet:
        case PasteKind::DirectVideo: {
            // Build a synthetic ad-hoc play through StreamPlayerController.
            // imdbId uses an "adhoc:" prefix so progress persistence keys
            // don't collide with real IMDB-indexed entries; mediaType is
            // "movie" because these are single-file plays without season
            // / episode context.
            if (!m_playerController) return;

            tankostream::addon::Stream stream;
            if (kind == PasteKind::Magnet) {
                // Extract the info-hash from the magnet URI. QUrl parses
                // "magnet:?xt=urn:btih:HASH" into the query — we walk it.
                const QUrl magnetUrl(s);
                QString hash;
                const auto queryItems =
                    QUrl::fromPercentEncoding(magnetUrl.query().toUtf8())
                        .split(QLatin1Char('&'));
                for (const QString& item : queryItems) {
                    if (item.startsWith(QStringLiteral("xt=urn:btih:"),
                                        Qt::CaseInsensitive)) {
                        hash = item.mid(QStringLiteral("xt=urn:btih:").length())
                                   .trimmed();
                        break;
                    }
                }
                if (hash.isEmpty()) return;
                stream.source = tankostream::addon::StreamSource::magnetSource(hash);
            } else {
                stream.source = tankostream::addon::StreamSource::httpSource(QUrl(s));
            }

            // Set m_session.pending so the existing onSourceActivated /
            // onReadyToPlay pipeline has the context it needs for progress
            // persistence + player wiring.
            PendingPlay p;
            p.imdbId    = QStringLiteral("adhoc:") + s.left(40);
            p.mediaType = QStringLiteral("movie");
            p.season    = 0;
            p.episode   = 0;
            p.epKey     = QStringLiteral("stream:") + p.imdbId;
            p.valid     = true;
            beginSession(p.epKey, p, QStringLiteral("magnet-paste"));

            m_mainStack->setCurrentIndex(2);
            m_bufferLabel->setText(tr("Connecting..."));
            m_bufferOverlay->show();

            m_playerController->startStream(p.imdbId, p.mediaType,
                                            p.season, p.episode, stream);

            if (m_searchInput) m_searchInput->clear();
            return;
        }
        case PasteKind::None:
        default:
            return;
    }
}

// ─── Phase 4 Batch 4.2 — search history ─────────────────────────────────────

void StreamPage::loadSearchHistory()
{
    QSettings s;
    m_searchHistory = s.value(QStringLiteral("stream/searchHistory")).toStringList();
    // Defensive clamp in case old settings data exceeds the cap.
    if (m_searchHistory.size() > kMaxSearchHistory)
        m_searchHistory = m_searchHistory.mid(0, kMaxSearchHistory);
}

void StreamPage::saveSearchHistory()
{
    QSettings s;
    s.setValue(QStringLiteral("stream/searchHistory"), m_searchHistory);
}

void StreamPage::pushSearchHistory(const QString& query)
{
    const QString q = query.trimmed();
    if (q.isEmpty()) return;
    // Dedup: remove any prior exact-match so the new one takes the top slot.
    m_searchHistory.removeAll(q);
    m_searchHistory.prepend(q);
    if (m_searchHistory.size() > kMaxSearchHistory)
        m_searchHistory = m_searchHistory.mid(0, kMaxSearchHistory);
    saveSearchHistory();
}

void StreamPage::removeSearchHistoryEntry(const QString& query)
{
    m_searchHistory.removeAll(query);
    saveSearchHistory();
    // If the dropdown is visible, rebuild its rows on the fly.
    if (m_searchHistoryDropdown && m_searchHistoryDropdown->isVisible()) {
        showSearchHistoryDropdown();   // rebuilds via the same code path
    }
}

void StreamPage::buildSearchHistoryDropdown()
{
    m_searchHistoryDropdown = new QFrame(this);
    m_searchHistoryDropdown->setObjectName("StreamSearchHistory");
    m_searchHistoryDropdown->setStyleSheet(
        "#StreamSearchHistory { background: #1b1b1b;"
        "  border: 1px solid rgba(255,255,255,0.14); border-radius: 6px; }");
    m_searchHistoryDropdown->hide();

    auto* outer = new QVBoxLayout(m_searchHistoryDropdown);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(0);

    m_searchHistoryList = new QWidget(m_searchHistoryDropdown);
    auto* listLayout = new QVBoxLayout(m_searchHistoryList);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);
    outer->addWidget(m_searchHistoryList);

    // Delayed-hide timer — gives click-on-row events time to register
    // before focus-out hides the dropdown.
    m_searchHistoryHideTimer = new QTimer(this);
    m_searchHistoryHideTimer->setSingleShot(true);
    m_searchHistoryHideTimer->setInterval(150);
    connect(m_searchHistoryHideTimer, &QTimer::timeout, this, [this]() {
        if (m_searchHistoryDropdown) m_searchHistoryDropdown->hide();
    });
}

void StreamPage::positionSearchHistoryDropdown()
{
    if (!m_searchHistoryDropdown || !m_searchInput) return;
    // Map the input's top-left in StreamPage coords, then position the
    // dropdown directly below. Width matches input; height is content-driven
    // with a reasonable max (10 rows × ~28px ≈ 280 + padding).
    const QPoint topLeft =
        m_searchInput->mapTo(this, QPoint(0, m_searchInput->height() + 2));
    m_searchHistoryDropdown->setGeometry(
        topLeft.x(), topLeft.y(), m_searchInput->width(),
        m_searchHistoryDropdown->sizeHint().height());
}

void StreamPage::showSearchHistoryDropdown()
{
    if (!m_searchHistoryDropdown || !m_searchHistoryList) return;
    if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->stop();

    // Clear old rows.
    auto* layout = qobject_cast<QVBoxLayout*>(m_searchHistoryList->layout());
    if (!layout) return;
    while (auto* item = layout->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }

    if (m_searchHistory.isEmpty()) {
        // No history yet — hide instead of showing an empty frame.
        m_searchHistoryDropdown->hide();
        return;
    }

    const int rows = qMin(m_searchHistory.size(), kDisplaySearchHistory);
    const char* kRowBtnStyle =
        "QPushButton { background: transparent; color: #d0d0d0; border: none;"
        "  text-align: left; padding: 6px 10px; font-size: 12px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.08); }";
    const char* kRemoveBtnStyle =
        "QPushButton { background: transparent; color: rgba(255,255,255,0.45);"
        "  border: none; font-size: 14px; padding: 0 10px; }"
        "QPushButton:hover { color: #fff; }";

    for (int i = 0; i < rows; ++i) {
        const QString q = m_searchHistory.at(i);

        auto* row = new QWidget(m_searchHistoryList);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(0);

        auto* queryBtn = new QPushButton(q, row);
        queryBtn->setCursor(Qt::PointingHandCursor);
        queryBtn->setStyleSheet(kRowBtnStyle);
        queryBtn->setFocusPolicy(Qt::NoFocus);
        connect(queryBtn, &QPushButton::clicked, this, [this, q]() {
            // Fill the input + fire the search immediately (Enter-equivalent
            // path). onSearchSubmit pushes to history (no-op on dedup) and
            // hides the dropdown.
            if (m_searchInput) {
                m_searchInput->setText(q);
            }
            onSearchSubmit();
        });
        rowLayout->addWidget(queryBtn, 1);

        auto* removeBtn = new QPushButton(QStringLiteral("\u00D7"), row);   // ×
        removeBtn->setCursor(Qt::PointingHandCursor);
        removeBtn->setStyleSheet(kRemoveBtnStyle);
        removeBtn->setFocusPolicy(Qt::NoFocus);
        removeBtn->setToolTip(tr("Remove from history"));
        connect(removeBtn, &QPushButton::clicked, this, [this, q]() {
            removeSearchHistoryEntry(q);
        });
        rowLayout->addWidget(removeBtn);

        layout->addWidget(row);
    }

    m_searchHistoryDropdown->adjustSize();
    positionSearchHistoryDropdown();
    m_searchHistoryDropdown->show();
    m_searchHistoryDropdown->raise();
}

void StreamPage::hideSearchHistoryDropdown()
{
    if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->stop();
    if (m_searchHistoryDropdown) m_searchHistoryDropdown->hide();
}

bool StreamPage::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_searchInput) {
        if (event->type() == QEvent::FocusIn) {
            // Show history only if input is empty — otherwise we'd obscure
            // the user's live typing with irrelevant past queries.
            if (m_searchInput->text().trimmed().isEmpty()) {
                showSearchHistoryDropdown();
            }
        } else if (event->type() == QEvent::FocusOut) {
            // Delayed hide — if focus moved to a dropdown row's button,
            // the click handler runs first via the 150ms timer.
            if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->start();
        }
    }
    return QWidget::eventFilter(obj, event);
}

void StreamPage::showBrowse()
{
    m_mainStack->setCurrentIndex(0);
    m_searchWidget->hide();
    m_browseScroll->show();
    // Phase 4 Batch 4.1 — defensive spinner reset when navigating away from
    // search (covers the "user hit Back / Esc while a search was in flight"
    // case — the later catalogResults lands but the UI is already elsewhere).
    setSearchBusy(false);
    // Phase 4 Batch 4.2 — defensive history-dropdown dismissal.
    hideSearchHistoryDropdown();
    // 2026-04-15 — cancel any pending seek-pre-gate retry. User navigated
    // away; no more launchPlayer should fire.
    m_session.seekRetry.reset();
    // Invalidate any in-flight play context so a late streamsReady arrival
    // followed by an accidental card-click can't dispatch playback for the
    // title the user just backed away from.
    m_session.pending.valid = false;
    cancelAutoLaunch();
    // Phase 2 Batch 2.5 — if a next-episode overlay was pending, clear it;
    // user explicitly navigated away.
    hideNextEpisodeOverlay();
    resetNextEpisodePrefetch();
    if (m_homeBoard)
        m_homeBoard->refresh();
    if (m_libraryLayout)
        m_libraryLayout->refresh();
}

void StreamPage::showAddonManager()
{
    cancelAutoLaunch();
    hideNextEpisodeOverlay();
    resetNextEpisodePrefetch();
    if (m_addonManager) {
        m_addonManager->refresh();
    }
    m_mainStack->setCurrentIndex(3);
}

void StreamPage::showCatalogBrowse(const QString& addonId, const QString& type,
                                   const QString& catalogId, const QString& /*title*/)
{
    if (!m_catalogBrowse) {
        return;
    }
    cancelAutoLaunch();
    hideNextEpisodeOverlay();
    resetNextEpisodePrefetch();
    m_catalogBrowse->open(addonId, type, catalogId);
    m_mainStack->setCurrentIndex(4);
}

void StreamPage::onCatalogBtnClicked()
{
    // Stream library UX rework 2026-04-15 — open catalog browse with no
    // preselection. CatalogBrowseScreen::open with empty args calls
    // selectAddon/selectCatalog with empty strings — the for-loop lookups
    // find no match, so the combos stay at index 0 (first addon, first
    // catalog) after rebuildSelectors populates them. reload() then kicks
    // a fetch for that default pair. On subsequent clicks, the screen
    // persists combo state via its own mechanisms so the user returns to
    // whatever they had selected.
    showCatalogBrowse(QString(), QString(), QString(), QString());
}

void StreamPage::showCalendar()
{
    if (!m_calendarScreen || !m_calendarEngine) return;
    cancelAutoLaunch();
    hideNextEpisodeOverlay();
    resetNextEpisodePrefetch();
    m_calendarScreen->setLoading(true);
    m_mainStack->setCurrentIndex(5);
    m_calendarEngine->loadUpcoming();
}

void StreamPage::showDetail(const QString& imdbId)
{
    // Idempotent: the library/home-board wires BOTH tileSingleClicked and
    // tileDoubleClicked to this slot, and Qt delivers BOTH for a double-click
    // gesture (first mousePress → single-click, then the second press becomes
    // a dedicated double-click event). The second call would otherwise reset
    // the detail state + re-fire the meta fetch just as the first call's
    // response lands.
    if (m_mainStack->currentIndex() == 1 && m_detailView
        && m_detailView->currentImdb() == imdbId) {
        return;
    }
    // Phase 2 Batch 2.4 — switching to a different detail entry clears any
    // in-flight auto-launch from the prior entry. Movie paths also hit this
    // via onPlayRequested's own cancelAutoLaunch, but series detail doesn't
    // auto-fire playRequested — need the cancel here for that case.
    cancelAutoLaunch();
    m_detailView->showEntry(imdbId);
    m_mainStack->setCurrentIndex(1);
}

void StreamPage::showDetail(const tankostream::addon::MetaItemPreview& preview,
                            int preselectSeason,
                            int preselectEpisode)
{
    // Phase 1 Batch 1.1: catalog/home/search tile path. Same idempotency
    // guard as the imdbId overload — repeat clicks on the same tile don't
    // re-fire state resets. The preview is passed through as `previewHint`
    // so the detail view paints title/year/poster/description from the
    // tile's metadata without needing the title to be in the library.
    if (preview.id.isEmpty()) return;
    if (m_mainStack->currentIndex() == 1 && m_detailView
        && m_detailView->currentImdb() == preview.id) {
        return;
    }
    cancelAutoLaunch();
    m_detailView->showEntry(preview.id, preselectSeason, preselectEpisode, preview);
    m_mainStack->setCurrentIndex(1);
}

void StreamPage::onPlayRequested(const QString& imdbId, const QString& mediaType,
                                  int season, int episode)
{
    // Build episode key for choice persistence
    QString epKey = (mediaType == "movie")
        ? StreamProgress::movieKey(imdbId)
        : StreamProgress::episodeKey(imdbId, season, episode);

    // Stash this play context for onSourceActivated to consume when the
    // user clicks a card. Replaces what the dialog used to keep alive
    // between exec() and accept(). 'valid' guards against late streamsReady
    // arrivals after the user backed out of the detail view. Phase 1 Batch
    // 1.3 — beginSession stamps the new generation + clears prior session
    // state (stopping the countdown timer + disconnecting aggregators) in
    // one boundary, so a detail re-entry mid-prefetch doesn't leak stale
    // async closures into the new session.
    beginSession(epKey,
                 PendingPlay{imdbId, mediaType, season, episode, epKey, true},
                 QStringLiteral("onPlayRequested"));

    // Check if we have a saved choice for this episode — if so, build the
    // picker-key shape so the StreamSourceList can highlight the matching
    // card on populate. (Auto-launch on saved choice lands in Batch 2.4; for
    // now the highlight is a one-click visual hint.)
    QJsonObject savedChoice = StreamChoices::loadChoice(epKey);
    QString savedChoiceKey;
    if (!savedChoice.isEmpty()) {
        const QString sourceKind = savedChoice.value("sourceKind").toString();
        const QString addonId    = savedChoice.value("addonId").toString();
        const QString hashOrUrl  = (sourceKind == QLatin1String("magnet"))
            ? savedChoice.value("infoHash").toString().toLower()
            : savedChoice.value("directUrl").toString();
        const int fileIndex = savedChoice.value("fileIndex").toInt(-1);
        savedChoiceKey = addonId + QLatin1Char('|')
                       + sourceKind + QLatin1Char('|')
                       + hashOrUrl + QLatin1Char('|')
                       + QString::number(fileIndex);
    }

    // Phase 2 Batch 2.3 — if per-episode is empty and this is a series, fall
    // through to the series-level bingeGroup match. Resolved inside the
    // streamsReady lambda because we need the incoming stream list to find
    // the matching card.
    QString seriesBingeGroup;
    qint64  seriesUpdatedAt = 0;
    if (savedChoiceKey.isEmpty()
        && mediaType == QLatin1String("series")) {
        const QJsonObject seriesChoice = StreamChoices::loadSeriesChoice(imdbId);
        seriesBingeGroup = seriesChoice.value("bingeGroup").toString();
        seriesUpdatedAt  = seriesChoice.value("updatedAt").toInteger(0);
    }

    // Phase 2 Batch 2.4 — auto-launch eligibility: either the per-episode
    // saved choice OR the per-series saved choice must be within 10 minutes
    // of the last-watched stamp. The streamsReady lambda uses this to decide
    // whether to fire the resume toast + arm m_autoLaunchTimer.
    constexpr qint64 kAutoLaunchWindowMs = 10LL * 60LL * 1000LL;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 episodeUpdatedAt = savedChoice.value("updatedAt").toInteger(0);
    const bool   perEpisodeRecent  = episodeUpdatedAt > 0
                                       && (nowMs - episodeUpdatedAt) < kAutoLaunchWindowMs;
    const bool   perSeriesRecent   = seriesUpdatedAt > 0
                                       && (nowMs - seriesUpdatedAt) < kAutoLaunchWindowMs;
    const bool   autoLaunchEligible = perEpisodeRecent || perSeriesRecent;

    // Reset any in-flight auto-launch before we kick off the new resolve —
    // a second onPlayRequested (user clicks another episode mid-countdown)
    // must replace, not stack.
    cancelAutoLaunch();

    // Fetch streams via StreamAggregator, then push into the right pane of
    // StreamDetailView. No modal — the cards live inside the detail view
    // and the user clicks one to play (handled by onSourceActivated).
    disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
               this, nullptr);

    connect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady, this,
        [this, savedChoiceKey, seriesBingeGroup, autoLaunchEligible](
            const QList<tankostream::addon::Stream>& streams,
            const QHash<QString, QString>& addonsById) {
            disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamsReady,
                       this, nullptr);

            const auto choices = tankostream::stream::buildPickerChoices(streams, addonsById);

            // Resolve the highlight key + retain a pointer to the matched
            // choice for Batch 2.4's auto-launch path. Per-episode wins;
            // fall through to series-level bingeGroup match.
            QString highlightKey = savedChoiceKey;
            const tankostream::stream::StreamPickerChoice* matchedChoice = nullptr;
            if (!highlightKey.isEmpty()) {
                for (const auto& c : choices) {
                    if (tankostream::stream::pickerChoiceKey(c) == highlightKey) {
                        matchedChoice = &c;
                        break;
                    }
                }
            }
            if (!matchedChoice && !seriesBingeGroup.isEmpty()) {
                for (const auto& c : choices) {
                    if (c.stream.behaviorHints.bingeGroup == seriesBingeGroup) {
                        highlightKey   = tankostream::stream::pickerChoiceKey(c);
                        matchedChoice  = &c;
                        break;
                    }
                }
            }

            if (m_detailView) {
                m_detailView->setStreamSources(choices, highlightKey);
            }

            // Phase 2 Batch 2.4 — auto-launch DISABLED 2026-04-16 per
            // Hemanth UX call (Phase 1 telemetry session, post One Piece
            // pack regression). The 2-second countdown was too aggressive —
            // entering Sources view would fire playback before the user
            // could meaningfully pick a different source. Manual source
            // selection (user clicks a card) still works via the existing
            // setStreamSources path above. The 10-minute eligibility gate +
            // m_autoLaunchTimer infrastructure are preserved in case future
            // UX iteration wants a longer countdown variant; one-line
            // re-enable point is the `if (false &&` guard below — flip to
            // restore (with a kAutoLaunchCountdownMs bump in the timer
            // setInterval at the top of buildUI before re-enabling).
            //
            // Suppressed variables keep clean shape for the re-enable diff:
            (void)matchedChoice;
            (void)autoLaunchEligible;
            if (false && matchedChoice && autoLaunchEligible && m_detailView) {
                m_autoLaunchChoice = *matchedChoice;
                m_detailView->showAutoLaunchToast(
                    tr("Resuming with last-used source..."));
                if (m_autoLaunchTimer) m_autoLaunchTimer->start();
            }
        });

    tankostream::stream::StreamLoadRequest req;
    req.type = (mediaType == "movie") ? QStringLiteral("movie") : QStringLiteral("series");
    req.id = (mediaType == "movie")
                 ? imdbId
                 : imdbId + QLatin1Char(':') + QString::number(qMax(1, season))
                          + QLatin1Char(':') + QString::number(qMax(1, episode));
    disconnect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamError,
               this, nullptr);
    connect(m_streamAggregator, &tankostream::stream::StreamAggregator::streamError, this,
        [this](const QString& addonId, const QString& message) {
            const QString shown = addonId.isEmpty()
                ? message
                : QStringLiteral("[%1] %2").arg(addonId, message);
            if (m_detailView) {
                m_detailView->setStreamSourcesError(
                    QStringLiteral("Failed to fetch sources: ") + shown);
            }
        });

    m_streamAggregator->load(req);
}

// Phase 2 Batch 2.4 — auto-launch orchestration.

void StreamPage::onAutoLaunchFire()
{
    if (m_detailView) m_detailView->hideAutoLaunchToast();
    if (!m_autoLaunchChoice.has_value()) return;
    const auto choice = *m_autoLaunchChoice;
    m_autoLaunchChoice.reset();
    // Same entry point user-click takes — keeps persistence + handoff
    // behavior identical between manual and auto-launch flows.
    onSourceActivated(choice);
}

void StreamPage::cancelAutoLaunch()
{
    if (m_autoLaunchTimer) m_autoLaunchTimer->stop();
    m_autoLaunchChoice.reset();
    if (m_detailView) m_detailView->hideAutoLaunchToast();
}

// Phase 2 Batch 2.5 — next-episode pre-fetch + overlay orchestration.

void StreamPage::startNextEpisodePrefetch(const QString& imdbId,
                                           int currentSeason, int currentEpisode)
{
    // Skip movies — no next episode concept.
    if (currentSeason <= 0 || currentEpisode <= 0) return;
    if (!m_metaAggregator || !m_streamAggregator) return;

    // Use the cached series meta (populated when the user opened detail view
    // for this series). fetchSeriesMeta emits synchronously on cache hit.
    // Re-wire once per fetch to avoid leaking stale connections.
    disconnect(m_metaAggregator,
               &tankostream::stream::MetaAggregator::seriesMetaReady,
               this, nullptr);
    connect(m_metaAggregator,
            &tankostream::stream::MetaAggregator::seriesMetaReady,
            this,
        [this, imdbId, currentSeason, currentEpisode](
            const QString& resolvedImdb,
            const QMap<int, QList<tankostream::stream::StreamEpisode>>& seasons) {
            if (resolvedImdb != imdbId) return;
            // One-shot: disconnect after resolving for this imdb.
            disconnect(m_metaAggregator,
                       &tankostream::stream::MetaAggregator::seriesMetaReady,
                       this, nullptr);

            // Flatten + sort.
            QList<QPair<int, int>> episodesInOrder;
            for (auto it = seasons.constBegin(); it != seasons.constEnd(); ++it) {
                for (const auto& ep : it.value()) {
                    episodesInOrder.append({it.key(), ep.episode});
                }
            }
            std::sort(episodesInOrder.begin(), episodesInOrder.end());

            // Build a synthetic allProgress that marks the current episode as
            // finished, so nextUnwatchedEpisode correctly advances past it
            // even though we're only at 95% (the real "finished" stamp
            // arrives at user-close or at 90% boundary, but we want the
            // prefetch to fire NOW).
            QJsonObject allProgress = m_bridge->allProgress("stream");
            const QString curKey = StreamProgress::episodeKey(
                imdbId, currentSeason, currentEpisode);
            QJsonObject curState = allProgress.value(curKey).toObject();
            curState["finished"] = true;
            allProgress[curKey] = curState;
            StreamProgress::invalidateNextUnwatchedCache(imdbId);

            const QPair<int, int> next =
                StreamProgress::nextUnwatchedEpisode(imdbId, episodesInOrder, allProgress);
            if (next.first <= 0 || next.second <= 0) return;  // series finished

            // Stash the prefetch context and fire a stream load.
            NextEpisodePrefetch prefetch;
            prefetch.imdbId   = imdbId;
            prefetch.season   = next.first;
            prefetch.episode  = next.second;
            prefetch.epKey    = StreamProgress::episodeKey(imdbId, next.first, next.second);
            m_session.nextPrefetch    = prefetch;

            // Reuse m_streamAggregator — the current episode's streamsReady
            // has already fired (we're at 95%), so nothing in-flight. Its
            // load() resets internal state cleanly.
            disconnect(m_streamAggregator,
                       &tankostream::stream::StreamAggregator::streamsReady,
                       this, nullptr);
            connect(m_streamAggregator,
                    &tankostream::stream::StreamAggregator::streamsReady, this,
                [this](const QList<tankostream::addon::Stream>& streams,
                       const QHash<QString, QString>& addonsById) {
                    disconnect(m_streamAggregator,
                               &tankostream::stream::StreamAggregator::streamsReady,
                               this, nullptr);
                    onNextEpisodePrefetchStreams(streams, addonsById);
                });

            tankostream::stream::StreamLoadRequest req;
            req.type = QStringLiteral("series");
            req.id   = imdbId + QLatin1Char(':') + QString::number(qMax(1, next.first))
                              + QLatin1Char(':') + QString::number(qMax(1, next.second));
            m_streamAggregator->load(req);
        });

    m_metaAggregator->fetchSeriesMeta(imdbId);
}

void StreamPage::onNextEpisodePrefetchStreams(
    const QList<tankostream::addon::Stream>& streams,
    const QHash<QString, QString>& addonsById)
{
    if (!m_session.nextPrefetch.has_value()) return;

    const auto choices = tankostream::stream::buildPickerChoices(streams, addonsById);
    if (choices.isEmpty()) return;

    // Match priority: per-episode saved choice > per-series bingeGroup.
    // We only fire auto-play for the next episode when one of these matches;
    // the overlay won't show otherwise.
    const QJsonObject epChoice = StreamChoices::loadChoice(m_session.nextPrefetch->epKey);
    QString epChoiceKey;
    if (!epChoice.isEmpty()) {
        const QString sourceKind = epChoice.value("sourceKind").toString();
        const QString addonId    = epChoice.value("addonId").toString();
        const QString hashOrUrl  = (sourceKind == QLatin1String("magnet"))
            ? epChoice.value("infoHash").toString().toLower()
            : epChoice.value("directUrl").toString();
        const int fileIndex = epChoice.value("fileIndex").toInt(-1);
        epChoiceKey = addonId + QLatin1Char('|') + sourceKind + QLatin1Char('|')
                    + hashOrUrl + QLatin1Char('|') + QString::number(fileIndex);
    }

    const QJsonObject seriesChoice = StreamChoices::loadSeriesChoice(m_session.nextPrefetch->imdbId);
    const QString seriesBingeGroup = seriesChoice.value("bingeGroup").toString();

    for (const auto& c : choices) {
        const bool epMatch = !epChoiceKey.isEmpty()
                           && tankostream::stream::pickerChoiceKey(c) == epChoiceKey;
        const bool seriesMatch = !seriesBingeGroup.isEmpty()
                              && c.stream.behaviorHints.bingeGroup == seriesBingeGroup;
        if (epMatch || seriesMatch) {
            m_session.nextPrefetch->matchedChoice = c;
            break;
        }
    }
    m_session.nextPrefetch->streamsLoaded = true;

    // Phase 2 Batch 2.6 — Shift+N path: if the user fired the shortcut
    // while prefetch was still resolving, auto-play the moment a match
    // lands (no overlay, no countdown). No match → silent no-op per
    // TODO "No-op if no next episode".
    if (m_session.nextShortcutPending) {
        m_session.nextShortcutPending = false;
        if (m_session.nextPrefetch->matchedChoice.has_value()) {
            onNextEpisodePlayNow();
        }
    }
}

void StreamPage::showNextEpisodeOverlay()
{
    if (!m_nextEpisodeOverlay || !m_session.nextPrefetch.has_value()) return;

    const QString seriesName = m_library
        ? m_library->get(m_session.nextPrefetch->imdbId).name
        : QString();
    const QString label = seriesName.isEmpty()
        ? QStringLiteral("S%1E%2")
              .arg(m_session.nextPrefetch->season, 2, 10, QChar('0'))
              .arg(m_session.nextPrefetch->episode, 2, 10, QChar('0'))
        : seriesName + QStringLiteral(" \u00B7 ")
              + QStringLiteral("S%1E%2")
                    .arg(m_session.nextPrefetch->season, 2, 10, QChar('0'))
                    .arg(m_session.nextPrefetch->episode, 2, 10, QChar('0'));

    if (m_nextEpisodeTitleLabel) {
        m_nextEpisodeTitleLabel->setText(tr("Up next: ") + label);
    }

    m_nextEpisodeCountdownSec = 10;
    if (m_nextEpisodeCountdownLabel) {
        m_nextEpisodeCountdownLabel->setText(
            tr("Playing in %1s...").arg(m_nextEpisodeCountdownSec));
    }

    m_bufferOverlay->hide();
    m_nextEpisodeOverlay->show();
    m_mainStack->setCurrentIndex(2);

    if (m_nextEpisodeCountdownTimer) m_nextEpisodeCountdownTimer->start();
}

void StreamPage::hideNextEpisodeOverlay()
{
    if (m_nextEpisodeCountdownTimer) m_nextEpisodeCountdownTimer->stop();
    if (m_nextEpisodeOverlay) m_nextEpisodeOverlay->hide();
}

void StreamPage::onNextEpisodeCountdownTick()
{
    --m_nextEpisodeCountdownSec;
    if (m_nextEpisodeCountdownSec <= 0) {
        onNextEpisodePlayNow();
        return;
    }
    if (m_nextEpisodeCountdownLabel) {
        m_nextEpisodeCountdownLabel->setText(
            tr("Playing in %1s...").arg(m_nextEpisodeCountdownSec));
    }
}

void StreamPage::onNextEpisodePlayNow()
{
    if (!m_session.nextPrefetch.has_value() || !m_session.nextPrefetch->matchedChoice.has_value()) {
        // Defensive: no prefetch available. Treat as cancel.
        onNextEpisodeCancel();
        return;
    }

    const auto choice = *m_session.nextPrefetch->matchedChoice;
    const auto prefetchCopy = *m_session.nextPrefetch;

    hideNextEpisodeOverlay();

    // Populate m_session.pending so onSourceActivated knows which episode
    // this is — same shape onPlayRequested would have produced, minus the
    // streams fan-out round-trip (we already have the matched choice).
    // Batch 1.3 — route through beginSession so the new episode's session
    // gets a fresh generation + prior session's async closures are aborted
    // at the boundary instead of firing against the new epKey.
    PendingPlay p;
    p.imdbId    = prefetchCopy.imdbId;
    p.mediaType = QStringLiteral("series");
    p.season    = prefetchCopy.season;
    p.episode   = prefetchCopy.episode;
    p.epKey     = prefetchCopy.epKey;
    p.valid     = true;
    beginSession(p.epKey, p, QStringLiteral("nextEpisodePlayNow"));

    // Reset prefetch + near-end so the next episode's playback can re-
    // prefetch when it approaches its own end.
    resetNextEpisodePrefetch();

    // Drive through the canonical source-activation path — same persistence,
    // subtitle fan-out, and player handoff the user-click flow uses.
    onSourceActivated(choice);
}

void StreamPage::onNextEpisodeCancel()
{
    hideNextEpisodeOverlay();
    resetNextEpisodePrefetch();
    // Stream was already stopped in closeRequested. Just return to browse.
    showBrowse();
}

void StreamPage::resetNextEpisodePrefetch()
{
    if (m_nextEpisodeCountdownTimer) m_nextEpisodeCountdownTimer->stop();
    m_session.nextPrefetch.reset();
    m_session.nearEndCrossed = false;
    m_session.nextShortcutPending = false;
    // Drop any in-flight prefetch connections from MetaAggregator /
    // StreamAggregator — we reset them on each prefetch start anyway, but
    // safer to clear when fully canceling so stale lambdas don't accumulate.
    if (m_metaAggregator) {
        disconnect(m_metaAggregator,
                   &tankostream::stream::MetaAggregator::seriesMetaReady,
                   this, nullptr);
    }
    if (m_streamAggregator) {
        disconnect(m_streamAggregator,
                   &tankostream::stream::StreamAggregator::streamsReady,
                   this, nullptr);
    }
}

// STREAM_LIFECYCLE_FIX Phase 1 Batches 1.1 + 1.2 + 1.3 — PlaybackSession
// foundation + full migration. 1.1 introduced the struct + generation
// counter + beginSession/resetSession API. 1.2 migrated `_currentEpKey` +
// m_pendingPlay + m_lastDeadlineUpdateMs. 1.3 migrated m_session.nextPrefetch +
// m_session.nearEndCrossed + m_session.nextShortcutPending + m_seekRetryState (raw QObject*
// identity-token pattern replaced with generation-check — first real
// consumer of currentGeneration()/isCurrentGeneration()). 1.3 also fleshed
// SeekRetryState, added reason param + wrap-guard + begin-log on
// beginSession, and wired beginSession into the 4 session-start sites
// (trailer paste, magnet paste, onPlayRequested, onNextEpisodePlayNow).
// Phase 1 CLOSED at 1.3. Prototype credit:
// agents/prototypes/stream_lifecycle/Batch1.1_PlaybackSession_struct_API.cpp
// (Agent 7, Codex) — shape adopted as-is modulo file-style conventions.

quint64 StreamPage::currentGeneration() const
{
    return m_session.generation;
}

bool StreamPage::isCurrentGeneration(quint64 gen) const
{
    return gen != 0 && gen == m_session.generation;
}

quint64 StreamPage::beginSession(const QString& epKey, const PendingPlay& pending,
                                 const QString& reason)
{
    resetSession(reason.isEmpty()
                     ? QStringLiteral("beginSession")
                     : QStringLiteral("beginSession:%1").arg(reason));

    // Defensive wrap guard (prototype shape). quint64 doesn't wrap in
    // practical lifetime, but if m_nextGeneration ever landed at 0 we'd
    // stamp m_session.generation = 0 which is the "no-session" sentinel —
    // breaking isValid() / isCurrentGeneration() contract silently.
    if (m_nextGeneration == 0) m_nextGeneration = 1;

    m_session.generation = m_nextGeneration++;
    m_session.epKey      = epKey;
    m_session.pending    = pending;

    qInfo().noquote() << QStringLiteral("[stream-session] begin: gen=%1 epKey=%2")
                             .arg(m_session.generation).arg(epKey);

    return m_session.generation;
}

void StreamPage::resetSession(const QString& reason)
{
    // Same teardown shape as resetNextEpisodePrefetch (countdown timer stop +
    // prefetch aggregator disconnect) plus a full state clear. Kept as a pure
    // boundary: no showBrowse(), no signal emits, no player touches — callers
    // decide what UI follows. Matches audit advisory #1's "single boundary"
    // shape so every scattered inline reset can funnel here.
    if (m_nextEpisodeCountdownTimer) m_nextEpisodeCountdownTimer->stop();
    if (m_metaAggregator) {
        disconnect(m_metaAggregator,
                   &tankostream::stream::MetaAggregator::seriesMetaReady,
                   this, nullptr);
    }
    if (m_streamAggregator) {
        disconnect(m_streamAggregator,
                   &tankostream::stream::StreamAggregator::streamsReady,
                   this, nullptr);
    }
    m_session = PlaybackSession{};
    qInfo().noquote() << QStringLiteral("[stream-session] reset: reason=%1")
                             .arg(reason.isEmpty() ? QStringLiteral("unspecified")
                                                    : reason);
}

void StreamPage::onStreamNextEpisodeShortcut()
{
    // STREAM_LIFECYCLE_FIX Phase 4 Batch 4.1 — audit P2-3 close. Pre-4.1 guard
    // used `m_session.pending.valid` which onSourceActivated clears before
    // playback starts (pending is consumed as the session installs) — making
    // Shift+N a silent no-op during actual playback. Correct identity signals:
    //   (a) m_session.isValid() — active session (generation != 0 AND
    //       epKey non-empty). Holds true from beginSession through
    //       resetSession/next beginSession, spans the entire playback.
    //   (b) pending.mediaType == "series" — filter out movies / trailers /
    //       adhoc URLs where "next episode" has no meaning.
    //   (c) m_playerController->isActive() — sanity check; even if m_session
    //       reports valid, the player may be between states (buffering, seek
    //       retry). isActive gates us to "playback path committed."
    // Unblocks STREAM_UX_PARITY Batch 2.6 (Shift+N player shortcut) — previously
    // would land on this silent-no-op guard. Post-4.1, Shift+N fires the
    // next-episode flow as intended.
    if (!m_session.isValid()) return;
    if (m_session.pending.mediaType != QStringLiteral("series")) return;
    if (!m_playerController || !m_playerController->isActive()) return;
    if (m_session.pending.imdbId.isEmpty()) return;  // defensive; isValid implies epKey non-empty but imdbId is a separate field

    // Already resolved (user crossed 95% earlier in this playback). Skip
    // the countdown and play immediately.
    if (m_session.nextPrefetch.has_value()
        && m_session.nextPrefetch->matchedChoice.has_value())
    {
        onNextEpisodePlayNow();
        return;
    }

    // Prefetch already in flight from near-end trigger — mark shortcut
    // pending so onNextEpisodePrefetchStreams auto-plays once match lands.
    if (m_session.nextPrefetch.has_value() && !m_session.nextPrefetch->streamsLoaded) {
        m_session.nextShortcutPending = true;
        return;
    }

    // Cold path — no prefetch yet. Kick one off for the current episode
    // and arm the shortcut-pending flag. startNextEpisodePrefetch reuses
    // the series-meta → next-unwatched resolve path from Batch 2.5;
    // if the series has no next unwatched episode, matchedChoice never
    // lands and the shortcut falls through to no-op silently.
    m_session.nextShortcutPending = true;
    startNextEpisodePrefetch(m_session.pending.imdbId,
                             m_session.pending.season,
                             m_session.pending.episode);
}

void StreamPage::onSourceActivated(const tankostream::stream::StreamPickerChoice& choice)
{
    if (!m_session.pending.valid) return;   // late click after the user backed out

    // Phase 2 Batch 2.4 — if the user clicked a source card manually during
    // the auto-launch window, cancel the pending timer + hide the toast.
    // onAutoLaunchFire calls back into us for the automated path, but by the
    // time we're here the timer should already be stopped — this is the
    // user-click entry.
    if (m_autoLaunchTimer && m_autoLaunchTimer->isActive()) {
        cancelAutoLaunch();
    }

    const PendingPlay ctx = m_session.pending;
    m_session.pending.valid = false;

    // STREAM_LIFECYCLE_FIX Phase 4 Batch 4.2 — audit P2-2 close. Pre-4.2 code
    // reset only m_session.nearEndCrossed + m_session.nextPrefetch inline,
    // skipping m_session.nextShortcutPending + the MetaAggregator/StreamAggregator
    // disconnect logic. resetNextEpisodePrefetch() is the canonical cleanup
    // that Phase 1 Batch 1.3 migrated to route through m_session — using it
    // here ensures all three prefetch-related session fields clear uniformly
    // AND in-flight aggregator connections from the prior episode's prefetch
    // get dropped (preventing a stale streamsReady lambda from landing against
    // the NEW episode's prefetch slot with the old episode's stream list).
    // Note: resetSession() would be over-broad — it also clears m_session.epKey
    // and pending, which onSourceActivated is about to re-install via ctx. The
    // narrower resetNextEpisodePrefetch is the right choice.
    resetNextEpisodePrefetch();

    // Save choice for re-use — extended payload with addon + source-kind fields.
    // Same persistence shape the dialog used to write so loadChoice in
    // future onPlayRequested calls keeps working.
    QJsonObject saved;
    saved["sourceKind"]    = choice.sourceKind;
    saved["addonId"]       = choice.addonId;
    saved["addonName"]     = choice.addonName;
    saved["magnetUri"]     = choice.magnetUri;
    saved["infoHash"]      = choice.infoHash;
    saved["fileIndex"]     = choice.fileIndex;
    saved["fileNameHint"]  = choice.fileNameHint;
    saved["directUrl"]     = choice.stream.source.url.toString();
    saved["youtubeId"]     = choice.stream.source.youtubeId;
    saved["quality"]       = choice.stream.behaviorHints.other
                                 .value("qualityLabel").toString();
    saved["trackerSource"] = choice.stream.behaviorHints.other
                                 .value("trackerSource").toString();
    saved["bingeGroup"]    = choice.stream.behaviorHints.bingeGroup;
    // Phase 2 Batch 2.4 — stamp the "last-used" timestamp so the auto-launch
    // 10-minute gate has a read source for both per-episode and per-series
    // resume paths.
    saved["updatedAt"]     = QDateTime::currentMSecsSinceEpoch();
    StreamChoices::saveChoice(ctx.epKey, saved);

    // Phase 2 Batch 2.3 — series-level source memory. When the addon
    // declares `behaviorHints.bingeGroup` on this stream, persist a parallel
    // per-series entry so the next episode's picker can default-highlight a
    // matching source from the same release group. Movies don't need this
    // layer — per-movie saveChoice covers single-title state already.
    if (ctx.mediaType == QLatin1String("series")
        && !choice.stream.behaviorHints.bingeGroup.isEmpty()) {
        StreamChoices::saveSeriesChoice(ctx.imdbId, saved);
    }

    m_session.epKey = ctx.epKey;

    m_mainStack->setCurrentIndex(2);
    m_bufferLabel->setText("Connecting...");
    m_bufferOverlay->show();

    // Batch 5.3 — fan out a subtitle request for the selected stream in
    // parallel with playback prep. Result lands in the SubtitleMenu via the
    // subtitlesReady connection wired in the ctor.
    tankostream::stream::SubtitleLoadRequest subReq;
    subReq.type = (ctx.mediaType == "movie")
                      ? QStringLiteral("movie")
                      : QStringLiteral("series");
    subReq.id = (ctx.mediaType == "movie")
                      ? ctx.imdbId
                      : ctx.imdbId + QLatin1Char(':')
                                   + QString::number(qMax(1, ctx.season))
                                   + QLatin1Char(':')
                                   + QString::number(qMax(1, ctx.episode));
    subReq.selectedStream = choice.stream;
    m_subtitlesAggregator->load(subReq);

    // Phase 4.3: controller dispatches by source.kind.
    m_playerController->startStream(
        ctx.imdbId, ctx.mediaType, ctx.season, ctx.episode, choice.stream);
}

// ─── Player controller signals ───────────────────────────────────────────────

void StreamPage::onBufferUpdate(const QString& statusText, double /*percent*/)
{
    m_bufferLabel->setText(statusText);
}

void StreamPage::onReadyToPlay(const QString& httpUrl)
{
    m_bufferOverlay->hide();

    // Find the VideoPlayer in the widget hierarchy (created by MainWindow)
    // and open the stream URL as a file — the sidecar handles HTTP URLs
    auto* mainWin = window();
    if (!mainWin) return;

    auto* player = mainWin->findChild<VideoPlayer*>();
    if (!player) return;

    // Save progress on player progress updates
    disconnect(player, &VideoPlayer::progressUpdated, this, nullptr);
    disconnect(player, &VideoPlayer::closeRequested, this, nullptr);
    disconnect(player, &VideoPlayer::streamNextEpisodeRequested, this, nullptr);

    // Capture current stream info for progress saving
    QString imdbId = m_playerController->property("_imdbId").toString();
    // Use the controller's stored state instead
    connect(player, &VideoPlayer::progressUpdated, this,
        [this](const QString& /*path*/, double posSec, double durSec) {
            QString epKey = m_session.epKey;
            if (epKey.isEmpty()) return;
            // Only save once real playback has started — ignore probe/initial 0-value updates
            if (posSec < 5.0 || durSec <= 0.0) return;

            bool finished = (durSec > 0 && posSec / durSec >= 0.9);
            QJsonObject state = StreamProgress::makeWatchState(posSec, durSec, finished);
            m_bridge->saveProgress("stream", epKey, state);

            // STREAM_PLAYBACK_FIX Phase 2 Batch 2.3 — sliding-window deadline
            // retargeting. Rate-limited to once per 2s so libtorrent's
            // deadline table doesn't churn on every progress tick.
            // StreamEngine handles the byte-offset math + piece lookup; we
            // just gate + forward.
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (nowMs - m_session.lastDeadlineUpdateMs >= 2000) {
                m_session.lastDeadlineUpdateMs = nowMs;
                const QString infoHash = m_playerController
                    ? m_playerController->currentInfoHash()
                    : QString();
                if (!infoHash.isEmpty() && m_streamEngine) {
                    m_streamEngine->updatePlaybackWindow(infoHash, posSec, durSec);
                }
            }

            // Phase 2 Batch 2.5 — near-end detection: at 95% OR within 60s
            // of duration, kick off pre-fetch of the next unwatched episode.
            // `m_session.nearEndCrossed` guards the fire-once semantic; reset in
            // onSourceActivated when a new playback starts.
            if (!m_session.nearEndCrossed && durSec > 0) {
                const double pct       = posSec / durSec;
                const double remaining = durSec - posSec;
                if (pct >= 0.95 || remaining <= 60.0) {
                    m_session.nearEndCrossed = true;
                    // Parse epKey to extract (imdbId, season, episode). Format:
                    //   "stream:ttXXXXXXX:s{n}:e{m}"   (series)
                    //   "stream:ttXXXXXXX"              (movie — no next)
                    const QStringList parts = epKey.split(':');
                    if (parts.size() >= 4 && parts[0] == QLatin1String("stream")) {
                        const QString imdbId = parts[1];
                        const int season  = parts[2].mid(1).toInt();
                        const int episode = parts[3].mid(1).toInt();
                        startNextEpisodePrefetch(imdbId, season, episode);
                    }
                }
            }
        });

    // On player close → stop stream, clear progress key, refresh continue strip.
    // Also reset persistence mode so next Videos-mode playback writes to the
    // "videos" store as expected (pairs with the None set before openFile).
    // Phase 2 Batch 2.5 — if the user crossed the 95% near-end threshold AND
    // the pre-fetch landed a matched next-episode source, show the next-
    // episode overlay instead of returning to browse. User can accept
    // (Play Now / countdown) to binge OR Cancel to end the session.
    // Phase 2 Batch 2.6 — Shift+N manual next-episode shortcut. Replaces
    // any stale connection from a prior stream session so the handler
    // fires exactly once per key press.
    disconnect(player, &VideoPlayer::streamNextEpisodeRequested, this, nullptr);
    connect(player, &VideoPlayer::streamNextEpisodeRequested,
            this, &StreamPage::onStreamNextEpisodeShortcut,
            Qt::UniqueConnection);

    connect(player, &VideoPlayer::closeRequested, this, [this, player]() {
        m_session.epKey.clear();
        disconnect(player, &VideoPlayer::progressUpdated, this, nullptr);
        disconnect(player, &VideoPlayer::streamNextEpisodeRequested, this, nullptr);
        player->setPersistenceMode(VideoPlayer::PersistenceMode::LibraryVideos);

        // Phase 2 Batch 2.5 — overlay must be shown BEFORE stopStream. The
        // streamStopped signal fires synchronously via direct-connect to
        // onStreamStopped, which calls showBrowse unless the overlay is
        // already visible. Reversing the order would race-condition the
        // overlay off-screen on the browse layer.
        const bool overlayEligible = m_session.nearEndCrossed
                                      && m_session.nextPrefetch.has_value()
                                      && m_session.nextPrefetch->matchedChoice.has_value();
        if (overlayEligible) {
            player->hide();
            showNextEpisodeOverlay();
        }
        m_playerController->stopStream();
        m_continueStrip->refresh();
        m_libraryLayout->refresh();
    });

    // Stream-mode playback: suppress VideoPlayer's internal "videos"-domain
    // bridge reads/writes. Progress still flows via the progressUpdated →
    // saveProgress("stream", epKey, state) lambda above. Fixes the stream →
    // videos continue-watching leak routed by Agent 0 at chat.md:9661.
    player->setPersistenceMode(VideoPlayer::PersistenceMode::None);

    // Phase 1 Batch 1.3 (STREAM_UX_PARITY) — read the stream-domain saved
    // progress for this episode/movie and pass it to openFile so playback
    // resumes at the saved offset. PersistenceMode::None suppresses the
    // player's own "videos"-domain resume lookup; caller-supplied seconds
    // take that slot. Gated on the same "not near end" rule VideoPlayer uses
    // for its own resume (avoid resuming a title the user effectively
    // finished).
    double streamResumeSec = 0.0;
    double streamSavedDur  = 0.0;
    const QString epKey = m_session.epKey;
    if (!epKey.isEmpty() && m_bridge) {
        const QJsonObject prog = m_bridge->progress("stream", epKey);
        const double savedPos = prog.value("positionSec").toDouble(0.0);
        const double savedDur = prog.value("durationSec").toDouble(0.0);
        if (savedPos > 2.0 && savedDur > 0 && savedPos < savedDur * 0.95) {
            streamResumeSec = savedPos;
            streamSavedDur  = savedDur;
        }
    }

    // STREAM_PLAYBACK_FIX Phase 2 Batch 2.4 — seek/resume target pre-gate.
    //
    // Without pre-fetch: handing a resume offset of e.g. 47:00 on a
    // half-watched title causes ffmpeg to issue an HTTP range request
    // deep into the file immediately. The HTTP server's waitForPieces
    // blocks up to 15s waiting for those pieces, times out, the sidecar
    // retries, pieces still missing, and the user sees repeated buffering
    // cycles or a failed resume.
    //
    // With pre-fetch: before launching the player, fire urgent deadlines
    // on the target window via StreamEngine::prepareSeekTarget and poll
    // contiguous-bytes availability every 300ms. Launch the player only
    // when the first 3 MB around the target are contiguous OR after a
    // 9s cap (at which point we proceed anyway — the in-player buffering
    // path from Batch 1.2 handles residual delay gracefully).
    //
    // Zero-resume path (streamResumeSec == 0.0) bypasses the pre-gate —
    // the Batch 2.2 head deadline already covers byte-offset 0.
    // 2026-04-15 fix-up — always cancel any outstanding seek-retry state
    // at the top of onReadyToPlay. Every path below either launches the
    // player synchronously (and the orphan retry must not fire after) or
    // sets up a fresh retry state. One-shot invalidation here covers all
    // exit paths without scattering the cancel call. (Batch 1.3: the
    // generation-check in the retry closure below would also abort any
    // orphan from a prior session, but clearing here is still valuable for
    // same-session re-entries like an immediate re-open of the same URL.)
    m_session.seekRetry.reset();

    auto launchPlayer = [this, player, httpUrl, streamResumeSec, mainWin]() {
        player->openFile(httpUrl, {}, 0, streamResumeSec);
        if (auto* mw = qobject_cast<QMainWindow*>(mainWin))
            player->setGeometry(mw->centralWidget()->rect());
        else
            player->setGeometry(mainWin->rect());
        player->show();
        player->raise();
    };

    if (streamResumeSec <= 0.0 || streamSavedDur <= 0.0 || !m_streamEngine
        || !m_playerController)
    {
        launchPlayer();
        return;
    }

    const QString infoHash = m_playerController->currentInfoHash();
    if (infoHash.isEmpty()) {
        launchPlayer();
        return;
    }

    if (m_streamEngine->prepareSeekTarget(infoHash, streamResumeSec,
                                           streamSavedDur))
    {
        launchPlayer();
        return;
    }

    // Need to wait. Keep the buffer overlay on the player layer visible
    // with a "Seeking..." status while we poll. Cap at 9s total (30
    // iterations × 300ms) then fall through.
    m_bufferOverlay->show();
    m_mainStack->setCurrentIndex(2);
    const int hh = static_cast<int>(streamResumeSec) / 3600;
    const int mm = (static_cast<int>(streamResumeSec) % 3600) / 60;
    const int ss = static_cast<int>(streamResumeSec) % 60;
    m_bufferLabel->setText(
        hh > 0
            ? QStringLiteral("Seeking to %1:%2:%3...")
                  .arg(hh).arg(mm, 2, 10, QChar('0')).arg(ss, 2, 10, QChar('0'))
            : QStringLiteral("Seeking to %1:%2...")
                  .arg(mm).arg(ss, 2, 10, QChar('0')));

    // STREAM_LIFECYCLE_FIX Phase 1 Batch 1.3 — seek-retry orphan guard now
    // uses PlaybackSession generation instead of raw-QObject* identity.
    // Pre-1.3: a prior onReadyToPlay session created `new QObject(this)`,
    // stored the address in m_seekRetryState, and the retry closure
    // compared its captured pointer against m_seekRetryState at fire time
    // to detect replacement. Post-1.3: m_session.seekRetry is a
    // std::shared_ptr<SeekRetryState> carrying the captured generation +
    // attempt counter. The retry closure captures currentGeneration() at
    // setup; `isCurrentGeneration(retryGen)` aborts silently on any
    // session turnover. Closes the same class as the original fix
    // (orphan retries post close/re-open → double openFile → sidecar
    // boot race) via a more honest identity model — generation turns over
    // atomically at resetSession boundary regardless of same-URL vs
    // different-URL re-entry, whereas the raw pointer only turned over
    // when onReadyToPlay itself was re-entered.
    m_session.seekRetry.reset();  // cancel any prior (defensive; top-of-function also clears)
    m_session.seekRetry = std::make_shared<SeekRetryState>();
    m_session.seekRetry->generation = currentGeneration();
    m_session.seekRetry->attempts   = 0;
    const quint64 retryGen = m_session.seekRetry->generation;

    auto scheduleRetry = std::make_shared<std::function<void()>>();
    *scheduleRetry = [this, infoHash, streamResumeSec, streamSavedDur,
                      launchPlayer, retryGen, scheduleRetry]() {
        // Generation check: if a newer session took over (user closed +
        // re-opened, or source-switched mid-buffer), abort silently. New
        // session owns launching. When retryGen == 0 (seek-retry armed
        // without an active session — theoretically impossible once
        // beginSession is wired into every session-start site, but defensive
        // against a path that armed seek-retry without beginSession), the
        // isCurrentGeneration check returns false and we abort.
        if (!isCurrentGeneration(retryGen)) return;
        if (!m_session.seekRetry) return;  // already cancelled

        // User navigated away / swapped sources / stream was cancelled.
        // Abort the retry loop silently; new play context owns the UI.
        if (!m_playerController
            || m_playerController->currentInfoHash() != infoHash)
        {
            m_session.seekRetry.reset();
            return;
        }

        int& attempts = m_session.seekRetry->attempts;
        if (attempts >= 30) {
            // 9s cap — launch anyway; Batch 1.2 HTTP retry handles rest.
            m_session.seekRetry.reset();
            launchPlayer();
            return;
        }
        ++attempts;
        if (m_streamEngine->prepareSeekTarget(infoHash, streamResumeSec,
                                               streamSavedDur))
        {
            m_session.seekRetry.reset();
            launchPlayer();
            return;
        }
        QTimer::singleShot(300, this, [scheduleRetry]() { (*scheduleRetry)(); });
    };
    QTimer::singleShot(300, this, [scheduleRetry]() { (*scheduleRetry)(); });
}

void StreamPage::onStreamFailed(const QString& message)
{
    m_session.epKey.clear();
    cancelAutoLaunch();   // Phase 2 Batch 2.4 — clear any pending resume UI.
    hideNextEpisodeOverlay();   // Phase 2 Batch 2.5 — clear next-ep state.
    resetNextEpisodePrefetch();
    // 2026-04-15 — cancel any pending seek-pre-gate retry on failure.
    m_session.seekRetry.reset();
    // Disconnect any lingering progress connection + reset persistence mode
    // defensively — if setPersistenceMode(None) fired in onReadyToPlay but
    // playback never started cleanly, the next Videos-mode open would
    // otherwise inherit None and silently skip its own progress write.
    if (auto* player = window() ? window()->findChild<VideoPlayer*>() : nullptr) {
        disconnect(player, &VideoPlayer::progressUpdated, this, nullptr);
        disconnect(player, &VideoPlayer::closeRequested, this, nullptr);
        disconnect(player, &VideoPlayer::streamNextEpisodeRequested, this, nullptr);
        player->setPersistenceMode(VideoPlayer::PersistenceMode::LibraryVideos);
    }
    m_bufferLabel->setText("Stream failed: " + message);
    // STREAM_LIFECYCLE_FIX Phase 3 Batch 3.2 — generation-check + user-navigation
    // guard on the 3s auto-navigate timer. Audit P1-2 scenario: failure at T,
    // user nav to AddonManager at T+0.5s, T+3s fires, isActive() still false,
    // showBrowse() yanks user back off AddonManager. Triple-gate the fire:
    //   (a) isCurrentGeneration — aborts if a new session started after
    //       failure (user clicked a different tile, Shift+N, etc.). First
    //       real consumer of the Batch 1.3 generation-check pattern in the
    //       failure path specifically.
    //   (b) still-on-player-layer — mainStack index 2 is the only layer
    //       where the failure label is visible. If user navigated to
    //       detail(1) / browse(0) / addon(3) / other(4+) the failure
    //       countdown is no longer user-visible and we must not yank them.
    //       NOTE: TODO text at line 243 had `!= 0 /*not browse*/` which
    //       would invert intent (return for every non-browse layer, including
    //       the player layer we want to navigate FROM). Taking `!= 2` as the
    //       correct check per my read of the user-facing UX.
    //   (c) !isActive — preserved from pre-3.2 for belt-and-suspenders. If
    //       (a) and (b) both pass but a new playback is somehow active,
    //       showBrowse would interrupt it. Unlikely post-(a) but cheap.
    const quint64 gen = currentGeneration();
    QTimer::singleShot(3000, this, [this, gen]() {
        if (!isCurrentGeneration(gen)) return;
        constexpr int kPlayerLayerIndex = 2;
        if (m_mainStack->currentIndex() != kPlayerLayerIndex) return;
        if (!m_playerController->isActive()) showBrowse();
    });
}

void StreamPage::onStreamStopped(StreamPlayerController::StopReason reason)
{
    using StopReason = StreamPlayerController::StopReason;
    auto* player = window() ? window()->findChild<VideoPlayer*>() : nullptr;

    // STREAM_LIFECYCLE_FIX Phase 2 Batch 2.2 — source-switch reentrancy split.
    // Replacement = startStream()'s first-line defensive stop, fired because
    // a NEW session is about to begin. Audit P0-1 root cause: pre-2.2 this
    // handler ran the full UserEnd teardown (clear epKey + hide buffer +
    // showBrowse) synchronously inside startStream, which cleared the
    // JUST-INSTALLED new session state and navigated the user to browse a
    // fraction of a second before the new session's readyToPlay fired.
    // Result: flash-to-browse + progress writes dropped for the new session.
    //
    // Post-2.2: Replacement skips all teardown + navigation. Only disconnects
    // the OLD player signal receivers on `this`; the new session's
    // onReadyToPlay reconnects fresh per-session handlers. m_session is left
    // alone — beginSession at the new session's entry already clobbered it.
    if (reason == StopReason::Replacement) {
        if (player) {
            disconnect(player, &VideoPlayer::progressUpdated, this, nullptr);
            disconnect(player, &VideoPlayer::closeRequested, this, nullptr);
            disconnect(player, &VideoPlayer::streamNextEpisodeRequested, this, nullptr);
        }
        return;
    }

    // Failure arrives in parallel with streamFailed(msg) when Batch 2.2 wires
    // stopStream(StopReason::Failure) at controller failure sites. onStreamFailed
    // drives the full failure UX — sets "Stream failed: msg" on the buffer
    // overlay label, starts a 3s timer, then navigates to browse. Running the
    // UserEnd teardown below would hide the buffer overlay (and therefore the
    // failure label) before onStreamFailed can fire, collapsing the 3s error
    // display window. Early-return here; onStreamFailed owns the UX.
    // Observability side effect: the [stream-session] log in stopStream already
    // captured the failure boundary — this signal is the hook for future
    // Phase 3 failure-flow consolidation.
    if (reason == StopReason::Failure) {
        return;
    }

    // UserEnd — normal end-of-session teardown.
    m_session.epKey.clear();
    if (player) {
        disconnect(player, &VideoPlayer::progressUpdated, this, nullptr);
        disconnect(player, &VideoPlayer::closeRequested, this, nullptr);
        disconnect(player, &VideoPlayer::streamNextEpisodeRequested, this, nullptr);
        player->setPersistenceMode(VideoPlayer::PersistenceMode::LibraryVideos);
    }
    m_bufferOverlay->hide();
    // Phase 2 Batch 2.5 — if the next-episode overlay is visible (player
    // closed at near-end with a matched prefetch), keep the user on the
    // player layer so they can see the countdown + Play Now/Cancel buttons.
    // showBrowse would navigate to index 0 and orphan the overlay.
    if (m_nextEpisodeOverlay && m_nextEpisodeOverlay->isVisible()) {
        return;
    }
    showBrowse();
}

