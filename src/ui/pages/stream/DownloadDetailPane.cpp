// DOWNLOADS_OVERHAUL_V2 Task 5 (2026-06-11) — right pane of the Downloads
// command center. Hosts a header (title + progress + stats), a section-aware
// button row, and the three reused Tankorent property tabs (Files / Peers /
// Trackers) pointed at the row's carrying torrent.
//
// Stats accessor: TorrentClient::listActive() scanned for the matching infoHash
// to obtain dlSpeed / ulSpeed / peers / seeds / totalDone / totalWanted.
// This matches how TorrentGeneralTab sources size (engine()->torrentDetails),
// but speed+peers are only in TorrentInfo (listActive), not TorrentDetails —
// so we use listActive() and fall back gracefully when the row has no live
// torrent (Completed/history rows with empty infoHash).

#include "DownloadDetailPane.h"

#include "core/torrent/TorrentClient.h"
#include "core/TorrentResult.h"   // humanSize()
#include "ui/pages/tankorent/TorrentFilesTab.h"
#include "ui/pages/tankorent/TorrentPeersTab.h"
#include "ui/pages/tankorent/TorrentTrackersTab.h"

#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

// ─────────────────────────────────────────────────────────────────────────────
// Local helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// Reuse the same button stylesheet pattern as StreamDownloadsPage's topbar /
// strip buttons: no-background, white text, rounded, pointer cursor.
static const char* kBtnStyle =
    "QPushButton {"
    "  background: rgba(255,255,255,0.10);"
    "  color: #dddddd;"
    "  border: 1px solid rgba(255,255,255,0.18);"
    "  border-radius: 4px;"
    "  padding: 4px 12px;"
    "  font-size: 12px;"
    "}"
    "QPushButton:hover {"
    "  background: rgba(255,255,255,0.16);"
    "  color: #eeeeee;"
    "}"
    "QPushButton:pressed {"
    "  background: rgba(255,255,255,0.08);"
    "}";

// Format bytes/s into a human-friendly string: "1.2 MB/s", "348 KB/s", etc.
QString formatSpeed(int bps)
{
    if (bps <= 0) return QStringLiteral("0 B/s");
    if (bps < 1024)
        return QStringLiteral("%1 B/s").arg(bps);
    if (bps < 1024 * 1024)
        return QStringLiteral("%1 KB/s").arg(bps / 1024);
    return QStringLiteral("%1 MB/s")
        .arg(double(bps) / (1024.0 * 1024.0), 0, 'f', 1);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

DownloadDetailPane::DownloadDetailPane(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

void DownloadDetailPane::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Empty state ──────────────────────────────────────────────────────────
    m_emptyLabel = new QLabel(tr("Select a download"), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(
        QStringLiteral("color: rgba(255,255,255,0.35); font-size: 14px;"
                       " background: transparent;"));
    root->addWidget(m_emptyLabel, 1);

    // ── Content container ────────────────────────────────────────────────────
    m_content = new QWidget(this);
    m_content->hide();
    auto* cv = new QVBoxLayout(m_content);
    cv->setContentsMargins(16, 14, 16, 14);
    cv->setSpacing(8);

    // Title
    m_titleLabel = new QLabel(m_content);
    m_titleLabel->setObjectName(QStringLiteral("DownloadDetailPaneTitle"));
    m_titleLabel->setStyleSheet(
        QStringLiteral("font-size: 15px; font-weight: 600; color: #eeeeee;"));
    m_titleLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Ellipsize long titles
    m_titleLabel->setWordWrap(false);
    cv->addWidget(m_titleLabel, 0);

    // Progress bar
    m_progressBar = new QProgressBar(m_content);
    m_progressBar->setRange(0, 100);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setStyleSheet(
        QStringLiteral(
            "QProgressBar {"
            "  background: rgba(255,255,255,0.12);"
            "  border: none;"
            "  border-radius: 3px;"
            "}"
            "QProgressBar::chunk {"
            "  background: rgba(255,255,255,0.55);"
            "  border-radius: 3px;"
            "}"));
    cv->addWidget(m_progressBar, 0);

    // Stats line
    m_statsLabel = new QLabel(m_content);
    m_statsLabel->setStyleSheet(
        QStringLiteral("color: rgba(255,255,255,0.55); font-size: 12px;"));
    cv->addWidget(m_statsLabel, 0);

    // Button row
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(6);
    btnRow->setContentsMargins(0, 0, 0, 0);

    auto makeBtn = [this, &btnRow](const QString& text) -> QPushButton* {
        auto* btn = new QPushButton(text, m_content);
        btn->setStyleSheet(QString::fromLatin1(kBtnStyle));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(28);
        btn->hide();
        btnRow->addWidget(btn, 0);
        return btn;
    };

    m_pauseBtn  = makeBtn(tr("Pause"));
    m_resumeBtn = makeBtn(tr("Resume"));
    m_cancelBtn = makeBtn(tr("Cancel"));
    m_retryBtn  = makeBtn(tr("Retry"));
    m_bumpBtn   = makeBtn(tr("Bump to top"));
    m_playBtn   = makeBtn(tr("Play"));
    btnRow->addStretch(1);
    cv->addLayout(btnRow);

    // Wire button signals — emit intent only, no client mutation
    connect(m_pauseBtn,  &QPushButton::clicked, this, [this]() {
        emit pauseRequested(m_row);
    });
    connect(m_resumeBtn, &QPushButton::clicked, this, [this]() {
        emit resumeRequested(m_row);
    });
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        emit cancelRequested(m_row);
    });
    connect(m_retryBtn,  &QPushButton::clicked, this, [this]() {
        emit retryRequested(m_row);
    });
    connect(m_bumpBtn,   &QPushButton::clicked, this, [this]() {
        emit bumpRequested(m_row);
    });
    connect(m_playBtn,   &QPushButton::clicked, this, [this]() {
        emit playRequested(m_row);
    });

    // Tab widget placeholder — tabs installed lazily in ensureTabsBuilt()
    m_tabWidget = new QTabWidget(m_content);
    m_tabWidget->setObjectName(QStringLiteral("DownloadDetailPaneTabs"));
    m_tabWidget->setStyleSheet(
        QStringLiteral(
            "QTabWidget::pane {"
            "  border: none;"
            "  background: transparent;"
            "}"
            "QTabBar::tab {"
            "  color: rgba(255,255,255,0.65);"
            "  background: transparent;"
            "  padding: 5px 14px;"
            "  font-size: 12px;"
            "}"
            "QTabBar::tab:selected {"
            "  color: #eeeeee;"
            "  border-bottom: 2px solid rgba(255,255,255,0.55);"
            "}"
            "QTabBar::tab:hover:!selected {"
            "  color: #cccccc;"
            "}"));
    m_tabWidget->hide();
    cv->addWidget(m_tabWidget, 1);

    root->addWidget(m_content, 1);

    // ── Stats refresh timer ──────────────────────────────────────────────────
    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(1000);
    m_statsTimer->setSingleShot(false);
    connect(m_statsTimer, &QTimer::timeout, this, &DownloadDetailPane::refreshStats);
}

