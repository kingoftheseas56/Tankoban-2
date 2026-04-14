#include "TorrentPickerDialog.h"

#include "core/stream/TorrentioClient.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QVBoxLayout>

// ── Human-readable size ──────────────────────────────────────────────────────

static QString humanSize(qint64 bytes)
{
    if (bytes <= 0) return "-";
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    int i = 0;
    double val = bytes;
    while (val >= 1024.0 && i < 4) { val /= 1024.0; ++i; }
    return QString::number(val, 'f', i > 0 ? 1 : 0) + " " + units[i];
}

// ═══════════════════════════════════════════════════════════════════════════

TorrentPickerDialog::TorrentPickerDialog(const QList<TorrentioStream>& streams,
                                         QWidget* parent)
    : QDialog(parent)
    , m_streams(streams)
{
    setWindowTitle("Select Source");
    setMinimumSize(900, 500);
    resize(900, 500);
    buildUI(streams);
}

TorrentioStream TorrentPickerDialog::selectedStream() const
{
    if (m_selectedRow >= 0 && m_selectedRow < m_streams.size())
        return m_streams[m_selectedRow];
    return {};
}

// ─── UI ──────────────────────────────────────────────────────────────────────

void TorrentPickerDialog::buildUI(const QList<TorrentioStream>& streams)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    // Info label
    m_infoLabel = new QLabel(QString::number(streams.size()) + " sources available", this);
    m_infoLabel->setStyleSheet("color: rgba(255,255,255,0.5); font-size: 12px;");
    root->addWidget(m_infoLabel);

    // Table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({"Title", "Quality", "Size", "Seeders", "Source"});

    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(true);
    m_table->verticalHeader()->hide();

    m_table->setStyleSheet(
        "QTableWidget { background: #1a1a1a; border: 1px solid rgba(255,255,255,0.1);"
        "  color: #ccc; alternate-background-color: rgba(255,255,255,0.03); }"
        "QTableWidget::item { padding: 4px; }"
        "QTableWidget::item:selected { background: rgba(255,255,255,0.1); }"
        "QHeaderView::section { background: rgba(255,255,255,0.06); color: rgba(255,255,255,0.5);"
        "  border: none; font-size: 11px; padding: 4px; }");

    // Populate rows
    m_table->setSortingEnabled(false);
    for (int i = 0; i < streams.size(); ++i) {
        const auto& s = streams[i];
        int row = m_table->rowCount();
        m_table->insertRow(row);

        // Col 0: Title (trackerSource = release name)
        auto* titleItem = new QTableWidgetItem(s.trackerSource);
        titleItem->setData(Qt::UserRole, i); // original index
        m_table->setItem(row, 0, titleItem);

        // Col 1: Quality
        auto* qualItem = new QTableWidgetItem(s.quality);
        m_table->setItem(row, 1, qualItem);

        // Col 2: Size (sortable by bytes)
        auto* sizeItem = new QTableWidgetItem(humanSize(s.sizeBytes));
        sizeItem->setData(Qt::UserRole, s.sizeBytes);
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, 2, sizeItem);

        // Col 3: Seeders (integer sort)
        auto* seedItem = new QTableWidgetItem();
        seedItem->setData(Qt::DisplayRole, s.seeders);
        seedItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, 3, seedItem);

        // Col 4: Source/tracker
        auto* srcItem = new QTableWidgetItem(s.tracker);
        m_table->setItem(row, 4, srcItem);
    }
    m_table->setSortingEnabled(true);

    // Default sort: seeders descending
    m_table->sortByColumn(3, Qt::DescendingOrder);

    connect(m_table, &QTableWidget::cellDoubleClicked,
            this, &TorrentPickerDialog::onRowDoubleClicked);

    root->addWidget(m_table, 1);

    // Buttons
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_selectBtn = new QPushButton("Select", this);
    m_selectBtn->setFixedHeight(30);
    m_selectBtn->setCursor(Qt::PointingHandCursor);
    m_selectBtn->setStyleSheet(
        "QPushButton { background: rgba(255,255,255,0.1); border: 1px solid rgba(255,255,255,0.15);"
        "  border-radius: 6px; color: #e0e0e0; font-size: 12px; padding: 0 20px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.15); }");
    connect(m_selectBtn, &QPushButton::clicked, this, &TorrentPickerDialog::onSelectClicked);
    btnLayout->addWidget(m_selectBtn);

    m_cancelBtn = new QPushButton("Cancel", this);
    m_cancelBtn->setFixedHeight(30);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setStyleSheet(
        "QPushButton { background: transparent; border: 1px solid rgba(255,255,255,0.1);"
        "  border-radius: 6px; color: rgba(255,255,255,0.5); font-size: 12px; padding: 0 20px; }"
        "QPushButton:hover { color: rgba(255,255,255,0.7); }");
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(m_cancelBtn);

    root->addLayout(btnLayout);

    // Dialog dark styling
    setStyleSheet(
        "QDialog { background: #141414; }");
}

// ─── Selection ───────────────────────────────────────────────────────────────

void TorrentPickerDialog::onRowDoubleClicked(int row, int /*col*/)
{
    auto* item = m_table->item(row, 0);
    if (!item) return;
    m_selectedRow = item->data(Qt::UserRole).toInt();
    accept();
}

void TorrentPickerDialog::onSelectClicked()
{
    auto selected = m_table->selectedItems();
    if (selected.isEmpty()) return;

    int row = selected.first()->row();
    auto* item = m_table->item(row, 0);
    if (!item) return;
    m_selectedRow = item->data(Qt::UserRole).toInt();
    accept();
}
