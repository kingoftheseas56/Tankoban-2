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
class OrganisePage;
class StreamPage;
class TankorentPage;
class DevControlServer;
class SidebarDrawer;
class StreamDownloadIndex;
class QJsonObject;
struct StreamBulkGroupRecord;

namespace tankostream::stream {
struct BulkPackVerificationResult;
}

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

    // FFMPEG_KEEP_OR_REMOVE_DECISION 2026-05-04 — public opener exposed
    // for the `--play-file` CLI auto-open flow used by the compare-mpv /
    // compare-ffmpeg launcher batch files. Internal callers continue to
    // use the private overload at lines 92+. Wrapper just forwards.
    void openVideoFromCli(const QString& filePath);

    // STREAM_DOWNLOADED_LIBRARY 2026-05-10 Phase 1 — accessor for the
    // persistent index of bulk-downloaded episodes. Owned by MainWindow;
    // consumed by VideosScanner (skip lookup) + StreamPage/StreamDetailView
    // (per-episode markers + click routing) in later phases.
    StreamDownloadIndex* streamDownloadIndex() const { return m_streamDownloadIndex; }

public slots:
    // Frameless-chrome public slots — connectable from any takeover surface
    // (VideoPlayer, ComicReader, BookReader) via the per-view chrome buttons.
    // PER_VIEW_CHROME_FIX 2026-05-02. Minimize + close already have built-in
    // QWidget slots (showMinimized, close); only max-toggle needs a named entry.
    void onChromeMaximizeToggle();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    // Frameless-chrome support (FRAMELESS_CHROME_FIX 2026-05-01).
    void changeEvent(QEvent *event) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

private:
    void buildTopBar();
    void buildPageStack();
    void bindShortcuts();
    // Re-mirror m_topBarLeftSlot's fixed width to m_topBarRightSlot's
    // current sizeHint. Called after right-slot content visibility changes
    // (e.g. Organise button shown/hidden on page activation) to keep the
    // central nav pills geometrically centered in the window.
    void mirrorTopBarSlotWidths();

    // FRAMELESS_CHROME_FULLSCREEN_EXIT_FIX 2026-05-04 — re-apply the
    // Win32 frameless-style hack (keep WS_CAPTION etc + SWP_FRAMECHANGED
    // to re-fire WM_NCCALCSIZE) whenever the window state changes in a
    // way that may have reset our styles. Called from the constructor
    // (initial setup) and from changeEvent on Qt::WindowFullScreen
    // transitions (fixes the brief OS-title-bar flash on fullscreen
    // exit + the carry-forward S2 takeover-flash bug). No-op on
    // non-Windows platforms.
    void applyFramelessWin32Style();

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

    // STREAM_ADD_TO_TANKORENT (2026-05-06) — handler for StreamPage's
    // addToTankorentRequested signal. Activates Tankorent (PAGE_TANKORENT)
    // and forwards the magnet to TankorentPage::addMagnetFromExternal.
    // Page-switch only — does NOT pause/teardown any in-flight stream
    // playback session (the user can be watching one stream and queueing
    // another for download in parallel).
    void onAddToTankorentRequested(const QString& magnetUri,
                                   const QString& displayName);
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL 2026-05-12 Phase 6 — renamed from
    // onAddToTankorentBulkRequested. No page-switch; user stays on the
    // page they were on (typically StreamDetailView).
    void onStreamBulkDispatchRequested(
        const StreamBulkGroupRecord& group,
        const tankostream::stream::BulkPackVerificationResult& verifierOutput,
        const QString& displayLabel);

    // STREAM_DOWNLOADED_LIBRARY Phase 4 (2026-05-10) — Stream-mode playback
    // gateway for bulk-downloaded episodes. Routes through openVideoPlayer
    // (the same surface VideosPage uses); StreamPage has already handled the
    // SubtitlesAggregator fan-out. Spec §6.2.
    void onPlayLocalFileFromStreamRequested(const QString& localPath,
                                            const QString& imdbId,
                                            const QString& showTitle,
                                            int season,
                                            int episode);

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
    OrganisePage *m_organisePage = nullptr;

    // STREAM_ADD_TO_TANKORENT (2026-05-06) — cached at buildPageStack
    // time so the cross-page magnet hand-off (right-click stream card →
    // "Add torrent to Tankorent") can connect StreamPage's outbound
    // signal at construction without doing a qobject_cast walk through
    // m_pageStack on each invocation.
    StreamPage    *m_streamPage    = nullptr;
    TankorentPage *m_tankorentPage = nullptr;

    // REPO_HYGIENE Phase 3 — dev-control bridge. Null until
    // enableDevControl() is called (gated behind --dev-control flag).
    DevControlServer *m_devControl = nullptr;

    // STREAM_DOWNLOADED_LIBRARY 2026-05-10 Phase 1 — persistent index of
    // bulk-downloaded episodes. Constructed early in MainWindow ctor (after
    // m_bridge is set so JsonStore is available). Phase 1 ships dead — UI
    // wiring lands in Phase 3.
    StreamDownloadIndex *m_streamDownloadIndex = nullptr;

    // CW_NAMESPACE_BOUNDARY 2026-05-13 — tracks the progressUpdated
    // lambda subscription that onPlayLocalFileFromStreamRequested
    // installs to redirect downloaded-stream playback progress writes
    // from the "videos" domain (Video-mode CW) to the "stream" domain
    // (Stream-mode CW). openVideoPlayer (the library-videos path)
    // tears this conn down to ensure plain library playback uses the
    // default LibraryVideos persistence mode.
    QMetaObject::Connection m_streamPlaybackProgressConn;
    void migrateLegacyStreamProgressEntries();

    // System tray
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu           *m_trayMenu = nullptr;
    // m_quitRequested removed 2026-04-26 (REPO_HYGIENE P1.5): closeEvent
    // unconditionally calls QApplication::quit, so the flag was unused state.
    bool m_wasMaximizedBeforeHide = false;
    bool m_wasMaximizedBeforeFullscreen = false;

    // Top bar
    QWidget       *m_topBar      = nullptr;
    QWidget       *m_topBarLeftSlot  = nullptr;  // brand + hamburger; width mirrors right slot
    QWidget       *m_topBarRightSlot = nullptr;  // theme/scan/add/organise/chrome
    QLabel        *m_brandLabel  = nullptr;
    QButtonGroup  *m_navGroup    = nullptr;
    QPushButton   *m_hamburgerBtn = nullptr;

    // SOURCES_SIDEBAR — slide-in left drawer holding Tankorent / Tankoyomi /
    // TankoLibrary list buttons. Toggled by m_hamburgerBtn. Replaces the prior
    // PAGE_SOURCES topbar entry; the three sub-pages are now peer pages in
    // m_pageStack.
    SidebarDrawer *m_sidebar     = nullptr;

    // Frameless-chrome buttons (FRAMELESS_CHROME_FIX 2026-05-01).
    // Folded into the right edge of m_topBar so the OS title bar can be dropped.
    QPushButton   *m_chromeMin   = nullptr;
    QPushButton   *m_chromeMax   = nullptr;
    QPushButton   *m_chromeClose = nullptr;
    QPushButton   *m_organiseBtn = nullptr;
    void updateMaxRestoreIcon();

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
