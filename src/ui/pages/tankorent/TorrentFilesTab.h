#pragma once

#include <QWidget>
#include <QString>
#include <QMap>
#include <QHash>
#include <QList>

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

    // Walk all descendant leaves of `root` (depth-first) and drive each
    // leaf's priority combo to `comboIdx`. Folder items are skipped (they
    // have ROLE_FILE_INDEX == -1). Caller is responsible for pushing the
    // resulting priority vector to the engine after this returns.
    void cascadePriorityToDescendants(QTreeWidgetItem* root, int comboIdx);

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

    // Folder rows by their cumulative path key (e.g. "Pack/Season 1"). Keys
    // use '/' as separator regardless of host OS — libtorrent file_path() is
    // always POSIX-style. Used during populateTree to deduplicate parent
    // folder creation, and during refresh() to update folder-level progress
    // text in place.
    struct FolderRow {
        QString          pathKey;     // cumulative path from root (no trailing '/')
        qint64           totalSize = 0;
        QTreeWidgetItem* item     = nullptr;
        // Indices of leaf files anywhere below this folder; used to recompute
        // the aggregate progress on each refresh() tick without re-walking
        // the engine's file list.
        QList<int>       descendantFileIndexes;
    };
    QHash<QString, FolderRow> m_folders;
};
