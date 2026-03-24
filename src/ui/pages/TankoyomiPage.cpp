#include "TankoyomiPage.h"
#include "core/CoreBridge.h"

#include <QHBoxLayout>
#include <QHeaderView>

// ── Constructor ─────────────────────────────────────────────────────────────
TankoyomiPage::TankoyomiPage(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent), m_bridge(bridge)
{
    buildUI();
}

// ── UI ──────────────────────────────────────────────────────────────────────
void TankoyomiPage::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 0);
    root->setSpacing(6);

    buildSearchControls(root);
    buildStatusRow(root);
    buildMainTabs(root);
}

// ── Search controls bar ─────────────────────────────────────────────────────
void TankoyomiPage::buildSearchControls(QVBoxLayout* parent)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    // Query input
    m_queryEdit = new QLineEdit;
    m_queryEdit->setPlaceholderText("Search manga & comics...");
    m_queryEdit->setFixedHeight(30);
    connect(m_queryEdit, &QLineEdit::returnPressed, this, [this]() {
        // TODO: _start_search()
    });
    row->addWidget(m_queryEdit, 3);

    // Source filter combo
    m_sourceCombo = new QComboBox;
    m_sourceCombo->setFixedHeight(30);
    m_sourceCombo->setMinimumWidth(160);
    m_sourceCombo->addItem("All Sources", "all");
    row->addWidget(m_sourceCombo, 1);

    // Search button
    m_searchBtn = new QPushButton("Search");
    m_searchBtn->setFixedHeight(30);
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_searchBtn, &QPushButton::clicked, this, [this]() {
        // TODO: _start_search()
    });
    row->addWidget(m_searchBtn);

    // Cancel button (hidden initially)
    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setFixedHeight(30);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setVisible(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        // TODO: _cancel_search()
    });
    row->addWidget(m_cancelBtn);

    // Refresh button
    m_refreshBtn = new QPushButton("Refresh");
    m_refreshBtn->setFixedHeight(30);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, [this]() {
        // TODO: _refresh_transfers()
    });
    row->addWidget(m_refreshBtn);

    parent->addLayout(row);
}

// ── Status row ──────────────────────────────────────────────────────────────
void TankoyomiPage::buildStatusRow(QVBoxLayout* parent)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    m_searchStatus = new QLabel("Ready");
    m_searchStatus->setStyleSheet("color: #a1a1aa; font-size: 11px;");
    row->addWidget(m_searchStatus, 2);

    m_downloadStatus = new QLabel("Active: 0 | History: 0");
    m_downloadStatus->setStyleSheet("color: #a1a1aa; font-size: 11px;");
    row->addWidget(m_downloadStatus, 1);

    parent->addLayout(row);
}

// ── Main tabs ───────────────────────────────────────────────────────────────
void TankoyomiPage::buildMainTabs(QVBoxLayout* parent)
{
    m_tabWidget = new QTabWidget;

    m_resultsTable = createResultsTable();
    m_tabWidget->addTab(m_resultsTable, "Search Results");

    m_transfersTable = createTransfersTable();
    m_tabWidget->addTab(m_transfersTable, "Transfers");

    parent->addWidget(m_tabWidget, 1);
}

// ── Results table ───────────────────────────────────────────────────────────
QTableWidget* TankoyomiPage::createResultsTable()
{
    auto *table = new QTableWidget(0, 5);
    table->setMinimumHeight(280);

    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->setContextMenuPolicy(Qt::CustomContextMenu);

    QStringList headers = { "Title", "Author", "Source", "Status", "Type" };
    table->setHorizontalHeaderLabels(headers);

    auto *hdr = table->horizontalHeader();
    hdr->setMinimumSectionSize(80);
    hdr->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 5; ++i)
        hdr->setSectionResizeMode(i, QHeaderView::Interactive);

    hdr->resizeSection(1, 160);
    hdr->resizeSection(2, 140);
    hdr->resizeSection(3, 120);
    hdr->resizeSection(4, 100);

    return table;
}

// ── Transfers table ─────────────────────────────────────────────────────────
QTableWidget* TankoyomiPage::createTransfersTable()
{
    auto *table = new QTableWidget(0, 5);
    table->setMinimumHeight(220);

    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->setContextMenuPolicy(Qt::CustomContextMenu);

    QStringList headers = { "Series", "Progress", "Status", "Speed", "Chapters" };
    table->setHorizontalHeaderLabels(headers);

    auto *hdr = table->horizontalHeader();
    hdr->setMinimumSectionSize(80);
    hdr->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 5; ++i)
        hdr->setSectionResizeMode(i, QHeaderView::Interactive);

    hdr->resizeSection(1, 100);
    hdr->resizeSection(2, 120);
    hdr->resizeSection(3, 100);
    hdr->resizeSection(4, 120);

    return table;
}
