#pragma once

#include <QWidget>
#include <QThread>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>
#include <QTimer>
#include <QSettings>
#include <QStringList>
#include <QMap>
#include <QJsonObject>
#include "../LayerEntry.h"
class QPushButton;
class QFrame;
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
class ReadComicsScraper;
class MangaDownloader;
class MangaDownloadIndex;        // Phase 5 creates the type; Phase 4 stores nullptr
class TorrentClient;
namespace tankoban::manga {
    class NyaaRuntimeSource;
    class WeebCentralVolumePacker;
    class WesternVolumeDownloader;
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
    namespace mangafire {
        class MangaFireCatalogClient;
        class MangaWeebCentralResolver;
    }
    struct MangaCatalog;
}
#include "core/manga/LocalMangaCatalogIndex.h"
#include "core/manga/MangaResult.h"
#include "core/manga/mangafire/MangaWeebCentralResolver.h"
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
    // v1.6 Phase D.4 (2026-05-19) — cross-mode library-section snapshot used
    // by library_get_* commands + embedded in devSnapshot under "library".
    QJsonObject devLibrarySection() const;
    QJsonObject devSelectVolume(int row);
    QJsonObject devOpenSeries(const QString& seriesId);
    QJsonObject devOpenChapter(const QString& seriesId, int volumeNumber, int chapterNumber);
    QJsonObject devSearchTankoyomi(const QString& query, int timeoutMs);
    QJsonObject devDownloadsSnapshot() const;
    QJsonObject devDispatchVolume(const QString& seriesId, int volumeNumber, const QString& source);
    QJsonObject devSourcesSnapshot() const;

    // COMICS_DOWNLOADS_SIDEBAR_PAGE 2026-05-26 (Agent 9) — non-owning accessor
    // so MainWindow can wire the shared MangaDownloadIndex into ComicsDownloadsPage.
    // Returns nullptr before ComicsPage::activate() completes construction.
    MangaDownloadIndex* mangaDownloadIndex() const { return m_mangaDownloadIndex; }

    // COMICS_DOWNLOAD_DISPLAY_PROJECTION 2026-05-26 (Agent 9) — canonical
    // display helpers. ComicsDownloadsPage calls these to project raw
    // (sourceId, seriesId) buckets into human-grouped series cards.

    // Canonical grouping key: "anilist:<id>", "title:<normalized>", or
    // "raw:<sourceId>:<seriesId>". Used to merge entries from different
    // sources into one display entity.
    QString resolveCanonicalGroupKey(const QString& sourceId,
                                     const QString& seriesId) const;

    // Resolve the best human-readable display title for a download entry.
    // Order: AniList cache → MangaFire local catalog → Premium catalog →
    // Tankoyomi library record → empty (caller falls back to humanizing).
    QString resolveDisplayTitle(const QString& sourceId,
                                const QString& seriesId) const;

    // COMICS_CANONICAL_COVER 2026-05-26 (Agent 9) — resolve the canonical
    // series cover. Priority: MangaFire Volume 1 coverUrlJapanese → empty
    // (caller falls back to existing AniList → CBZ thumbnail → placeholder).
    // Accepts an anilistId (may be 0) and display title; uses the catalog
    // index to find the matching JSON, then returns Volume 1's cover URL.
    QString resolveCanonicalSeriesCover(int anilistId,
                                        const QString& displayTitle) const;

    // COMICS_CR_VOLUME_COVER 2026-05-29 (Agent 1) — resolve the cover for a
    // SPECIFIC volume (the one shown in a Continue Reading tile). A WeebCentral-
    // compiled volume's cbz first page is interior chapter art, not the cover,
    // so the Continue strip must pull the real per-volume cover from the catalog.
    // Returns that volume's coverUrlJapanese; falls back to Volume 1's cover;
    // empty if the series isn't in the catalog (caller keeps the cbz thumbnail).
    QString resolveReadVolumeCover(const QString& displayTitle,
                                   int volumeNumber) const;

    // Static: map sourceId to human display label.
    // tankoyomi_premium → "Premium", mangafire_catalog → "MangaFire",
    // weebcentral → "WeebCentral", fallback: title-case with suffix stripping.
    static QString resolveSourceLabel(const QString& sourceId);

    // Static: derive a readable title from a slug or seriesId.
    // "one-piece" → "One Piece", "anilist_30013" → "" (returns empty —
    // callers must already have resolved via AniList cache first).
    static QString humanizeSlug(const QString& slug);
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

    // COMICS_MANGAFIRE_PIVOT Phase B.2 (2026-05-23). Slot wired to
    // ComicsSeriesView's forceRefreshRequested signal (network fallback gone;
    // catalog is always local-first now).
    void onForceRefreshRequested();

    // COMICS_MANGAFIRE_ON_DEMAND_FETCH 2026-05-23 (Agent 1). When the
    // local-first dispatchCatalogResolve finds no catalog JSON on disk,
    // m_mangafireClient->fetchByTitle() fires; the reply lands here. On
    // ready we refresh the local index and re-dispatch so populateVolumeRows-
    // FromCatalog renders the rows. On failed we just log; the series-view
    // already paints WeebCentral/AniList fallback content.
    void onMangaFireCatalogReady(const tankoban::manga::MangaCatalog& catalog,
                                  const QString& writtenPath);
    void onMangaFireCatalogFailed(const QString& title, const QString& reason);
    void onWcResolveRequested(const QString& mangaFireSeriesId,
                              int volumeNumber);
    void onWcResolveRangeRequested(const QString& mangaFireSeriesId,
                                   int volumeNumber,
                                   int rangeStart,
                                   int rangeEnd);
    void onWcResolverViable(
        tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey key,
        QStringList chapterIds);
    void onWcResolverSkip(
        tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey key,
        QString reasonCode);

    // COMICS_WC_LIBRARY_ENRICH 2026-05-24 (Agent 1). Best-effort AniList
    // enrichment when the user clicks Add to Library on a series with no
    // AniList id (MangaFire-catalog-only). Fires an async searchByTitle and
    // on top-match adds a bookmark via the existing AniList-keyed cache
    // plus re-shows the series with the enriched MediaPreview so the local
    // view picks up its new identity.
    void onAddToLibraryByTitleRequested(const QString& title);

    // COMICS_WC_AUTOENRICH 2026-05-24 (Agent 1). Sibling slot fired on
    // every showSeries(MangaResult) when anilistId is 0. Same search +
    // cache-seed + re-show flow as the Add-to-Library path, but does NOT
    // call addBookmark — the user opted into viewing, not into committing.
    void onEnrichSeriesByTitleRequested(const QString& title);

