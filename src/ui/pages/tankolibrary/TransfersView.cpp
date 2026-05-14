#include "TransfersView.h"

#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QtGlobal>

namespace {

QString formatBytes(qint64 b)
{
    if (b <= 0) return QString();
    if (b < 1024) return QStringLiteral("%1 B").arg(b);
    if (b < 1024LL * 1024)
        return QStringLiteral("%1 kB").arg(QString::number(double(b) / 1024.0, 'f', 1));
    if (b < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(QString::number(double(b) / (1024.0 * 1024.0), 'f', 1));
    return QStringLiteral("%1 GB").arg(QString::number(double(b) / (1024.0 * 1024.0 * 1024.0), 'f', 2));
}

QString formatProgress(const TransferRecord& r)
{
    switch (r.state) {
    case TransferRecord::State::Queued:      return QStringLiteral("Queued");
    case TransferRecord::State::Downloading: {
        if (r.bytesTotal > 0) {
            const int pct = int((double(r.bytesReceived) / double(r.bytesTotal)) * 100.0);
            return QStringLiteral("%1% (%2 / %3)")
                .arg(pct).arg(formatBytes(r.bytesReceived), formatBytes(r.bytesTotal));
        }
        return formatBytes(r.bytesReceived);
    }
    case TransferRecord::State::Done:   return QStringLiteral("100%");
    case TransferRecord::State::Failed: return QStringLiteral("—");
    }
    return QString();
}

// CR.8 — Title Case status text, stripped of emoji prefixes.
// Enum vocabulary: Queued / Downloading / Done / Failed.
// "Done" surface label is "Complete" to match cross-page convention (Tankoyomi parity).
QString formatStatus(const TransferRecord& r)
{
    switch (r.state) {
    case TransferRecord::State::Queued:      return QStringLiteral("Queued");
    case TransferRecord::State::Downloading: return QStringLiteral("Downloading");
    case TransferRecord::State::Done:        return QStringLiteral("Complete");
    case TransferRecord::State::Failed:
        return r.errorReason.isEmpty()
            ? QStringLiteral("Failed")
            : QStringLiteral("Failed — %1").arg(r.errorReason);
    }
    return QString();
}

QColor colorForState(TransferRecord::State state)
{
    switch (state) {
    case TransferRecord::State::Done:        return QColor(QStringLiteral("#7ec17e"));
    case TransferRecord::State::Failed:      return QColor(QStringLiteral("#c07"));
    case TransferRecord::State::Queued:      return QColor(QStringLiteral("#bbb"));
    case TransferRecord::State::Downloading: return QColor(QStringLiteral("#c7a76b"));
    }
    return QColor();
}

} // namespace

TransfersView::TransfersView(QWidget* parent)
    : QWidget(parent)
{
    buildUI();
}

void TransfersView::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("transfersTable"));
    m_table->setColumnCount(3);

    const QStringList headers = { "Title", "Progress", "Status" };
    m_table->setHorizontalHeaderLabels(headers);

    // CR.9 — header alignment: default Left+VCenter; Progress column centered.
    QHeaderView* hdr = m_table->horizontalHeader();
    hdr->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // Column 1 (Progress) is numeric/percentage — center it.
    {
        auto* hi = m_table->horizontalHeaderItem(1);
        if (!hi) {
            hi = new QTableWidgetItem(headers[1]);
            m_table->setHorizontalHeaderItem(1, hi);
        }
        hi->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
    }

    hdr->setSectionResizeMode(0, QHeaderView::Stretch);
    hdr->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(2, QHeaderView::Stretch);

    m_table->verticalHeader()->setVisible(false);
    // CR.1 — density lift: 32 px row height.
    m_table->verticalHeader()->setDefaultSectionSize(32);

    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);

    // CR.5 (hover), CR.7 (header weight + padding), density (cell font 13).
    m_table->setStyleSheet(QStringLiteral(
        "QTableWidget#transfersTable {"
        "  font-size: 13px;"
        "}"
        "QTableWidget#transfersTable::item {"
        "  padding: 0 8px;"
        "}"
        "QTableWidget#transfersTable::item:hover {"
        "  background: rgba(255,255,255,0.04);"
        "}"
        "QHeaderView::section {"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "  padding: 6px 8px;"
        "  border: none;"
        "  border-bottom: 1px solid rgba(255,255,255,0.08);"
        "}"
    ));

    root->addWidget(m_table);
}

void TransfersView::setRecords(const QList<TransferRecord>& records)
{
    if (!m_table) return;
    if (records.isEmpty()) {
        m_table->setRowCount(0);
        return;
    }
    m_table->setRowCount(records.size());
    for (int i = 0; i < records.size(); ++i) {
        const TransferRecord& r = records[i];
        auto* titleItem    = new QTableWidgetItem(r.title.isEmpty() ? r.md5 : r.title);
        auto* progressItem = new QTableWidgetItem(formatProgress(r));
        auto* statusItem   = new QTableWidgetItem(formatStatus(r));

        // CR.9 — cell alignment matches header: Title+Status Left, Progress Center.
        titleItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        progressItem->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
        statusItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        const QColor c = colorForState(r.state);
        if (c.isValid()) statusItem->setForeground(c);

        m_table->setItem(i, 0, titleItem);
        m_table->setItem(i, 1, progressItem);
        m_table->setItem(i, 2, statusItem);
    }
}
