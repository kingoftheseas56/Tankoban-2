#pragma once

// THEATRE_DOWNLOAD_OVERHAUL 2026-05-16 — scope-picker episode tile.
// Compact row: S·E label + title (if known) + size + checkbox + optional
// "Have" badge for already-downloaded episodes. Per Codex expansion
// 5.5.A + 5.5.B.

#include <QFrame>

// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — state-input contract for the
// download-source-aware episode tile. EpisodeTile reads state + progressPct
// to render the chip; reads provenance to apply (or skip) the warm-amber tint.
#include "core/stream/StreamDownloadIndex.h"

class QCheckBox;
class QLabel;
class QPaintEvent;

namespace tankoban::stream::theatre {

struct EpisodeTileData {
    int     season = 0;
    int     episode = 0;
    QString title;
    qint64  sizeBytes = 0;
    bool    alreadyHave = false;
};

struct EpisodeTileState {
    StreamDownloadIndex::Entry::State state =
        StreamDownloadIndex::Entry::Complete;
    int progressPct = 100;
    enum Provenance {
        AddonBulk = 0,   // sourced from Stremio addon download
        Tankorent = 1,   // sourced from Tankorent pack
        LocalScan = 2    // pre-existing file discovered by StreamRescueScanner
    };
    Provenance provenance = AddonBulk;
};

class EpisodeTile : public QFrame {
    Q_OBJECT
public:
    explicit EpisodeTile(const EpisodeTileData& data, QWidget* parent = nullptr);

    bool isChecked() const;
    void setChecked(bool checked);

    // THEATRE_BULK_PICKER_SHIFT_RANGE 2026-05-22 — set the underlying
    // checkbox state without emitting the toggled* signals. Used by the
    // bulk picker's shift-click range-fill so a single shift-click
    // doesn't cascade N tile-toggled events back into the panel's
    // m_tileChecked map updates.
    void setCheckedQuiet(bool checked);

    int season() const  { return m_data.season; }
    int episode() const { return m_data.episode; }

    void setEpisodeState(const EpisodeTileState& s);

    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 Task 15 — wire the tile to
    // live StreamDownloadIndex state. After setStreamDownloadIndex(idx) the
    // tile subscribes to entryStateChanged scoped on (m_imdbId, m_data.season,
    // m_data.episode) and pulls its initial state from the index.
    void setImdbId(const QString& imdbId);
    void setStreamDownloadIndex(StreamDownloadIndex* idx);

signals:
    void toggled(bool checked);

    // THEATRE_BULK_PICKER_SHIFT_RANGE 2026-05-22 — richer toggle signal
    // carrying the keyboard-modifier state at click time. Consumers that
    // want shift+click range-fill connect here; consumers that only care
    // about the binary checked state stay on `toggled(bool)`.
    void toggledShift(bool checked, bool shiftHeld);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void buildUI();
    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 Task 15 — re-read live state
    // from the index for this tile's (imdbId, season, episode). Called once
    // on setStreamDownloadIndex(idx) and again on each entryStateChanged signal
    // that scopes to this tile.
    void refreshFromIndex();

    EpisodeTileData      m_data;
    EpisodeTileState     m_episodeState;
    bool                 m_hasIndexEntry = false;  // gates chip rendering
    QString              m_imdbId;
    StreamDownloadIndex* m_streamDownloadIndex = nullptr;
    QCheckBox*           m_checkBox = nullptr;
    QLabel*              m_seLabel  = nullptr;
    QLabel*              m_titleLabel = nullptr;
    QLabel*              m_sizeLabel  = nullptr;
    QLabel*              m_haveBadge  = nullptr;
};

}  // namespace tankoban::stream::theatre
