#pragma once

#include <QDialog>
#include <QTableWidget>

class TorrentClient;

class HistoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HistoryDialog(TorrentClient* client, QWidget* parent = nullptr);

private:
    QTableWidget* m_table = nullptr;
};
