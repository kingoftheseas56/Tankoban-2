#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include "core/manga/MangaResult.h"

class CoreBridge;
class MangaScraper;
class MangaDownloader;
class QNetworkAccessManager;
class QTimer;

class TankoyomiPage : public QWidget
{
    Q_OBJECT

public:
    explicit TankoyomiPage(CoreBridge* bridge, QWidget* parent = nullptr);

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

    // Status row
    QLabel* m_searchStatus   = nullptr;
    QLabel* m_downloadStatus = nullptr;

    // Main area
    QTabWidget*   m_tabWidget      = nullptr;
    QTableWidget* m_resultsTable   = nullptr;
    QTableWidget* m_transfersTable = nullptr;
    QTimer*       m_transferTimer  = nullptr;
};
