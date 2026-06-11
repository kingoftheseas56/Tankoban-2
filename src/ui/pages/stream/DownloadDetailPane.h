#pragma once
// DOWNLOADS_OVERHAUL_V2 Task 5 (2026-06-11) — right pane of the Downloads
// command center. Renders one DownloadRow: header + numeric stats + controls +
// the reused Tankorent property tabs pointed at the row's carrying torrent.
// Emits intents only; StreamDownloadsPage routes them (this pane uses
// TorrentClient strictly read-only, for the tabs + stats snapshot).
#include "core/stream/DownloadsCommandModel.h"
#include <QWidget>

class TorrentClient;
class TorrentFilesTab;
class TorrentPeersTab;
class TorrentTrackersTab;
class QLabel;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QTimer;

class DownloadDetailPane : public QWidget {
    Q_OBJECT
public:
    explicit DownloadDetailPane(QWidget* parent = nullptr);

    // Lazy: tabs are constructed on the first non-null client call.
    void setClient(TorrentClient* client);

    // Display the given row. displayTitle is the enriched show name (or imdbId
    // fallback) resolved by the page from its title cache.
    void setRow(const tankostream::stream::DownloadRow& row,
                const QString& displayTitle);

    // Reset to empty state: "Select a download" label, everything else hidden.
    void clearRow();

signals:
    void pauseRequested(const tankostream::stream::DownloadRow& row);
    void resumeRequested(const tankostream::stream::DownloadRow& row);
    void cancelRequested(const tankostream::stream::DownloadRow& row);
    void retryRequested(const tankostream::stream::DownloadRow& row);
    void bumpRequested(const tankostream::stream::DownloadRow& row);
    void playRequested(const tankostream::stream::DownloadRow& row);

protected:
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void buildUi();
    void ensureTabsBuilt();   // construct tabs once m_client is non-null
    void rebuildUiForRow();   // update visible controls / button row for current row
    void refreshStats();      // 1s timer tick: pull TorrentInfo from listActive()

    // ── Injection state ──────────────────────────────────────────────────────
    TorrentClient* m_client  = nullptr;
    bool           m_tabsBuilt = false;

    // ── Row state ────────────────────────────────────────────────────────────
    tankostream::stream::DownloadRow m_row;
    QString                          m_displayTitle;
    bool                             m_hasRow = false;

    // ── Empty-state widget ───────────────────────────────────────────────────
    QLabel* m_emptyLabel = nullptr;

    // ── Content container (hidden in empty state) ────────────────────────────
    QWidget* m_content = nullptr;

    // ── Header ───────────────────────────────────────────────────────────────
    QLabel*      m_titleLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel*      m_statsLabel = nullptr;

    // ── Button row ───────────────────────────────────────────────────────────
    QPushButton* m_pauseBtn  = nullptr;
    QPushButton* m_resumeBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_retryBtn  = nullptr;
    QPushButton* m_bumpBtn   = nullptr;
    QPushButton* m_playBtn   = nullptr;

    // ── Property tabs (constructed lazily via ensureTabsBuilt) ───────────────
    QTabWidget*       m_tabWidget    = nullptr;
    TorrentFilesTab*  m_filesTab     = nullptr;
    TorrentPeersTab*  m_peersTab     = nullptr;
    TorrentTrackersTab* m_trackersTab = nullptr;

    // ── Stats refresh timer ──────────────────────────────────────────────────
    QTimer* m_statsTimer = nullptr;
};
