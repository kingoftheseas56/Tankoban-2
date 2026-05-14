#include "StreamPage.h"

#include "core/CoreBridge.h"
#include "core/DebugLogBuffer.h"
#include "core/stream/MetaAggregator.h"
#include "core/stream/BulkPackVerifier.h"
#include "core/stream/BulkSourceCollector.h"
#include "core/stream/StreamBulkPlan.h"
#include "core/stream/addon/AddonRegistry.h"
#include "ui/pages/stream/AddonManagerScreen.h"
#include "core/stream/stremio/StreamServerEngine.h"
#include "core/stream/StreamDownloadIndex.h"
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
#include "core/torrent/TorrentClient.h"

#include "ui/player/VideoPlayer.h"
#include "ui/player/IPlayerBackend.h"
#include "ui/dialogs/AddAddonDialog.h"
#include "core/stream/addon/StreamSource.h"

#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFrame>
#include <QEvent>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QMainWindow>
#include <QProgressBar>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QUrl>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QVariant>

#include <functional>
#include <memory>

namespace {

int episodeNumberFromBulkItemKey(const QString& itemKey)
{
    static const QRegularExpression pattern(QStringLiteral("[Ss]\\d{1,2}[Ee](\\d{1,3})"));
    const QRegularExpressionMatch match = pattern.match(itemKey);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

int seasonNumberFromBulkItemKey(const QString& itemKey)
{
    static const QRegularExpression pattern(QStringLiteral("[Ss](\\d{1,2})[Ee]\\d{1,3}"));
    const QRegularExpressionMatch match = pattern.match(itemKey);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

StreamBulkItemState streamBulkItemStateFromJsonString(const QString& state)
{
    if (state == QLatin1String("Downloading")) return StreamBulkItemState::Downloading;
    if (state == QLatin1String("Publishing")) return StreamBulkItemState::Publishing;
    if (state == QLatin1String("Published")) return StreamBulkItemState::Published;
    if (state == QLatin1String("MissingSource")) return StreamBulkItemState::MissingSource;
    if (state == QLatin1String("MetadataFailed")) return StreamBulkItemState::MetadataFailed;
    if (state == QLatin1String("PublishFailed")) return StreamBulkItemState::PublishFailed;
    if (state == QLatin1String("Failed")) return StreamBulkItemState::Failed;
    if (state == QLatin1String("Completed")) return StreamBulkItemState::Completed;
    if (state == QLatin1String("Cancelled")) return StreamBulkItemState::Cancelled;
    if (state == QLatin1String("Orphaned")) return StreamBulkItemState::Orphaned;
    return StreamBulkItemState::Pending;
}

StreamBulkGroupRecord streamBulkGroupRecordFromJson(const QString& groupId,
                                                    const QJsonObject& obj)
{
    StreamBulkGroupRecord group;
    group.groupId = obj.value(QStringLiteral("groupId")).toString(groupId);
    group.groupKind = obj.value(QStringLiteral("groupKind")).toString(QStringLiteral("streamSeason"));
    group.label = obj.value(QStringLiteral("label")).toString(group.groupId);
    const QJsonObject sourceIds = obj.value(QStringLiteral("sourceIds")).toObject();
    group.sourceSeriesId = sourceIds.value(QStringLiteral("seriesId")).toString();
    group.sourceSeason = sourceIds.value(QStringLiteral("season")).toInt(-1);
    group.destinationRoot = obj.value(QStringLiteral("destinationRoot")).toString();
    group.stagingPath = obj.value(QStringLiteral("stagingPath")).toString();
    group.retryGeneration = obj.value(QStringLiteral("retryGeneration")).toInt(0);
    group.createdAtMs = obj.value(QStringLiteral("createdAtMs")).toVariant().toLongLong();
    group.updatedAtMs = obj.value(QStringLiteral("updatedAtMs")).toVariant().toLongLong();

    const QJsonObject canonicalNames = obj.value(QStringLiteral("canonicalNames")).toObject();
    for (auto it = canonicalNames.begin(); it != canonicalNames.end(); ++it)
        group.canonicalNames.insert(it.key(), it.value().toString());

    const QJsonArray items = obj.value(QStringLiteral("items")).toArray();
    for (const auto& value : items) {
        const QJsonObject itemObj = value.toObject();
        StreamBulkGroupItem item;
        item.itemKey = itemObj.value(QStringLiteral("itemKey")).toString();
        item.destinationKey = itemObj.value(QStringLiteral("destinationKey")).toString();
        item.infoHash = itemObj.value(QStringLiteral("infoHash")).toString();
        item.fileIndex = itemObj.value(QStringLiteral("fileIndex")).toInt(-1);
        item.canonicalFilename = itemObj.value(QStringLiteral("canonicalFilename")).toString();
        item.itemState = streamBulkItemStateFromJsonString(
            itemObj.value(QStringLiteral("itemState")).toString());
        item.lastError = itemObj.value(QStringLiteral("lastError")).toString();
        group.items.push_back(item);
    }
    return group;
}

} // namespace

StreamPage::StreamPage(CoreBridge* bridge, TorrentClient* torrentClient,
                       QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
    , m_torrentClient(torrentClient)
    , m_torrentEngine(torrentClient ? torrentClient->engine() : nullptr)
{
    setObjectName("stream");

    // Core services
    m_addonRegistry = new tankostream::addon::AddonRegistry(bridge->dataDir(), nullptr, this);
    m_streamAggregator = new tankostream::stream::StreamAggregator(m_addonRegistry, this);
    m_metaAggregator = new tankostream::stream::MetaAggregator(m_addonRegistry, this);
    m_subtitlesAggregator = new tankostream::stream::SubtitlesAggregator(m_addonRegistry, this);

    // Phase 4 Batch 4.1 â€” drive the search-bar spinner off the aggregator's
    // catalog signals. StreamSearchWidget is the primary result consumer;
    // these connects only toggle the busy UI state. Lambdas capture `this`
    // safely â€” StreamPage owns m_metaAggregator so lifetimes are coupled.
    connect(m_metaAggregator, &tankostream::stream::MetaAggregator::catalogResults,
            this, [this](const QList<tankostream::addon::MetaItemPreview>&) {
                setSearchBusy(false);
            });
    connect(m_metaAggregator, &tankostream::stream::MetaAggregator::catalogError,
            this, [this](const QString&) {
                setSearchBusy(false);
            });

    // Batch 5.3 â€” route subtitle aggregator results to the VideoPlayer's
    // SubtitlePopover (merged subtitle UI; previously a separate
    // SubtitleMenu drawer pre-VIDEO_HUD_MINIMALIST 2026-04-25). Player
    // is created by MainWindow and reachable via findChild; connection
    // is persistent for the StreamPage lifetime.
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

    // Batch 6.1 â€” Calendar backend. Needs AddonRegistry (meta fan-out) +
    // StreamLibrary (series source) + dataDir (cache path). No UI hookup
    // in 6.1; Batch 6.2 wires CalendarScreen to this engine's signals and
    // triggers loadUpcoming() on the calendar entry button.
    m_calendarEngine = new tankostream::stream::CalendarEngine(
        m_addonRegistry, m_library, bridge->dataDir(), this);

    // STREAM_SERVER_PIVOT Phase 3 (2026-04-25) â€” legacy libtorrent engine
    // deleted. Stream mode is stream-server subprocess only, no env gate,
    // no fallback. Cache lives under dataDir/stream_server_cache (renamed
    // from the legacy dataDir/stream_cache path).
    const QString cacheDir = bridge->dataDir() + "/stream_server_cache";
    m_streamEngine = new StreamServerEngine(cacheDir, this);
    m_streamEngine->start();
    m_streamEngine->cleanupOrphans();
    m_streamEngine->startPeriodicCleanup();

    if (m_torrentClient) {
        connect(m_torrentClient, &TorrentClient::streamBulkRetrySourcePickRequested,
                this, &StreamPage::retryBulkSeasonDownload);
    }

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

// STREAM_DOWNLOADED_LIBRARY Phase 3 (2026-05-10) â€” fan out the download
// index to both consumers: StreamLibrary (so remove() also evicts per-
// episode rows) and StreamLibraryLayout (so tile DOWNLOADED chips render +
// re-evaluate on entriesChanged). Wired by MainWindow after both
// m_streamPage and m_streamDownloadIndex are constructed.
void StreamPage::setStreamDownloadIndex(StreamDownloadIndex* idx)
{
    if (m_library)
        m_library->setStreamDownloadIndex(idx);
    if (m_libraryLayout) {
        m_libraryLayout->setStreamDownloadIndex(idx);
        // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — wire TorrentClient alongside
        // so the DOWNLOADING chip can reflect in-flight cohort state.
        m_libraryLayout->setTorrentClient(m_torrentClient);
    }
    // STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) â€” also wire the detail
    // view so per-row "on disk" markers + click-branch + alt-stream menu can
    // resolve. m_detailView may be null on early invocation (buildUI hasn't
    // run yet); MainWindow re-fires after buildPageStack so this lands.
    if (m_detailView)
        m_detailView->setStreamDownloadIndex(idx);
}

// â”€â”€â”€ UI construction â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

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

    // STREAM_DOWNLOADED_LIBRARY Phase 7 (2026-05-10) â€” wire TorrentClient
    // through to the detail view so Remove-from-Library can detect active
    // bulk groups for the show and gate the destructive action behind a
    // confirmation dialog. Spec Â§10.10.
    if (m_torrentClient)
        m_detailView->setTorrentClient(m_torrentClient);

    connect(m_detailView, &StreamDetailView::backRequested, this, &StreamPage::goBack);
    connect(m_detailView, &StreamDetailView::playRequested, this, &StreamPage::onPlayRequested);
    connect(m_detailView, &StreamDetailView::sourceActivated,
            this, &StreamPage::onSourceActivated);
    connect(m_detailView, &StreamDetailView::addToTankorentRequested,
            this, &StreamPage::onAddToTankorentRequested);
    connect(m_detailView, &StreamDetailView::bulkDownloadRequested,
            this, &StreamPage::triggerBulkSeasonDownload);
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — direct-dispatch from episode/season/
    // selected-set download signals. Bypasses the preflight dialog.
    connect(m_detailView, &StreamDetailView::seasonDownloadRequested,
            this, &StreamPage::onSeasonDownloadRequested);
    connect(m_detailView, &StreamDetailView::selectedEpisodesDownloadRequested,
            this, &StreamPage::onSelectedEpisodesDownloadRequested);
    connect(m_detailView, &StreamDetailView::singleEpisodeDownloadRequested,
            this, &StreamPage::onSingleEpisodeDownloadRequested);
    // Phase 3 Batch 3.5 (deferred ship) â€” direct-URL trailer playback.
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
    // Phase 2 Batch 2.4 â€” Pick-different button in the toast; aborts the
    // auto-launch timer and leaves the picker open so the user can pick a
    // different source manually.
    connect(m_detailView, &StreamDetailView::autoLaunchCancelRequested,
            this, &StreamPage::cancelAutoLaunch);

    // STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) â€” downloaded-episode
    // click. Detail view fires this AHEAD of playRequested when the
    // StreamDownloadIndex hit AND the file exists on disk; falls through to
    // playRequested otherwise. Spec Â§6.2.
    connect(m_detailView, &StreamDetailView::playLocalFileFromStreamRequested,
            this, &StreamPage::onDetailPlayLocalFileFromStream);

    // STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) â€” right-click â†’ "Show
    // alternate streams". Same flow as a default click on an undownloaded
    // episode: flip right pane to loading + emit the existing playRequested
    // so onPlayRequested runs the source-pick aggregator. Spec Â§6.3.
    connect(m_detailView, &StreamDetailView::alternateStreamRequested,
            this, [this](int season, int episode) {
                if (!m_detailView) return;
                m_detailView->setStreamSourcesLoading();
                onPlayRequested(m_detailView->currentImdb(),
                                QStringLiteral("series"), season, episode);
            });

    // Auto-launch timer â€” single-shot, 2s window (enough for the user to
    // notice the "Resuming with last-used source" toast and cancel).
    m_autoLaunchTimer = new QTimer(this);
    m_autoLaunchTimer->setSingleShot(true);
    m_autoLaunchTimer->setInterval(2000);
    connect(m_autoLaunchTimer, &QTimer::timeout,
            this, &StreamPage::onAutoLaunchFire);

    // Wire library grid single+double-click â†’ show detail. Explicit lambda
    // because showDetail is overloaded â€” pointer-to-member would be ambiguous
    // between the (QString) and (MetaItemPreview) overloads.
    connect(m_libraryLayout, &StreamLibraryLayout::showClicked, this,
        [this](const QString& imdbId) { showDetail(imdbId); });

    // Player layer â€” buffer overlay only (VideoPlayer handles its own controls)
    // STREAM_AUTO_NEXT Stremio-parity (2026-04-21) â€” promoted to member
    // so onNextEpisodeCancel can restore m_nextEpisodeOverlay's parent
    // after the mid-playback reparent to VideoPlayer.
    m_playerLayer = new QWidget(this);
    auto* playerLayer = m_playerLayer;
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

    // Phase 2 Batch 2.5 â€” next-episode overlay lives on the same player
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

    // Countdown timer â€” 1s tick. Decrements m_nextEpisodeCountdownSec; fires
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
            this, &StreamPage::goBack);
    // addAddonRequested stays unwired until Batch 2.2 ships AddAddonDialog.

    // Catalog browse layer (Phase 3 Batch 3.3)
    m_catalogBrowse = new tankostream::stream::CatalogBrowseScreen(m_addonRegistry, this);
    m_mainStack->addWidget(m_catalogBrowse); // index 4: catalog browse

    connect(m_catalogBrowse, &tankostream::stream::CatalogBrowseScreen::backRequested,
            this, &StreamPage::goBack);
    connect(m_catalogBrowse, &tankostream::stream::CatalogBrowseScreen::metaActivated, this,
        [this](const tankostream::addon::MetaItemPreview& preview) {
            showDetail(preview);
        });
    // Stream library UX rework 2026-04-15 â€” StreamHomeBoard no longer emits
    // browseCatalogRequested (featured rows deleted). Users reach the
    // catalog browser via the Catalog button in the search bar instead.

    // Calendar layer (Phase 6 Batch 6.2)
    m_calendarScreen = new tankostream::stream::CalendarScreen(this);
    m_mainStack->addWidget(m_calendarScreen); // index 5: calendar

    // Engine â†’ screen dataflow. Prefer grouped signal (pre-bucketed + date
    // math already done engine-side); the flat signal fires too but the
    // screen's setItems regroups harmlessly if grouped hasn't arrived yet.
    connect(m_calendarEngine, &tankostream::stream::CalendarEngine::calendarGroupedReady,
            m_calendarScreen, &tankostream::stream::CalendarScreen::setGroupedItems);
    connect(m_calendarEngine, &tankostream::stream::CalendarEngine::calendarError,
            m_calendarScreen, &tankostream::stream::CalendarScreen::setError);

    // Screen â†’ navigation.
    connect(m_calendarScreen, &tankostream::stream::CalendarScreen::backRequested,
            this, &StreamPage::goBack);
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

    // Phase 4 Batch 4.1 â€” indeterminate spinner. Busy mode QProgressBar
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

    // Batch 6.2 â€” Calendar entry button. Placed after Addons; opens the
    // calendar stack layer and triggers an engine load on each click
    // (CalendarEngine has its own 12h TTL cache + signature guard so
    // repeated clicks don't re-fan-out unnecessarily).
    m_calendarBtn = new QPushButton("Calendar", m_searchBarFrame);
    m_calendarBtn->setFixedHeight(36);
    m_calendarBtn->setCursor(Qt::PointingHandCursor);
    m_calendarBtn->setObjectName("StreamCalendarBtn");
    m_calendarBtn->setToolTip("Upcoming episodes from your library");
    layout->addWidget(m_calendarBtn);

    // Stream library UX rework 2026-04-15 â€” Catalog button replaces the
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

    // Live-search debounce REMOVED 2026-04-25 per Hemanth: even at 800 ms
    // (raised from 300 â†’ 800 ms on 2026-04-20 for the same reason) the
    // search fired during natural mid-typing pauses while Hemanth was
    // still composing his query. Search now only fires on Enter / Search
    // button / history-row click. textChanged is still wired so paste-kind
    // detection + history-dropdown show/hide-on-empty stay live; it just
    // no longer arms a deferred search.
    connect(m_searchInput, &QLineEdit::textChanged,
            this, &StreamPage::onSearchTextChanged);

    // Phase 4 Batch 4.2 â€” search history. Load persisted list, build
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
    // Phase 2 Batch 2.2 â€” m_metaAggregator plumbed through so the
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
            // fires onPlayRequested â†’ loads streams â†’ populates m_detailView
            // which is not currently shown â†’ user sees nothing.
            if (m_detailView && m_detailView->currentImdb() != imdbId) {
                m_detailView->showEntry(imdbId, season, episode);
            }
            m_mainStack->setCurrentIndex(1);
            // For movies, showEntry's movie-branch already emits playRequested
            // back into us â€” skip to avoid double-loading streams. For series,
            // showEntry waits on episode-click, so we explicitly fire with the
            // specific episode context from the continue-strip click.
            if (mediaType != QLatin1String("movie")) {
                onPlayRequested(imdbId, mediaType, season, episode);
            }
        });

