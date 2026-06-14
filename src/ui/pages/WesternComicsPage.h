#pragma once

// SIX_MODE_RESTRUCTURE Arc 1 STEP 1 (2026-06-14, Agent 1) — WesternComicsPage.
//
// The Western (RCO / readallcomics) half of the old Comics mode, extracted from
// MangaPage into its own standalone page (expand-contract: this is the "expand"
// — additive copy; the MangaPage strip is STEP 3). MangaPage keeps the Asian-
// manga half (Tankoyomi / WeebCentral / MangaFire). Each page owns its OWN
// ComicsSeriesView + QStackedWidget so the two lanes no longer share a single
// render surface (the deepest coupling the wave-1 map flagged).
//
// SHARED ENGINE IS INJECTED, NOT OWNED. Western downloads run through the SAME
// MangaDownloader / MangaDownloadIndex / MangaSourceRegistry / QNetworkAccessManager
// / TorrentClient that MangaPage builds — a single shared store. Duplicating it
// would corrupt the download index, so MainWindow (STEP 2) injects these via the
// setters below AFTER MangaPage has constructed them. The ONLY engine object this
// page owns is its per-user WesternLibrary store (see m_westernLibrary).
//
// Page shape mirrors MangaPage: activate(), resetToRoot(), refreshContinueStrip(),
// restoreLayer(), enteredLayer/exitedLayer signals, dev-snapshot accessors.

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
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
class TileCard;
class TileStrip;
class ComicsTankoyomiSearchWidget;
class MangaSourceRegistry;
class ReadComicsScraper;
class ReadAllComicsScraper;
class MangaDownloader;
class MangaDownloadIndex;
class TorrentClient;
class QNetworkAccessManager;
struct MangaResult;

namespace tankoban::manga {
    class WesternVolumeDownloader;
    class WesternLibrary;
    struct MangaCatalog;
    namespace comics {
        class ComicsSeriesView;
    }
}

class WesternComicsPage : public QWidget {
    Q_OBJECT
public:
    explicit WesternComicsPage(CoreBridge* bridge, QWidget* parent = nullptr);
    ~WesternComicsPage() override;

    // ── Injected shared engine (STEP 2 wires these from MainWindow AFTER
    //    MangaPage builds them). Single shared store — must NOT duplicate. ──
    void setSourceRegistry(MangaSourceRegistry* registry);
    void setMangaDownloader(MangaDownloader* downloader);
    void setMangaDownloadIndex(MangaDownloadIndex* index);
    void setNetworkManager(QNetworkAccessManager* nam);
    void setTorrentClient(TorrentClient* client);

    // ── Page-shape contract (mirrors MangaPage) ──
    void activate();
    void resetToRoot();
    // Public so MainWindow::closeComicReader can refresh the continue strip the
    // moment the reader returns to the library (mirrors MangaPage's precedent).
    void refreshContinueStrip();

    // v1.11 Western download smoke harness (dev-cmd names kept as comics_* for
    // back-compat; STEP 2 repoints MainWindow's handlers here — see fork
    // 'western-dev-prefix').
    QJsonObject devOpenWesternSeries(const QString& seriesId);
    QJsonObject devDownloadWesternEdition(int volumeNumber);
    QJsonObject devWesternDownloadState(int volumeNumber) const;
    QJsonObject devSnapshot() const;
    QJsonObject devSeriesSnapshot() const;

    // Non-owning accessor (parity with MangaPage::mangaDownloadIndex()).
    MangaDownloadIndex* mangaDownloadIndex() const { return m_mangaDownloadIndex; }

signals:
    void openComic(const QString& cbzPath, const QStringList& seriesCbzList, const QString& seriesName);
    // Emitted BEFORE every user-initiated in-page layer transition; MainWindow
    // connects this to PerModeNavController::pushLayer. Suppressed during
    // restoreLayer via m_inNavRestore.
    void enteredLayer(const tankoban::ui::LayerEntry& entry);
    // Emitted when the user closes a deep layer via an in-page affordance.
    void exitedLayer();

public slots:
    // Re-render the targeted layer in-place WITHOUT emitting enteredLayer.
    // Called by MainWindow when PerModeNavController::layerRestoreRequested
    // fires for pageId="western_comics".
    void restoreLayer(const tankoban::ui::LayerEntry& target);

protected:
    // Re-validate the on-disk MangaDownloadIndex each time the page is shown so
    // external file deletions surface as chip-state updates.
    void showEvent(QShowEvent* e) override;
    // Drives the search-history dropdown off the search bar focus events.
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onSearchResultActivated(const MangaResult& result);
    void onProviderVolumeCompleted(const QString& seriesId,
                                   int volumeNumber,
                                   const QString& cbzPath,
                                   int fallbackSourceKind);
    void onProviderVolumeFailed(const QString& seriesId,
                                int volumeNumber,
                                const QString& errorCode,
                                const QString& errorMessage,
                                int fallbackSourceKind);

private:
    void buildUI();

    // ── Shared search chrome (COPIED from MangaPage; STEP 4 de-dups into a
    //    ComicsPageBase). De-branched to the Western bar/history. ──
    QWidget* buildSearchRow(QLineEdit*& outBar,
                            QWidget*&   outBusy,
                            QPushButton*& outBtn,
                            const QString& placeholder,
                            const QString& sourceId);
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
    void showSearchMode(const QString& query);
    void showLibraryMode();

