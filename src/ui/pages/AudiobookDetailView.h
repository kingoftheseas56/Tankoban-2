#pragma once

#include <QWidget>

class QPushButton;
class QLabel;
class QTableWidget;
struct AudiobookInfo;

// AudiobookDetailView — chapter-list info view for an audiobook
// (AUDIOBOOK_PAIRED_READING_FIX Phase 2). Pushed onto BooksPage's
// m_stack when a user clicks an audiobook tile in the Audiobooks strip.
// Read-only — this view does NOT play audio. Playback happens inside
// BookReader's sidebar Audio tab (Phase 3). The "In-reader only" pill
// badge in the header signals that explicitly.
//
// Columns: #, Chapter, Duration, Progress.
// Progress column is "-" placeholder in Phase 2; Phase 4 fills it with
// per-chapter listened-ms from audiobook_progress.json.
class AudiobookDetailView : public QWidget {
    Q_OBJECT
public:
    explicit AudiobookDetailView(QWidget* parent = nullptr);

    // Populate the view for a given audiobook. Call this every time the view
    // is pushed onto the stack (idempotent — clears and repopulates).
    void showAudiobook(const AudiobookInfo& audiobook);

signals:
    void backRequested();

private:
    void buildUI();
    void populateChapters(const AudiobookInfo& audiobook);

    static QString formatDuration(qint64 ms);

    QPushButton*  m_backBtn      = nullptr;
    QLabel*       m_titleLabel   = nullptr;
    QLabel*       m_coverLabel   = nullptr;
    QLabel*       m_metaLabel    = nullptr;  // "N chapters · HH:MM:SS"
    QLabel*       m_badgeLabel   = nullptr;  // "In-reader only" pill
    QTableWidget* m_chapterTable = nullptr;

    QString m_audiobookPath;  // kept so populateChapters can re-query MetaCache
};
