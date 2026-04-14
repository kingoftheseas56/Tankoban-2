#include "HistoryDialog.h"
#include "core/torrent/TorrentClient.h"
#include "core/TorrentResult.h"  // for humanSize()

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QStyleFactory>

static const QString GLASS_BG = QStringLiteral("rgba(12, 12, 12, 0.95)");
static const QString BORDER   = QStringLiteral("rgba(255,255,255,0.08)");

HistoryDialog::HistoryDialog(TorrentClient* client, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Torrent History");
    setMinimumSize(700, 400);
    resize(900, 500);
    setStyleSheet(QStringLiteral(
        "HistoryDialog { background: %1; border: 1px solid %2; border-radius: 12px; }"
    ).arg(GLASS_BG, BORDER));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);

    m_table = new QTableWidget;
    m_table->setObjectName("HistoryTable");
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({"Name", "Category", "Size", "Completed"});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(true);
    m_table->setStyle(QStyleFactory::create("Fusion"));

    auto* hdr = m_table->horizontalHeader();
    hdr->setSectionResizeMode(0, QHeaderView::Stretch);
    hdr->resizeSection(1, 100);
    hdr->resizeSection(2, 100);
    hdr->resizeSection(3, 170);

    QPalette pal = m_table->palette();
    pal.setColor(QPalette::Base,            QColor(0x11, 0x11, 0x11));
    pal.setColor(QPalette::AlternateBase,   QColor(0x18, 0x18, 0x18));
    pal.setColor(QPalette::Text,            QColor(0xee, 0xee, 0xee));
    pal.setColor(QPalette::Highlight,       QColor(192, 200, 212, 36));
    pal.setColor(QPalette::HighlightedText, QColor(0xee, 0xee, 0xee));
    m_table->setPalette(pal);

    m_table->setStyleSheet(QStringLiteral(
        "#HistoryTable { border: none; outline: none; font-size: 12px; }"
        "#HistoryTable::item { padding: 0 8px; }"
        "#HistoryTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
        "#HistoryTable QHeaderView::section {"
        "  background: #1a1a1a; color: #888; border: none;"
        "  border-right: 1px solid #222; border-bottom: 1px solid #222;"
        "  padding: 4px 8px; font-size: 11px; }"
    ));
    root->addWidget(m_table, 1);

    // Populate
    QJsonArray history = client->listHistory();
    m_table->setRowCount(history.size());

    for (int i = 0; i < history.size(); ++i) {
        QJsonObject entry = history[i].toObject();

        auto* nameItem = new QTableWidgetItem(entry["name"].toString());
        m_table->setItem(i, 0, nameItem);

        auto* catItem = new QTableWidgetItem(entry["category"].toString());
        m_table->setItem(i, 1, catItem);

        qint64 size = entry["totalWanted"].toVariant().toLongLong();
        auto* sizeItem = new QTableWidgetItem(size > 0 ? humanSize(size) : "-");
        sizeItem->setData(Qt::UserRole, static_cast<qlonglong>(size));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(i, 2, sizeItem);

        qint64 completedMs = entry["completedAt"].toVariant().toLongLong();
        QString dateStr = completedMs > 0
            ? QDateTime::fromMSecsSinceEpoch(completedMs).toString("yyyy-MM-dd hh:mm")
            : "-";
        auto* dateItem = new QTableWidgetItem(dateStr);
        dateItem->setData(Qt::UserRole, static_cast<qlonglong>(completedMs));
        m_table->setItem(i, 3, dateItem);
    }

    // Close button
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto* closeBtn = new QPushButton("Close");
    closeBtn->setFixedHeight(28);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);
}