protected:
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 Phase 5 Task 30 —
    // re-validate the on-disk MangaDownloadIndex each time this page
    // is shown so external file deletions surface as chip-state updates.
    void showEvent(QShowEvent* e) override;

    // Stream/Theatre-parity search-bar (2026-05-22): drives the search
    // history dropdown off m_searchBar FocusIn / FocusOut events.
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void buildUI();

    // Stream/Theatre-parity search-bar helpers (2026-05-22). All inline on
    // this class, mirroring StreamPage's pattern (no separate helper class).
    // See docs/superpowers/plans/2026-05-22-comics-search-bar-parity.md.
    void loadSearchHistory();
    void saveSearchHistory();
    void pushSearchHistory(const QString& query);
    void removeSearchHistoryEntry(const QString& query);
    void clearSearchHistory();
    void buildSearchHistoryDropdown();
    void showSearchHistoryDropdown();
    void hideSearchHistoryDropdown();
    void positionSearchHistoryDropdown();
    void setSearchBusy(bool busy);
    void renderSearchOpenFallback(const MangaResult& result);

    void addSeriesTile(const SeriesInfo& series);
    void toggleViewMode();
    // COMICS_MANGAFIRE_PIVOT Phase B.2 (2026-05-23). Local-only catalog
    // resolve: looks up the series in m_localCatalogIndex (data/mangafire_catalog/)
    // and calls populateVolumeRowsFromCatalog on hit. No-ops on miss.
    // Renamed from dispatchFandomResolve; network fallback chain removed.
    void dispatchCatalogResolve(const QString& seriesId,
                                const QString& titleHint);
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

    // COMICS_CONTEXT_MENU 2026-05-28: thin public forwarder so
    // ComicsDownloadsPage can navigate to series from context menu.
    Q_INVOKABLE void openSeriesForDownloadEntry(const QString& sourceId, const QString& seriesId,
                                                  const QString& displayTitle);

    void openSeriesByRecord(const ComicsLibraryRecord& record);

    // COMICS_WESTERN_CATALOGUE Task 7 (2026-05-31, Agent 2) — Western shelf.
    // buildWesternScreen() adds the browse grid as a new m_stack screen;
    // refreshWesternGrid() (re)scans data/western_catalogue/*.json into tiles;
    // openWesternSeriesFromJson() does the GUARDED render-only open (direct
    // populateVolumeRowsFromCatalog, never showSeries — no AniList/mangafire
    // enrichment bleed onto a Western comic); show*Mode() drive the toggle.
    // Shared-recipe search row builder (2026-06-02). Constructs the full
    // manga-parity chrome (input + busy spinner + search icon button) and
    // wires all handlers. Each caller receives the three live widget pointers
    // via out-params and gets back a container QWidget* to add to its layout.
    QWidget* buildSearchRow(QLineEdit*& outBar,
                            QWidget*&   outBusy,
                            QPushButton*& outBtn,
                            const QString& placeholder,
                            const QString& sourceId);

    void buildWesternScreen();
    void refreshWesternGrid();
    void openWesternSeriesFromJson(const QString& jsonPath);
    // COMICS_WESTERN_ADD 2026-06-01 (Agent 2) — shared render-only open used by
    // BOTH the disk path (openWesternSeriesFromJson loads a baked JSON) and the
    // live-search path (a freshly fetched RCO series object). Does the GUARDED
    // populateVolumeRowsFromCatalog render + nav-entry + Western-detail state.
    // jsonPath is the on-disk path when opening a baked file (empty for a
    // live, not-yet-saved series); onShelf pre-sets the library button.
    void openWesternSeriesFromCatalog(const tankoban::manga::MangaCatalog& catalog,
                                      const QString& jsonPath,
                                      bool onShelf);
    // COMICS_WESTERN_DOWNLOAD 2026-06-02 (Agent 1). Connects all signals from
    // m_westernDownloader into ComicsPage slots and wires
    // m_tyVolumeSeriesView::downloadWesternEditionRequested -> request slot.
    // Called once from setTorrentClient() after m_westernDownloader is constructed.
    void wireWesternDownloader();
    void showMangaMode();
    void showWesternMode();

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
        WeebCentralPacker = 2,
        WesternGetComics = 3
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

    // Stream/Theatre-parity search-bar additions (2026-05-22, Agent 5 cross-
    // domain commission). Mirrors StreamPage's chrome: magnifying-glass icon
    // button + indeterminate busy spinner + persistent history dropdown.
    // m_searchBusy is held as QWidget* so this header doesn't need to pull
    // <QProgressBar> (matches StreamPage.h:409). QSettings key is
    // "comics/searchHistory" -- disjoint from "stream/searchHistory".
    QPushButton*     m_searchBtn               = nullptr;
    QWidget*         m_searchBusy              = nullptr;
    QFrame*          m_searchHistoryDropdown   = nullptr;
    QWidget*         m_searchHistoryList       = nullptr;
    QTimer*          m_searchHistoryHideTimer  = nullptr;
    QStringList      m_searchHistory;          // manga shelf
    QStringList      m_westernSearchHistory;   // Western shelf (kept separate)
    static constexpr int kMaxSearchHistory = 10;
    // Per-shelf history routing: the active list/key follow which bar has focus
    // (m_activeSearchBar). Manga and Western histories never cross-pollinate.
    QStringList& activeSearchHistory();
    QString      activeSearchHistoryKey() const;
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

    // COMICS_WESTERN_CATALOGUE Task 7 (2026-05-31, Agent 2). Western browse grid
    // as a separate m_stack screen + a Manga/Western toggle in the top chrome.
    // Western series render through the SAME m_tyVolumeSeriesView (render-only,
    // no enrichment) via WesternCatalogLoader -> populateVolumeRowsFromCatalog.
    // Manga path is untouched — Western is purely additive (own loader, own dir,
    // own screen). m_westernStackIndex captured from addWidget (index-agnostic).
    TileStrip*   m_westernGrid        = nullptr;
    QScrollArea* m_westernScroll      = nullptr;
    QLineEdit*   m_westernSearchBar   = nullptr;   // live RCO search on the Western shelf
    QWidget*     m_westernSearchBusy  = nullptr;   // spinner for Western bar (parity with manga)
    QPushButton* m_westernSearchBtn   = nullptr;   // search icon button for Western bar
    QPushButton* m_mangaTabBtn        = nullptr;
    QPushButton* m_westernTabBtn      = nullptr;
    int          m_westernStackIndex  = -1;

    // Shared-recipe search bar: tracks which bar has focus so the
    // shared history dropdown + busy widget target the right bar.
    QLineEdit*   m_activeSearchBar    = nullptr;
    QWidget*     m_activeSearchBusy   = nullptr;

    // COMICS_WESTERN_ADD 2026-06-01 (Agent 2). Live Western search + add-to-shelf.
    // m_readComicsScraper is the registry-owned RCO scraper, grabbed in the ctor
    // so onSearchResultActivated can route an RCO search pick into the live
    // fetchWesternSeries() page-scrape (bypassing the AniList/mangafire manga
    // enrichment that would corrupt a Western comic's identity). The raw JSON of
    // the currently-displayed live Western series is stashed in
    // m_pendingWesternJson so addWesternToLibraryRequested can persist it verbatim
    // to data/western_catalogue/<seriesId>.json. m_pendingWesternSeriesId is the
    // file stem used for that write + the on-disk existence check.
    ReadComicsScraper* m_readComicsScraper = nullptr;
    QJsonObject        m_pendingWesternJson;
    QString            m_pendingWesternSeriesId;
    // COMICS_WESTERN_DOWNLOAD 2026-06-02 (Agent 1). GetComics resolve + download
    // provider. Constructed lazily in setTorrentClient() (alongside
    // TorrentVolumeProvider) so the magnet path has a live TorrentClient.
    // DDL path works without a TorrentClient (nam is always available).
    tankoban::manga::WesternVolumeDownloader* m_westernDownloader = nullptr;

    // COMICS_MANGAFIRE_PIVOT Phase B.2 (2026-05-23). Local MangaFire catalog
    // index. Scans data/mangafire_catalog/*.json at construction; consulted
    // by dispatchCatalogResolve for every series open. The live network
    // fallback chain (FallbackChainResolver + FandomVolumeResolver +
    // WikipediaResolver + WikidataClient) was removed — local-first is the
    // ONLY resolution path now.
    tankoban::manga::LocalMangaCatalogIndex m_localCatalogIndex;
    // COMICS_MANGAFIRE_ON_DEMAND_FETCH 2026-05-23 (Agent 1). Live MangaFire
    // scraper for the search-first architecture — runs only on no-local-match
    // inside dispatchCatalogResolve. Caches to data/mangafire_catalog/*.json
    // so subsequent opens of the same series are instant via the index.
    tankoban::manga::mangafire::MangaFireCatalogClient* m_mangafireClient = nullptr;
    tankoban::manga::mangafire::MangaWeebCentralResolver* m_wcResolver = nullptr;
    // Identity of the most recently dispatched catalog resolve — stale-event
    // guard mirroring the m_currentSeriesKey pattern in ComicsSeriesView.
    QString m_pendingCatalogSeriesId;
    // Title hint stashed alongside m_pendingCatalogSeriesId so the on-demand
    // MangaFire fetch ready-callback can re-dispatch with the same seriesId/
    // titleHint pair the original resolve used.
    QString m_pendingCatalogTitleHint;
    tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey m_currentWcResolveKey;
    quint64 m_wcResolveSerial = 0;

    // COMICS_WC_LIBRARY_ENRICH 2026-05-24 (Agent 1). State for the AniList
    // search-by-title enrichment fired from onAddToLibraryByTitleRequested.
    // The requestId is allocated per-click and filtered in the persistent
    // searchSucceeded/searchFailed connect lambdas so unrelated searches
    // (the search bar) don't trample this flow.
    int     m_nextLibraryEnrichReqId = 5000000;
    int     m_pendingLibraryEnrichReqId = 0;
    QString m_pendingLibraryEnrichTitle;
    // COMICS_WC_AUTOENRICH 2026-05-24 — true when this pending enrichment
    // should also add a bookmark on match (the Add-to-Library path); false
    // when it's just data enrichment (the auto-on-series-open path).
    bool    m_pendingLibraryEnrichAddBookmark = false;
    int     m_pendingSearchOpenEnrichReqId = 0;
    MangaResult m_pendingSearchOpenFallback;
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
    // COMICS_WESTERN_RICHNESS 2026-06-01. True while viewing a Western-catalogue
    // series detail, so the in-view "← Back" (onDetailBack) returns to the
    // Western grid instead of the manga library. Set in openWesternSeriesFromJson,
    // cleared on any grid land (showLibraryMode/showWesternMode) + after onDetailBack.
    bool m_detailEnteredFromWestern = false;

    // PHASE 0 NAV CONTRACT RESTORE 2026-05-17 (Agent 5) — restoreNavState
    // raises this flag so the mode-flipper slots it invokes (showLibraryMode,
    // showSearchMode, etc.) skip their navigationRequested emit. Without this
    // guard a Back-restore would re-record the restored target, polluting the
    // global NavHistory. Mirrors StreamPage's emitNav=false branch.
    bool m_inNavRestore = false;
};
