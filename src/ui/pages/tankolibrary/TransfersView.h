#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QTableWidget;

// Tankoyomi-parity Transfers surface for TankoLibrary. Inline QWidget
// embedded as page 1 of the results-area QStackedWidget (page 0 = grid).
// Pure view — record state is owned by TankoLibraryPage; setRecords()
// repaints the table.
//
// Column layout mirrors Tankoyomi's Transfers tab: Title / Progress / Status.
struct TransferRecord {
    enum class State { Queued, Downloading, Done, Failed };

    QString title;
    QString md5;
    State   state = State::Queued;
    qint64  bytesReceived = 0;
    qint64  bytesTotal    = 0;
    QString filePath;
    QString errorReason;
    qint64  startedMs = 0;
};

class TransfersView : public QWidget
{
    Q_OBJECT

public:
    explicit TransfersView(QWidget* parent = nullptr);

    void setRecords(const QList<TransferRecord>& records);

private:
    void buildUI();

    QTableWidget* m_table = nullptr;
};
