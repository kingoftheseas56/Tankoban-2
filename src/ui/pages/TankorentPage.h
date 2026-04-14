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
#include <QStyledItemDelegate>
#include <QPainter>

#include "core/TorrentResult.h"
#include "core/torrent/TorrentClient.h"

class CoreBridge;

// ── Progress bar delegate — paints a bar instead of text ─────────────────────
class ProgressBarDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};
class TorrentIndexer;
class QNetworkAccessManager;
class QTimer;

class TankorentPage : public QWidget
{
    Q_OBJECT

public:
    explicit TankorentPage(CoreBridge* bridge, TorrentClient* client, QWidget* parent = nullptr);

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
    void onAddTorrentClicked(int row);
    void refreshTransfers();
    void showTransfersContextMenu(const QPoint& pos);

    // Quality tag + health helpers
    static QString qualityTagSuffix(const QString& title);
    static QString healthDot(int seeders);
    static QColor healthColor(int seeders);

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
    QComboBox*   m_sortCombo       = nullptr;
    QPushButton* m_searchBtn       = nullptr;
    QPushButton* m_cancelBtn       = nullptr;
    QPushButton* m_refreshBtn      = nullptr;
    QPushButton* m_moreBtn         = nullptr;

    // Status row
    QLabel* m_searchStatus   = nullptr;
    QLabel* m_downloadStatus = nullptr;
    QLabel* m_backendStatus  = nullptr;

    // Main area
    QTabWidget*   m_tabWidget      = nullptr;
    QTableWidget* m_resultsTable   = nullptr;
    QTableWidget* m_transfersTable = nullptr;

    // Transfers state
    QList<TorrentInfo> m_cachedActive;
    int m_sortCol   = -1;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;

    // Speed formatting helper
    static QString humanSpeed(int bytesPerSec);
};