    // Stream library UX rework 2026-04-15 â€” StreamHomeBoard no longer emits
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

    connect(m_searchWidget, &StreamSearchWidget::backRequested, this, &StreamPage::goBack);
    connect(m_searchWidget, &StreamSearchWidget::libraryChanged, m_libraryLayout, &StreamLibraryLayout::refresh);
    // Phase 1 Batch 1.2 â€” search-tile click opens the detail view with the
    // result's MetaItemPreview. Previously the click toggled library add/
    // remove; that moved into the detail view's Add-to-Library button.
    connect(m_searchWidget, &StreamSearchWidget::metaActivated, this,
        [this](const tankostream::addon::MetaItemPreview& preview) {
            showDetail(preview);
        });
}

// â”€â”€â”€ Search â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void StreamPage::onSearchSubmit()
{
    QString query = m_searchInput->text().trimmed();
    if (query.isEmpty())
        return;

    hideSearchHistoryDropdown();

    // Phase 4 Batch 4.3 â€” route URL-paste kinds to their action instead
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

    // STREAM_NAV_BACK_STACK 2026-05-06 â€” push a Search entry onto the
    // nav stack so Back from a Detail view (entered via search-result
    // click) returns to Search. Re-submitting from an existing Search
    // overwrites the current entry's query (no duplicate stack layer)
    // so the depth stays clean.
    if (!m_navStack.isEmpty() && m_navStack.top().kind == NavEntry::Kind::Search) {
        m_navStack.top().searchQuery = query;
    } else {
        NavEntry e;
        e.kind = NavEntry::Kind::Search;
        e.searchQuery = query;
        m_navStack.push(e);
    }

    showSearchResults();   // visual transition (hide browseScroll, show searchWidget)
    setSearchBusy(true);
    m_searchWidget->search(query);
}

// Phase 4 Batch 4.1 â€” search input observer + spinner handlers.
//
// Live-search debounce was removed 2026-04-25 per Hemanth. This handler now
// only updates UI affordances tied to the input's current text â€” it never
// kicks off a search. Search execution is gated entirely on Enter, the
// Search button, or a history-row click (all routed through onSearchSubmit).

void StreamPage::onSearchTextChanged(const QString& text)
{
    const QString trimmed = text.trimmed();

    // Phase 4 Batch 4.3 â€” re-detect paste kind on every keystroke. The
    // Search button label reflects the intended action; Enter uses it.
    const PasteKind newKind = detectPasteKind(trimmed);
    if (newKind != m_pasteKind) {
        m_pasteKind = newKind;
        applyPasteKindToSearchButton(m_pasteKind);
    }

    if (trimmed.isEmpty()) {
        // Clearing restores the home/browse layer + offers the history
        // dropdown if the input still has focus (Batch 4.2).
        setSearchBusy(false);
        if (m_searchWidget && m_searchWidget->isVisible()) {
            showBrowse();
        }
        if (m_searchInput && m_searchInput->hasFocus()) {
            showSearchHistoryDropdown();
        }
        return;
    }

    // Any non-empty text hides the history dropdown (Batch 4.2) â€” the user
    // is typing a new query, not browsing history.
    hideSearchHistoryDropdown();
}

void StreamPage::setSearchBusy(bool busy)
{
    if (!m_searchBusy) return;
    m_searchBusy->setVisible(busy);
}

// â”€â”€â”€ Phase 4 Batch 4.3 â€” URL / magnet paste handling â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

StreamPage::PasteKind StreamPage::detectPasteKind(const QString& input) const
{
    const QString s = input.trimmed();
    if (s.isEmpty()) return PasteKind::None;

    // Magnet URI â€” regex anchored per RFC 2056 style: magnet:?xt=urn:btih:HASH
    static const QRegularExpression kMagnetRe(
        QStringLiteral("^magnet:\\?xt=urn:btih:[A-Za-z0-9]{32,}"),
        QRegularExpression::CaseInsensitiveOption);
    if (kMagnetRe.match(s).hasMatch()) return PasteKind::Magnet;

    // Stremio addon deep link.
    if (s.startsWith(QStringLiteral("stremio://"), Qt::CaseInsensitive))
        return PasteKind::AddonManifest;

    // HTTP / HTTPS â€” branch further by path shape.
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

    // Plain HTTP without a recognized path shape â€” ambiguous between
    // "arbitrary search query that happens to look like a URL" and
    // "an unknown addon / media endpoint". Per the TODO's "only if the
    // full value is parseable as a URL â€” regex guarded" â€” we stay
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
            // dialog's existing install flow â€” install signals still fire
            // through AddonRegistry as in the regular "Addons â†’ Add" path.
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
                // "magnet:?xt=urn:btih:HASH" into the query â€” we walk it.
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

// â”€â”€â”€ Phase 4 Batch 4.2 â€” search history â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

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

void StreamPage::clearSearchHistory()
{
    if (m_searchHistory.isEmpty()) return;
    m_searchHistory.clear();
    saveSearchHistory();
    // Empty history â†’ dropdown self-hides on next show, so just hide now.
    hideSearchHistoryDropdown();
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

    // Delayed-hide timer â€” gives click-on-row events time to register
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
    // with a reasonable max (10 rows Ã— ~28px â‰ˆ 280 + padding).
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
        // No history yet â€” hide instead of showing an empty frame.
        m_searchHistoryDropdown->hide();
        return;
    }

    const int rows = qMin(m_searchHistory.size(), kMaxSearchHistory);
    const char* kRowBtnStyle =
        "QPushButton { background: transparent; color: #d0d0d0; border: none;"
        "  text-align: left; padding: 6px 10px; font-size: 12px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.08); }";
    const char* kRemoveBtnStyle =
        "QPushButton { background: transparent; color: rgba(255,255,255,0.45);"
        "  border: none; font-size: 14px; padding: 0 10px; }"
        "QPushButton:hover { color: #fff; }";
    const char* kClearAllBtnStyle =
        "QPushButton { background: transparent; color: rgba(255,255,255,0.55);"
        "  border: none; text-align: left; padding: 6px 10px;"
        "  font-size: 11px; font-weight: 500; letter-spacing: 0.4px; }"
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

        auto* removeBtn = new QPushButton(QStringLiteral("\u00D7"), row);   // Ã—
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

    // Footer: "Ã— Clear search history" wipes the entire list. Single visible
    // affordance per Hemanth's 2026-04-25 ask ("there isn't an option to
    // clear search history"). Per-entry Ã— on each row above is preserved.
    // Hairline divider above so the footer reads as a separate action,
    // not an additional history item.
    auto* divider = new QFrame(m_searchHistoryList);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(
        "QFrame { border: none; background: rgba(255,255,255,0.08);"
        "  max-height: 1px; min-height: 1px; }");
    layout->addWidget(divider);

    auto* clearAllBtn = new QPushButton(
        QStringLiteral("\u00D7  Clear search history"), m_searchHistoryList);
    clearAllBtn->setCursor(Qt::PointingHandCursor);
    clearAllBtn->setStyleSheet(kClearAllBtnStyle);
    clearAllBtn->setFocusPolicy(Qt::NoFocus);
    connect(clearAllBtn, &QPushButton::clicked,
            this, &StreamPage::clearSearchHistory);
    layout->addWidget(clearAllBtn);

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
            // Show history only if input is empty â€” otherwise we'd obscure
            // the user's live typing with irrelevant past queries.
            if (m_searchInput->text().trimmed().isEmpty()) {
                showSearchHistoryDropdown();
            }
        } else if (event->type() == QEvent::FocusOut) {
            // Delayed hide â€” if focus moved to a dropdown row's button,
            // the click handler runs first via the 150ms timer.
            if (m_searchHistoryHideTimer) m_searchHistoryHideTimer->start();
        }
    }
    return QWidget::eventFilter(obj, event);
}

// STREAM_NAV_BACK_STACK 2026-05-06 â€” depth-first back navigation.
// Pop one entry off m_navStack; re-show the previous entry without
// pushing. Stack-bottom (Library) is a no-op re-show â€” currently
// stays on Library; future enhancement could propagate up to
// MainWindow if cross-mode back is wanted.
void StreamPage::goBack()
{
    if (m_navStack.size() <= 1) {
        // STREAM_CATALOG_BACK_NAVSTACK_SEED_FIX 2026-05-06 â€” Hemanth-reported
        // "the back button besides catalog is not working now": when user
        // opens Tankoban â†’ Stream tab â†’ clicks Catalog without ever pushing
        // Browse first (the ctor doesn't seed it; only show*() slots push),
        // navStack ends up [CatalogBrowse] only. Original guard only caught
        // the empty-stack case, so size=1 with non-Browse top was a no-op
        // (re-showed the same view, looked like Back was broken). Fix:
        // unconditional normalize to Browse on top-of-stack escape â€” clears
        // whatever's there and seeds Browse fresh.
        m_navStack.clear();
        NavEntry e;
        e.kind = NavEntry::Kind::Browse;
        m_navStack.push(e);
        showEntryRaw(m_navStack.top());
        return;
    }
    m_navStack.pop();
    showEntryRaw(m_navStack.top());
}

