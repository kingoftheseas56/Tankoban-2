#pragma once

#include <QWidget>
#include <QThread>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>
#include <QTimer>
#include <QSettings>
#include "../INavStateProvider.h"
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
class ComicsTankoyomiDetailView;
class MangaSourceRegistry;
class MangaDownloader;
class MangaDownloadIndex;        // Phase 5 creates the type; Phase 4 stores nullptr
class TorrentClient;
namespace tankoban::manga::premium {
    class PremiumCatalog;
    class TorrentRequestLedger;
    class TorrentVolumeProvider;
    class MangaTransferCoordinator;
    struct PremiumCatalogEntry;
}
class QNetworkAccessManager;
struct ComicsLibraryRecord;
struct MangaResult;
struct SeriesInfo;

class ComicsPage : public QWidget, public INavStateProvider {
    Q_OBJECT
public:
    explicit ComicsPage(CoreBridge* bridge, QWidget* parent = nullptr);
    ~ComicsPage();

    void activate();
    void triggerScan();

    // TANKOYOMI_PREMIUM Phase 3 -- MainWindow constructs TorrentClient AFTER
    // ComicsPage (line ordering: pages first, then TorrentClient hoisted at
    // MainWindow scope post-SOURCES_SIDEBAR). Mirrors VideosPage's
    // setTorrentClient() pattern. Constructs the TorrentVolumeProvider on
    // first non-null call (idempotent for re-wiring scenarios). Safe to call
    // with nullptr -- Premium downloads simply remain unavailable until a
    // real TorrentClient arrives.
    void setTorrentClient(TorrentClient* client);

    // INavStateProvider (GLOBAL_NAV_HISTORY Task 8)
    QJsonObject captureNavState() const override;
    bool restoreNavState(const QJsonObject& blob) override;
    QString navStateLabel() const override { return QStringLiteral("comics"); }

    // Public so MainWindow::closeComicReader can refresh the continue
    // strip the moment the reader returns to the library — mirrors the
    // VideosPage::refreshContinueOnly precedent at MainWindow.cpp:696-698.
    // 2026-05-03 — Hemanth verbatim "I want it instantaneous."
    void refreshContinueStrip();

signals:
    void openComic(const QString& cbzPath, const QStringList& seriesCbzList, const QString& seriesName);

private slots:
    void onSeriesFound(const SeriesInfo& series);
    void onScanFinished(const QList<SeriesInfo>& allSeries);
    void onTileClicked(const QString& seriesPath, const QString& seriesName);
    void showGrid();
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Task 18 — state machine for
    // the search-takeover surface. Library is the default grid + Continue
    // strip; SearchResults flips the stack to ComicsTankoyomiSearchWidget;
    // TankoyomiDetail is reserved for Phase 4 (detail-view wiring).
    void showLibraryMode();
    void showSearchMode(const QString& query);
    void onSearchResultActivated(const MangaResult& preview);
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
    TileStrip*       m_tileStrip = nullptr;
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

    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Task 26 — Phase 4 detail
    // view + manga downloader. m_tyDetailView lives at m_stack index 3
    // (post grid 0 + seriesView 1 + searchTakeover 2). Single downloader
    // owned by this page (Phase 8 retired the prior TankoyomiPage duplicate).
    // m_mangaDownloadIndex is instantiated in Phase 5 alongside the detail
    // view's chapter-marker wiring.
    ComicsTankoyomiDetailView*   m_tyDetailView       = nullptr;
    MangaDownloader*             m_mangaDownloader    = nullptr;
    MangaDownloadIndex*          m_mangaDownloadIndex = nullptr;
    tankoban::manga::premium::PremiumCatalog* m_premiumCatalog = nullptr;
    tankoban::manga::premium::TorrentRequestLedger*  m_premiumLedger   = nullptr;
    tankoban::manga::premium::TorrentVolumeProvider* m_premiumProvider = nullptr;
    // TANKOYOMI_PREMIUM Phase 9 -- thin facade over MangaDownloader +
    // TorrentVolumeProvider for one shared "Transfers paused" state. UI
    // affordance binding lands in Phase 11+; v1 is infrastructure only.
    tankoban::manga::premium::MangaTransferCoordinator* m_transferCoordinator = nullptr;
    TorrentClient*                                   m_torrentClient   = nullptr;

    enum class Mode { Library, SearchResults, TankoyomiDetail };
    Mode m_mode = Mode::Library;
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 9 Task 52 —
    // tracks the mode the user was in immediately BEFORE entering the
    // Tankoyomi detail view. Drives onDetailBack's routing (back to
    // search results vs back to library) and the "enteredFrom" hint
    // captured in the tankoyomiDetail nav-state blob.
    Mode m_enteredDetailFrom = Mode::Library;
};
