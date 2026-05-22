// src/ui/pages/comics/VolumeTile.h
//
// COMICS_CATALOG_SERIES_VIEW Phase 2 (2026-05-22) — EpisodeTile parallel
// for Comics volume rows. 130px row, 80×120 cover, 5-state chip palette.
// Subscribes to MangaDownloadIndex::entriesChanged for Complete/NotStarted;
// ComicsSeriesView pushes transient states (Queued/Downloading/Failed)
// via setEpisodeState-equivalent slots.

#pragma once

#include <QFrame>
#include <QPixmap>
#include <QPointer>
#include <QString>

class QCheckBox;
class QLabel;
class QPushButton;

// MangaDownloadIndex lives at global scope (no namespace wrapper in its header).
class MangaDownloadIndex;

namespace tankoban::ui::comics {

struct VolumeTileData {
    QString sourceId;                  // "fandom_catalog" / "tankoyomi" / "weebcentral"
    QString seriesId;                  // slug or anilist_<N>
    int     volumeNumber = 0;          // 1..N
    QString title;                     // English volume title, may be empty
    QString chapterRange;              // "ch 1-8"
    int     pages = 0;                 // 0 if unknown
    QString publishDate;               // free-form, may be empty
    QString coverUrl;                  // initial cover; setCoverFromDisk overrides post-DL
};

struct VolumeTileState {
    enum State {
        NotStarted = 0,
        Queued     = 1,
        Downloading = 2,
        Complete   = 3,
        Failed     = 4,
    };
    State   state         = NotStarted;
    int     progressPct   = 0;        // 0..100, only meaningful when state == Downloading
    QString statusText;               // free-form chip suffix or failure reason
    QString cbzPath;                  // set when state == Complete
    QString provenance;               // "" / "Fandom" / "Tankoyomi" / "LocalScan"
};

class VolumeTile : public QFrame {
    Q_OBJECT
public:
    explicit VolumeTile(const VolumeTileData& data, QWidget* parent = nullptr);

    int  volumeNumber() const  { return m_data.volumeNumber; }
    bool isChecked() const;
    void setChecked(bool checked);
    void setCheckedQuiet(bool checked);   // shift+click range-fill: no signal emit

    void setVolumeState(const VolumeTileState& s);
    VolumeTileState volumeState() const { return m_state; }

    void setCoverFromDisk(const QString& coverPath);
    void setCoverFromUrl(const QString& url);     // for AniList-path covers
    void setCoverFromPixmap(const QPixmap& pm);   // for async-fetch paint path
    void setStatusText(const QString& text);

    // Per-tile subscription. Non-owning. May be set after construction.
    void setMangaDownloadIndex(MangaDownloadIndex* idx);

    // Pure-logic helper exposed for testing -- maps (presence-of-DL-index-entry,
    // statusText) tuple to the canonical State value. Static so the test
    // doesn't need a QApplication.
    static VolumeTileState::State computeState(bool hasIndexEntry,
                                                const QString& statusText);

signals:
    void toggled(bool checked);
    void toggledShift(bool checked, bool shiftHeld);
    void openRequested(int volumeNumber);          // user clicked Open on Complete
    void downloadRequested(int volumeNumber);      // user clicked Download on NotStarted
    void cancelRequested(int volumeNumber);        // user clicked Cancel on Queued/Downloading
    void retryRequested(int volumeNumber);         // user clicked Retry on Failed

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void onIndexEntriesChanged();
    void onActionClicked();

private:
    void buildUi();
    void applyState();

    VolumeTileData  m_data;
    VolumeTileState m_state;

    QPointer<MangaDownloadIndex> m_idx;

    QCheckBox*   m_checkbox = nullptr;
    QLabel*      m_numberLabel = nullptr;
    QLabel*      m_coverLabel = nullptr;
    QLabel*      m_titleLabel = nullptr;
    QLabel*      m_metaLabel = nullptr;
    QLabel*      m_chipLabel = nullptr;
    QLabel*      m_progressLabel = nullptr;   // text "38% · 12.4 MB / 32.7 MB"
    QPushButton* m_actionBtn = nullptr;
};

} // namespace tankoban::ui::comics
