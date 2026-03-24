#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

class CoreBridge;

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

    CoreBridge* m_bridge;

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
