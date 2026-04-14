#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QList>

struct TorrentioStream;

class TorrentPickerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TorrentPickerDialog(const QList<TorrentioStream>& streams,
                                 QWidget* parent = nullptr);

    TorrentioStream selectedStream() const;
    bool hasSelection() const { return m_selectedRow >= 0; }

private:
    void buildUI(const QList<TorrentioStream>& streams);
    void onRowDoubleClicked(int row, int col);
    void onSelectClicked();

    QTableWidget* m_table     = nullptr;
    QLabel*       m_infoLabel = nullptr;
    QPushButton*  m_selectBtn = nullptr;
    QPushButton*  m_cancelBtn = nullptr;

    QList<TorrentioStream> m_streams;
    int m_selectedRow = -1;
};
