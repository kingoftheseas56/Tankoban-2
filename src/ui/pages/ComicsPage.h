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
#include "../LayerEntry.h"
class QPushButton;
class QScrollArea;
class CoreBridge;
class FadingStackedWidget;
class LibraryListView;
class TileCard;
class TileStrip;
class LibraryScanner;
class SeriesView;
class ComicsTankoyomiLibrary;
class ComicsTankoyomiSearchWidget;
class MangaSourceRegistry;
class MangaDownloader;
class MangaDownloadIndex;        // Phase 5 creates the type; Phase 4 stores nullptr
class TorrentClient;
namespace tankoban::manga {
    class NyaaRuntimeSource;
    class WeebCentralVolumePacker;
    namespace anilist {
        class AniListClient;
        class AniListCache;
        struct MediaPreview;
    }
    namespace mangaupdates {
        class MangaUpdatesClient;
        class VolumeMetadataResolver;
    }
    namespace premium {
        class PremiumCatalog;
        class TorrentRequestLedger;
        class TorrentVolumeProvider;
        class MangaTransferCoordinator;
        struct PremiumCatalogEntry;
    }
    namespace comics {
        class ComicsSeriesView;
        struct UnifiedSourceRow;
    }
}
class QNetworkAccessManager;
struct ComicsLibraryRecord;
struct MangaResult;
struct SeriesInfo;

class ComicsPage : public QWidget {
    Q_OBJECT
public:
    explicit ComicsPage(CoreBridge* bridge, QWidget* parent = nullptr);
    ~ComicsPage();

    void activate();
    void triggerScan();

    // PHASE 0 NAV CONTRACT RESTORE 2026-05-17 (Agent 5) — public entry point
    // for MainWindow::resetActivePageToRoot. Standing Tankoban contract:
    // clicking the Comics topbar pill from any deep sub-view (search results,
    // tankoyomi-detail series view, folder series view) returns the user to
    // the library-grid root. Thin forwarder to the private showLibraryMode
    // slot so MainWindow doesn't need friend access. Phase 1+ may promote
    // this to a shared IPageRoot interface for polymorphic dispatch.
    void resetToRoot();

    // TANKOYOMI_PREMIUM Phase 3 -- MainWindow constructs TorrentClient AFTER
    // ComicsPage (line ordering: pages first, then TorrentClient hoisted at
    // MainWindow scope post-SOURCES_SIDEBAR). Mirrors VideosPage's
    // setTorrentClient() pattern. Constructs the TorrentVolumeProvider on
    // first non-null call (idempotent for re-wiring scenarios). Safe to call
    // with nullptr -- Premium downloads simply remain unavailable until a
    // real TorrentClient arrives.
    void setTorrentClient(TorrentClient* client);

    // Public so MainWindow::closeComicReader can refresh the continue
    // strip the moment the reader returns to the library — mirrors the
    // VideosPage::refreshContinueOnly precedent at MainWindow.cpp:696-698.
    // 2026-05-03 — Hemanth verbatim "I want it instantaneous."
    void refreshContinueStrip();

