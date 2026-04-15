#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QSet>

#include "core/manga/MangaResult.h"

class CoreBridge;
class MangaScraper;
class MangaDownloader;
class MangaResultsGrid;
class QNetworkAccessManager;
class QTimer;

class TankoyomiPage : public QWidget
{
    Q_OBJECT

public:
    explicit TankoyomiPage(CoreBridge* bridge, QWidget* parent = nullptr);

    // B1: async cover cache — checks disk, fetches if missing, emits coverReady
    // when the file is available. Safe to call repeatedly for the same key.
    // Returns the final cache path whether or not the file already exists.
    QString ensureCover(const QString& source, const QString& id, const QString& thumbUrl);

signals:
    // Emitted when a cover has just been written to disk (or was already there).
    // Consumers (B2 grid, C3 detail panel) connect to update their tiles.
    void coverReady(const QString& source, const QString& id, const QString& path);

private:
    void buildUI();
    void buildSearchControls(QVBoxLayout* parent);
    void buildStatusRow(QVBoxLayout* parent);
    void buildMainTabs(QVBoxLayout* parent);
    QTableWidget* createResultsTable();
    QTableWidget* createTransfersTable();

    void startSearch();
    void cancelSearch();
    void renderResults();
    void onResultDoubleClicked(int row);
    void refreshTransfers();
    void updateResultsView();   // B4: pick data view vs empty state
    void showResultContextMenu(int row, const QPoint& globalPos);   // E2

    CoreBridge*            m_bridge;
    QNetworkAccessManager* m_nam        = nullptr;
    MangaDownloader*       m_downloader = nullptr;
    QList<MangaScraper*>   m_scrapers;
    int                    m_pendingSearches = 0;
    QList<MangaResult>     m_allResults;
    QList<MangaResult>     m_displayedResults;  // deduped — matches table rows 1:1

    // Search controls
    QLineEdit*   m_queryEdit   = nullptr;
    QComboBox*   m_sourceCombo = nullptr;
    QPushButton* m_searchBtn   = nullptr;
    QPushButton* m_cancelBtn   = nullptr;
    QPushButton* m_refreshBtn  = nullptr;
    QPushButton* m_pauseBtn    = nullptr;   // A2: toggles pause/resume of the download engine
    QPushButton* m_moreBtn     = nullptr;   // A3: overflow menu — Cancel All, etc.
    QPushButton* m_viewToggleBtn = nullptr; // B3: flips between list (table) and grid view
    QComboBox*   m_sortCombo     = nullptr; // C2: client-side sort of search results

    // Status row
    QLabel* m_searchStatus   = nullptr;
    QLabel* m_downloadStatus = nullptr;

    // Main area
    QTabWidget*     m_tabWidget      = nullptr;
    QStackedWidget* m_resultsStack   = nullptr;   // B3: list/grid/empty/loading pages
    QTableWidget*   m_resultsTable   = nullptr;
    MangaResultsGrid* m_resultsGrid  = nullptr;   // B3
    QWidget*        m_emptyPage      = nullptr;   // B4/E3: empty-state container
    QLabel*         m_emptyLabel     = nullptr;   // B4: pre-search + zero-results state
    QPushButton*    m_emptyRetryBtn  = nullptr;   // E3: re-run last query
    QPushButton*    m_emptyClearBtn  = nullptr;   // E3: clear search + return to pre-search
    QWidget*        m_loadingPage    = nullptr;   // B5: indeterminate progress page
    QLabel*         m_loadingLabel   = nullptr;
    QTableWidget*   m_transfersTable = nullptr;
    QTimer*         m_transferTimer  = nullptr;

    // B4: state used by updateResultsView() to pick between data view and
    // empty-state page. m_preferredDataView tracks the user's toggle choice
    // so we restore it as soon as results arrive.
    QString m_lastQuery;
    int     m_preferredDataView = 1;   // 0 = list, 1 = grid

    // B1: poster cache
    QString       m_posterCacheDir;
    QSet<QString> m_coversInFlight;   // keys currently being downloaded
};