    // The page's western CONTINUE READING refresh (public refreshContinueStrip()
    // forwards to this; named with a trailing underscore so the two don't clash).
    void refreshWesternContinueStrip_();

    // ── Western page methods (the page's own copies) ──
    void ensureSearchTakeover();     // lazily build + wire the page's OWN search-takeover (needs registry)
    void wireSeriesView();           // wire the page's OWN ComicsSeriesView signals
    void wireMangaDownloader();      // wire the injected MangaDownloader signals
    void wireWesternDownloader();    // construct + wire WesternVolumeDownloader (needs TorrentClient)
    void grabScrapersFromRegistry(); // grab non-owning RCO scraper observers from the registry

    void refreshWesternLibrary();
    void reconcileWesternLibraryFromDisk();
    void openWesternSeriesFromLibrary(const QString& seriesId);
    void ensureWesternIssueInMap(const QString& cbzPath);
    void openWesternSeriesFromJson(const QString& jsonPath);
    void openWesternSeriesFromCatalog(const tankoban::manga::MangaCatalog& catalog,
                                      const QString& jsonPath,
                                      bool onShelf);
    void startWesternIssueDownload(const QString& seriesTitle, double issueNumber,
                                   const QString& editionTitle, int volumeNumber,
                                   const QString& destPath);
    void fetchAndRenderWesternIssues(const tankoban::manga::MangaCatalog& seriesMeta,
                                     bool onShelf);
    void updateWesternMangaStatus(const QString& recordId);
    void onWesternChapterCompleted(const QString& source, const QString& seriesTitle,
                                   const QString& finalPath);
    void fetchPosterForTile(TileCard* card, const QString& coverUrl);

    enum class PendingVolumeSourceKind {
        WesternGetComics = 3
    };

    CoreBridge*          m_bridge = nullptr;
    FadingStackedWidget* m_stack  = nullptr;

    // ── Western browse/library grid + search chrome (grid is m_stack index 0) ──
    QScrollArea* m_gridScroll  = nullptr;
    TileStrip*   m_westernGrid = nullptr;
    QLineEdit*   m_westernSearchBar  = nullptr;
    QWidget*     m_westernSearchBusy = nullptr;
    QPushButton* m_westernSearchBtn  = nullptr;
    QLabel*      m_westernEmptyLabel = nullptr;

    // Search history. QSettings key stays "comics/westernSearchHistory" VERBATIM
    // to preserve the user's saved Western history (fork 'history-key').
    QFrame*      m_searchHistoryDropdown  = nullptr;
    QWidget*     m_searchHistoryList      = nullptr;
    QTimer*      m_searchHistoryHideTimer = nullptr;
    QStringList  m_searchHistory;
    static constexpr int kMaxSearchHistory = 10;

    // ── Western Continue Reading (own strip; manga excludes western issues) ──
    QWidget*               m_westernContinueSection = nullptr;
    TileStrip*             m_westernContinueStrip   = nullptr;
    struct FileRef { QString filePath; QString seriesPath; QString coverPath; };
    QMap<QString, FileRef> m_westernProgressKeyMap;

    // ── The page's OWN series-detail view (the deep decoupling) ──
    tankoban::manga::comics::ComicsSeriesView* m_seriesView = nullptr;

    // ── The page's OWN search-takeover (results list), mirroring MangaPage's
    //    m_searchTakeover. Constructed LAZILY in setSourceRegistry (the takeover
    //    wires scraper searchFinished signals at ctor-time and the registry is
    //    injected after this page is built, so building it in buildUI/ctor — when
    //    m_sourceRegistry is still null — would silently drop those connections).
    //    setActiveSourceId("readallcomics"); resultPicked -> onSearchResultActivated. ──
    ComicsTankoyomiSearchWidget* m_searchTakeover = nullptr;

    // ── Injected shared engine (non-owning; MainWindow owns lifetimes) ──
    MangaSourceRegistry*   m_sourceRegistry     = nullptr;
    QNetworkAccessManager* m_nam                = nullptr;
    MangaDownloader*       m_mangaDownloader    = nullptr;
    MangaDownloadIndex*    m_mangaDownloadIndex = nullptr;
    TorrentClient*         m_torrentClient      = nullptr;
    ReadComicsScraper*     m_readComicsScraper    = nullptr; // non-owning; from registry
    ReadAllComicsScraper*  m_readAllComicsScraper = nullptr; // non-owning; from registry

    // ── OWNED Western engine objects (the only ones this page constructs) ──
    tankoban::manga::WesternLibrary*          m_westernLibrary    = nullptr;
    tankoban::manga::WesternVolumeDownloader* m_westernDownloader = nullptr;

    // ── Pending / in-flight Western state ──
    QJsonObject m_pendingWesternJson;
    QString     m_pendingWesternSeriesId;
    QString     m_currentWesternSeriesCover;
    QString     m_currentDetailSeriesTitle;
    QString     m_westernDownloadEdition;
    QString     m_westernDownloadRecordId;
    int         m_pendingWesternDownloadVolume = 0;

    // Suppresses enteredLayer re-emit during Back/restore (mirrors MangaPage).
    bool m_inNavRestore = false;
    // Guards wireWesternDownloader / wireMangaDownloader against double-wiring on
    // re-injection.
    bool m_downloaderWired = false;
    bool m_seriesViewWired = false;
};
