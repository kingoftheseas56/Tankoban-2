#include "TorrentTrackersTab.h"

#include "core/torrent/TorrentClient.h"
#include "core/torrent/TorrentEngine.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QMenu>
#include <QInputDialog>
#include <QGuiApplication>
#include <QClipboard>
#include <QDateTime>

namespace {
const char* kTableStyle =
    "#TrackersTable { background: rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.06); "
    "border-radius: 6px; color: #eee; font-size: 12px; }"
    "#TrackersTable::item { padding: 4px 8px; }"
    "#TrackersTable QHeaderView::section { background: #1a1a1a; color: #888; border: none; "
    "border-right: 1px solid #222; border-bottom: 1px solid #222; padding: 4px 8px; font-size: 11px; }";

QString formatSecsFromNow(const QDateTime& dt)
{
    if (!dt.isValid()) return QStringLiteral("—");
    const qint64 s = QDateTime::currentDateTime().secsTo(dt);
    if (s < 0) return QStringLiteral("now");
    if (s < 60) return QStringLiteral("%1s").arg(s);
    if (s < 3600) return QStringLiteral("%1m %2s").arg(s / 60).arg(s % 60);
    return QStringLiteral("%1h %2m").arg(s / 3600).arg((s % 3600) / 60);
}
} // namespace

TorrentTrackersTab::TorrentTrackersTab(TorrentClient* client, QWidget* parent)
    : QWidget(parent), m_client(client)
{
    buildUI();
}

void TorrentTrackersTab::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(8);

    auto* btnRow = new QHBoxLayout;
    auto* addBtn = new QPushButton("Add Tracker...");
    addBtn->setCursor(Qt::PointingHandCursor);
    addBtn->setFixedHeight(26);
    connect(addBtn, &QPushButton::clicked, this, &TorrentTrackersTab::onAddTracker);
    btnRow->addWidget(addBtn);
    btnRow->addStretch();
    root->addLayout(btnRow);

    m_table = new QTableWidget(0, 10);
    m_table->setObjectName("TrackersTable");
    m_table->setHorizontalHeaderLabels(
        { "URL", "Tier", "Status", "Next Announce", "Min Announce",
          "Peers", "Seeds", "Leeches", "Downloaded", "Message" });
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(26);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setStyleSheet(kTableStyle);
    auto* hdr = m_table->horizontalHeader();
    hdr->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 10; ++i) hdr->setSectionResizeMode(i, QHeaderView::Interactive);
    hdr->resizeSection(1, 50);
    hdr->resizeSection(2, 110);
    hdr->resizeSection(3, 110);
    hdr->resizeSection(4, 110);
    hdr->resizeSection(5, 60);
    hdr->resizeSection(6, 60);
    hdr->resizeSection(7, 60);
    hdr->resizeSection(8, 80);
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &TorrentTrackersTab::onContextMenu);
    root->addWidget(m_table, 1);
}

void TorrentTrackersTab::setInfoHash(const QString& infoHash)
{
    m_infoHash = infoHash;
    refresh();
}

void TorrentTrackersTab::refresh()
{
    if (m_infoHash.isEmpty() || !m_client) return;
    const QList<TrackerInfo> trackers = m_client->engine()->trackersFor(m_infoHash);
    m_table->setRowCount(trackers.size());
    for (int i = 0; i < trackers.size(); ++i) {
        const auto& t = trackers[i];
        m_table->setItem(i, 0, new QTableWidgetItem(t.url));
        m_table->setItem(i, 1, new QTableWidgetItem(QString::number(t.tier + 1))); // 1-indexed UI
        m_table->setItem(i, 2, new QTableWidgetItem(t.status));
        m_table->setItem(i, 3, new QTableWidgetItem(formatSecsFromNow(t.nextAnnounce)));
        m_table->setItem(i, 4, new QTableWidgetItem(formatSecsFromNow(t.minAnnounce)));
        m_table->setItem(i, 5, new QTableWidgetItem(QString::number(t.peers)));
        m_table->setItem(i, 6, new QTableWidgetItem(QString::number(t.seeds)));
        m_table->setItem(i, 7, new QTableWidgetItem(QString::number(t.leeches)));
        m_table->setItem(i, 8, new QTableWidgetItem(QString::number(t.downloaded)));
        auto* msg = new QTableWidgetItem(t.message);
        msg->setToolTip(t.message);
        m_table->setItem(i, 9, msg);
    }
}

void TorrentTrackersTab::onAddTracker()
{
    if (m_infoHash.isEmpty() || !m_client) return;
    bool ok = false;
    const QString url = QInputDialog::getText(this, "Add Tracker",
        "Tracker URL:", QLineEdit::Normal, QString(), &ok);
    if (!ok || url.trimmed().isEmpty()) return;
    bool tierOk = false;
    const int tier = QInputDialog::getInt(this, "Add Tracker",
        "Tier (1 = highest priority):", 1, 1, 10, 1, &tierOk);
    if (!tierOk) return;
    m_client->engine()->addTracker(m_infoHash, url.trimmed(), tier - 1); // UI 1-indexed → internal 0-indexed
    refresh();
}

void TorrentTrackersTab::onContextMenu(const QPoint& pos)
{
    const int row = m_table->rowAt(pos.y());
    if (row < 0) return;
    auto* item = m_table->item(row, 0);
    if (!item) return;
    const QString url = item->text();

    QMenu menu(this);
    menu.addAction("Force Reannounce", this, [this]() {
        if (m_client) m_client->forceReannounce(m_infoHash);
    });
    menu.addAction("Copy URL", this, [url]() {
        QGuiApplication::clipboard()->setText(url);
    });
    menu.addAction("Edit URL...", this, [this, url]() {
        bool ok = false;
        const QString newUrl = QInputDialog::getText(this, "Edit Tracker URL",
            "New URL:", QLineEdit::Normal, url, &ok);
        if (!ok || newUrl.trimmed().isEmpty() || newUrl == url) return;
        m_client->engine()->editTrackerUrl(m_infoHash, url, newUrl.trimmed());
        refresh();
    });
    menu.addSeparator();
    menu.addAction("Remove", this, [this, url]() {
        m_client->engine()->removeTracker(m_infoHash, url);
        refresh();
    });

    menu.exec(m_table->viewport()->mapToGlobal(pos));
}
