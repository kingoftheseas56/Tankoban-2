#pragma once

#include <QWidget>
#include <QString>

class QTableWidget;
class TorrentClient;

// Per-torrent trackers tab. QTableWidget with URL/Tier/Status/NextAnnounce/
// MinAnnounce/Peers/Seeds/Leeches/Downloaded/Message columns. Context menu
// wires Force reannounce + Copy URL + Edit URL + Remove; Add button above
// opens a QInputDialog for URL + tier.
class TorrentTrackersTab : public QWidget
{
    Q_OBJECT

public:
    explicit TorrentTrackersTab(TorrentClient* client, QWidget* parent = nullptr);

    void setInfoHash(const QString& infoHash);

public slots:
    void refresh();

private slots:
    void onAddTracker();
    void onContextMenu(const QPoint& pos);

private:
    void buildUI();

    TorrentClient* m_client = nullptr;
    QString        m_infoHash;
    QTableWidget*  m_table = nullptr;
};