    QJsonObject devSnapshot() const;
    QJsonObject devLibrarySnapshot() const;
    QJsonObject devSeriesSnapshot() const;
    QJsonObject devSelectVolume(int row);
    QJsonObject devOpenSeries(const QString& seriesId);
    QJsonObject devOpenChapter(const QString& seriesId, int volumeNumber, int chapterNumber);
    QJsonObject devSearchTankoyomi(const QString& query, int timeoutMs);
    QJsonObject devDownloadsSnapshot() const;
    QJsonObject devDispatchVolume(const QString& seriesId, int volumeNumber, const QString& source);
    QJsonObject devSourcesSnapshot() const;
    Q_INVOKABLE bool dispatchDevCommand(const QString& cmd,
                                        const QJsonObject& payload,
                                        QJsonObject& reply);

signals:
    void openComic(const QString& cbzPath, const QStringList& seriesCbzList, const QString& seriesName);
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- emitted BEFORE every
    // user-initiated in-page layer transition (Library <-> SearchResults
    // <-> TankoyomiDetail and library-tile->folder series view). The
    // emitted LayerEntry captures the OUTGOING state so the controller
    // can restore it on Back. MainWindow connects this to
    // PerModeNavController::pushLayer. Suppressed during restoreLayer via
    // m_inNavRestore.
    void enteredLayer(const tankoban::ui::LayerEntry& entry);
    // Emitted when the user closes a deep layer via an in-page affordance
    // (Esc from series view, in-page back button on a search-takeover).
    // The controller pops via this signal so the back-stack stays consistent
    // with the in-page state machine.
    void exitedLayer();

public slots:
    // PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- re-render the targeted
    // layer in-place WITHOUT emitting enteredLayer. Called by MainWindow
    // when PerModeNavController::layerRestoreRequested fires for
    // pageId="comics".
    void restoreLayer(const tankoban::ui::LayerEntry& target);

private slots:
    void onSeriesFound(const SeriesInfo& series);
    void onScanFinished(const QList<SeriesInfo>& allSeries);
    void onTileClicked(const QString& seriesPath, const QString& seriesName);
    void showGrid();
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Task 18 -- state machine for
    // the search-takeover surface. Library is the default grid + Continue
    // strip; SearchResults flips the stack to ComicsTankoyomiSearchWidget;
    // TankoyomiDetail flips to ComicsSeriesView (TANKOYOMI_VOLUME_PIVOT
    // Phase 9 -- replaces the legacy ComicsTankoyomiDetailView).
    void showLibraryMode();
    void showSearchMode(const QString& query);
    // Tasks 9+10: signature changed from MediaPreview to MangaResult (WC pivot).
    void onSearchResultActivated(const MangaResult& result);
    // Phase 9: routes ComicsSeriesView::downloadDispatchRequested to either
    // TorrentVolumeProvider (Catalog / NyaaRuntime) or
    // WeebCentralVolumePacker (WeebCentralPacker).
    void onDownloadDispatchRequested(const tankoban::manga::comics::UnifiedSourceRow& row,
                                     const QString& seriesTitle,
                                     int            anilistSeriesId,
                                     int            volumeNumber,
                                     const QStringList& chapterIds);
    void onProviderVolumeCompleted(const QString& seriesId,
                                   int volumeNumber,
                                   const QString& cbzPath,
                                   int fallbackSourceKind);
    void onProviderVolumeFailed(const QString& seriesId,
                                int volumeNumber,
                                const QString& errorCode,
                                const QString& errorMessage,
                                int fallbackSourceKind);
    void onComicsSeriesOpenVolume(int volumeNumber, const QString& cbzPath);
    void onVolumeMetadataResolved(int anilistId, int volumeCount, int chapterCount);
    void onVolumeMetadataUnresolved(int anilistId, const QString& reason);
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Task 26 — Back from
    // Tankoyomi detail returns to library mode in Phase 4. Phase 9
    // refines (Back-from-search-detail returns to search results).
    void onDetailBack();
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Task 19 — applySearch
    // remains as a local-library filter. ONE real caller survives:
    // restoreNavState (GLOBAL_NAV_HISTORY restore path) — re-runs the
    // local filter when a captured nav-state blob with a non-empty
    // query is restored. The continue-strip hide-series flow calls
    // m_tileStrip->filterTiles(...) directly (not applySearch). The
    // m_searchTimer→applySearch connection is gone; search-bar input
    // now flows to Tankoyomi via returnPressed.
    void applySearch();
    void onCardClicked();
    void onTileContextMenu(const QPoint& pos);
    void onMultiSelectContextMenu(const QList<TileCard*>& selected, const QPoint& globalPos);
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — push refreshed claimed
    // paths to the scanner + rebuild the merged tile set when the
    // Tankoyomi library changes (add / remove).
    void onTankoyomiLibraryChanged();

protected:
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 5 Task 30 —
    // re-validate the on-disk MangaDownloadIndex each time this page
    // is shown so external file deletions surface as chip-state updates.
    void showEvent(QShowEvent* e) override;

private:
    void buildUI();
    void addSeriesTile(const SeriesInfo& series);
    void toggleViewMode();
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 5 Task 34 — walks
    // m_tileStrip->tiles() and refreshes each Tankoyomi-origin tile's
    // DOWNLOADING chip from the current MangaDownloader record state.
    // Driven by downloadUpdated + downloadCompleted subscriptions.
    void refreshTileChips();
    void openSeriesByPath(const QString& seriesPath, const QString& seriesName,
                          const QString& coverPath = QString());
    void onChapterCompleted(const QString& source, const QString& seriesTitle,
                            const QString& chapterId, const QString& finalPath,
                            qint64 fileSize);
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — assemble tiles from
    // folder-origin SeriesInfo (held inside m_tileStrip after scan) plus
    // Tankoyomi-origin records from m_tyLibrary->all(). Called from
    // onTankoyomiLibraryChanged() and after onScanFinished().
    void rebuildTiles();

    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- rebuild the new
    // DOWNLOADED + BOOKMARKED tile sections from
    // MangaDownloadIndex::entriesForAllSeries() + AniListCache::bookmarkedPreviews().
    // Called from onScanFinished, onTankoyomiLibraryChanged, and whenever
    // either the download index or the bookmark set changes.
    void refreshLibraryStrips();

    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- resolve anilistId for
    // a MangaDownloadIndex Entry. Catalog rows ("tankoyomi_premium:<seriesId>")
    // look up PremiumCatalog::entryById to extract entry.anilistId; runtime
    // rows ("anilist_<N>" suffix) parse the numeric suffix. Returns 0 when
    // no anilistId can be resolved -- caller falls back to a non-anilist
    // tile (still renders title + a generic cover, click is a no-op).
    int anilistIdForDownloadEntry(const QString& sourceId,
                                  const QString& seriesId) const;

    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- fire-and-forget async
    // poster fetch. Mirrors ComicsTankoyomiSearchWidget's NAM-direct path
    // (see search widget ~line 180). Writes to <appDataDir>/anilist_posters/
    // and updates the card via TileCard::setThumbPath on completion.
    // No-op when coverUrl is empty or the disk path already exists.
    void fetchPosterForTile(TileCard* card, int anilistId, const QString& coverUrl);

    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- click handler for
    // anilist-keyed library tiles (downloaded + bookmarked). Resolves the
    // MediaPreview from AniListCache (synthesising a minimal preview when
    // the cache has no entry yet) and routes to ComicsSeriesView.
    void openSeriesByAnilistId(int anilistId, const QString& fallbackTitle);

