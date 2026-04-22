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

QString formatStatus(const TransferRecord& r)
{
    switch (r.state) {
    case TransferRecord::State::Queued:      return QStringLiteral("queued");
    case TransferRecord::State::Downloading: return QStringLiteral("downloading");
    case TransferRecord::State::Done:
        return QStringLiteral("✓ done — %1").arg(r.filePath);
    case TransferRecord::State::Failed:
        return QStringLiteral("✗ %1").arg(r.errorReason);
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
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({ "Title", "Progress", "Status" });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
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

        const QColor c = colorForState(r.state);
        if (c.isValid()) statusItem->setForeground(c);

        m_table->setItem(i, 0, titleItem);
        m_table->setItem(i, 1, progressItem);
        m_table->setItem(i, 2, statusItem);
    }
}
