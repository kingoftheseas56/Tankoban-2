#pragma once

#include <QWidget>
#include <QString>

class QLabel;
class QFormLayout;
class TorrentClient;

// Per-torrent General tab. Read-only QFormLayout surfacing static metadata
// (name / size / pieces / created / creator / comment / infoHash / save path)
// + live-refreshing dynamic fields (share ratio, reannounce countdown,
// availability, current tracker).
class TorrentGeneralTab : public QWidget
{
    Q_OBJECT

public:
    explicit TorrentGeneralTab(TorrentClient* client, QWidget* parent = nullptr);

    void setInfoHash(const QString& infoHash);

public slots:
    void refresh();

private:
    void buildUI();

    TorrentClient* m_client = nullptr;
    QString        m_infoHash;
    bool           m_staticPopulated = false;   // name/hash/size/pieces etc. — populate once

    // Static (populated once on setInfoHash)
    QLabel* m_name      = nullptr;
    QLabel* m_size      = nullptr;
    QLabel* m_pieces    = nullptr;
    QLabel* m_pieceSize = nullptr;
    QLabel* m_created   = nullptr;
    QLabel* m_createdBy = nullptr;
    QLabel* m_comment   = nullptr;
    QLabel* m_infoHashLabel = nullptr;
    QLabel* m_savePath  = nullptr;

    // Dynamic (refreshed every 1 Hz)
    QLabel* m_currentTracker = nullptr;
    QLabel* m_availability   = nullptr;
    QLabel* m_shareRatio     = nullptr;
    QLabel* m_nextReannounce = nullptr;
};
