#pragma once

#include <QWidget>
#include <QStackedWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

class CoreBridge;
class TorrentEngine;
class CinemetaClient;
class TorrentioClient;
class StreamEngine;
class StreamLibrary;
class StreamLibraryLayout;
class StreamSearchWidget;
class StreamDetailView;
class StreamPlayerController;
class StreamContinueStrip;

class StreamPage : public QWidget
{
    Q_OBJECT

public:
    explicit StreamPage(CoreBridge* bridge, TorrentEngine* torrentEngine,
                        QWidget* parent = nullptr);

    void activate();

private:
    void buildUI();
    void buildSearchBar();
    void buildBrowseLayer();

    void onSearchSubmit();
    void showBrowse();
    void showDetail(const QString& imdbId);
    void onPlayRequested(const QString& imdbId, const QString& mediaType,
                         int season, int episode);
    void onBufferUpdate(const QString& statusText, double percent);
    void onReadyToPlay(const QString& httpUrl);
    void onStreamFailed(const QString& message);
    void onStreamStopped();

    CoreBridge*      m_bridge;
    TorrentEngine*   m_torrentEngine;

    // Core services
    CinemetaClient*  m_cinemeta  = nullptr;
    TorrentioClient* m_torrentio = nullptr;
    StreamEngine*    m_streamEngine = nullptr;
    StreamLibrary*   m_library   = nullptr;

    // UI layers
    QStackedWidget*  m_mainStack = nullptr;  // browse, detail, player

    // Search bar
    QFrame*     m_searchBarFrame = nullptr;
    QLineEdit*  m_searchInput    = nullptr;
    QPushButton* m_searchBtn     = nullptr;

    // Browse layer
    QWidget*     m_browseLayer   = nullptr;
    QScrollArea* m_browseScroll  = nullptr;
    QWidget*     m_scrollHome    = nullptr;
    QVBoxLayout* m_scrollLayout  = nullptr;

    // Continue watching strip
    StreamContinueStrip* m_continueStrip = nullptr;

    // Library grid
    StreamLibraryLayout* m_libraryLayout = nullptr;

    // Search results overlay
    StreamSearchWidget* m_searchWidget = nullptr;

    // Detail view
    StreamDetailView* m_detailView = nullptr;

    // Player controller
    StreamPlayerController* m_playerController = nullptr;

    // Buffer overlay
    QWidget* m_bufferOverlay = nullptr;
    QLabel*  m_bufferLabel   = nullptr;
    QPushButton* m_bufferCancelBtn = nullptr;
};
