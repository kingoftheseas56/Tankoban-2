#pragma once

#include <QDialog>
#include <QString>

class QTabWidget;
class QTimer;
class TorrentClient;
class TorrentFilesTab;
class TorrentTrackersTab;
class TorrentPeersTab;
class TorrentGeneralTab;

// Tabbed per-torrent properties dialog (qBittorrent-style).
class TorrentPropertiesWidget : public QDialog
{
    Q_OBJECT

public:
    explicit TorrentPropertiesWidget(TorrentClient* client, QWidget* parent = nullptr);
    ~TorrentPropertiesWidget() override;

    // Stable tab indices for callers that want to pre-select a tab.
    enum Tab { TabGeneral = 0, TabTrackers = 1, TabPeers = 2, TabFiles = 3 };

    // Stashes the infoHash, resolves torrent name from the client's active
    // list, sets window title, starts the refresh timer. Call before exec().
    // Optionally pre-selects a tab (e.g. showTorrent(hash, TabFiles)).
    void showTorrent(const QString& infoHash, int tabIndex = TabGeneral);

private slots:
    void refresh();

private:
    void buildUI();

    TorrentClient*      m_client       = nullptr;
    QString             m_infoHash;
    QTabWidget*         m_tabs         = nullptr;
    QTimer*             m_refreshTimer = nullptr;
    TorrentGeneralTab*  m_generalTab   = nullptr;
    TorrentTrackersTab* m_trackersTab  = nullptr;
    TorrentPeersTab*    m_peersTab     = nullptr;
    TorrentFilesTab*    m_filesTab     = nullptr;
};
