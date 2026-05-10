#pragma once

// STREAM_DOWNLOADED_LIBRARY 2026-05-10 Phase 6 — modal progress dialog for the
// first-launch migration scan. Spec §9.6.

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;
class StreamRescueScanner;

class StreamRescueProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StreamRescueProgressDialog(StreamRescueScanner* scanner,
                                        QWidget* parent = nullptr);

private slots:
    void onProgress(int current, int total, const QString& showName);
    void onComplete(int matched, int unmatched, int ambiguous,
                    int episodes);

private:
    StreamRescueScanner* m_scanner;
    QLabel*       m_status    = nullptr;
    QProgressBar* m_bar       = nullptr;
    QPushButton*  m_cancelBtn = nullptr;
};
