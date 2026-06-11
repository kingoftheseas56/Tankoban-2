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

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
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

// Format bytes/s into a human-friendly string: "1.2 MB/s", "348.0 KB/s", etc.
// Mirrors TorrentPeersTab::formatSpeed — delegates to humanSize() for consistent
// one-decimal KB/MB formatting across all Tankorent surfaces, then appends "/s".
QString formatSpeed(qint64 bps)
{
    if (bps <= 0) return QStringLiteral("0 B/s");
    return humanSize(bps) + QStringLiteral("/s");
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

    // Title — manual eliding via reelideTitle() / resizeEvent().
    // QLabel has no native elide mode: with wordWrap off a long text sets a hard
    // minimum width that locks the splitter. We store the full title in m_fullTitle
    // + tooltip, and call QFontMetrics::elidedText in resizeEvent (mirrors
    // StreamSourceCard::reelideTitle). SizePolicy::Ignored lets the label shrink
    // below its natural text width so the splitter can move freely.
    m_titleLabel = new QLabel(m_content);
    m_titleLabel->setObjectName(QStringLiteral("DownloadDetailPaneTitle"));
    m_titleLabel->setStyleSheet(
        QStringLiteral("font-size: 15px; font-weight: 600; color: #eeeeee;"));
    m_titleLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_titleLabel->setWordWrap(false);
    m_titleLabel->setMinimumWidth(0);
    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
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
    // Single-injection guard: tabs are wired to the first non-null client.
    // Re-injection with a DIFFERENT client after tabs are built is unsupported —
    // all three tab widgets hold a raw pointer to the original client and would
    // need to be torn down and reconstructed to rebind. Warn and bail out.
    if (m_tabsBuilt && m_client && client != m_client) {
        qWarning("DownloadDetailPane::setClient: re-injection with a different "
                 "client after tabs are built is unsupported — ignoring.");
        return;
    }
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
    // Capture old hash BEFORE mutating m_row so rebuildUiForRow() can
    // detect a same-hash re-selection (C1 short-circuit).
    const QString oldHash = m_row.infoHash;

    m_row          = row;
    m_displayTitle = displayTitle;
    m_hasRow       = true;

    // C1: propagate whether this is a same-hash re-selection.
    // rebuildUiForRow() uses this to skip the tab teardown on 4 Hz re-fires.
    m_sameHashReselect = (row.infoHash == oldHash) && m_tabsBuilt;

    m_emptyLabel->hide();
    m_content->show();

    rebuildUiForRow();
}

void DownloadDetailPane::clearRow()
{
    m_hasRow = false;
    m_row    = {};
    m_displayTitle.clear();
    m_fullTitle.clear();
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

    // C1 short-circuit flag set by setRow() before this call.
    // m_sameHashReselect == true means: the infoHash did not change AND tabs
    // are already populated. rebuildUiForRow() will update title/progress/buttons
    // but must NOT call setInfoHash() on any tab — that triggers full tree clear
    // + per-file QComboBox allocation + a listActive() GUI-thread SQL scan, and
    // the rebuild() → restoreSelection() → setRow() chain fires up to 4×/s.

    // ── Title ──────────────────────────────────────────────────────────────
    const QString episodePart =
        (m_row.type == QLatin1String("movie"))
        ? tr("Movie")
        : QStringLiteral("S%1E%2")
              .arg(m_row.season,  2, 10, QLatin1Char('0'))
              .arg(m_row.episode, 2, 10, QLatin1Char('0'));

    m_fullTitle = m_displayTitle.isEmpty()
        ? episodePart
        : m_displayTitle + QStringLiteral(" \xB7 ") + episodePart;

    m_titleLabel->setToolTip(m_fullTitle);
    reelideTitle();   // paints elided text at current width

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
        // C1 short-circuit: when the infoHash has not changed and tabs are
        // already populated (m_sameHashReselect == true), skip the full
        // setInfoHash() teardown (tree clear + QComboBox reallocation +
        // GUI-thread SQL listActive()). In-place progress updates are driven
        // by the 1 Hz stats timer via refreshStats() → m_filesTab->refresh().
        // setInfoHash() runs only when the hash actually changes (new selection)
        // or on the very first population (m_tabsBuilt == false → ensureTabsBuilt
        // sets it, m_sameHashReselect is false for first call).
        if (!m_sameHashReselect) {
            m_filesTab->setInfoHash(m_row.infoHash);
            m_filesTab->refresh();
            m_peersTab->setInfoHash(m_row.infoHash);
            m_peersTab->refresh();
            m_trackersTab->setInfoHash(m_row.infoHash);
            m_trackersTab->refresh();
        }
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
        // No live torrent (Completed/history rows with empty infoHash):
        // stats line is not meaningful — clear it.
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

    parts << tr("%1 down").arg(formatSpeed(info.dlSpeed));
    parts << tr("%1 up").arg(formatSpeed(info.ulSpeed));

    if (info.peers > 0)
        parts << tr("%1 peers").arg(info.peers);

    if (info.totalWanted > 0)
        parts << humanSize(info.totalWanted);

    // Ride the 1 Hz timer to push in-place file-progress updates to the Files
    // tab without a full setInfoHash() teardown (C1 short-circuit complement).
    if (m_tabsBuilt && m_filesTab)
        m_filesTab->refresh();

    m_statsLabel->setText(parts.join(QStringLiteral(" \xB7 ")));
}

// ─────────────────────────────────────────────────────────────────────────────
// Title eliding — mirrors StreamSourceCard::reelideTitle()
// ─────────────────────────────────────────────────────────────────────────────

void DownloadDetailPane::reelideTitle()
{
    if (!m_titleLabel || m_fullTitle.isEmpty()) return;
    const int avail = m_titleLabel->width();
    if (avail <= 0) {
        // Widget not yet laid out — set the full text so the layout can measure
        // it; resizeEvent will correct it once we have a real width.
        m_titleLabel->setText(m_fullTitle);
        return;
    }
    const QFontMetrics fm(m_titleLabel->font());
    m_titleLabel->setText(fm.elidedText(m_fullTitle, Qt::ElideRight, avail));
}

void DownloadDetailPane::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    reelideTitle();
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