// STREAM_NAV_BACK_STACK 2026-05-06 â€” restore the search overlay visual
// state without re-running the network fetch. m_searchWidget caches its
// query + results widgets across hide/show cycles, so a simple
// hide-browse + show-search is sufficient. Used by showEntryRaw on
// Kind::Search restore; onSearchSubmit calls it explicitly before
// invoking m_searchWidget->search() to actually run a fresh query.
void StreamPage::showSearchResults()
{
    m_browseScroll->hide();
    m_searchWidget->show();
}

// STREAM_NAV_BACK_STACK 2026-05-06 â€” internal view-swap helper. Does
// the visual transition implied by NavEntry::kind WITHOUT pushing onto
// m_navStack. show* slots push then call this; goBack pops then calls
// this; the player-exit restore path calls this directly.
void StreamPage::showEntryRaw(const NavEntry& entry)
{
    using Kind = NavEntry::Kind;
    switch (entry.kind) {
    case Kind::Browse:
        cancelBulkSeasonDownload();
        m_mainStack->setCurrentIndex(0);
        m_searchWidget->hide();
        m_browseScroll->show();
        // Phase 4 Batch 4.1 â€” defensive spinner reset when navigating
        // away from search (covers the "user hit Back / Esc while a
        // search was in flight" case â€” the later catalogResults lands
        // but the UI is already elsewhere).
        setSearchBusy(false);
        // Phase 4 Batch 4.2 â€” defensive history-dropdown dismissal.
        hideSearchHistoryDropdown();
        // 2026-04-15 â€” cancel any pending seek-pre-gate retry. User
        // navigated away; no more launchPlayer should fire.
        m_session.seekRetry.reset();
        // Invalidate any in-flight play context so a late streamsReady
        // arrival followed by an accidental card-click can't dispatch
        // playback for the title the user just backed away from.
        m_session.pending.valid = false;
        cancelAutoLaunch();
        // Phase 2 Batch 2.5 â€” if a next-episode overlay was pending,
        // clear it; user explicitly navigated away.
        hideNextEpisodeOverlay();
        resetNextEpisodePrefetch();
        if (m_homeBoard)
            m_homeBoard->refresh();
        if (m_libraryLayout)
            m_libraryLayout->refresh();
        break;

    case Kind::CatalogBrowse:
        if (!m_catalogBrowse) break;
        cancelBulkSeasonDownload();
        cancelAutoLaunch();
        hideNextEpisodeOverlay();
        resetNextEpisodePrefetch();
        m_catalogBrowse->open(entry.catalogAddonId,
                              entry.catalogType,
                              entry.catalogId);
        m_mainStack->setCurrentIndex(4);
        break;

    case Kind::Detail:
        if (!m_detailView) break;
        cancelBulkSeasonDownload();
        cancelAutoLaunch();
        if (entry.detailHasPreview) {
            m_detailView->showEntry(entry.detailPreview.id,
                                    entry.detailPreselectSeason,
                                    entry.detailPreselectEpisode,
                                    entry.detailPreview);
        } else {
            m_detailView->showEntry(entry.detailImdbId);
        }
        m_mainStack->setCurrentIndex(1);
        break;

    case Kind::AddonManager:
        if (!m_addonManager) break;
        cancelBulkSeasonDownload();
        cancelAutoLaunch();
        hideNextEpisodeOverlay();
        resetNextEpisodePrefetch();
        m_addonManager->refresh();
        m_mainStack->setCurrentIndex(3);
        break;

    case Kind::Calendar:
        if (!m_calendarScreen || !m_calendarEngine) break;
        cancelBulkSeasonDownload();
        cancelAutoLaunch();
        hideNextEpisodeOverlay();
        resetNextEpisodePrefetch();
        m_calendarScreen->setLoading(true);
        m_mainStack->setCurrentIndex(5);
        m_calendarEngine->loadUpcoming();
        break;

    case Kind::Search:
        showSearchResults();
        break;
    }
}

// STREAM_NAV_BACK_STACK 2026-05-06 â€” restore pre-player view if snapshot
// was captured at launch; otherwise fall back to library (legacy
// default). Called from onStreamStopped UserEnd, the defensive 3s
// post-close timer, and onNextEpisodeCancel case (a).
void StreamPage::restorePlayerExitView()
{
    if (m_beforePlayerEntry.has_value()) {
        const NavEntry entry = *m_beforePlayerEntry;
        m_beforePlayerEntry.reset();
        showEntryRaw(entry);
        return;
    }
    showBrowse();
}

void StreamPage::showBrowse()
{
    // STREAM_NAV_BACK_STACK 2026-05-06 â€” Library is the stack bottom.
    // showBrowse explicitly resets the stack to a clean Browse-only
    // state. Called on Stream-mode entry, on legitimate library-home
    // navigation (e.g. nav-bar Library button), and as the legacy
    // fallback when no pre-player snapshot exists.
    m_navStack.clear();
    NavEntry e;
    e.kind = NavEntry::Kind::Browse;
    m_navStack.push(e);
    showEntryRaw(e);
}

void StreamPage::showAddonManager()
{
    NavEntry e;
    e.kind = NavEntry::Kind::AddonManager;
    m_navStack.push(e);
    showEntryRaw(e);
}

void StreamPage::showCatalogBrowse(const QString& addonId, const QString& type,
                                   const QString& catalogId, const QString& title)
{
    NavEntry e;
    e.kind = NavEntry::Kind::CatalogBrowse;
    e.catalogAddonId = addonId;
    e.catalogType = type;
    e.catalogId = catalogId;
    e.catalogTitle = title;
    m_navStack.push(e);
    showEntryRaw(e);
}

void StreamPage::onCatalogBtnClicked()
{
    // Stream library UX rework 2026-04-15 â€” open catalog browse with no
    // preselection. CatalogBrowseScreen::open with empty args calls
    // selectAddon/selectCatalog with empty strings â€” the for-loop lookups
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
    NavEntry e;
    e.kind = NavEntry::Kind::Calendar;
    m_navStack.push(e);
    showEntryRaw(e);
}

void StreamPage::showDetail(const QString& imdbId)
{
    // Idempotent: the library/home-board wires BOTH tileSingleClicked and
    // tileDoubleClicked to this slot, and Qt delivers BOTH for a double-click
    // gesture (first mousePress â†’ single-click, then the second press becomes
    // a dedicated double-click event). The second call would otherwise reset
    // the detail state + re-fire the meta fetch just as the first call's
    // response lands AND would push a duplicate Detail entry onto m_navStack.
    if (m_mainStack->currentIndex() == 1 && m_detailView
        && m_detailView->currentImdb() == imdbId) {
        return;
    }
    NavEntry e;
    e.kind = NavEntry::Kind::Detail;
    e.detailImdbId = imdbId;
    e.detailHasPreview = false;
    m_navStack.push(e);
    showEntryRaw(e);
}

