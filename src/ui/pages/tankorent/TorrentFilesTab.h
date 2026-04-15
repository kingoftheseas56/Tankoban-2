#pragma once

#include <QWidget>
#include <QString>
#include <QMap>

class QTreeWidget;
class QTreeWidgetItem;
class QComboBox;
class TorrentClient;

// Per-torrent files tab. QTreeWidget with Name / Size / Progress / Priority
// columns; each row's Priority cell is a live QComboBox writing back to
// TorrentEngine::setFilePriorities on change. Live progress via refresh()
// called from the parent TorrentPropertiesWidget's 1 Hz timer.
class TorrentFilesTab : public QWidget
{
    Q_OBJECT

public:
    explicit TorrentFilesTab(TorrentClient* client, QWidget* parent = nullptr);

    // Load the given torrent's files into the tree. Clears previous contents.
    void setInfoHash(const QString& infoHash);

public slots:
    // Re-fetch torrentFiles and update the Progress column in-place.
    // Priority column is left alone (user-driven — don't overwrite mid-edit).
    void refresh();

private slots:
    void onPriorityCombo(int fileIndex, int libtorrentPriority);
    void onTreeContextMenu(const QPoint& pos);
    void applyBulkPriority(int libtorrentPriority);

private:
    void buildUI();
    void populateTree(const QString& rootName);
    void writePrioritiesToEngine();

    static int  priorityComboIndex(int libtorrentPriority);
    static int  libtorrentPriorityForComboIndex(int idx);
    static QString priorityLabel(int libtorrentPriority);

    TorrentClient* m_client = nullptr;
    QString        m_infoHash;

    QTreeWidget*   m_tree = nullptr;

    struct FileRow {
        int              index = -1;
        QString          fullPath;   // relative to torrent root
        qint64           size  = 0;
        QTreeWidgetItem* item  = nullptr;
        QComboBox*       combo = nullptr;
    };
    // index → row metadata
    QMap<int, FileRow> m_rows;
};
