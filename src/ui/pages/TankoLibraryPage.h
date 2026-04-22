#pragma once

#include <QHash>
#include <QPixmap>
#include <QWidget>

#include "core/book/BookResult.h"
#include "tankolibrary/TransfersView.h"

class CoreBridge;
class BookScraper;
class BookDownloader;
class BookResultsGrid;
class QLineEdit;
class QPushButton;
class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QStackedWidget;
class QNetworkAccessManager;
class QNetworkReply;

// M1 scaffold + M2.1 detail view — books search sub-app sibling to TankoyomiPage.
//
// Current wiring: one scraper (Anna's Archive, QWebEngineView-backed). M3
// will add LibGenScraper + dual-source aggregation. M2.1 adds a detail view
// via a QStackedWidget (results page + detail page) swapped in on row
// double-click. No download button yet (M2.2+); filters + tile-grid in Track B.
class TankoLibraryPage : public QWidget
{
    Q_OBJECT

public:
    explicit TankoLibraryPage(CoreBridge* bridge, QWidget* parent = nullptr);

private:
    // UI builders
    void buildUI();
    void buildResultsPage();
    void buildDetailPage();

    // Search flow (M1 → M2.3 multi-source → Track B client filter)
    void startSearch();
    void cancelSearch();
    void refreshSearchStatus();                     // M2.3 — per-source status builder
    void onFormatFilterToggled(bool checked);       // Track B — any of EPUB/PDF/MOBI toggled
    void onEnglishOnlyToggled(bool checked);        // Track B — English-only filter
    void onSortChanged(int idx);                    // Track B closeout — sort combo
    void applyClientFilter();                       // Track B — rebuild grid from m_results honoring filter state
    QList<BookResult> filteredResults() const;      // Track B — m_results narrowed by checkbox state

    // Detail flow (M2.1)
    void onResultActivated(int row);
    void showDetailFor(const BookResult& r);
    void showResultsPage();
    void onDetailReady(const BookResult& detail);
    void onDetailError(const QString& message);
    void paintDetail(const BookResult& r);
    void loadDetailCover(const QString& url);

    // Track B closeout — grid thumbnail population.
    void populateGridThumbnails();
    void fetchAndCacheThumbnail(const QString& md5, const QString& url);
    void applyThumbnailToCurrentGrid(const QString& md5, const QPixmap& pix);

    // Download flow (M2.4 — real Download button + BookDownloader).
    // State machine: Idle → user clicks → Resolving (disabled button +
    // indeterminate progress) → scraper emits urls → Downloading (progress
    // bar determinate) → complete → Idle (button re-arms).
    enum class DownloadStage { Idle, Resolving, Downloading };

    void onDownloadClicked();
    void onScraperUrlsReady(const QString& md5, const QStringList& urls);
    void onScraperResolveFailed(const QString& md5, const QString& reason);
    void onDownloaderProgress(const QString& md5, qint64 bytesReceived, qint64 bytesTotal);
    void onDownloaderComplete(const QString& md5, const QString& filePath);
    void onDownloaderFailed(const QString& md5, const QString& reason);

    void resetDownloadUiToIdle();

    // Track B closeout — Transfers inline tab (Tankoyomi-parity).
    void showSearchResultsTab();
    void showTransfersTab();
    void updateTransfersTabBadge();
    void refreshTransfersView();
    TransferRecord* findTransferRecord(const QString& md5);

    CoreBridge*            m_bridge = nullptr;
    QNetworkAccessManager* m_nam    = nullptr;

    // M2.3 — multi-source scraper list. LibGen first (primary, no anti-bot
    // gate); AA second (blocked by /books/ captcha, kept available for
    // future opt-in flow). Order matters for display ordering in the grid.
    QList<BookScraper*> m_scrapers;

    BookScraper* scraperFor(const QString& sourceId) const;

    // Top-level stack — swaps between results page + detail page
    QStackedWidget* m_stack       = nullptr;
    QWidget*        m_resultsPage = nullptr;
    QWidget*        m_detailPage  = nullptr;

    // Search controls (live inside m_resultsPage)
    QLineEdit*   m_queryEdit         = nullptr;
    QPushButton* m_searchBtn         = nullptr;
    QPushButton* m_cancelBtn         = nullptr;
    // Track B closeout — format filter is now a multi-select trio; EPUB
    // default-on, PDF + MOBI default-off (migrated from legacy epub_only key).
    QCheckBox*   m_epubChk             = nullptr;
    QCheckBox*   m_pdfChk              = nullptr;
    QCheckBox*   m_mobiChk             = nullptr;
    QCheckBox*   m_englishOnlyCheckbox = nullptr;  // Track B — default ON, QSettings-persisted
    QComboBox*   m_sortCombo           = nullptr;  // Track B closeout — relevance/year/size sort
    QLabel*      m_statusLbl           = nullptr;

    // Results area (lives inside m_resultsPage)
    BookResultsGrid* m_grid = nullptr;

    // Detail panel widgets (live inside m_detailPage)
    QPushButton* m_detailBackBtn     = nullptr;
    QLabel*      m_detailCover       = nullptr;
    QLabel*      m_detailTitle       = nullptr;
    QLabel*      m_detailAuthor      = nullptr;
    QLabel*      m_detailPublisher   = nullptr;
    QLabel*      m_detailYear        = nullptr;
    QLabel*      m_detailPages       = nullptr;
    QLabel*      m_detailLanguage    = nullptr;
    QLabel*      m_detailFormat      = nullptr;
    QLabel*      m_detailSize        = nullptr;
    QLabel*      m_detailIsbn        = nullptr;
    QLabel*      m_detailDescription = nullptr;

    // M2.4 — real Download UI (replaces M2.2 scaffold button).
    QPushButton*  m_downloadButton   = nullptr;
    QProgressBar* m_downloadProgress = nullptr;
    QLabel*       m_detailStatus     = nullptr;

    BookDownloader* m_downloader     = nullptr;
    DownloadStage   m_downloadStage  = DownloadStage::Idle;

    // Track B closeout — Tankoyomi-parity inline Transfers tab.
    // `m_resultsInnerStack` sits below the tab pills and flips between
    // `m_grid` (page 0) and `m_transfersView` (page 1). Tab pills are two
    // QPushButtons styled as a toggle group.
    QStackedWidget*       m_resultsInnerStack = nullptr;
    QPushButton*          m_searchResultsTab  = nullptr;
    QPushButton*          m_transfersTab      = nullptr;
    QLabel*               m_transfersCounter  = nullptr;   // "Active: X | History: Y"
    TransfersView*        m_transfersView     = nullptr;
    QList<TransferRecord> m_transfers;

    QList<BookResult>       m_results;
    BookResult              m_selectedResult;    // M2.1 — search-row snapshot for instant-paint
    bool                    m_searchInFlight = false;
    QNetworkReply*          m_coverReply     = nullptr;  // M2.1 — at-most-one in-flight cover fetch
    QHash<QString, QPixmap> m_thumbnailCache;     // Track B closeout — md5-keyed in-memory thumbnail cache

    // M2.3 — per-scraper search-cycle tracking so multiple sources can
    // complete asynchronously and the status line can report per-source
    // outcomes ("Done: 20 from LibGen, 0 from Anna's Archive (timeout)").
    int                                m_searchesPending = 0;
    QMap<QString, int>                 m_searchCountBySource;
    QMap<QString, QString>             m_searchErrorBySource;
};
