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

class CoreBridge;
class TorrentIndexer;
class QNetworkAccessManager;

class TankorentPage : public QWidget
{
    Q_OBJECT

public:
    explicit TankorentPage(CoreBridge* bridge, QWidget* parent = nullptr);

private:
    void buildUI();
    void buildSearchControls(QVBoxLayout* parent);
    void buildStatusRow(QVBoxLayout* parent);
    void buildMainTabs(QVBoxLayout* parent);
    QTableWidget* createResultsTable();
    QTableWidget* createTransfersTable();

    void startSearch();
    void cancelSearch();
    void onSearchFinished(const QList<TorrentResult>& results);
    void onSearchError(const QString& error);
    void renderResults();
    void populateSourceCombo();
    void reloadCategoryOptions();
    void showResultsContextMenu(const QPoint& pos);

    // Quality tag + health helpers
    static QString qualityTagSuffix(const QString& title);
    static QString healthDot(int seeders);
    static QColor healthColor(int seeders);

    CoreBridge* m_bridge;
    QNetworkAccessManager* m_nam = nullptr;

    // Active indexers during a search
    QList<TorrentIndexer*> m_activeIndexers;
    int m_pendingSearches = 0;
    QList<TorrentResult> m_allResults;

    // Search controls
    QLineEdit*   m_queryEdit       = nullptr;
    QComboBox*   m_searchTypeCombo = nullptr;
    QComboBox*   m_sourceCombo     = nullptr;
    QComboBox*   m_categoryCombo   = nullptr;
    QComboBox*   m_sortCombo       = nullptr;
    QPushButton* m_searchBtn       = nullptr;
    QPushButton* m_cancelBtn       = nullptr;
    QPushButton* m_refreshBtn      = nullptr;

    // Status row
    QLabel* m_searchStatus   = nullptr;
    QLabel* m_downloadStatus = nullptr;
    QLabel* m_backendStatus  = nullptr;

    // Main area
    QTabWidget*   m_tabWidget      = nullptr;
    QTableWidget* m_resultsTable   = nullptr;
    QTableWidget* m_transfersTable = nullptr;
};