void StreamPage::showDetail(const tankostream::addon::MetaItemPreview& preview,
                            int preselectSeason,
                            int preselectEpisode)
{
    // Phase 1 Batch 1.1: catalog/home/search tile path. Same idempotency
    // guard as the imdbId overload â€” repeat clicks on the same tile don't
    // re-fire state resets. The preview is passed through as `previewHint`
    // so the detail view paints title/year/poster/description from the
    // tile's metadata without needing the title to be in the library.
    if (preview.id.isEmpty()) return;
    if (m_mainStack->currentIndex() == 1 && m_detailView
        && m_detailView->currentImdb() == preview.id) {
        return;
    }
    NavEntry e;
    e.kind = NavEntry::Kind::Detail;
    e.detailImdbId = preview.id;
    e.detailHasPreview = true;
    e.detailPreview = preview;
    e.detailPreselectSeason = preselectSeason;
    e.detailPreselectEpisode = preselectEpisode;
    m_navStack.push(e);
    showEntryRaw(e);
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
    // 1.3 â€” beginSession stamps the new generation + clears prior session
    // state (stopping the countdown timer + disconnecting aggregators) in
    // one boundary, so a detail re-entry mid-prefetch doesn't leak stale
    // async closures into the new session.
    beginSession(epKey,
                 PendingPlay{imdbId, mediaType, season, episode, epKey, true},
                 QStringLiteral("onPlayRequested"));

    // Check if we have a saved choice for this episode â€” if so, build the
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

    // Phase 2 Batch 2.3 â€” if per-episode is empty and this is a series, fall
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

    // Phase 2 Batch 2.4 â€” auto-launch eligibility: either the per-episode
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

    // Reset any in-flight auto-launch before we kick off the new resolve â€”
    // a second onPlayRequested (user clicks another episode mid-countdown)
    // must replace, not stack.
    cancelAutoLaunch();

    // Fetch streams via StreamAggregator, then push into the right pane of
    // StreamDetailView. No modal â€” the cards live inside the detail view
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

            // Phase 2 Batch 2.4 â€” auto-launch DISABLED 2026-04-16 per
            // Hemanth UX call (Phase 1 telemetry session, post One Piece
            // pack regression). The 2-second countdown was too aggressive â€”
            // entering Sources view would fire playback before the user
            // could meaningfully pick a different source. Manual source
            // selection (user clicks a card) still works via the existing
            // setStreamSources path above. The 10-minute eligibility gate +
            // m_autoLaunchTimer infrastructure are preserved in case future
            // UX iteration wants a longer countdown variant; one-line
            // re-enable point is the `if (false &&` guard below â€” flip to
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

// Phase 2 Batch 2.4 â€” auto-launch orchestration.

void StreamPage::onAutoLaunchFire()
{
    if (m_detailView) m_detailView->hideAutoLaunchToast();
    if (!m_autoLaunchChoice.has_value()) return;
    const auto choice = *m_autoLaunchChoice;
    m_autoLaunchChoice.reset();
    // Same entry point user-click takes â€” keeps persistence + handoff
    // behavior identical between manual and auto-launch flows.
    onSourceActivated(choice);
}

void StreamPage::cancelAutoLaunch()
{
    if (m_autoLaunchTimer) m_autoLaunchTimer->stop();
    m_autoLaunchChoice.reset();
    if (m_detailView) m_detailView->hideAutoLaunchToast();
}

// Phase 2 Batch 2.5 â€” next-episode pre-fetch + overlay orchestration.

void StreamPage::startNextEpisodePrefetch(const QString& imdbId,
                                           int currentSeason, int currentEpisode)
{
    // Skip movies â€” no next episode concept.
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

            // Reuse m_streamAggregator â€” the current episode's streamsReady
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

    // Phase 2 Batch 2.6 â€” Shift+N path: if the user fired the shortcut
    // while prefetch was still resolving, auto-play the moment a match
    // lands (no overlay, no countdown). No match â†’ silent no-op per
    // TODO "No-op if no next episode".
    if (m_session.nextShortcutPending) {
        m_session.nextShortcutPending = false;
        if (m_session.nextPrefetch->matchedChoice.has_value()) {
            onNextEpisodePlayNow();
        }
        return;
    }

    // STREAM_AUTO_NEXT Stremio-parity (2026-04-21) â€” if we reached a
    // matched choice during normal playback (not the Shift+N shortcut),
    // surface the overlay NOW as a floating layer over the still-playing
    // video. Stremio / Netflix UX: user sees "Up next" popup during the
    // last ~30-60s of the episode (or earlier, depending on how long the
    // async prefetch took), can Play Now to skip credits, let the 10s
    // countdown expire to auto-advance, or Cancel to finish the episode
    // naturally. Previous behavior only showed the overlay on
    // closeRequested â€” too late for binge-watch ergonomics.
    if (m_session.nextPrefetch->matchedChoice.has_value()) {
        showNextEpisodeOverlayInPlayer();
    }
}

void StreamPage::showNextEpisodeOverlay()
{
    if (!m_nextEpisodeOverlay || !m_session.nextPrefetch.has_value()) return;

    // STREAM_AUTO_NEXT Stremio-parity (2026-04-21) â€” defensive reparent
    // back to m_playerLayer in case the mid-playback path earlier in
    // this session parented the overlay onto VideoPlayer. The legacy
    // close-path fires with the player hidden, so a widget parented to
    // a hidden VideoPlayer would be invisible on show(). Re-adding to
    // the layout restores the centered geometry the close-path UX
    // expects.
    if (m_playerLayer && m_nextEpisodeOverlay->parent() != m_playerLayer) {
        m_nextEpisodeOverlay->setParent(m_playerLayer);
        if (auto* lay = qobject_cast<QVBoxLayout*>(m_playerLayer->layout())) {
            lay->addWidget(m_nextEpisodeOverlay, 0, Qt::AlignCenter);
        }
    }

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

// STREAM_AUTO_NEXT Stremio-parity (2026-04-21) â€” mid-playback sibling of
// showNextEpisodeOverlay. Reparents the same overlay widget onto the
// floating VideoPlayer (found via mainWin->findChild<VideoPlayer*>, same
// pattern as the progressUpdated-lambda setup at line ~1743) so the
// overlay paints OVER still-playing video rather than waiting for
// closeRequested to swap the player away first. Position: bottom-right
// of the player's rect with 40px horizontal + 80px vertical margins â€”
// matches Stremio/Netflix binge-overlay placement (doesn't block content
// center where the climax of a scene usually plays). Countdown starts
// fresh at 10s each invocation â€” safe to re-enter if called twice
// before dismiss (mutates text in place via setText).
void StreamPage::showNextEpisodeOverlayInPlayer()
{
    if (!m_nextEpisodeOverlay || !m_session.nextPrefetch.has_value()) return;

    auto* mainWin = window();
    if (!mainWin) return;
    auto* player = mainWin->findChild<VideoPlayer*>();
    if (!player || !player->isVisible()) {
        // Defensive: player not visible (session may have torn down
        // between prefetch start + stream resolve). Let the close-path
        // overlay handle it via the existing closeRequested branch.
        return;
    }

    m_nextEpisodeOverlay->setParent(player);
    constexpr int kRightMarginPx  = 40;
    constexpr int kBottomMarginPx = 80;
    m_nextEpisodeOverlay->move(
        player->width()  - m_nextEpisodeOverlay->width()  - kRightMarginPx,
        player->height() - m_nextEpisodeOverlay->height() - kBottomMarginPx);

    // Text identical to showNextEpisodeOverlay â€” same "Up next: SeriesName Â· SxxExx"
    // + "Playing in Ns..." format. Kept duplicated here rather than
    // refactored into a helper so the code path stays readable and each
    // overlay-surface gets to independently tune its own text / layout
    // (e.g., in-player overlay may want a more subtle font in future).
    const QString seriesName = m_library
        ? m_library->get(m_session.nextPrefetch->imdbId).name
        : QString();
    const QString label = seriesName.isEmpty()
        ? QStringLiteral("S%1E%2")
              .arg(m_session.nextPrefetch->season, 2, 10, QChar('0'))
              .arg(m_session.nextPrefetch->episode, 2, 10, QChar('0'))
        : seriesName + QStringLiteral(" Â· ")
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

    // Do NOT hide m_bufferOverlay or switch m_mainStack â€” the player
    // window is already visible (floating above the stack's current
    // page). We only show our overlay on top.
    m_nextEpisodeOverlay->show();
    m_nextEpisodeOverlay->raise();

    if (m_nextEpisodeCountdownTimer) m_nextEpisodeCountdownTimer->start();
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
    // this is â€” same shape onPlayRequested would have produced, minus the
    // streams fan-out round-trip (we already have the matched choice).
    // Batch 1.3 â€” route through beginSession so the new episode's session
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

    // Drive through the canonical source-activation path â€” same persistence,
    // subtitle fan-out, and player handoff the user-click flow uses.
    onSourceActivated(choice);
}

void StreamPage::onNextEpisodeCancel()
{
    // STREAM_AUTO_NEXT Stremio-parity (2026-04-21) â€” two contexts now call
    // Cancel:
    //   (a) Legacy close-path: overlay appeared AFTER closeRequested; the
    //       stream was already stopped; Cancel should return to browse.
    //   (b) Mid-playback path: overlay appeared DURING last ~30-60s of
    //       playback (new Stremio-parity path); the stream is still
    //       running; Cancel should ONLY dismiss the overlay and let the
    //       user finish watching.
    // Discriminator: if the player is visible + active, we're in case (b).
    auto* mainWin = window();
    auto* player = mainWin ? mainWin->findChild<VideoPlayer*>() : nullptr;
    const bool midPlayback = player && player->isVisible()
                          && m_playerController && m_playerController->isActive();

    hideNextEpisodeOverlay();
    resetNextEpisodePrefetch();

    if (midPlayback) {
        // Case (b): reparent overlay back to m_playerLayer so if the
        // close-path later tries to showNextEpisodeOverlay for a future
        // session, the widget tree is consistent. resetNextEpisodePrefetch
        // above already cleared matchedChoice so overlayEligible will be
        // false on close for this session anyway â€” this is belt-and-
        // braces cleanup against future session state. Widget is hidden
        // (via hideNextEpisodeOverlay above) so layout recalc is cheap.
        if (m_nextEpisodeOverlay && m_playerLayer) {
            m_nextEpisodeOverlay->setParent(m_playerLayer);
            m_nextEpisodeOverlay->hide();
        }
        return;  // player keeps playing
    }
    // Case (a): player has already closed; restore the pre-player view
    // (STREAM_NAV_BACK_STACK 2026-05-06) so the user lands on their
    // originating Detail / Catalog / Search page instead of library.
    restorePlayerExitView();
}

void StreamPage::resetNextEpisodePrefetch()
{
    if (m_nextEpisodeCountdownTimer) m_nextEpisodeCountdownTimer->stop();
    m_session.nextPrefetch.reset();
    m_session.nearEndCrossed = false;
    m_session.nextShortcutPending = false;
    // Drop any in-flight prefetch connections from MetaAggregator /
    // StreamAggregator â€” we reset them on each prefetch start anyway, but
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

// STREAM_LIFECYCLE_FIX Phase 1 Batches 1.1 + 1.2 + 1.3 â€” PlaybackSession
// foundation + full migration. 1.1 introduced the struct + generation
// counter + beginSession/resetSession API. 1.2 migrated `_currentEpKey` +
// m_pendingPlay + m_lastDeadlineUpdateMs. 1.3 migrated m_session.nextPrefetch +
// m_session.nearEndCrossed + m_session.nextShortcutPending + m_seekRetryState (raw QObject*
// identity-token pattern replaced with generation-check â€” first real
// consumer of currentGeneration()/isCurrentGeneration()). 1.3 also fleshed
// SeekRetryState, added reason param + wrap-guard + begin-log on
// beginSession, and wired beginSession into the 4 session-start sites
// (trailer paste, magnet paste, onPlayRequested, onNextEpisodePlayNow).
// Phase 1 CLOSED at 1.3. Prototype credit:
// agents/prototypes/stream_lifecycle/Batch1.1_PlaybackSession_struct_API.cpp
// (Agent 7, Codex) â€” shape adopted as-is modulo file-style conventions.

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
    // stamp m_session.generation = 0 which is the "no-session" sentinel â€”
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
    // boundary: no showBrowse(), no signal emits, no player touches â€” callers
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
    // STREAM_LIFECYCLE_FIX Phase 4 Batch 4.1 â€” audit P2-3 close. Pre-4.1 guard
    // used `m_session.pending.valid` which onSourceActivated clears before
    // playback starts (pending is consumed as the session installs) â€” making
    // Shift+N a silent no-op during actual playback. Correct identity signals:
    //   (a) m_session.isValid() â€” active session (generation != 0 AND
    //       epKey non-empty). Holds true from beginSession through
    //       resetSession/next beginSession, spans the entire playback.
    //   (b) pending.mediaType == "series" â€” filter out movies / trailers /
    //       adhoc URLs where "next episode" has no meaning.
    //   (c) m_playerController->isActive() â€” sanity check; even if m_session
    //       reports valid, the player may be between states (buffering, seek
    //       retry). isActive gates us to "playback path committed."
    // Unblocks STREAM_UX_PARITY Batch 2.6 (Shift+N player shortcut) â€” previously
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

    // Prefetch already in flight from near-end trigger â€” mark shortcut
    // pending so onNextEpisodePrefetchStreams auto-plays once match lands.
    if (m_session.nextPrefetch.has_value() && !m_session.nextPrefetch->streamsLoaded) {
        m_session.nextShortcutPending = true;
        return;
    }

    // Cold path â€” no prefetch yet. Kick one off for the current episode
    // and arm the shortcut-pending flag. startNextEpisodePrefetch reuses
    // the series-meta â†’ next-unwatched resolve path from Batch 2.5;
    // if the series has no next unwatched episode, matchedChoice never
    // lands and the shortcut falls through to no-op silently.
    m_session.nextShortcutPending = true;
    startNextEpisodePrefetch(m_session.pending.imdbId,
                             m_session.pending.season,
                             m_session.pending.episode);
}

void StreamPage::onAddToTankorentRequested(const tankostream::stream::StreamPickerChoice& choice)
{
    // Defensive guard. The card-side menu suppresses the action for
    // non-magnet sources (StreamSourceCard::contextMenuEvent), so a
    // signal-arrival here with a bad payload would only happen if a
    // future code path emits the signal directly without going through
    // the card. Treat as no-op rather than crash.
    if (choice.sourceKind != QLatin1String("magnet")
     || choice.magnetUri.isEmpty()) {
        qWarning() << "StreamPage::onAddToTankorentRequested: refusing non-magnet"
                   << "sourceKind=" << choice.sourceKind
                   << "magnetUri.isEmpty=" << choice.magnetUri.isEmpty();
        return;
    }

    // Display name. STREAM_SOURCE_CARD_TITLE_FIX 2026-05-06 repurposed
    // displayTitle to carry the release name (was: addon name). It is
    // now the right primary identifier here â€” no longer needs the
    // displayFilename fallback (field removed). Session-pending fallback
    // synthesizes a "<imdbId> S<NN>E<NN>" identifier when extractReleaseName
    // bottomed out at "(unnamed release)" but we have episode context.
    QString displayName = choice.displayTitle;
    if ((displayName.isEmpty() || displayName == QLatin1String("(unnamed release)"))
     && m_session.pending.valid
     && m_session.pending.mediaType == QLatin1String("series")
     && m_session.pending.season > 0
     && m_session.pending.episode > 0) {
        displayName = QStringLiteral("%1 S%2E%3")
                          .arg(m_session.pending.imdbId)
                          .arg(m_session.pending.season, 2, 10, QChar('0'))
                          .arg(m_session.pending.episode, 2, 10, QChar('0'));
    }

    emit addToTankorentRequested(choice.magnetUri, displayName);
}

void StreamPage::triggerBulkSeasonDownload(int season)
{
    using namespace tankostream::stream;

    cancelBulkSeasonDownload();

    if (!m_detailView || !m_bridge || !m_torrentClient)
        return;
    if (m_detailView->currentType() != QLatin1String("series"))
        return;

    const QList<StreamEpisode> episodes = m_detailView->episodesForSeason(season);
    if (m_detailView->currentImdb().isEmpty() || season <= 0 || episodes.isEmpty()) {
        if (m_detailView)
            m_detailView->setStreamSourcesError(tr("No episodes available for season download"));
        return;
    }

    const QStringList roots = m_bridge->rootFolders(QStringLiteral("videos"));
    if (roots.isEmpty() || roots.first().isEmpty()) {
        m_detailView->setStreamSourcesError(tr("Videos library root is not configured"));
        return;
    }

    // DOWNLOAD_AUTO_LIBRARY_ADD 2026-05-13 — Hemanth report "adding an
    // episode to download isn't automatically adding the show to the
    // library". The existing auto-add hook fires from the progressUpdated
    // lambda (StreamPage.cpp:2988) only on the FIRST successful playback
    // tick — i.e., download-without-play didn't pin the show. Stremio
    // behavior pins on any user gesture that implies intent to consume
    // (play OR download). Fire here too. autoAddToLibrary is idempotent
    // (m_library->has() short-circuit + add-only, never toggles off).
    m_detailView->autoAddToLibrary();

    m_bulkInput = BulkPlanInput{};
    m_bulkInput.seriesId = m_detailView->currentImdb();
    m_bulkInput.seriesTitle = m_detailView->currentTitle();
    if (m_bulkInput.seriesTitle.isEmpty())
        m_bulkInput.seriesTitle = m_bulkInput.seriesId;
    m_bulkInput.seriesYear = m_detailView->currentYear();
    m_bulkInput.seasonNumber = season;
    m_bulkInput.videosRootPath = roots.first();
    for (const StreamEpisode& episode : episodes) {
        BulkPlanEpisodeInput ep;
        ep.season = season;
        ep.episode = episode.episode;
        ep.title = episode.title;
        ep.extensionHint = QStringLiteral("mkv");
        m_bulkInput.episodes.push_back(ep);
    }

    m_bulkPlanResult = buildBulkPlan(m_bulkInput, [](const QString& path) {
        return QFileInfo::exists(path);
    });
    m_bulkSourcePayload = BulkSourceCollectionPayload{};
    m_bulkVerificationNote.clear();

    m_bulkProgressDialog = new QDialog(this);
    m_bulkProgressDialog->setWindowTitle(tr("Preparing stream download"));
    m_bulkProgressDialog->setModal(false);
    auto* layout = new QVBoxLayout(m_bulkProgressDialog);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(10);
    m_bulkProgressLabel = new QLabel(tr("Resolving sources..."), m_bulkProgressDialog);
    m_bulkProgressLabel->setTextFormat(Qt::PlainText);
    layout->addWidget(m_bulkProgressLabel);
    m_bulkProgressBar = new QProgressBar(m_bulkProgressDialog);
    m_bulkProgressBar->setRange(0, 0);
    layout->addWidget(m_bulkProgressBar);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, m_bulkProgressDialog);
    connect(buttons, &QDialogButtonBox::rejected, this, &StreamPage::cancelBulkSeasonDownload);
    layout->addWidget(buttons);
    connect(m_bulkProgressDialog, &QDialog::rejected,
            this, &StreamPage::cancelBulkSeasonDownload);
    m_bulkProgressDialog->show();

    m_bulkSourceCollector = new BulkSourceCollector(m_addonRegistry, this);
    connect(m_bulkSourceCollector, &BulkSourceCollector::progressTick,
            this, [this](int resolved, int total) {
                if (m_bulkProgressLabel) {
                    m_bulkProgressLabel->setText(
                        tr("Resolving sources... %1/%2").arg(resolved).arg(total));
                }
            });
    connect(m_bulkSourceCollector, &BulkSourceCollector::collectionComplete,
            this, &StreamPage::onBulkSourcesCollected);
    connect(m_bulkSourceCollector, &BulkSourceCollector::cancelled,
            this, &StreamPage::cancelBulkSeasonDownload);
    m_bulkSourceCollector->begin(m_bulkInput);
}

// STREAM_DOWNLOADS_NETFLIX_OVERHAUL — three new slots wired from StreamDetailView's
// seasonDownloadRequested / selectedEpisodesDownloadRequested / singleEpisodeDownloadRequested
// signals. All three funnel into triggerBulkSelectedEpisodes which builds the episode
// list then runs the normal BulkSourceCollector path.

void StreamPage::onSeasonDownloadRequested(int season)
{
    if (!m_detailView) return;
    triggerBulkSelectedEpisodes(m_detailView->currentImdb(), season, QList<int>{});
}

void StreamPage::onSelectedEpisodesDownloadRequested(int season, const QList<int>& episodes)
{
    if (!m_detailView) return;
    triggerBulkSelectedEpisodes(m_detailView->currentImdb(), season, episodes);
}

void StreamPage::onSingleEpisodeDownloadRequested(int season, int episode)
{
    if (!m_detailView) return;
    triggerBulkSelectedEpisodes(m_detailView->currentImdb(), season, QList<int>{episode});
}

// triggerBulkSelectedEpisodes — shared entry point for the three direct-dispatch
// slots. When episodeFilter is non-empty only those episode numbers are included
// in the m_bulkInput; empty filter = whole season (identical to the legacy
// triggerBulkSeasonDownload path). Uses the same BulkSourceCollector orchestration.
void StreamPage::triggerBulkSelectedEpisodes(const QString& imdbId, int season,
                                              const QList<int>& episodeFilter)
{
    using namespace tankostream::stream;

    cancelBulkSeasonDownload();

    if (!m_detailView || !m_bridge || !m_torrentClient)
        return;
    if (m_detailView->currentType() != QLatin1String("series"))
        return;

    const QList<StreamEpisode> allEpisodes = m_detailView->episodesForSeason(season);
    if (imdbId.isEmpty() || season <= 0 || allEpisodes.isEmpty()) {
        if (m_detailView)
            m_detailView->setStreamSourcesError(tr("No episodes available for season download"));
        return;
    }

    const QStringList roots = m_bridge->rootFolders(QStringLiteral("videos"));
    if (roots.isEmpty() || roots.first().isEmpty()) {
        m_detailView->setStreamSourcesError(tr("Videos library root is not configured"));
        return;
    }

    // DOWNLOAD_AUTO_LIBRARY_ADD 2026-05-13 — see triggerBulkSeasonDownload
    // for rationale. Same call here covers the per-row [↓] action-icon
    // single-episode dispatch path + the "Download Selected (N)" + the
    // P3-shipped onSeasonDownloadRequested signal. autoAddToLibrary is
    // idempotent.
    m_detailView->autoAddToLibrary();

    m_bulkInput = BulkPlanInput{};
    m_bulkInput.seriesId = imdbId;
    m_bulkInput.seriesTitle = m_detailView->currentTitle();
    if (m_bulkInput.seriesTitle.isEmpty())
        m_bulkInput.seriesTitle = imdbId;
    m_bulkInput.seriesYear = m_detailView->currentYear();
    m_bulkInput.seasonNumber = season;
    m_bulkInput.videosRootPath = roots.first();

    // Build the episode list — apply filter if non-empty
    QSet<int> filterSet;
    for (int ep : episodeFilter)
        filterSet.insert(ep);

    for (const StreamEpisode& episode : allEpisodes) {
        if (!filterSet.isEmpty() && !filterSet.contains(episode.episode))
            continue;
        BulkPlanEpisodeInput ep;
        ep.season = season;
        ep.episode = episode.episode;
        ep.title = episode.title;
        ep.extensionHint = QStringLiteral("mkv");
        m_bulkInput.episodes.push_back(ep);
    }

    if (m_bulkInput.episodes.isEmpty()) {
        m_detailView->setStreamSourcesError(tr("No matching episodes for download"));
        return;
    }

    m_bulkPlanResult = buildBulkPlan(m_bulkInput, [](const QString& path) {
        return QFileInfo::exists(path);
    });
    m_bulkSourcePayload = BulkSourceCollectionPayload{};
    m_bulkVerificationNote.clear();

    m_bulkProgressDialog = new QDialog(this);
    m_bulkProgressDialog->setWindowTitle(tr("Preparing stream download"));
    m_bulkProgressDialog->setModal(false);
    auto* layout = new QVBoxLayout(m_bulkProgressDialog);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(10);
    m_bulkProgressLabel = new QLabel(tr("Resolving sources..."), m_bulkProgressDialog);
    m_bulkProgressLabel->setTextFormat(Qt::PlainText);
    layout->addWidget(m_bulkProgressLabel);
    m_bulkProgressBar = new QProgressBar(m_bulkProgressDialog);
    m_bulkProgressBar->setRange(0, 0);
    layout->addWidget(m_bulkProgressBar);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, m_bulkProgressDialog);
    connect(buttons, &QDialogButtonBox::rejected, this, &StreamPage::cancelBulkSeasonDownload);
    layout->addWidget(buttons);
    connect(m_bulkProgressDialog, &QDialog::rejected,
            this, &StreamPage::cancelBulkSeasonDownload);
    m_bulkProgressDialog->show();

    m_bulkSourceCollector = new BulkSourceCollector(m_addonRegistry, this);
    connect(m_bulkSourceCollector, &BulkSourceCollector::progressTick,
            this, [this](int resolved, int total) {
                if (m_bulkProgressLabel) {
                    m_bulkProgressLabel->setText(
                        tr("Resolving sources... %1/%2").arg(resolved).arg(total));
                }
            });
    connect(m_bulkSourceCollector, &BulkSourceCollector::collectionComplete,
            this, &StreamPage::onBulkSourcesCollected);
    connect(m_bulkSourceCollector, &BulkSourceCollector::cancelled,
            this, &StreamPage::cancelBulkSeasonDownload);
    m_bulkSourceCollector->begin(m_bulkInput);
}
void StreamPage::retryBulkSeasonDownload(const QString& groupId, const QStringList& itemKeys)
{
    using namespace tankostream::stream;

    cancelBulkSeasonDownload();

    if (!m_torrentClient || groupId.isEmpty() || itemKeys.isEmpty())
        return;

    const QJsonObject groupObj = m_torrentClient->streamBulkGroups().value(groupId).toObject();
    if (groupObj.isEmpty())
        return;

    const StreamBulkGroupRecord group = streamBulkGroupRecordFromJson(groupId, groupObj);
    QSet<QString> retryKeys;
    for (const QString& itemKey : itemKeys)
        retryKeys.insert(itemKey);

    m_bulkRetryMode = true;
    m_bulkRetryGroupId = groupId;
    m_bulkRetryItemKeys = itemKeys;
    m_bulkVerificationNote.clear();
    m_bulkSourcePayload = BulkSourceCollectionPayload{};

    m_bulkInput = BulkPlanInput{};
    m_bulkInput.seriesId = group.sourceSeriesId;
    m_bulkInput.seriesTitle = group.label;
    if (m_bulkInput.seriesTitle.isEmpty())
        m_bulkInput.seriesTitle = group.sourceSeriesId;
    m_bulkInput.seasonNumber = group.sourceSeason;
    m_bulkInput.videosRootPath = group.destinationRoot;

    m_bulkPlanResult = BulkPlanResult{};
    for (const StreamBulkGroupItem& groupItem : group.items) {
        if (!retryKeys.contains(groupItem.itemKey))
            continue;
        const int episodeNum = episodeNumberFromBulkItemKey(groupItem.itemKey);
        if (episodeNum <= 0)
            continue;
        const int seasonNum = group.sourceSeason > 0
            ? group.sourceSeason
            : seasonNumberFromBulkItemKey(groupItem.itemKey);

        BulkPlanEpisodeInput episode;
        episode.season = seasonNum;
        episode.episode = episodeNum;
        episode.extensionHint = QStringLiteral("mkv");
        m_bulkInput.episodes.push_back(episode);

        BulkPlanItem planItem;
        planItem.input = episode;
        planItem.canonicalFilename = groupItem.canonicalFilename;
        planItem.canonicalRelativePath = groupItem.destinationKey;
        planItem.canonicalAbsolutePath = group.destinationRoot.isEmpty()
            ? QString()
            : QDir(group.destinationRoot).filePath(groupItem.destinationKey);
        planItem.status = BulkPlanItemStatus::PendingSource;
        planItem.itemKey = groupItem.itemKey;
        planItem.destinationKey = groupItem.destinationKey;
        m_bulkPlanResult.items.push_back(planItem);
    }

    if (m_bulkInput.seasonNumber <= 0 && !m_bulkInput.episodes.isEmpty())
        m_bulkInput.seasonNumber = m_bulkInput.episodes.first().season;

    if (m_bulkInput.episodes.isEmpty() || m_bulkInput.seasonNumber <= 0) {
        m_bulkRetryMode = false;
        m_bulkRetryGroupId.clear();
        m_bulkRetryItemKeys.clear();
        return;
    }

    m_bulkProgressDialog = new QDialog(this);
    m_bulkProgressDialog->setWindowTitle(tr("Retrying stream download"));
    m_bulkProgressDialog->setModal(false);
    auto* layout = new QVBoxLayout(m_bulkProgressDialog);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(10);
    m_bulkProgressLabel = new QLabel(tr("Resolving retry sources..."), m_bulkProgressDialog);
    m_bulkProgressLabel->setTextFormat(Qt::PlainText);
    layout->addWidget(m_bulkProgressLabel);
    m_bulkProgressBar = new QProgressBar(m_bulkProgressDialog);
    m_bulkProgressBar->setRange(0, 0);
    layout->addWidget(m_bulkProgressBar);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, m_bulkProgressDialog);
    connect(buttons, &QDialogButtonBox::rejected, this, &StreamPage::cancelBulkSeasonDownload);
    layout->addWidget(buttons);
    connect(m_bulkProgressDialog, &QDialog::rejected,
            this, &StreamPage::cancelBulkSeasonDownload);
    m_bulkProgressDialog->show();

    m_bulkSourceCollector = new BulkSourceCollector(m_addonRegistry, this);
    connect(m_bulkSourceCollector, &BulkSourceCollector::progressTick,
            this, [this](int resolved, int total) {
                if (m_bulkProgressLabel) {
                    m_bulkProgressLabel->setText(
                        tr("Resolving retry sources... %1/%2").arg(resolved).arg(total));
                }
            });
    connect(m_bulkSourceCollector, &BulkSourceCollector::collectionComplete,
            this, &StreamPage::onBulkSourcesCollected);
    connect(m_bulkSourceCollector, &BulkSourceCollector::cancelled,
            this, &StreamPage::cancelBulkSeasonDownload);
    m_bulkSourceCollector->begin(m_bulkInput);
}

