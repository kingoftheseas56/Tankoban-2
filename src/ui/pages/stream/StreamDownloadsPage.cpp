#include "StreamDownloadsPage.h"

#include "core/torrent/TorrentClient.h"
#include "core/stream/StreamDownloadIndex.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

StreamDownloadsPage::StreamDownloadsPage(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("StreamDownloadsPage");
    buildUi();
}

void StreamDownloadsPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Topbar: back button + title
    auto* topbar = new QFrame(this);
    topbar->setObjectName("StreamDownloadsTopbar");
    topbar->setFixedHeight(48);
    auto* topbarLayout = new QHBoxLayout(topbar);
    topbarLayout->setContentsMargins(14, 6, 14, 6);
    topbarLayout->setSpacing(10);

    m_backBtn = new QPushButton(tr("< Back"), topbar);
    m_backBtn->setObjectName("StreamDownloadsBackBtn");
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setFixedHeight(28);
    connect(m_backBtn, &QPushButton::clicked, this, &StreamDownloadsPage::backRequested);

    m_titleLabel = new QLabel(tr("Downloads"), topbar);
    m_titleLabel->setObjectName("StreamDownloadsTitle");
    m_titleLabel->setStyleSheet(
        "QLabel#StreamDownloadsTitle { font-size: 16pt; font-weight: 600; color: #eeeeee; }");

    topbarLayout->addWidget(m_backBtn, 0);
    topbarLayout->addWidget(m_titleLabel, 0);
    topbarLayout->addStretch(1);

    root->addWidget(topbar, 0);

    // Scrollable body
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName("StreamDownloadsScroll");
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);

    m_scrollContent = new QWidget(m_scroll);
    m_scrollContent->setObjectName("StreamDownloadsScrollContent");
    m_contentLayout = new QVBoxLayout(m_scrollContent);
    m_contentLayout->setContentsMargins(20, 12, 20, 20);
    m_contentLayout->setSpacing(18);

    // Active section
    m_activeHeader = new QLabel(tr("ACTIVE"), m_scrollContent);
    m_activeHeader->setObjectName("StreamDownloadsSectionHeader");
    m_activeHeader->setStyleSheet(
        "QLabel#StreamDownloadsSectionHeader { font-size: 9pt; font-weight: 700;"
        " color: rgba(255,255,255,0.55); letter-spacing: 1.2px; }");

    m_activeBody = new QWidget(m_scrollContent);
    m_activeBody->setObjectName("StreamDownloadsActiveBody");
    m_activeBodyLayout = new QVBoxLayout(m_activeBody);
    m_activeBodyLayout->setContentsMargins(0, 0, 0, 0);
    m_activeBodyLayout->setSpacing(8);

    // History section
    m_historyHeader = new QLabel(tr("HISTORY"), m_scrollContent);
    m_historyHeader->setObjectName("StreamDownloadsSectionHeader");
    m_historyHeader->setStyleSheet(
        "QLabel#StreamDownloadsSectionHeader { font-size: 9pt; font-weight: 700;"
        " color: rgba(255,255,255,0.55); letter-spacing: 1.2px; }");

    m_historyBody = new QWidget(m_scrollContent);
    m_historyBody->setObjectName("StreamDownloadsHistoryBody");
    m_historyBodyLayout = new QVBoxLayout(m_historyBody);
    m_historyBodyLayout->setContentsMargins(0, 0, 0, 0);
    m_historyBodyLayout->setSpacing(8);

    // Empty state placeholder (shown when both sections have zero rows).
    m_emptyState = new QLabel(
        tr("No downloads yet.\n\nDispatch a season pack from any Theatre detail view."),
        m_scrollContent);
    m_emptyState->setObjectName("StreamDownloadsEmptyState");
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setStyleSheet(
        "QLabel#StreamDownloadsEmptyState { color: rgba(255,255,255,0.45);"
        " font-size: 11pt; padding: 60px 20px; }");

    m_contentLayout->addWidget(m_activeHeader, 0);
    m_contentLayout->addWidget(m_activeBody, 0);
    m_contentLayout->addWidget(m_historyHeader, 0);
    m_contentLayout->addWidget(m_historyBody, 0);
    m_contentLayout->addWidget(m_emptyState, 0);
    m_contentLayout->addStretch(1);

    m_scroll->setWidget(m_scrollContent);
    root->addWidget(m_scroll, 1);
}

void StreamDownloadsPage::setTorrentClient(TorrentClient* client)
{
    if (m_torrentClient == client)
        return;
    if (m_torrentClient) {
        disconnect(m_torrentClient, nullptr, this, nullptr);
    }
    m_torrentClient = client;
    if (m_torrentClient) {
        connect(m_torrentClient, &TorrentClient::streamBulkGroupsChanged,
                this, [this](const QString&) { refreshActive(); },
                Qt::QueuedConnection);
    }
    refreshActive();
}

void StreamDownloadsPage::setStreamDownloadIndex(StreamDownloadIndex* index)
{
    if (m_streamDownloadIndex == index)
        return;
    if (m_streamDownloadIndex) {
        disconnect(m_streamDownloadIndex, nullptr, this, nullptr);
    }
    m_streamDownloadIndex = index;
    if (m_streamDownloadIndex) {
        connect(m_streamDownloadIndex, &StreamDownloadIndex::entriesChanged,
                this, &StreamDownloadsPage::refreshHistory,
                Qt::QueuedConnection);
    }
    refreshHistory();
}

void StreamDownloadsPage::refreshActive()
{
    // Stub - Task 7 fills in row rendering.
    if (!m_activeBodyLayout)
        return;
    while (auto* item = m_activeBodyLayout->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
}

void StreamDownloadsPage::refreshHistory()
{
    // Stub - Task 8 fills in row rendering.
    if (!m_historyBodyLayout)
        return;
    while (auto* item = m_historyBodyLayout->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
}
