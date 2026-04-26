#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QShortcut>
#include <QButtonGroup>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QCloseEvent>

class GlassBackground;
class CoreBridge;
class RootFoldersOverlay;
class BookReader;
class ComicReader;
class VideoPlayer;
class VideosPage;
class DevControlServer;
class QJsonObject;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(CoreBridge* bridge, QWidget *parent = nullptr);

    /// Force window to front — used by single-instance signal
    void bringToFront();

    // REPO_HYGIENE Phase 3 (2026-04-26) — dev-control bridge entry point.
    // Called from main.cpp when --dev-control or TANKOBAN_DEV_CONTROL=1.
    // Idempotent — only the first invocation listens.
    void enableDevControl();

    // Dispatcher invoked by DevControlServer on each accepted command.
    // Returns the full reply object including {"type":"reply"|"error","seq":...}.
    // Pure UI-thread call.
    QJsonObject handleDevCommand(const QString& cmd, int seq, const QJsonObject& payload);

    // Top-level snapshot for `get_state` command.
    QJsonObject devSnapshot() const;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildTopBar();
    void buildPageStack();
    void bindShortcuts();
    void activatePage(const QString &pageId);
    void showRootFolders();
    void hideRootFolders();

    // Comic reader
    void openComicReader(const QString& cbzPath, const QStringList& seriesCbzList, const QString& seriesName);
    void closeComicReader();

    // Book reader
    void openBookReader(const QString& filePath);
    void closeBookReader();

    // Video player
    void openVideoPlayer(const QString& filePath);
    void closeVideoPlayer();

    // System tray
    void setupTrayIcon();
    void hideToTray();
    void restoreFromTray();
    void quitFromTray();

    // Map page id → domain for root folders
    QString domainForPage(const QString& pageId) const;

    CoreBridge *m_bridge = nullptr;

    // Glass background
    GlassBackground *m_glassBg = nullptr;

    // Root folders overlay
    RootFoldersOverlay *m_rootFoldersOverlay = nullptr;

    // Comic reader overlay
    ComicReader *m_comicReader = nullptr;

    // Book reader overlay
    BookReader *m_bookReader = nullptr;

    // Video player overlay
    VideoPlayer *m_videoPlayer = nullptr;

    // VideosPage cached at buildPageStack time — needed by Phase 3
    // dev-bridge dispatcher (scan_videos / get_videos).
    VideosPage *m_videosPage = nullptr;

    // REPO_HYGIENE Phase 3 — dev-control bridge. Null until
    // enableDevControl() is called (gated behind --dev-control flag).
    DevControlServer *m_devControl = nullptr;

    // System tray
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu           *m_trayMenu = nullptr;
    // m_quitRequested removed 2026-04-26 (REPO_HYGIENE P1.5): closeEvent
    // unconditionally calls QApplication::quit, so the flag was unused state.
    bool m_wasMaximizedBeforeHide = false;
    bool m_wasMaximizedBeforeFullscreen = false;

    // Top bar
    QWidget       *m_topBar      = nullptr;
    QLabel        *m_brandLabel  = nullptr;
    QButtonGroup  *m_navGroup    = nullptr;

    // Page stack
    QStackedWidget *m_pageStack = nullptr;

    // Navigation buttons keyed by page id
    struct NavButton {
        QString     pageId;
        QPushButton *button = nullptr;
    };
    QVector<NavButton> m_navButtons;

    QString m_activePageId;
};
