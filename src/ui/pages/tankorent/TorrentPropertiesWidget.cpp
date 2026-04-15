#include "TorrentPropertiesWidget.h"

#include "core/torrent/TorrentClient.h"
#include "TorrentGeneralTab.h"
#include "TorrentTrackersTab.h"
#include "TorrentPeersTab.h"
#include "TorrentFilesTab.h"

#include <QVBoxLayout>
#include <QTabWidget>
#include <QLabel>
#include <QTimer>

namespace {

// Tiny helper that builds a centered-label placeholder widget for each
// batch's future tab. 6.2-6.5 replace these with real concrete tab classes.
QWidget* buildPlaceholderTab(const QString& label)
{
    auto* w = new QWidget;
    auto* root = new QVBoxLayout(w);
    root->setAlignment(Qt::AlignCenter);
    auto* msg = new QLabel(label);
    msg->setStyleSheet(QStringLiteral("color: #888; font-size: 12px;"));
    msg->setAlignment(Qt::AlignCenter);
    root->addWidget(msg);
    return w;
}

} // namespace

TorrentPropertiesWidget::TorrentPropertiesWidget(TorrentClient* client, QWidget* parent)
    : QDialog(parent), m_client(client)
{
    setWindowTitle("Torrent Properties");
    setMinimumSize(800, 500);
    resize(900, 560);
    setStyleSheet(QStringLiteral(
        "TorrentPropertiesWidget { background: rgba(12,12,12,0.98); border: 1px solid rgba(255,255,255,0.08); border-radius: 12px; }"
    ));

    buildUI();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(1000);
    connect(m_refreshTimer, &QTimer::timeout, this, &TorrentPropertiesWidget::refresh);
}

TorrentPropertiesWidget::~TorrentPropertiesWidget() = default;

void TorrentPropertiesWidget::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(8);

    m_tabs = new QTabWidget;
    m_tabs->setObjectName("TorrentPropertiesTabs");
    m_tabs->setStyleSheet(QStringLiteral(
        "#TorrentPropertiesTabs::pane { background: rgba(0,0,0,0.3); border: 1px solid rgba(255,255,255,0.06); border-radius: 6px; }"
        "#TorrentPropertiesTabs QTabBar::tab { background: #1a1a1a; color: #ccc; padding: 6px 14px; border: none; border-right: 1px solid #222; }"
        "#TorrentPropertiesTabs QTabBar::tab:selected { background: rgba(255,255,255,0.08); color: #eee; }"
        "#TorrentPropertiesTabs QTabBar::tab:hover { background: rgba(255,255,255,0.05); }"
    ));

    m_generalTab  = new TorrentGeneralTab(m_client);
    m_trackersTab = new TorrentTrackersTab(m_client);
    m_peersTab    = new TorrentPeersTab(m_client);
    m_filesTab    = new TorrentFilesTab(m_client);

    m_tabs->addTab(m_generalTab,  "General");
    m_tabs->addTab(m_trackersTab, "Trackers");
    m_tabs->addTab(m_peersTab,    "Peers");
    m_tabs->addTab(m_filesTab,    "Files");

    root->addWidget(m_tabs, 1);
}

void TorrentPropertiesWidget::showTorrent(const QString& infoHash, int tabIndex)
{
    m_infoHash = infoHash;

    QString title = QStringLiteral("Torrent Properties — %1").arg(infoHash.left(8));
    if (m_client) {
        for (const auto& t : m_client->listActive()) {
            if (t.infoHash == infoHash) {
                if (!t.name.isEmpty())
                    title = QStringLiteral("Torrent Properties — %1").arg(t.name);
                break;
            }
        }
    }
    setWindowTitle(title);

    if (m_generalTab)  m_generalTab->setInfoHash(infoHash);
    if (m_trackersTab) m_trackersTab->setInfoHash(infoHash);
    if (m_peersTab)    m_peersTab->setInfoHash(infoHash);
    if (m_filesTab)    m_filesTab->setInfoHash(infoHash);

    if (tabIndex >= 0 && tabIndex < m_tabs->count())
        m_tabs->setCurrentIndex(tabIndex);

    m_refreshTimer->start();
}

void TorrentPropertiesWidget::refresh()
{
    if (m_generalTab)  m_generalTab->refresh();
    if (m_trackersTab) m_trackersTab->refresh();
    if (m_peersTab)    m_peersTab->refresh();
    if (m_filesTab)    m_filesTab->refresh();
}