    // Shared projection used by both onScanFinished (first-scan path)
    // and rebuildTiles (full-rebuild path). Keeps the two sites in sync.
    static SeriesInfo seriesInfoFromRecord(const ComicsLibraryRecord& r);

    // TANKOYOMI_PREMIUM Phase 7 Task 7.2 -- resolve the destination folder
    // for a Premium download. Prefers an existing Tankoyomi-library record
    // whose title matches the catalog entry (collision-disambiguated
    // canonical path established by the merger arc); otherwise builds
    // <comics-root-0>/<sanitised-title>. Returns empty if no comics root
    // is configured.
    QString canonicalSeriesPathForPremium(
        const tankoban::manga::premium::PremiumCatalogEntry& entry) const;

    // TANKOYOMI_PREMIUM Phase 9 -- adopt-existing-folder lookup. When the
    // user clicks Add-to-Library for a Premium-catalog series whose
    // normalized title matches EXACTLY ONE entry in m_folderSeries (folder-
    // imported), reuse that folder's canonicalSeriesPath. Ambiguous (zero
    // or many) returns empty -> caller falls back to new-folder-creation.
    // Per Codex section 22 "adopt, do not migrate" -- no file move/rename.
    QString findFolderImportedSeriesPathForTitle(const QString& title) const;
    static QString normalizeTitleForMatch(const QString& title);

    enum class PendingVolumeSourceKind {
        Catalog = 0,
        NyaaRuntime = 1,
        WeebCentralPacker = 2
    };
    struct PendingVolumeDispatch {
        PendingVolumeSourceKind kind = PendingVolumeSourceKind::Catalog;
        int anilistId = 0;
        QStringList chapterIds;
    };
    static QString pendingVolumeKey(const QString& seriesId, int volumeNumber);
    void rememberPendingVolumeDispatch(const QString& seriesId,
                                       int volumeNumber,
                                       PendingVolumeSourceKind kind,
                                       int anilistId,
                                       const QStringList& chapterIds);