// ─────────────────────────────────────────────────────────────────────────────
// setClient — lazy tab construction
// ─────────────────────────────────────────────────────────────────────────────

void DownloadDetailPane::setClient(TorrentClient* client)
{
    m_client = client;
    // If we already have a row with a live infoHash, rebuild now that we have
    // a client to point the tabs at.
    if (m_hasRow && !m_row.infoHash.isEmpty())
        rebuildUiForRow();
}

void DownloadDetailPane::ensureTabsBuilt()
{
    if (m_tabsBuilt || !m_client) return;

    m_filesTab     = new TorrentFilesTab(m_client, m_tabWidget);
    m_peersTab     = new TorrentPeersTab(m_client, m_tabWidget);
    m_trackersTab  = new TorrentTrackersTab(m_client, m_tabWidget);

    m_tabWidget->addTab(m_filesTab,    tr("Files"));
    m_tabWidget->addTab(m_peersTab,    tr("Peers"));
    m_tabWidget->addTab(m_trackersTab, tr("Trackers"));

    m_tabsBuilt = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// setRow / clearRow
// ─────────────────────────────────────────────────────────────────────────────

void DownloadDetailPane::setRow(const tankostream::stream::DownloadRow& row,
                                const QString& displayTitle)
{
    m_row          = row;
    m_displayTitle = displayTitle;
    m_hasRow       = true;

    m_emptyLabel->hide();
    m_content->show();

    rebuildUiForRow();
}

void DownloadDetailPane::clearRow()
{
    m_hasRow = false;
    m_statsTimer->stop();
    m_content->hide();
    m_emptyLabel->show();
}

// ─────────────────────────────────────────────────────────────────────────────
// rebuildUiForRow — update all content widgets for the current m_row
// ─────────────────────────────────────────────────────────────────────────────

void DownloadDetailPane::rebuildUiForRow()
{
    using DS = tankostream::stream::DownloadSection;

    if (!m_hasRow) {
        clearRow();
        return;
    }

    // ── Title ──────────────────────────────────────────────────────────────
    const QString episodePart =
        (m_row.type == QLatin1String("movie"))
        ? tr("Movie")
        : QStringLiteral("S%1E%2")
              .arg(m_row.season,  2, 10, QLatin1Char('0'))
              .arg(m_row.episode, 2, 10, QLatin1Char('0'));

    const QString fullTitle = m_displayTitle.isEmpty()
        ? episodePart
        : m_displayTitle + QStringLiteral(" \xB7 ") + episodePart;

    // Truncate with ellipsis if too long — QLabel handles this via elide mode
    // when wordWrap is off and the geometry is constrained.
    m_titleLabel->setText(fullTitle);

    // ── Progress ──────────────────────────────────────────────────────────
    m_progressBar->setValue(m_row.pct);

    // ── Button row: hide all first, then show by section ─────────────────
    m_pauseBtn->hide();
    m_resumeBtn->hide();
    m_cancelBtn->hide();
    m_retryBtn->hide();
    m_bumpBtn->hide();
    m_playBtn->hide();

    switch (m_row.section) {
    case DS::Active:
        if (!m_row.paused) {
            m_pauseBtn->show();
        } else {
            m_resumeBtn->show();
        }
        m_cancelBtn->show();
        break;
    case DS::Queued:
        m_bumpBtn->show();
        m_cancelBtn->show();
        break;
    case DS::Failed:
        m_retryBtn->show();
        m_cancelBtn->show();
        break;
    case DS::Completed:
        m_playBtn->show();
        break;
    }

    // ── Tabs: only when there is a live infoHash and client+tabs are ready ──
    const bool hasLiveTorrent = !m_row.infoHash.isEmpty() && m_client;
    if (hasLiveTorrent) {
        ensureTabsBuilt();
        m_filesTab->setInfoHash(m_row.infoHash);
        m_filesTab->refresh();
        m_peersTab->setInfoHash(m_row.infoHash);
        m_peersTab->refresh();
        m_trackersTab->setInfoHash(m_row.infoHash);
        m_trackersTab->refresh();
        m_tabWidget->show();
    } else {
        m_tabWidget->hide();
    }

    // ── Stats timer: run only when visible and there is a live torrent ────
    if (isVisible() && hasLiveTorrent) {
        refreshStats();   // immediate first tick
        m_statsTimer->start();
    } else {
        m_statsTimer->stop();
        // For rows with no live torrent, show size only (from the row pct
        // we can't derive size here; leave stats blank for Completed rows).
        m_statsLabel->setText(QString());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// refreshStats — 1 Hz tick: pull TorrentInfo from listActive()
// ─────────────────────────────────────────────────────────────────────────────
// The accessor used here is TorrentClient::listActive(), scanning for the
// matching infoHash. This is the same data source that TorrentGeneralTab uses
// for the "live" fields (via engine()->torrentDetails), but speed+peers are
// stored on TorrentInfo (dlSpeed / ulSpeed / peers / seeds / totalDone /
// totalWanted), which is only emitted by the alert-worker tick and exposed by
// listActive() — TorrentDetails does not carry them.

void DownloadDetailPane::refreshStats()
{
    if (!isVisible() || !m_hasRow || !m_client || m_row.infoHash.isEmpty()) {
        m_statsTimer->stop();
        return;
    }

    // Scan active list for matching infoHash
    const auto actives = m_client->listActive();
    TorrentInfo info;
    bool found = false;
    for (const auto& t : actives) {
        if (t.infoHash == m_row.infoHash) {
            info = t;
            found = true;
            break;
        }
    }

    if (!found) {
        // Torrent no longer active (just completed or removed) — stop
        m_statsTimer->stop();
        m_statsLabel->setText(QString());
        return;
    }

    // Format: "<dl_speed> down · <ul_speed> up · <peers> peers · <size>"
    QStringList parts;

    const QString dlStr = formatSpeed(info.dlSpeed);
    const QString ulStr = formatSpeed(info.ulSpeed);
    parts << dlStr + tr(" down");
    parts << ulStr + tr(" up");

    if (info.peers > 0)
        parts << QString::number(info.peers) + tr(" peers");

    if (info.totalWanted > 0)
        parts << humanSize(info.totalWanted);

    m_statsLabel->setText(parts.join(QStringLiteral(" \xB7 ")));
}

// ─────────────────────────────────────────────────────────────────────────────
// Show / hide event — manage timer lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void DownloadDetailPane::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_statsTimer->stop();
}

void DownloadDetailPane::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_hasRow && !m_row.infoHash.isEmpty() && m_client) {
        refreshStats();
        m_statsTimer->start();
    }
}