void StreamPage::cancelBulkSeasonDownload()
{
    if (m_bulkSourceCollector) {
        auto* collector = m_bulkSourceCollector;
        m_bulkSourceCollector = nullptr;
        collector->disconnect(this);
        collector->cancel();
        collector->deleteLater();
    }
    if (m_bulkPackVerifier) {
        auto* verifier = m_bulkPackVerifier;
        m_bulkPackVerifier = nullptr;
        verifier->disconnect(this);
        verifier->cancel();
        verifier->deleteLater();
    }
    if (m_bulkProgressDialog) {
        auto* dialog = m_bulkProgressDialog;
        m_bulkProgressDialog = nullptr;
        m_bulkProgressLabel = nullptr;
        m_bulkProgressBar = nullptr;
        dialog->disconnect(this);
        dialog->hide();
        dialog->deleteLater();
    }
    m_bulkRetryMode = false;
    m_bulkRetryGroupId.clear();
    m_bulkRetryItemKeys.clear();
}

void StreamPage::onBulkSourcesCollected(const tankostream::stream::BulkSourceCollectionPayload& payload)
{
    using namespace tankostream::stream;

    if (payload.cancelled) {
        cancelBulkSeasonDownload();
        return;
    }
    m_bulkSourcePayload = payload;
    if (m_bulkSourceCollector) {
        m_bulkSourceCollector->deleteLater();
        m_bulkSourceCollector = nullptr;
    }

    BulkSourceCollectionPayload selectionPayload = m_bulkSourcePayload;
    if (m_bulkRetryMode) {
        for (auto it = selectionPayload.byEpisode.begin(); it != selectionPayload.byEpisode.end(); ++it) {
            QList<StreamPickerChoice> filtered;
            for (const StreamPickerChoice& choice : it->choices) {
                if (choice.packType != QLatin1String("season"))
                    filtered.push_back(choice);
            }
            it->choices = filtered;
        }
    }

    BulkSelectionPlan selection = buildBulkSelection(m_bulkPlanResult, selectionPayload);
    if (m_bulkRetryMode) {
        BulkPackVerificationResult result;
        result.updatedPlan = selection;
        onBulkPackVerified(result);
        return;
    }

    if (selection.mode == BulkSelectionMode::Pack && m_torrentClient) {
        if (m_bulkProgressLabel)
            m_bulkProgressLabel->setText(tr("Verifying pack..."));
        m_bulkPackVerifier = new BulkPackVerifier(m_torrentClient, this);
        connect(m_bulkPackVerifier, &BulkPackVerifier::verificationComplete,
                this, &StreamPage::onBulkPackVerified);
        connect(m_bulkPackVerifier, &BulkPackVerifier::verificationFailed,
                this, &StreamPage::onBulkPackVerificationFailed);
        connect(m_bulkPackVerifier, &BulkPackVerifier::cancelled,
                this, &StreamPage::cancelBulkSeasonDownload);
        m_bulkPackVerifier->begin(selection, m_bulkInput.seasonNumber);
        return;
    }

    BulkPackVerificationResult result;
    result.updatedPlan = selection;
    onBulkPackVerified(result);
}