    // TANKOYOMI_CONTINUE_READING 2026-05-15 — just-in-time population of
    // m_progressKeyMap when a Tankoyomi-origin chapter is about to be
    // read (called from the openComicRequested slot before forwarding
    // to MainWindow). No-op if cbzPath is already in the map or if
    // cbzPath isn't inside any Tankoyomi-claimed series folder.
    void ensureTankoyomiChapterInMap(const QString& cbzPath);

    // TANKOYOMI_CONTINUE_READING 2026-05-15 — produces the Continue tile's
    // title + subtitle for a Tankoyomi-origin entry. Title is the series
    // name (record.title), subtitle is "<ChapterName> • Page X/Y" derived
    // from the cbz filename + the saveProgress JSON's page/pageCount.
    // Static because it has no ComicsPage state dependencies — keeps it
    // unit-testable in principle and easy to relocate if the Tankoyomi-
    // exclusive pivot happens later.
    struct ContinueLabels {
        QString title;
        QString subtitle;
    };
    static ContinueLabels continueLabelsForRecord(const ComicsLibraryRecord& rec,
                                                  const QString& cbzPath,
                                                  int page,
                                                  int pageCount);

    CoreBridge*             m_bridge = nullptr;
    FadingStackedWidget*    m_stack = nullptr;
    // GLOBAL_NAV_HISTORY Task 8 review fix: cache the grid QScrollArea
    // pointer so capture/restore don't pay an O(n) findChild walk on
    // every Back/Forward.
    QScrollArea*            m_gridScroll = nullptr;
    QWidget*         m_continueSection = nullptr;
    TileStrip*       m_continueStrip = nullptr;
    // TANKOYOMI_VOLUME_PIVOT Phase 10 (2026-05-16) -- the legacy m_tileStrip
    // (which painted "SERIES" tiles from m_tyLibrary->all() + m_folderSeries)
    // is repurposed as the DOWNLOADED-section tile container: one tile per
    // (sourceId, seriesId) bucket via MangaDownloadIndex::entriesForAllSeries.
    // A second strip + section wrapper renders BOOKMARKED previews from
    // AniListCache::bookmarkedPreviews(). Section labels live in
    // m_downloadedLabel / m_bookmarkedLabel so refreshLibraryStrips() can
    // hide/show sections per current data.
    TileStrip*       m_tileStrip = nullptr;          // DOWNLOADED strip
    QLabel*          m_downloadedLabel = nullptr;
    QWidget*         m_bookmarkedSection = nullptr;
    QLabel*          m_bookmarkedLabel = nullptr;
    TileStrip*       m_bookmarkedStrip = nullptr;
    LibraryListView* m_listView = nullptr;
    QLabel*          m_statusLabel = nullptr;
    QLineEdit*       m_searchBar = nullptr;
    QComboBox*       m_sortCombo = nullptr;
    QTimer*          m_searchTimer = nullptr;
    SeriesView*      m_seriesView = nullptr;
    QPushButton*     m_viewToggle = nullptr;
    QSlider*         m_densitySlider = nullptr;
    bool             m_gridMode = true;

    // Progress key → file info for continue strip
    struct FileRef { QString filePath; QString seriesPath; QString coverPath; };
    QMap<QString, FileRef> m_progressKeyMap;

    QThread*         m_scanThread = nullptr;
    LibraryScanner*  m_scanner = nullptr;
    bool             m_hasScanned = false;
    bool             m_scanning = false;
    // REPO_HYGIENE Phase 4 P4.3 (2026-04-26) — buffer-not-drop rescan flag.
    bool             m_rescanPending = false;

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — Tankoyomi-origin record
    // store (JsonStore-backed, comics_library.json schema v1). Authoritative
    // source-of-truth for "is this series Tankoyomi-origin?". Folder-origin
    // series do NOT have records here.
    ComicsTankoyomiLibrary* m_tyLibrary = nullptr;
    // Cache of folder-origin SeriesInfo from the last completed scan;
    // rebuildTiles() reads this when merging with Tankoyomi records.
    QList<SeriesInfo> m_folderSeries;

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Task 18 — search-takeover
    // widget + scraper registry + shared NAM. m_searchTakeover lives at
    // m_stack index 2 (after grid index 0 + seriesView index 1).
    ComicsTankoyomiSearchWidget* m_searchTakeover = nullptr;
    MangaSourceRegistry*         m_sourceRegistry  = nullptr;
    QNetworkAccessManager*       m_nam             = nullptr;

