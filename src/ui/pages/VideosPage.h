#pragma once

#include <QWidget>
#include <QThread>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>
#include <QTimer>
#include <QSettings>
#include <QMap>
#include <QJsonObject>
#include <QVBoxLayout>
#include <functional>
#include "core/VideosScanner.h"
#include "core/library/VideoCategory.h"
#include "../INavStateProvider.h"
class QPushButton;
class QNetworkAccessManager;
class QScrollArea;
class CoreBridge;
class FadingStackedWidget;
class LibraryListView;
class TileStrip;
class TorrentClient;
class VideosScanner;
class ShowView;
class StreamDownloadIndex;
namespace tankostream { namespace stream { class MetaAggregator; } }

class VideosPage : public QWidget, public INavStateProvider {
    Q_OBJECT
public:
    explicit VideosPage(CoreBridge* bridge, QWidget* parent = nullptr);
    ~VideosPage();

    void activate();
    void triggerScan();
    void refreshContinueOnly();
    void refreshFromCategoryStore();
    QList<ShowInfo> currentShows() const { return m_allShows; }

    // Shared MetaAggregator handle owned by StreamPage. Set once at MainWindow
    // wire-up; powers the "Fetch poster from internet" context-menu action on
    // folder tiles. Null-safe — the action is disabled until this is set.
    void setMetaAggregator(tankostream::stream::MetaAggregator* meta);

    // Shared TorrentClient handle owned by MainWindow. Set once at wire-up;
    // used by the (auto-)rename path in renameShowFolder to release any active
    // libtorrent record pointing at the folder being renamed BEFORE the
    // QFile::rename happens. Without this, libtorrent silently re-creates the
    // original folder + re-downloads on next periodic resume-data save or
    // boot, producing the "multiplying folders" symptom. Null-safe — when
    // unset (e.g., test harness), the rename proceeds without the release.
    void setTorrentClient(TorrentClient* client) { m_torrentClient = client; }

    // STREAM_DOWNLOADED_LIBRARY Phase 5 (2026-05-10) — wire the stream-side
    // download index. Forwards to m_scanner so file-level skipping happens at
    // scan time, and subscribes to entriesChanged with a 500ms debounced
    // rescan so bulk-completion / Remove-from-Library batches collapse into
    // one triggerScan. Spec §8.3.
    void setStreamDownloadIndex(StreamDownloadIndex* idx);

    // REPO_HYGIENE Phase 3 (2026-04-26) — dev-control bridge snapshot.
    // Returns library tile state for the `get_videos` command. Pure read.
    QJsonObject devSnapshot(int limit = 50) const;

    // INavStateProvider (GLOBAL_NAV_HISTORY Task 10)
    QJsonObject captureNavState() const override;
    bool restoreNavState(const QJsonObject& blob) override;
    QString navStateLabel() const override { return QStringLiteral("videos"); }

signals:
    void playVideo(const QString& filePath);
    void categoryAssignmentsChanged();
    // Emitted just before a library→detail transition so MainWindow's
    // NavHistory can capture current library state and push a fresh entry.
    void navigationRequested();

private slots:
    void onShowFound(const ShowInfo& show);
    void onScanFinished(const QList<ShowInfo>& allShows);
    void applySearch();
    void onTileClicked(const QString& showPath, const QString& showName);
    void showGrid();
    void refreshContinueStrip();

private:
    struct ContinueItem {
        qint64 updatedAt = 0;
        QString showPath;
        QString showName;
        QString resumeFilePath;
        double resumePosSec = 0.0;
        double resumeDurSec = 0.0;
    };

