#pragma once
// DOWNLOADS_OVERHAUL_V2 (2026-06-11) — pure aggregation for the Downloads
// command center. Inputs are plain snapshots (testable without TorrentClient);
// output is the status-sectioned, show-grouped row list the page renders.
#include "core/stream/StreamDownloadIndex.h"
#include "core/queue/TransferLane.h"
#include <QHash>
#include <QList>
#include <QMetaType>
#include <QString>

namespace tankostream::stream {

enum class DownloadSection { Failed, Active, Queued, Completed };

struct DownloadRow {
    QString imdbId;
    QString showTitle;        // enriched later by the page (meta cache); imdbId fallback
    QString type;             // "series" | "movie"
    int     season = 0;
    int     episode = 0;
    QString infoHash;         // carrying transfer (empty when none, e.g. old history)
    QString canonicalPath;    // for Play on Completed rows
    DownloadSection section = DownloadSection::Completed;
    int     pct = 0;
    bool    paused = false;
    qint64  addedAt = 0;      // Completed auto-trim key
};

struct DownloadsSnapshot {
    QList<StreamDownloadIndex::Entry>                 indexEntries;  // StreamDownloadIndex::all()
    QHash<QString, tankoban::queue::TransferLane>     lanes;         // TransferQueue::lanesSnapshot()
};

// Find the lane item carrying (season, episode) for this show, if any. Season
// packs carry no episodeNumber — they match any episode of their season.
// Exposed for reuse by the episode-row state gatherer (click-feedback task).
const tankoban::queue::TransferItem* laneItemFor(
    const QHash<QString, tankoban::queue::TransferLane>& lanes,
    const QString& imdbId, int season, int episode);

// Section rules (spec §3.1): Failed/Paused/Running from the lane item state of
// the episode's carrying transfer; index Pending with a Queued lane item (or no
// lane item yet) -> Queued; index Complete -> Completed, trimmed when older
// than maxCompletedAgeMs (0 = no trim). Rows sort: section order
// Failed->Active->Queued->Completed, then showTitle/imdbId, then season, episode.
QList<DownloadRow> buildDownloadRows(const DownloadsSnapshot& snap,
                                     qint64 nowMs,
                                     qint64 maxCompletedAgeMs);

}  // namespace tankostream::stream

Q_DECLARE_METATYPE(tankostream::stream::DownloadRow)