    // TANKOYOMI_VOLUME_PIVOT Phase 9 (2026-05-16) -- legacy
    // ComicsTankoyomiDetailView REMOVED in favor of ComicsSeriesView
    // (m_tyVolumeSeriesView below). Single downloader owned by this page
    // (Phase 8 retired the prior TankoyomiPage duplicate). m_mangaDownloadIndex
    // is instantiated for Tankoyomi-chapter chip-state tracking.
    MangaDownloader*             m_mangaDownloader    = nullptr;
    MangaDownloadIndex*          m_mangaDownloadIndex = nullptr;
    tankoban::manga::premium::PremiumCatalog* m_premiumCatalog = nullptr;
    tankoban::manga::premium::TorrentRequestLedger*  m_premiumLedger   = nullptr;
    tankoban::manga::premium::TorrentVolumeProvider* m_premiumProvider = nullptr;
    // TANKOYOMI_PREMIUM Phase 9 -- thin facade over MangaDownloader +
    // TorrentVolumeProvider for one shared "Transfers paused" state.
    tankoban::manga::premium::MangaTransferCoordinator* m_transferCoordinator = nullptr;
    TorrentClient*                                   m_torrentClient   = nullptr;

    // TANKOYOMI_VOLUME_PIVOT Phase 9 (2026-05-16) -- new ComicsSeriesView
    // (volume-pivot detail view) at m_stack index 3. Named with a "_ty"
    // prefix to disambiguate from the existing m_seriesView above
    // (SeriesView*, folder-imported series). AniList client + cache are
    // owned by this page; nyaa runtime + WeebCentralPacker handle
    // off-catalog volume packs routed via downloadDispatchRequested.
    tankoban::manga::anilist::AniListClient* m_anilistClient = nullptr;
    tankoban::manga::anilist::AniListCache*  m_anilistCache  = nullptr;
    tankoban::manga::mangaupdates::MangaUpdatesClient* m_mangaUpdatesClient = nullptr;
    tankoban::manga::mangaupdates::VolumeMetadataResolver* m_volumeResolver = nullptr;
    tankoban::manga::NyaaRuntimeSource*      m_nyaaRuntime   = nullptr;
    tankoban::manga::WeebCentralVolumePacker* m_weebCentralPacker = nullptr;
    tankoban::manga::comics::ComicsSeriesView* m_tyVolumeSeriesView = nullptr;
    QMap<QString, PendingVolumeDispatch> m_pendingVolumeDispatches;
    // Last anilistId surfaced via showSeries(); used by captureNavState.
    int m_currentDetailAnilistId = 0;
    QString m_currentDetailSeriesTitle;

    enum class Mode { Library, SearchResults, TankoyomiDetail };
    Mode m_mode = Mode::Library;
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 9 Task 52 —
    // tracks the mode the user was in immediately BEFORE entering the
    // Tankoyomi detail view. Drives onDetailBack's routing (back to
    // search results vs back to library) and the "enteredFrom" hint
    // captured in the tankoyomiDetail nav-state blob.
    Mode m_enteredDetailFrom = Mode::Library;

    // PHASE 0 NAV CONTRACT RESTORE 2026-05-17 (Agent 5) — restoreNavState
    // raises this flag so the mode-flipper slots it invokes (showLibraryMode,
    // showSearchMode, etc.) skip their navigationRequested emit. Without this
    // guard a Back-restore would re-record the restored target, polluting the
    // global NavHistory. Mirrors StreamPage's emitNav=false branch.
    bool m_inNavRestore = false;
};
