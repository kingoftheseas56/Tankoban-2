#pragma once

#include <QDialog>
#include <QTreeWidget>

class TorrentClient;

class TorrentFilesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TorrentFilesDialog(const QString& torrentName, const QString& infoHash,
                                TorrentClient* client, QWidget* parent = nullptr);

private:
    void buildFileTree(const QJsonArray& files);
    QTreeWidget* m_tree = nullptr;
};
