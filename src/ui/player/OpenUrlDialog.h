#pragma once

#include <QDialog>

class QLineEdit;

// VIDEO_PLAYER_FIX Batch 4.1 — small modal that prompts for a stream URL.
// Pre-populates the line edit with clipboard contents when they look like
// a URL (http/https/rtsp/rtmp scheme). Accept routes the text through
// `VideoPlayer::openFile(url)` via the caller; this dialog is UI-only,
// no playback coupling. Rejects empty + obviously malformed input at
// accept time; lets the sidecar surface protocol-level errors downstream
// for anything that parses clean here.
class OpenUrlDialog : public QDialog {
    Q_OBJECT
public:
    explicit OpenUrlDialog(QWidget* parent = nullptr);

    // Accepted URL text. Empty when the dialog was cancelled or rejected.
    QString url() const;

private:
    void populateFromClipboard();

    QLineEdit* m_edit = nullptr;
};
