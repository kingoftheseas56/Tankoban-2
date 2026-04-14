#include "StreamPage.h"

#include "core/CoreBridge.h"
#include "core/stream/CinemetaClient.h"
#include "core/stream/TorrentioClient.h"
#include "core/stream/StreamEngine.h"
#include "core/stream/StreamLibrary.h"
#include "core/torrent/TorrentEngine.h"
#include "stream/StreamLibraryLayout.h"
#include "stream/StreamSearchWidget.h"
#include "stream/StreamDetailView.h"
#include "stream/TorrentPickerDialog.h"
#include "stream/StreamPlayerController.h"
#include "stream/StreamContinueStrip.h"
#include "core/stream/StreamProgress.h"

#include "ui/player/VideoPlayer.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QMainWindow>

StreamPage::StreamPage(CoreBridge* bridge, TorrentEngine* torrentEngine,
                       QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
    , m_torrentEngine(torrentEngine)
{
    setObjectName("stream");

    // Core services
    m_cinemeta  = new CinemetaClient(this);
    m_torrentio = new TorrentioClient(this);
    m_library   = new StreamLibrary(&bridge->store(), this);

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
    if (m_continueStrip)
        m_continueStrip->refresh();
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
    rootLayout->addWidget(m_searchBarFrame);

    m_mainStack = new QStackedWidget(this);

    buildBrowseLayer();
    m_mainStack->addWidget(m_browseLayer);   // index 0: browse

    // Detail layer
    m_detailView = new StreamDetailView(m_bridge, m_cinemeta, m_torrentio, m_library, this);
    m_mainStack->addWidget(m_detailView); // index 1: detail

    connect(m_detailView, &StreamDetailView::backRequested, this, &StreamPage::showBrowse);
    connect(m_detailView, &StreamDetailView::playRequested, this, &StreamPage::onPlayRequested);

    // Wire library grid double-click → show detail
    connect(m_libraryLayout, &StreamLibraryLayout::showClicked, this, &StreamPage::showDetail);

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
    m_mainStack->addWidget(playerLayer); // index 2: player

    rootLayout->addWidget(m_mainStack, 1);

    m_mainStack->setCurrentIndex(0);
}

void StreamPage::buildSearchBar()
{
    m_searchBarFrame = new QFrame(this);
    m_searchBarFrame->setObjectName("streamSearchBar");

    auto* layout = new QHBoxLayout(m_searchBarFrame);
    layout->setContentsMargins(16, 8, 16, 8);
    layout->setSpacing(8);

    m_searchInput = new QLineEdit(m_searchBarFrame);
    m_searchInput->setPlaceholderText("Search movies & TV shows...");
    m_searchInput->setMinimumHeight(32);
    m_searchInput->setObjectName("StreamSearchInput");
    layout->addWidget(m_searchInput, 1);

    m_searchBtn = new QPushButton("Search", m_searchBarFrame);
    m_searchBtn->setFixedHeight(32);
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    m_searchBtn->setObjectName("StreamSearchBtn");
    layout->addWidget(m_searchBtn);

    connect(m_searchInput, &QLineEdit::returnPressed,
            this, &StreamPage::onSearchSubmit);
    connect(m_searchBtn, &QPushButton::clicked,
            this, &StreamPage::onSearchSubmit);
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

    // Continue watching strip
    m_continueStrip = new StreamContinueStrip(m_bridge, m_library, m_scrollHome);
    m_continueStrip->refresh();
    m_scrollLayout->addWidget(m_continueStrip);

    connect(m_continueStrip, &StreamContinueStrip::playRequested, this,
        [this](const QString& imdbId, int season, int episode) {
            QString mediaType = (season == 0 && episode == 0) ? "movie" : "series";
            onPlayRequested(imdbId, mediaType, season, episode);
        });

    // Library grid
    m_libraryLayout = new StreamLibraryLayout(m_bridge, m_library, m_scrollHome);
    m_libraryLayout->refresh();
    m_scrollLayout->addWidget(m_libraryLayout, 1);

    connect(m_library, &StreamLibrary::libraryChanged, m_libraryLayout, &StreamLibraryLayout::refresh);

    m_browseScroll->setWidget(m_scrollHome);
    layerLayout->addWidget(m_browseScroll, 1);

    // Search results overlay
    m_searchWidget = new StreamSearchWidget(m_cinemeta, m_library, m_browseLayer);
    m_searchWidget->hide();
    layerLayout->addWidget(m_searchWidget, 1);

    connect(m_searchWidget, &StreamSearchWidget::backRequested, this, &StreamPage::showBrowse);
    connect(m_searchWidget, &StreamSearchWidget::libraryChanged, m_libraryLayout, &StreamLibraryLayout::refresh);
}

// ─── Search ──────────────────────────────────────────────────────────────────

void StreamPage::onSearchSubmit()
{
    QString query = m_searchInput->text().trimmed();
    if (query.isEmpty())
        return;

    m_browseScroll->hide();
    m_searchWidget->search(query);
}

void StreamPage::showBrowse()
{
    m_mainStack->setCurrentIndex(0);
    m_searchWidget->hide();
    m_browseScroll->show();
    m_continueStrip->refresh();
    m_libraryLayout->refresh();
}

void StreamPage::showDetail(const QString& imdbId)
{
    m_detailView->showEntry(imdbId);
    m_mainStack->setCurrentIndex(1);
}

void StreamPage::onPlayRequested(const QString& imdbId, const QString& mediaType,
                                  int season, int episode)
{
    // Build episode key for choice persistence
    QString epKey = (mediaType == "movie")
        ? StreamProgress::movieKey(imdbId)
        : StreamProgress::episodeKey(imdbId, season, episode);

    // Check if we have a saved choice for this episode
    QJsonObject savedChoice = StreamChoices::loadChoice(epKey);
    if (!savedChoice.isEmpty()) {
        // TODO batch 12-13: launch stream with saved choice
        // For now, still show picker but pre-select the saved source
    }

    // Fetch Torrentio streams, then show picker
    // Disconnect any previous one-shot connection
    disconnect(m_torrentio, &TorrentioClient::streamsReady, this, nullptr);
    disconnect(m_torrentio, &TorrentioClient::streamsError, this, nullptr);

    connect(m_torrentio, &TorrentioClient::streamsReady, this,
        [this, imdbId, mediaType, season, episode, epKey](const QList<TorrentioStream>& streams) {
            disconnect(m_torrentio, &TorrentioClient::streamsReady, this, nullptr);
            disconnect(m_torrentio, &TorrentioClient::streamsError, this, nullptr);

            if (streams.isEmpty()) {
                m_mainStack->setCurrentIndex(2);
                m_bufferLabel->setText("No sources found for this title");
                m_bufferOverlay->show();
                m_bufferCancelBtn->show();
                return;
            }

            TorrentPickerDialog dlg(streams, this);
            if (dlg.exec() != QDialog::Accepted || !dlg.hasSelection())
                return;

            TorrentioStream selected = dlg.selectedStream();

            // Save choice for re-use
            QJsonObject choice;
            choice["magnetUri"]     = selected.magnetUri;
            choice["infoHash"]      = selected.infoHash;
            choice["fileIndex"]     = selected.fileIndex;
            choice["quality"]       = selected.quality;
            choice["trackerSource"] = selected.trackerSource;
            choice["fileNameHint"]  = selected.fileNameHint;
            StreamChoices::saveChoice(epKey, choice);

            // Store state for progress saving
            setProperty("_currentEpKey", epKey);

            // Show buffer overlay and start streaming
            m_mainStack->setCurrentIndex(2);
            m_bufferLabel->setText("Connecting...");
            m_bufferOverlay->show();

            m_playerController->startStream(
                imdbId, mediaType, season, episode,
                selected.magnetUri, selected.fileIndex, selected.fileNameHint);
        });

    connect(m_torrentio, &TorrentioClient::streamsError, this,
        [this](const QString& message) {
            disconnect(m_torrentio, &TorrentioClient::streamsReady, this, nullptr);
            disconnect(m_torrentio, &TorrentioClient::streamsError, this, nullptr);
            m_mainStack->setCurrentIndex(2);
            m_bufferLabel->setText("Failed to fetch sources: " + message);
            m_bufferOverlay->show();
            m_bufferCancelBtn->show();
        });

    m_torrentio->fetchStreams(imdbId, mediaType, season, episode);
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

    // Capture current stream info for progress saving
    QString imdbId = m_playerController->property("_imdbId").toString();
    // Use the controller's stored state instead
    connect(player, &VideoPlayer::progressUpdated, this,
        [this](const QString& /*path*/, double posSec, double durSec) {
            QString epKey = property("_currentEpKey").toString();
            if (epKey.isEmpty()) return;
            // Only save once real playback has started — ignore probe/initial 0-value updates
            if (posSec < 5.0 || durSec <= 0.0) return;

            bool finished = (durSec > 0 && posSec / durSec >= 0.9);
            QJsonObject state = StreamProgress::makeWatchState(posSec, durSec, finished);
            m_bridge->saveProgress("stream", epKey, state);
        });

    // On player close → stop stream, clear progress key, refresh continue strip
    connect(player, &VideoPlayer::closeRequested, this, [this, player]() {
        setProperty("_currentEpKey", QString());
        disconnect(player, &VideoPlayer::progressUpdated, this, nullptr);
        m_playerController->stopStream();
        m_continueStrip->refresh();
        m_libraryLayout->refresh();
    });

    player->openFile(httpUrl);
    if (auto* mw = qobject_cast<QMainWindow*>(mainWin))
        player->setGeometry(mw->centralWidget()->rect());
    else
        player->setGeometry(mainWin->rect());
    player->show();
    player->raise();
}

void StreamPage::onStreamFailed(const QString& message)
{
    setProperty("_currentEpKey", QString());
    // Disconnect any lingering progress connection
    if (auto* player = window() ? window()->findChild<VideoPlayer*>() : nullptr) {
        disconnect(player, &VideoPlayer::progressUpdated, this, nullptr);
        disconnect(player, &VideoPlayer::closeRequested, this, nullptr);
    }
    m_bufferLabel->setText("Stream failed: " + message);
    QTimer::singleShot(3000, this, [this]() {
        if (!m_playerController->isActive())
            showBrowse();
    });
}

void StreamPage::onStreamStopped()
{
    setProperty("_currentEpKey", QString());
    // Disconnect any lingering progress connection
    if (auto* player = window() ? window()->findChild<VideoPlayer*>() : nullptr) {
        disconnect(player, &VideoPlayer::progressUpdated, this, nullptr);
        disconnect(player, &VideoPlayer::closeRequested, this, nullptr);
    }
    m_bufferOverlay->hide();
    showBrowse();
}

