#include "TorrentPeersTab.h"

#include "core/torrent/TorrentClient.h"
#include "core/torrent/TorrentEngine.h"
#include "core/TorrentResult.h"  // humanSize()

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QMenu>
#include <QInputDialog>
#include <QGuiApplication>
#include <QClipboard>
#include <QMessageBox>

namespace {
const char* kTableStyle =
    "#PeersTable { background: rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.06); "
    "border-radius: 6px; color: #eee; font-size: 12px; }"
    "#PeersTable::item { padding: 4px 8px; }"
    "#PeersTable QHeaderView::section { background: #1a1a1a; color: #888; border: none; "
    "border-right: 1px solid #222; border-bottom: 1px solid #222; padding: 4px 8px; font-size: 11px; }";

QString formatSpeed(qint64 bytesPerSec)
{
    if (bytesPerSec <= 0) return QStringLiteral("—");
    return humanSize(bytesPerSec) + QStringLiteral("/s");
}
} // namespace

TorrentPeersTab::TorrentPeersTab(TorrentClient* client, QWidget* parent)
    : QWidget(parent), m_client(client)
{
    buildUI();
}

void TorrentPeersTab::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(8);

    auto* btnRow = new QHBoxLayout;
    auto* addBtn = new QPushButton("Add Peer...");
    addBtn->setCursor(Qt::PointingHandCursor);
    addBtn->setFixedHeight(26);
    connect(addBtn, &QPushButton::clicked, this, &TorrentPeersTab::onAddPeer);
    btnRow->addWidget(addBtn);
    btnRow->addStretch();
    root->addLayout(btnRow);

    m_table = new QTableWidget(0, 11);
    m_table->setObjectName("PeersTable");
    m_table->setHorizontalHeaderLabels(
        { "Country", "IP:Port", "Client", "Flags", "Connection",
          "Progress", "Down", "Up", "Downloaded", "Uploaded", "Relevance" });
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
    hdr->resizeSection(0, 60);
    hdr->resizeSection(1, 140);
    hdr->setSectionResizeMode(2, QHeaderView::Stretch);
    hdr->resizeSection(3, 80);
    hdr->resizeSection(4, 80);
    hdr->resizeSection(5, 70);
    hdr->resizeSection(6, 80);
    hdr->resizeSection(7, 80);
    hdr->resizeSection(8, 90);
    hdr->resizeSection(9, 90);
    hdr->resizeSection(10, 70);
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &TorrentPeersTab::onContextMenu);
    root->addWidget(m_table, 1);
}

void TorrentPeersTab::setInfoHash(const QString& infoHash)
{
    m_infoHash = infoHash;
    refresh();
}

void TorrentPeersTab::refresh()
{
    if (m_infoHash.isEmpty() || !m_client) return;
    const QList<PeerInfo> peers = m_client->engine()->peersFor(m_infoHash);
    m_table->setRowCount(peers.size());
    for (int i = 0; i < peers.size(); ++i) {
        const auto& p = peers[i];
        m_table->setItem(i, 0, new QTableWidgetItem(p.country));
        m_table->setItem(i, 1, new QTableWidgetItem(
            QStringLiteral("%1:%2").arg(p.address).arg(p.port)));
        auto* client = new QTableWidgetItem(p.client);
        client->setToolTip(p.client);
        m_table->setItem(i, 2, client);
        m_table->setItem(i, 3, new QTableWidgetItem(p.flags));
        m_table->setItem(i, 4, new QTableWidgetItem(p.connection));
        m_table->setItem(i, 5, new QTableWidgetItem(
            QString::number(p.progress * 100.0f, 'f', 1) + "%"));
        m_table->setItem(i, 6, new QTableWidgetItem(formatSpeed(p.downSpeed)));
        m_table->setItem(i, 7, new QTableWidgetItem(formatSpeed(p.upSpeed)));
        m_table->setItem(i, 8, new QTableWidgetItem(humanSize(p.downloaded)));
        m_table->setItem(i, 9, new QTableWidgetItem(humanSize(p.uploaded)));
        m_table->setItem(i, 10, new QTableWidgetItem(
            QString::number(p.relevance * 100.0f, 'f', 0) + "%"));
    }
}

void TorrentPeersTab::onAddPeer()
{
    if (m_infoHash.isEmpty() || !m_client) return;
    bool ok = false;
    const QString ipPort = QInputDialog::getText(this, "Add Peer",
        "IP:Port (e.g. 192.168.1.100:6881):", QLineEdit::Normal, QString(), &ok);
    if (!ok || ipPort.trimmed().isEmpty()) return;
    m_client->engine()->addPeer(m_infoHash, ipPort.trimmed());
}

void TorrentPeersTab::onContextMenu(const QPoint& pos)
{
    const int row = m_table->rowAt(pos.y());
    if (row < 0) return;
    auto* ipItem = m_table->item(row, 1);
    if (!ipItem) return;
    const QString ipPort = ipItem->text();
    const int sep = ipPort.lastIndexOf(':');
    const QString ip = sep > 0 ? ipPort.left(sep) : ipPort;

    QMenu menu(this);
    menu.addAction("Copy IP:Port", this, [ipPort]() {
        QGuiApplication::clipboard()->setText(ipPort);
    });
    menu.addSeparator();
    menu.addAction("Ban Peer Permanently", this, [this, ip]() {
        if (QMessageBox::question(this, "Ban Peer",
            QStringLiteral("Permanently ban %1? This persists across app restart.").arg(ip))
            != QMessageBox::Yes) return;
        m_client->engine()->banPeer(ip);
    });

    menu.exec(m_table->viewport()->mapToGlobal(pos));
}
