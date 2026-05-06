#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QMenu>

#include "core/TorrentResult.h"
#include "core/torrent/TorrentClient.h"

class CoreBridge;
class TorrentIndexer;
class QNetworkAccessManager;
class QTimer;
class QDragEnterEvent;
class QDropEvent;
class QKeyEvent;

class TankorentPage : public QWidget
{
    Q_OBJECT

public:
    explicit TankorentPage(CoreBridge* bridge, TorrentClient* client, QWidget* parent = nullptr);

    // STREAM_ADD_TO_TANKORENT (2026-05-06) — cross-page magnet hand-off
    // entry point. Called by MainWindow::onAddToTankorentRequested when
    // the user picks "Add torrent to Tankorent" on a Stream-mode source
    // card. Thin wrapper around the existing private addMagnetBatch
    // (empty category → m_client->defaultPaths().value("") fallback,
    // matching the no-category flow); displayName is informational and
    // appears in m_searchStatus + DebugLogBuffer. Duplicate / invalid
    // magnets are surfaced through the same status row, not silently
    // dropped — addMagnetBatch's existing isDuplicate path is
    // authoritative.
    void addMagnetFromExternal(const QString& magnetUri,
                               const QString& displayName);

private:
    void buildUI();
    void buildSearchControls(QVBoxLayout* parent);
    void buildStatusRow(QVBoxLayout* parent);
    void buildMainTabs(QVBoxLayout* parent);
    QTableWidget* createResultsTable();
    QTableWidget* createTransfersTable();

    void startSearch();
    void cancelSearch();
    int  dispatchIndexers(const QString& mediaType,
                          const QString& sourceFilter,
                          const QString& query,
                          int limit,
                          const QString& categoryId);
    void onSearchFinished(const QList<TorrentResult>& results);
    void onSearchError(const QString& error);
    void renderResults();
    void populateSourceCombo();
    void reloadCategoryOptions();
    void showResultsContextMenu(const QPoint& pos);
    void onAddTorrentClicked(int row);
    void refreshTransfers();
    void showTransfersContextMenu(const QPoint& pos);
    void onSourcesClicked();
    void onAddFromUrlClicked();

    // Iterates a list of magnet URIs through isDuplicate + resolveMetadata +
    // startDownload with a minimal AddTorrentConfig. Returns {added, skipped}.
    // Used by the bulk add-from-URL path (onAddFromUrlClicked) where popping
    // a per-magnet AddTorrentDialog 10× would be hostile UX. NOT used by the
    // single-add flows — those go through startSingleAddFlow below.
    QPair<int, int> addMagnetBatch(const QStringList& magnets,
                                   const QString& category,
                                   bool startImmediately);

    // STREAM_ADD_TO_TANKORENT_DIALOG_FIX 2026-05-06 — shared single-add
    // body extracted from onAddTorrentClicked. Drives the
    // resolveMetadata + AddTorrentDialog.exec() + startDownload (or
    // deleteTorrent on cancel) sequence. Two entry points share it:
    //   1. onAddTorrentClicked(row): in-Tankorent search-result click
    //   2. addMagnetFromExternal(magnet, displayName): cross-page hand-off
    //      from StreamPage's right-click "Add torrent to Tankorent"
    // Both call this AFTER their own context-specific validation
    // (row bounds, empty-magnet, etc).
    void startSingleAddFlow(const QString& magnetUri,
                            const QString& title);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

    // A1: click-to-sort on results table
    void onResultsHeaderClicked(int col);
    static bool compareResults(int col, Qt::SortOrder order,
                               const TorrentResult& a, const TorrentResult& b);

    // Quality tag + health helpers
    static QString qualityTagSuffix(const QString& title);
    // B1: Nyaa-style trust class based on seeder count. "healthy" / "normal" /
    // "poor" — drives the row tint applied in renderResults.
    static QString trustClass(const TorrentResult& r);

    CoreBridge*    m_bridge;
    TorrentClient* m_client = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    QTimer* m_transferTimer = nullptr;

    // Active indexers during a search
    QList<TorrentIndexer*> m_activeIndexers;
    int m_pendingSearches = 0;
    QList<TorrentResult> m_allResults;
    QList<TorrentResult> m_displayedResults; // deduped, sorted — matches table rows 1:1

    // Search controls
    QLineEdit*   m_queryEdit       = nullptr;
    QComboBox*   m_searchTypeCombo = nullptr;
    QComboBox*   m_sourceCombo     = nullptr;
    QComboBox*   m_categoryCombo   = nullptr;
    QComboBox*   m_filterCombo     = nullptr;   // E1: client-side seeder filter
    QPushButton* m_searchBtn       = nullptr;
    QPushButton* m_cancelBtn       = nullptr;
    QPushButton* m_refreshBtn      = nullptr;
    QPushButton* m_sourcesBtn      = nullptr;
    QPushButton* m_addUrlBtn       = nullptr;
    QPushButton* m_moreBtn         = nullptr;

    // Status row
    QLabel* m_searchStatus   = nullptr;
    QLabel* m_downloadStatus = nullptr;
    QLabel* m_backendStatus  = nullptr;

    // D1/D2: result count line above the table + soft cap toggle. Label text
    // also embeds the "Show all" link via rich-text linkActivated.
    QLabel* m_resultsCountLabel = nullptr;
    bool    m_showAll           = false;

    // Main area
    QTabWidget*   m_tabWidget      = nullptr;
    QTableWidget* m_resultsTable   = nullptr;
    QTableWidget* m_transfersTable = nullptr;

    // Transfers state
    QList<TorrentInfo> m_cachedActive;
    int m_sortCol   = -1;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;

    // A1/C: results table sort state. Default = Seeders desc — col index is
    // 4 in the post-Track-C layout (0 Title, 1 Category, 2 Size, 3 Files,
    // 4 Seeders, 5 Leechers, 6 Link).
    int           m_resultsSortCol   = 4;
    Qt::SortOrder m_resultsSortOrder = Qt::DescendingOrder;

    // Speed formatting helper
    static QString humanSpeed(int bytesPerSec);
};