    void buildUI();
    void addShowTile(const ShowInfo& show);
    void addShowTileToStrip(const ShowInfo& show, TileStrip* strip);
    void rebuildLibraryRows();
    void clearCategoryRows();
    void sortCategoryRows();
    void setGridRowsVisible(bool visible);
    void applyDensityToAllStrips(int val);
    void moveShowToCategory(const QString& showId, VideoCategory category);
    QList<ContinueItem> collectContinueItems();
    // Map an episode file path back to its show-root folder. Prefers the
    // m_fileToShowRoot map populated by the scanner; falls back to walking
    // m_showPathToName looking for the longest path that is a prefix of
    // filePath. The walk handles shows whose nested files (e.g. "Sopranos
    // /Season 6/S06E04.mkv") aren't enumerated in show.files — without
    // it Continue Watching tiles label as "Season 6" instead of "Sopranos".
    QString resolveShowPath(const QString& filePath) const;
    // Bind the full folder-tile context menu (Play/Continue, Play from
    // beginning, Mark watched/unwatched, Clear from CW, Rename, Auto-rename,
    // Reveal, Copy path, Move to..., Set/Remove/Paste/Fetch poster, Remove)
    // to a TileStrip. Same shape regardless of which category the strip
    // represents. Pre-multi-category, this menu lived inline on the single
    // m_tileStrip; post-multi-category (Codex's 2026-05-05 ship) other
    // category strips got a 3-action reduced menu — Hemanth's regression
    // closure 2026-05-06 reinstates the full menu on every strip.
    void installFolderTileContextMenu(
        TileStrip* strip,
        std::function<QString(const QString&)> computeVideoId,
        std::function<void(const QString&, bool)> markAllEpisodes,
        std::function<QString(const QString&)> posterPath,
        std::function<bool(const QString&, const QString&)> renameShowFolder);
    void addContinueTile(TileStrip* strip, const ContinueItem& item);
    void clearContinueRows();
    void refreshContinueStripLegacy();
    void toggleViewMode();
    void executePendingClick();
    bool eventFilter(QObject* obj, QEvent* event) override;
    static QString formatSize(qint64 bytes);
    // SHA1(showPath)-keyed path to the cached poster jpg. Same formula used
    // by Set/Paste/Remove/Fetch poster actions; exposed as a helper so non-
    // buildUI code paths (ShowView hand-off) can resolve the same cache key
    // without re-deriving the hash.
    static QString posterPathFor(const QString& showPath);

    CoreBridge*             m_bridge = nullptr;
    FadingStackedWidget*    m_stack = nullptr;
    QWidget*         m_continueSection = nullptr;
    TileStrip*       m_continueStrip = nullptr;
    QWidget*         m_categoriesContainer = nullptr;
    QVBoxLayout*     m_categoriesLayout = nullptr;
    QMap<VideoCategory, QWidget*>   m_categorySections;
    QMap<VideoCategory, TileStrip*> m_categoryStrips;
    LibraryListView* m_listView = nullptr;
    QLabel*          m_statusLabel = nullptr;
    QLineEdit*       m_searchBar = nullptr;
    QComboBox*       m_sortCombo = nullptr;
    QTimer*          m_searchTimer = nullptr;
    ShowView*        m_showView = nullptr;
    QPushButton*     m_viewToggle = nullptr;
    QSlider*         m_densitySlider = nullptr;
    bool             m_gridMode = true;

    // 250ms single-click delay (double-click cancels and executes immediately)
    QTimer*          m_clickTimer = nullptr;
    QString          m_pendingClickPath;
    QString          m_pendingClickName;
    bool             m_pendingIsPlay = false;  // true = play video, false = open show view
    bool             m_pendingIsLoose = false;

    // Throttle continue strip refresh during active playback (max once per 5s)
    QTimer*          m_continueRefreshThrottle = nullptr;

    QThread*         m_scanThread = nullptr;
    VideosScanner*   m_scanner = nullptr;
    bool             m_hasScanned = false;
    bool             m_scanning = false;
    // REPO_HYGIENE Phase 4 P4.3 (2026-04-26) — set true when triggerScan is
    // called while a scan is in progress. onScanFinished consumes the flag
    // and fires another triggerScan(). Pre-fix, the second request was
    // silently dropped — visible as "I just dropped a new folder in but it
    // doesn't show up" if the user added folders mid-scan.
    bool             m_rescanPending = false;

    // Scan-time durations per show (showPath → {filePath → durationSec})
    QMap<QString, QMap<QString, double>> m_showDurations;

    // File path → show root (for continue strip dedup by show, not by subfolder)
    QMap<QString, QString> m_fileToShowRoot;
    QMap<QString, QString> m_showPathToName;
    QMap<QString, ShowInfo> m_showsById;
    QList<ShowInfo> m_allShows;

    tankostream::stream::MetaAggregator* m_meta = nullptr;
    QNetworkAccessManager* m_nam = nullptr;  // lazy-init on first poster fetch
    TorrentClient*         m_torrentClient = nullptr;

    // STREAM_DOWNLOADED_LIBRARY Phase 5 (2026-05-10) — non-owning. Set once at
    // MainWindow wire-up. m_streamDownloadDebounce is lazy-constructed in the
    // setter to coalesce bursts of entriesChanged signals into one triggerScan.
    StreamDownloadIndex*   m_downloadIndex = nullptr;
    QTimer*                m_streamDownloadDebounce = nullptr;

    // GLOBAL_NAV_HISTORY Task 10: cache the grid QScrollArea pointer so
    // captureNavState/restoreNavState don't pay an O(n) findChild walk.
    QScrollArea*           m_gridScroll = nullptr;
};
