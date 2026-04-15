#pragma once

#include <QWidget>
#include <QString>

class QTableWidget;
class TorrentClient;

// Per-torrent peers tab. QTableWidget with live peer list, context-menu
// Copy IP:Port + Ban peer permanently + Add peer. Country column shows
// "--" placeholder — GeoIP integration is explicit non-goal per the identity
// scope ceiling.
class TorrentPeersTab : public QWidget
{
    Q_OBJECT

public:
    explicit TorrentPeersTab(TorrentClient* client, QWidget* parent = nullptr);

    void setInfoHash(const QString& infoHash);

public slots:
    void refresh();

private slots:
    void onAddPeer();
    void onContextMenu(const QPoint& pos);

private:
    void buildUI();

    TorrentClient* m_client = nullptr;
    QString        m_infoHash;
    QTableWidget*  m_table = nullptr;
};