void StreamPage::onBulkPackVerificationFailed(const QString& reason)
{
    using namespace tankostream::stream;

    if (m_bulkPackVerifier) {
        m_bulkPackVerifier->deleteLater();
        m_bulkPackVerifier = nullptr;
    }
    m_bulkVerificationNote = tr("Pack verification failed; using per-episode sources.");
    if (!reason.isEmpty())
        m_bulkVerificationNote += QStringLiteral(" ") + reason;

    BulkSourceCollectionPayload fallbackPayload = m_bulkSourcePayload;
    for (auto it = fallbackPayload.byEpisode.begin(); it != fallbackPayload.byEpisode.end(); ++it) {
        QList<StreamPickerChoice> filtered;
        for (const StreamPickerChoice& choice : it->choices) {
            if (choice.packType != QLatin1String("season"))
                filtered.push_back(choice);
        }
        it->choices = filtered;
    }

    BulkPackVerificationResult result;
    result.updatedPlan = buildBulkSelection(m_bulkPlanResult, fallbackPayload);
    onBulkPackVerified(result);
}

void StreamPage::onBulkPackVerified(const tankostream::stream::BulkPackVerificationResult& result)
{
    using namespace tankostream::stream;

    if (m_bulkPackVerifier) {
        m_bulkPackVerifier->deleteLater();
        m_bulkPackVerifier = nullptr;
    }
    if (m_bulkProgressDialog) {
        auto* dialog = m_bulkProgressDialog;
        m_bulkProgressDialog = nullptr;
        m_bulkProgressLabel = nullptr;
        m_bulkProgressBar = nullptr;
        dialog->disconnect(this);
        dialog->hide();
        dialog->deleteLater();
    }

    if (m_bulkRetryMode) {
        const QString retryGroupId = m_bulkRetryGroupId;
        const QStringList retryItemKeys = m_bulkRetryItemKeys;
        m_bulkRetryMode = false;
        m_bulkRetryGroupId.clear();
        m_bulkRetryItemKeys.clear();

        if (!m_torrentClient || retryGroupId.isEmpty())
            return;

        const QJsonObject groupObj = m_torrentClient->streamBulkGroups().value(retryGroupId).toObject();
        if (groupObj.isEmpty())
            return;

        for (const BulkSelectionItem& item : result.updatedPlan.items) {
            if (item.reason == BulkSelectionReason::MissingNoSource) {
                m_torrentClient->updateStreamBulkGroupItemState(
                    retryGroupId,
                    item.itemKey,
                    StreamBulkItemState::MissingSource,
                    tr("No source found during retry"));
            }
        }

        BulkPackVerificationResult retryResult = result;
        StreamBulkGroupRecord group = streamBulkGroupRecordFromJson(
            retryGroupId,
            m_torrentClient->streamBulkGroups().value(retryGroupId).toObject());
        if (!group.items.isEmpty())
            m_torrentClient->dispatchStreamBulkGroup(group, retryResult);
        Q_UNUSED(retryItemKeys);
        return;
    }

    const QString displayLabel = QStringLiteral("%1 - Season %2")
        .arg(m_bulkInput.seriesTitle)
        .arg(m_bulkInput.seasonNumber);
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — no preflight dialog.
    // Direct dispatch path (Spec §1 "no popup, no modal, no dialog").
    // All items from the verified selection plan are included; no picker filter.
    qInfo("STREAM_BULK direct dispatch: group=%s:%d items=%d",
          qUtf8Printable(m_bulkInput.seriesId), m_bulkInput.seasonNumber,
          (int)result.updatedPlan.items.size());

    QHash<QString, BulkPlanItem> planItemByKey;
    for (const BulkPlanItem& item : m_bulkPlanResult.items)
        planItemByKey.insert(item.itemKey, item);

    StreamBulkGroupRecord group;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    group.groupId = QStringLiteral("stream:%1:s%2:%3")
        .arg(m_bulkInput.seriesId)
        .arg(m_bulkInput.seasonNumber, 2, 10, QChar('0'))
        .arg(now);
    group.groupKind = QStringLiteral("streamSeason");
    group.label = displayLabel;
    group.sourceSeriesId = m_bulkInput.seriesId;
    group.sourceSeason = m_bulkInput.seasonNumber;
    group.destinationRoot = m_bulkInput.videosRootPath;
    group.createdAtMs = now;
    group.updatedAtMs = now;

    for (const BulkSelectionItem& selectionItem : result.updatedPlan.items) {
        const BulkPlanItem planItem = planItemByKey.value(selectionItem.itemKey);
        if (planItem.itemKey.isEmpty())
            continue;
        StreamBulkGroupItem groupItem;
        groupItem.itemKey = planItem.itemKey;
        groupItem.destinationKey = planItem.destinationKey;
        groupItem.canonicalFilename = planItem.canonicalFilename;
        if (selectionItem.reason == BulkSelectionReason::MissingNoSource) {
            groupItem.itemState = StreamBulkItemState::MissingSource;
            groupItem.lastError = tr("No source found for episode");
        } else if (selectionItem.reason == BulkSelectionReason::Picked ||
                   selectionItem.reason == BulkSelectionReason::PackCovered) {
            groupItem.infoHash = selectionItem.choice.infoHash;
            groupItem.fileIndex = selectionItem.choice.fileIndex;
            groupItem.itemState = StreamBulkItemState::Pending;
        } else {
            continue;
        }
        group.items.push_back(groupItem);
    }

    if (!group.items.isEmpty())
        emit streamBulkDispatchRequested(group, result, displayLabel);
    m_bulkVerificationNote.clear();
}

// STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) â€” handler for the detail
// view's "downloaded episode click" path. Runs the SubtitlesAggregator
// fan-out with a synthetic Stream so the popover behaves identically to
// streamed playback (Spec Â§10.2), then re-emits the public signal up to
// MainWindow which actually opens the VideoPlayer.
//
// NOTE on StreamProgress (Spec Â§10.1): the existing time_update / progress
// flow runs through CoreBridge::saveProgress at the bridge layer. Local-file
// playback via VideoPlayer currently writes under the `videos` domain â€” the
// epKey/`stream`-domain bridging is a separable follow-up (it requires
// teaching VideoPlayer to write per-episode stream-domain progress when
// opened via this gateway). The Continue Watching strip already picks up
// `stream:imdb:sNN:eMM` keys; once the bridging is wired, these sessions
// will surface there. Phase 4 ships the open + signal + subtitles flow;
// continue-watching surfacing for downloaded-from-stream episodes is
// tracked as a follow-up.
void StreamPage::onDetailPlayLocalFileFromStream(
    const QString& localPath, const QString& imdbId,
    const QString& showTitle, int season, int episode)
{
    // Spec Â§10.2 â€” synthetic Stream + SubtitlesAggregator query so OpenSubs
    // and any other subtitle-resource addons populate the popover. videoHash
    // is intentionally empty: we don't compute the OpenSubs SubDB hash for
    // local files; the addon falls back to imdbId+filename matching.
    if (m_subtitlesAggregator && !imdbId.isEmpty() && season > 0 && episode > 0) {
        tankostream::stream::SubtitleLoadRequest req;
        req.type = QStringLiteral("series");
        req.id   = imdbId + QLatin1Char(':')
                          + QString::number(season) + QLatin1Char(':')
                          + QString::number(episode);

        tankostream::addon::Stream synth;
        const QFileInfo fi(localPath);
        synth.behaviorHints.filename  = fi.fileName();
        synth.behaviorHints.videoSize = fi.size();
        synth.behaviorHints.videoHash = QString();   // omitted for local files
        synth.name                    = showTitle;
        req.selectedStream = synth;

        m_subtitlesAggregator->load(req);
    }

    // Forward to MainWindow to actually open the local-file VideoPlayer.
    emit playLocalFileFromStreamRequested(localPath, imdbId, showTitle, season, episode);
}

