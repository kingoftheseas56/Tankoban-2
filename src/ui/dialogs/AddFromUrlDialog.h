#pragma once

#include <QDialog>
#include <QStringList>

class QTextEdit;
class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;

// Multi-URL / multi-magnet entry dialog for bulk add without going through
// the per-torrent AddTorrentDialog. Accepts magnet URIs today; `.torrent`
// URLs are detected, counted as "skipped," and flagged — full HTTP-to-file
// plumbing is follow-up work that needs a TorrentEngine::addTorrentFile API.
class AddFromUrlDialog : public QDialog
{
    Q_OBJECT

public:
    // Optional seed text pre-fills the textedit. Empty = clipboard auto-preload
    // (only when clipboard content looks like a magnet / .torrent URL).
    explicit AddFromUrlDialog(QWidget* parent = nullptr,
                              const QString& seedText = QString());

    // Parsed magnet URIs from the accepted textedit content (one per line,
    // trimmed, magnet-prefix only).
    QStringList magnets() const;

    // Target category for every URL in this batch ("videos" / "books" / ...).
    QString category() const;

    // User's toggle: start each torrent immediately (true, default) or add
    // as paused (false) so the user can configure per-torrent state first.
    bool startImmediately() const;

private slots:
    void onTextChanged();

private:
    void buildUI();
    void preloadFromClipboardIfApplicable();

    QTextEdit*   m_edit           = nullptr;
    QComboBox*   m_categoryCombo  = nullptr;
    QCheckBox*   m_startImmediate = nullptr;
    QLabel*      m_status         = nullptr;
    QPushButton* m_acceptBtn      = nullptr;

    QStringList  m_parsedMagnets;
};