void StreamPage::onSourceActivated(const tankostream::stream::StreamPickerChoice& choice)
{
    if (!m_session.pending.valid) return;   // late click after the user backed out

    // Phase 2 Batch 2.4 â€” if the user clicked a source card manually during
    // the auto-launch window, cancel the pending timer + hide the toast.
    // onAutoLaunchFire calls back into us for the automated path, but by the
    // time we're here the timer should already be stopped â€” this is the
    // user-click entry.
    if (m_autoLaunchTimer && m_autoLaunchTimer->isActive()) {
        cancelAutoLaunch();
    }

    const PendingPlay ctx = m_session.pending;
    m_session.pending.valid = false;

    // STREAM_NAV_BACK_STACK 2026-05-07 â€” Hemanth follow-up: closing the
    // player from a Season 2 episode was restoring the Detail view back
    // to Season 1 because the NavEntry.detailPreselectSeason was frozen
    // at the initial showDetail() call (typically -1 for "no
    // preselection" â†’ defaults to season 1). The user-driven season
    // combo change inside StreamDetailView never propagated to the
    // stack entry. Update the top Detail entry's preselects here, where
    // we have the actually-playing season/episode in `ctx`. By the time
    // launchPlayer's m_beforePlayerEntry snapshot fires, the top of
    // m_navStack already reflects what they were watching, so close +
    // restore lands on the correct season. Non-series media (movies,
    // ad-hoc trailers) skip the update â€” ctx.season/episode are 0/0
    // there and we don't want to clobber the entry's defaults.
    if (!m_navStack.isEmpty()
     && m_navStack.top().kind == NavEntry::Kind::Detail
     && ctx.mediaType == QLatin1String("series")
     && ctx.season > 0
     && ctx.episode > 0) {
        m_navStack.top().detailPreselectSeason  = ctx.season;
        m_navStack.top().detailPreselectEpisode = ctx.episode;
    }

    // STREAM_LIFECYCLE_FIX Phase 4 Batch 4.2 â€” audit P2-2 close. Pre-4.2 code
    // reset only m_session.nearEndCrossed + m_session.nextPrefetch inline,
    // skipping m_session.nextShortcutPending + the MetaAggregator/StreamAggregator
    // disconnect logic. resetNextEpisodePrefetch() is the canonical cleanup
    // that Phase 1 Batch 1.3 migrated to route through m_session â€” using it
    // here ensures all three prefetch-related session fields clear uniformly
    // AND in-flight aggregator connections from the prior episode's prefetch
    // get dropped (preventing a stale streamsReady lambda from landing against
    // the NEW episode's prefetch slot with the old episode's stream list).
    // Note: resetSession() would be over-broad â€” it also clears m_session.epKey
    // and pending, which onSourceActivated is about to re-install via ctx. The
    // narrower resetNextEpisodePrefetch is the right choice.
    resetNextEpisodePrefetch();

    // Save choice for re-use â€” extended payload with addon + source-kind fields.
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
    // Phase 2 Batch 2.4 â€” stamp the "last-used" timestamp so the auto-launch
    // 10-minute gate has a read source for both per-episode and per-series
    // resume paths.
    saved["updatedAt"]     = QDateTime::currentMSecsSinceEpoch();
    StreamChoices::saveChoice(ctx.epKey, saved);

    // Phase 2 Batch 2.3 â€” series-level source memory. When the addon
    // declares `behaviorHints.bingeGroup` on this stream, persist a parallel
    // per-series entry so the next episode's picker can default-highlight a
    // matching source from the same release group. Movies don't need this
    // layer â€” per-movie saveChoice covers single-title state already.
    if (ctx.mediaType == QLatin1String("series")
        && !choice.stream.behaviorHints.bingeGroup.isEmpty()) {
        StreamChoices::saveSeriesChoice(ctx.imdbId, saved);
    }

    m_session.epKey = ctx.epKey;

    m_mainStack->setCurrentIndex(2);
    m_bufferLabel->setText("Connecting...");
    m_bufferOverlay->show();

    // Batch 5.3 â€” fan out a subtitle request for the selected stream in
    // parallel with playback prep. Result lands in the SubtitlePopover
    // via the subtitlesReady connection wired in the ctor.
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

// â”€â”€â”€ Player controller signals â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void StreamPage::onBufferUpdate(const QString& statusText, double /*percent*/)
{
    m_bufferLabel->setText(statusText);
}

void StreamPage::onReadyToPlay(const QString& httpUrl)
{
    m_bufferOverlay->hide();

    // Find the VideoPlayer in the widget hierarchy (created by MainWindow)
    // and open the stream URL as a file â€” the sidecar handles HTTP URLs
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
    // STREAM_AV_SUB_SYNC_AFTER_STALL 2026-04-21 â€” wire stream-engine stall
    // signals to VideoPlayer â†’ sidecar IPC. Disconnect existing bindings
    // first so a previous session's player instance doesn't double-fire.
    // Direct-connect from StreamEngine to player (both live across sessions
    // so no lifetime concerns â€” StreamEngine is owned by StreamPage,
    // VideoPlayer is owned by MainWindow, both outlive this lambda).
    // Hash-filter: only forward signals for the currently-active stream â€”
    // StreamEngine may have multiple sessions in m_streams; we only care
    // about the one our VideoPlayer is playing.
    //
    // STREAM_STALL_RECOVERY_UX 2026-04-22 â€” also drive UI state (setStream
    // Stalled + setStreamStallInfo) from the edge signal here, not only
    // from the polling path in progressUpdated below. Direction C smoke
    // at 14:48 proved the polling path is structurally dead during stalls:
    // onStreamStallEdgeFromEngine(true) forwards to sidecar sendStallPause,
    // which halts the audio decoder (audio_decoder.cpp:104) which freezes
    // AVSyncClock, which suppresses the sidecar's time_update IPC â€”
    // so onTimeUpdate never fires, progressUpdated never emits, the
    // polling lambda never runs, statsSnapshot is never consulted, and
    // the LoadingOverlay never shows. Edge signal is the ONLY reliable
    // trigger during a stall. piece + peerHaveCount come on the signal
    // already â€” previously discarded as `/*piece*/`, now forwarded so
    // the overlay's "Buffering â€” waiting for piece N (K peers have it)"
    // text is honest on the first transition without needing a second
    // polling tick. Polling path in progressUpdated stays as belt-and-
    // braces for any future path that fires setStreamStalled without
    // the edge signal (pre-metadata stalls, edge-signal lost to overflow,
    // etc.) â€” setStreamStalled's transition-only dedup makes the
    // redundancy free.
    if (m_streamEngine) {
        // STREAM_SERVER_PIVOT Phase 1 (2026-04-24) â€” lambda bodies shared
        // across both engine backends. Qt's disconnect/connect function-
        // pointer form needs the concrete Q_OBJECT type; we branch by
        // dynamic_cast and reuse the same lambdas for both concrete types.
        // StreamServerEngine never emits these signals in Phase 1 (stream-
        // server doesn't expose a cleanly-stall predicate in stats.json);
        // the connect block is harmless when inactive.
        auto onStall = [this, player](const QString& infoHash, int piece,
                                       qint64 /*waitMs*/, int peerHaveCount) {
            const QString active = m_playerController
                ? m_playerController->currentInfoHash()
                : QString();
            if (infoHash != active) return;
            if (!player) return;
            // UI state first so the overlay shows before the sidecar
            // pause freezes the clock (UX-ordering; either order is
            // correct).
            player->setStreamStalled(true);
            player->setStreamStallInfo(piece, peerHaveCount);
            player->onStreamStallEdgeFromEngine(true);
        };
        auto onRecover = [this, player](const QString& infoHash, int /*piece*/,
                                         qint64 /*elapsedMs*/, const QString& /*via*/) {
            const QString active = m_playerController
                ? m_playerController->currentInfoHash()
                : QString();
            if (infoHash != active) return;
            if (!player) return;
            // Dismiss overlay first so it disappears before the
            // sidecar resume un-freezes the clock (user sees the
            // overlay clear, then audio resumes â€” matches the
            // setStreamStalled(true) ordering on entry).
            player->setStreamStalled(false);
            player->onStreamStallEdgeFromEngine(false);
        };

        // STREAM_SERVER_PIVOT Phase 3 (2026-04-25) â€” single-backend world;
        // dual-engine dynamic_cast branch collapsed. StreamServerEngine
        // doesn't emit stallDetected/stallRecovered today (Phase 2B deferred
        // Item 6), so these connects are dormant â€” the player's own stall
        // detection (VideoPlayer::onStreamStallEdgeFromEngine callers) still
        // works via other paths. Connect anyway for forward-compat when
        // stall signals are derived from dlSpeed=0.
        disconnect(m_streamEngine, &StreamServerEngine::stallDetected, this, nullptr);
        disconnect(m_streamEngine, &StreamServerEngine::stallRecovered, this, nullptr);
        connect(m_streamEngine, &StreamServerEngine::stallDetected,  this, onStall);
        connect(m_streamEngine, &StreamServerEngine::stallRecovered, this, onRecover);
    }

    connect(player, &VideoPlayer::progressUpdated, this,
        [this, player](const QString& /*path*/, double posSec, double durSec) {
            // STREAM_STALL_UX_FIX Batch 1 â€” push the stream-engine stall flag
            // into VideoPlayer each tick (~1 Hz). Pulled from statsSnapshot
            // (sentinel-safe on unknown hash). Kept outside the 2s deadline-
            // retarget gate so HUD transitions follow the 2s stall watchdog
            // cadence with only ~1s progressUpdated aliasing, not +2s extra.
            // No-op in non-stream playback (infoHash empty â†’ setStreamStalled
            // never gets called; VideoPlayer defaults m_streamStalled=false).
            //
            // Batch 2 â€” when stalled, also push stallPiece + stallPeerHaveCount
            // so LoadingOverlay's stall-diagnostic text carries "waiting for
            // piece N (K peers have it)". setStreamStalled(true) shows the
            // overlay on the falseâ†’true transition; setStreamStallInfo runs
            // every stalled tick to refresh the numbers if stallPiece advances
            // or peer count changes.
            {
                const QString stallHash = m_playerController
                    ? m_playerController->currentInfoHash()
                    : QString();
                if (player && !stallHash.isEmpty() && m_streamEngine) {
                    const StreamEngineStats stats = m_streamEngine->statsSnapshot(stallHash);
                    player->setStreamStalled(stats.stalled);
                    if (stats.stalled) {
                        player->setStreamStallInfo(stats.stallPiece,
                                                   stats.stallPeerHaveCount);
                    }
                }
            }

            // STREAM_CONTINUE_LIBRARY_AND_HUD_AUTOFIRE 2026-05-06 â€” diagnostic
            // trace baked in via DebugLogBuffer so `tankoctl logs` surfaces
            // every progressUpdated tick during stream playback. Hemanth-driven
            // smoke captures the actual posSec/durSec values for RC validation
            // (no MCP this RTC means no in-wake empirical run; trace stays in
            // tree until follow-up RTC after Hemanth's first smoke confirms
            // whether durSec lands as 0 across HTTP-URL streams as predicted).
            auto& dlog = DebugLogBuffer::instance();
            const QString epKey = m_session.epKey;

            dlog.info("stream",
                QStringLiteral("[STREAM_PROGRESS_TRACE] tick epKey=%1 posSec=%2 durSec=%3")
                    .arg(epKey).arg(posSec, 0, 'f', 2).arg(durSec, 0, 'f', 2));

            if (epKey.isEmpty()) {
                dlog.info("stream",
                    QStringLiteral("[STREAM_PROGRESS_TRACE] skip â€” empty epKey"));
                return;
            }

            // STREAM_CONTINUE_LIBRARY_AND_HUD_AUTOFIRE Bug 1 fix: gate relaxed.
            // Was: `if (posSec < 5.0 || durSec <= 0.0) return;`
            // HTTP-URL streams via stream-server commonly land durSec=0 because
            // the sidecar's demuxer discards FROM_BITRATE estimates as
            // unreliable (see VideoPlayer.cpp:1122-1126 â€” same anti-lie
            // contract that motivates "â€”:â€”" duration in the HUD). Saving with
            // durSec=0 is downstream-safe: StreamProgress::percent guards
            // div-by-0 (returns 0.0); StreamContinueStrip::refresh filters
            // only on `pos < MIN_POSITION_SEC` (StreamContinueStrip.cpp:106);
            // isFinished returns false when percent < 90%. Drop the durSec
            // gate so Continue Watching gets entries even when the addon
            // doesn't expose duration. The 5s posSec floor is preserved â€”
            // probe/initial-0 ticks still get filtered.
            if (posSec < 5.0) {
                dlog.info("stream",
                    QStringLiteral("[STREAM_PROGRESS_TRACE] skip â€” posSec under 5s gate (posSec=%1)")
                        .arg(posSec, 0, 'f', 2));
                return;
            }

            const bool finished = (durSec > 0 && posSec / durSec >= 0.9);
            const QJsonObject state = StreamProgress::makeWatchState(posSec, durSec, finished);
            m_bridge->saveProgress("stream", epKey, state);

            dlog.info("stream",
                QStringLiteral("[STREAM_PROGRESS_TRACE] saved epKey=%1 posSec=%2 durSec=%3 finished=%4")
                    .arg(epKey).arg(posSec, 0, 'f', 2).arg(durSec, 0, 'f', 2).arg(finished ? 1 : 0));

            // STREAM_CONTINUE_LIBRARY_AND_HUD_AUTOFIRE Bug 2 fix: auto-add to
            // StreamLibrary on the FIRST successful save in this session.
            // Stremio behavior â€” opening + playing a stream implicitly pins
            // it. Idempotent (StreamDetailView::autoAddToLibrary checks
            // m_library->has(imdbId) before add). Once-per-session gate via
            // m_session.autoLibraryAdded so we don't churn add() every tick.
            // Coupled with Bug 1 â€” even after the gate-relax fix, Continue
            // Watching's StreamContinueStrip filters at line 121 with
            // `!m_library->has(imdbId)`; library auto-add is the second piece
            // that makes the just-watched tile appear in the strip on close.
            if (!m_session.autoLibraryAdded && m_detailView) {
                m_session.autoLibraryAdded = true;
                m_detailView->autoAddToLibrary();
                dlog.info("stream",
                    QStringLiteral("[STREAM_PROGRESS_TRACE] auto-library-add fired (first save in session)"));
            }

            // STREAM_PLAYBACK_FIX Phase 2 Batch 2.3 â€” sliding-window deadline
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
                // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.3 â€” playback-phase
                // buffered-range emit. Same 2s cadence as updatePlaybackWindow
                // (both driven off the progressUpdated tick, rate-gated by
                // m_session.lastDeadlineUpdateMs). StreamPlayerController's
                // own pollStreamStatus timer has stopped post-readyToStart,
                // so this is the sole refresh source during playback â€” keeps
                // SeekSlider's buffered overlay current as pieces arrive
                // mid-stream. Equality-dedup inside pollBufferedRangesOnce
                // short-circuits when no new pieces have completed.
                if (m_playerController) {
                    m_playerController->pollBufferedRangesOnce();
                }
            }

            // Phase 2 Batch 2.5 â€” near-end detection: at 95% OR within 60s
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
                    //   "stream:ttXXXXXXX"              (movie â€” no next)
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

    // STREAM_AUTO_NEXT_ESTIMATE_FIX 2026-04-21 â€” parallel nearEndCrossed
    // trigger driven by the sidecar's byte-position watchdog instead of
    // the pct/remaining duration check above. Required for bitrate-estimate
    // sources (HUD tilde-prefix) where AVFormatContext::duration is ~2x
    // inflated and the `pct >= 0.95 || remaining <= 60` check at line ~1989
    // is structurally unreachable. Both triggers coexist â€” whichever fires
    // first wins; the `m_session.nearEndCrossed` guard prevents double-fire.
    // Honest-duration sources now fire AUTO_NEXT ~30 s earlier (90 s bytes
    // before EOF vs 60 s remaining) which is a free prefetch-budget bonus.
    if (player && player->sidecarProcess()) {
        disconnect(player->sidecarProcess(), &IPlayerBackend::nearEndEstimate,
                   this, nullptr);
        connect(player->sidecarProcess(), &IPlayerBackend::nearEndEstimate, this,
                [this]() {
                    if (m_session.nearEndCrossed) return;  // fire-once guard
                    const QString epKey = m_session.epKey;
                    if (epKey.isEmpty()) return;
                    const QStringList parts = epKey.split(':');
                    if (parts.size() < 4 || parts[0] != QLatin1String("stream"))
                        return;
                    m_session.nearEndCrossed = true;
                    const QString imdbId = parts[1];
                    const int season  = parts[2].mid(1).toInt();
                    const int episode = parts[3].mid(1).toInt();
                    startNextEpisodePrefetch(imdbId, season, episode);
                },
                Qt::UniqueConnection);
    }

    // On player close â†’ stop stream, clear progress key, refresh continue strip.
    // Also reset persistence mode so next Videos-mode playback writes to the
    // "videos" store as expected (pairs with the None set before openFile).
    // Phase 2 Batch 2.5 â€” if the user crossed the 95% near-end threshold AND
    // the pre-fetch landed a matched next-episode source, show the next-
    // episode overlay instead of returning to browse. User can accept
    // (Play Now / countdown) to binge OR Cancel to end the session.
    // Phase 2 Batch 2.6 â€” Shift+N manual next-episode shortcut. Replaces
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
        // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.3 â€” disable buffered-range
        // overlay + drop the per-session signal connection before persistence
        // mode flips back. Ordering: setStreamMode(false) first defangs any
        // emit-in-flight from a final StreamPlayerController poll tick
        // before disconnect runs; disconnect removes the wire; persistence
        // flip is unchanged. VideoPlayer::teardownUi also clears the
        // SeekSlider overlay directly on next open (belt + suspenders
        // against visible-stale-paint).
        player->setStreamMode(false);
        disconnect(m_playerController, &StreamPlayerController::bufferedRangesChanged,
                   player, &VideoPlayer::onBufferedRangesChanged);
        player->setPersistenceMode(VideoPlayer::PersistenceMode::LibraryVideos);

        // Phase 2 Batch 2.5 â€” overlay must be shown BEFORE stopStream. The
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
        // STREAM_PLAYER_CLOSE_FIX 2026-04-25 â€” explicit stopPlayback mirroring
        // MainWindow::closeVideoPlayer (MainWindow.cpp:557). Without this,
        // m_playerController->stopStream() below only removes the engine-side
        // torrent â€” the sidecar process keeps running with already-buffered
        // HTTP bytes, so audio + video continue playing until the buffer
        // drains (multiple seconds on HD content). stopPlayback fires
        // sendStop + sendShutdown to the sidecar so it gracefully closes
        // the AV pipeline FIRST, then we tell the engine to free the
        // torrent. Hemanth-reported "video keeps playing even after I close
        // the video" 2026-04-25.
        player->stopPlayback();
        m_playerController->stopStream();
        m_continueStrip->refresh();
        m_libraryLayout->refresh();
    });

    // Stream-mode playback: suppress VideoPlayer's internal "videos"-domain
    // bridge reads/writes. Progress still flows via the progressUpdated â†’
    // saveProgress("stream", epKey, state) lambda above. Fixes the stream â†’
    // videos continue-watching leak routed by Agent 0 at chat.md:9661.
    player->setPersistenceMode(VideoPlayer::PersistenceMode::None);

    // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.3 â€” enable buffered-range
    // overlay rendering for this stream session. Pairs with setStreamMode
    // (false) in the closeRequested handler below, mirroring the
    // setPersistenceMode bookend discipline. Per-session connect + emit
    // flow: StreamPlayerController::bufferedRangesChanged â†’ VideoPlayer::
    // onBufferedRangesChanged â†’ SeekSlider::setBufferedRanges paint.
    // UniqueConnection defends against re-wire if a prior session's
    // connection wasn't disconnected (belt-and-suspenders â€” closeRequested
    // below clears via disconnect, but stream-failure paths are varied).
    player->setStreamMode(true);
    connect(m_playerController, &StreamPlayerController::bufferedRangesChanged,
            player, &VideoPlayer::onBufferedRangesChanged,
            Qt::UniqueConnection);

    // Phase 1 Batch 1.3 (STREAM_UX_PARITY) â€” read the stream-domain saved
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

    // STREAM_PLAYBACK_FIX Phase 2 Batch 2.4 â€” seek/resume target pre-gate.
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
    // 9s cap (at which point we proceed anyway â€” the in-player buffering
    // path from Batch 1.2 handles residual delay gracefully).
    //
    // Zero-resume path (streamResumeSec == 0.0) bypasses the pre-gate â€”
    // the Batch 2.2 head deadline already covers byte-offset 0.
    // 2026-04-15 fix-up â€” always cancel any outstanding seek-retry state
    // at the top of onReadyToPlay. Every path below either launches the
    // player synchronously (and the orphan retry must not fire after) or
    // sets up a fresh retry state. One-shot invalidation here covers all
    // exit paths without scattering the cancel call. (Batch 1.3: the
    // generation-check in the retry closure below would also abort any
    // orphan from a prior session, but clearing here is still valuable for
    // same-session re-entries like an immediate re-open of the same URL.)
    m_session.seekRetry.reset();

    // Stream-mode HUD title: pass the resolved filename (or direct-URL
    // Stream.name fallback) into VideoPlayer so the bottom bar shows
    // the real title instead of the last URL path segment (which is
    // just the file-index digit â€” "0", "3" â€” from
    // http://127.0.0.1:PORT/stream/{hash}/{idx}). Cached on the
    // controller side per-session; empty until metadata lands on the
    // magnet path (harmless â€” updateTitleElision overwrites on the
    // next open).
    const QString streamHudTitle = m_playerController
                                       ? m_playerController->currentFileName()
                                       : QString();
    auto launchPlayer = [this, player, httpUrl, streamResumeSec, mainWin,
                         streamHudTitle]() {
        // STREAM_NAV_BACK_STACK 2026-05-06 â€” snapshot the current top-of-
        // stack view so onStreamStopped UserEnd / the defensive 3s timer /
        // onNextEpisodeCancel case (a) can all restore it after teardown
        // instead of yanking the user to library home (Hemanth-reported
        // 2026-05-06: "I close the player I find myself on the library
        // rather than the series page"). The user almost always launched
        // from a Detail view; this snapshot brings them back there.
        m_beforePlayerEntry = m_navStack.isEmpty()
            ? std::optional<NavEntry>{}
            : std::make_optional(m_navStack.top());

        player->openFile(httpUrl, {}, 0, streamResumeSec, streamHudTitle);
        if (auto* mw = qobject_cast<QMainWindow*>(mainWin))
            player->setGeometry(mw->centralWidget()->rect());
        else
            player->setGeometry(mainWin->rect());
        player->show();
        player->raise();
        // STREAM_PLAYER_FOCUS_FIX 2026-04-25 â€” explicit setFocus mirroring
        // MainWindow::openVideoPlayer (MainWindow.cpp:551). Without this, the
        // player widget shows + raises but keyboard focus stays on whichever
        // tile / search input had it before, so Space (toggle_pause) and
        // every other shortcut never reach VideoPlayer::keyPressEvent.
        // Hemanth-reported "can't pause the video in stream mode" 2026-04-25.
        player->setFocus();
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
    // iterations Ã— 300ms) then fall through.
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

    // STREAM_LIFECYCLE_FIX Phase 1 Batch 1.3 â€” seek-retry orphan guard now
    // uses PlaybackSession generation instead of raw-QObject* identity.
    // Pre-1.3: a prior onReadyToPlay session created `new QObject(this)`,
    // stored the address in m_seekRetryState, and the retry closure
    // compared its captured pointer against m_seekRetryState at fire time
    // to detect replacement. Post-1.3: m_session.seekRetry is a
    // std::shared_ptr<SeekRetryState> carrying the captured generation +
    // attempt counter. The retry closure captures currentGeneration() at
    // setup; `isCurrentGeneration(retryGen)` aborts silently on any
    // session turnover. Closes the same class as the original fix
    // (orphan retries post close/re-open â†’ double openFile â†’ sidecar
    // boot race) via a more honest identity model â€” generation turns over
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
        // without an active session â€” theoretically impossible once
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
            // 9s cap â€” launch anyway; Batch 1.2 HTTP retry handles rest.
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
    cancelAutoLaunch();   // Phase 2 Batch 2.4 â€” clear any pending resume UI.
    hideNextEpisodeOverlay();   // Phase 2 Batch 2.5 â€” clear next-ep state.
    resetNextEpisodePrefetch();
    // 2026-04-15 â€” cancel any pending seek-pre-gate retry on failure.
    m_session.seekRetry.reset();
    // Disconnect any lingering progress connection + reset persistence mode
    // defensively â€” if setPersistenceMode(None) fired in onReadyToPlay but
    // playback never started cleanly, the next Videos-mode open would
    // otherwise inherit None and silently skip its own progress write.
    if (auto* player = window() ? window()->findChild<VideoPlayer*>() : nullptr) {
        disconnect(player, &VideoPlayer::progressUpdated, this, nullptr);
        disconnect(player, &VideoPlayer::closeRequested, this, nullptr);
        disconnect(player, &VideoPlayer::streamNextEpisodeRequested, this, nullptr);
        // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.3 â€” mirror the close
        // path's stream-mode teardown here on failure so a later library
        // open doesn't inherit stale setStreamMode(true) state.
        player->setStreamMode(false);
        disconnect(m_playerController, &StreamPlayerController::bufferedRangesChanged,
                   player, &VideoPlayer::onBufferedRangesChanged);
        player->setPersistenceMode(VideoPlayer::PersistenceMode::LibraryVideos);
    }
    m_bufferLabel->setText("Stream failed: " + message);
    // STREAM_LIFECYCLE_FIX Phase 3 Batch 3.2 â€” generation-check + user-navigation
    // guard on the 3s auto-navigate timer. Audit P1-2 scenario: failure at T,
    // user nav to AddonManager at T+0.5s, T+3s fires, isActive() still false,
    // showBrowse() yanks user back off AddonManager. Triple-gate the fire:
    //   (a) isCurrentGeneration â€” aborts if a new session started after
    //       failure (user clicked a different tile, Shift+N, etc.). First
    //       real consumer of the Batch 1.3 generation-check pattern in the
    //       failure path specifically.
    //   (b) still-on-player-layer â€” mainStack index 2 is the only layer
    //       where the failure label is visible. If user navigated to
    //       detail(1) / browse(0) / addon(3) / other(4+) the failure
    //       countdown is no longer user-visible and we must not yank them.
    //       NOTE: TODO text at line 243 had `!= 0 /*not browse*/` which
    //       would invert intent (return for every non-browse layer, including
    //       the player layer we want to navigate FROM). Taking `!= 2` as the
    //       correct check per my read of the user-facing UX.
    //   (c) !isActive â€” preserved from pre-3.2 for belt-and-suspenders. If
    //       (a) and (b) both pass but a new playback is somehow active,
    //       showBrowse would interrupt it. Unlikely post-(a) but cheap.
    const quint64 gen = currentGeneration();
    QTimer::singleShot(3000, this, [this, gen]() {
        if (!isCurrentGeneration(gen)) return;
        constexpr int kPlayerLayerIndex = 2;
        if (m_mainStack->currentIndex() != kPlayerLayerIndex) return;
        // STREAM_NAV_BACK_STACK 2026-05-06 â€” restore pre-player view if
        // we have a snapshot (typical post-failure recovery path); fall
        // back to library if no snapshot was captured (defensive).
        if (!m_playerController->isActive()) restorePlayerExitView();
    });
}

void StreamPage::onStreamStopped(StreamPlayerController::StopReason reason)
{
    using StopReason = StreamPlayerController::StopReason;
    auto* player = window() ? window()->findChild<VideoPlayer*>() : nullptr;

    // STREAM_LIFECYCLE_FIX Phase 2 Batch 2.2 â€” source-switch reentrancy split.
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
    // alone â€” beginSession at the new session's entry already clobbered it.
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
    // drives the full failure UX â€” sets "Stream failed: msg" on the buffer
    // overlay label, starts a 3s timer, then navigates to browse. Running the
    // UserEnd teardown below would hide the buffer overlay (and therefore the
    // failure label) before onStreamFailed can fire, collapsing the 3s error
    // display window. Early-return here; onStreamFailed owns the UX.
    // Observability side effect: the [stream-session] log in stopStream already
    // captured the failure boundary â€” this signal is the hook for future
    // Phase 3 failure-flow consolidation.
    if (reason == StopReason::Failure) {
        return;
    }

    // UserEnd â€” normal end-of-session teardown.
    m_session.epKey.clear();
    if (player) {
        disconnect(player, &VideoPlayer::progressUpdated, this, nullptr);
        disconnect(player, &VideoPlayer::closeRequested, this, nullptr);
        disconnect(player, &VideoPlayer::streamNextEpisodeRequested, this, nullptr);
        // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.3 â€” mirror stream-mode
        // teardown here for the UserEnd path that arrives via direct
        // stopStream(UserEnd) rather than through the closeRequested
        // lambda (e.g. esc-key-to-stop scenarios post-STREAM_LIFECYCLE_FIX).
        player->setStreamMode(false);
        disconnect(m_playerController, &StreamPlayerController::bufferedRangesChanged,
                   player, &VideoPlayer::onBufferedRangesChanged);
        player->setPersistenceMode(VideoPlayer::PersistenceMode::LibraryVideos);
    }
    m_bufferOverlay->hide();
    // Phase 2 Batch 2.5 â€” if the next-episode overlay is visible (player
    // closed at near-end with a matched prefetch), keep the user on the
    // player layer so they can see the countdown + Play Now/Cancel buttons.
    // showBrowse would navigate to index 0 and orphan the overlay.
    if (m_nextEpisodeOverlay && m_nextEpisodeOverlay->isVisible()) {
        return;
    }
    // STREAM_NAV_BACK_STACK 2026-05-06 â€” load-bearing for Hemanth's bug.
    // Was: showBrowse() â€” yanked the user to library on every player
    // close. Now: restore the pre-player view from the launchPlayer
    // snapshot so the user lands on their originating Detail / Catalog
    // / Search page. Falls back to showBrowse only when no snapshot
    // exists (e.g., stream started without going through onSourceActivated
    // or the snapshot was already consumed).
    restorePlayerExitView();
}

