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
    QString sourceGroupId;    // index entry's group ("tankorent:<infohash>"; may be empty)
    QString canonicalPath;    // for Play on Completed rows
    DownloadSection section = DownloadSection::Completed;
    int     pct = 0;
    bool    paused = false;
    qint64  addedAt = 0;      // Completed auto-trim key
};

// Group convention is "tankorent:<lowercase-infohash>", stamped at the
// TorrentClient registration sites (registerEpisode / registerPendingEpisode /
// registerPendingMovie / markFailedByGroup callers). Returns the infohash for
// tankorent groups; empty for anything else (e.g. migration-rescued entries).
// Lets the Downloads page derive an engine hash for orphan rows whose lane
// item is gone (T6 review C2/I1).
inline QString infoHashFromGroup(const QString& sourceGroupId)
{
    return sourceGroupId.startsWith(QStringLiteral("tankorent:"))
        ? sourceGroupId.mid(10)
        : QString();
}

struct DownloadsSnapshot {
    QList<StreamDownloadIndex::Entry>                 indexEntries;  // StreamDownloadIndex::all()
    QHash<QString, tankoban::queue::TransferLane>     lanes;         // TransferQueue::lanesSnapshot()
};

// Find the lane item carrying (season, episode) for this show, if any. Season
// packs carry no episodeNumber — they match any episode of their season.
// Movie rows (season 0, episode 0) match a lane item with nullopt seasonNumber
// via the same path: seasonNumber.value_or(0) == 0, and the item's missing
// episodeNumber matches like a season pack does. When a lane holds both a
// pack and a specific-episode item for the same season, lane list order wins
// (first match is returned).
// LIFETIME: the returned pointer aliases storage inside `lanes` — it is valid
// only while the passed hash is alive and unmodified.
// Exposed for reuse by the episode-row state gatherer (click-feedback task).
const tankoban::queue::TransferItem* laneItemFor(
    const QHash<QString, tankoban::queue::TransferLane>& lanes,
    const QString& imdbId, int season, int episode);

// Section rules (spec §3.1 + §6): Failed/Paused/Queued/Running come from the
// lane item state of the episode's carrying transfer when one exists; otherwise
// the index state drives. Index Complete -> Completed, trimmed when older than
// maxCompletedAgeMs (0 = no trim). Index Failed -> Failed (lanes erase items on
// terminal states, so failure normally arrives via the index; a lane item, e.g.
// a retry re-queue, takes precedence). Index Downloading with no lane item ->
// Active (orphaned resume — the engine still runs the transfer; review I1).
// Index Pending with a Queued lane item (or no lane item yet) -> Queued.
// Rows sort: section order Failed->Active->Queued->Completed, then imdbId,
// then season, episode.
QList<DownloadRow> buildDownloadRows(const DownloadsSnapshot& snap,
                                     qint64 nowMs,
                                     qint64 maxCompletedAgeMs);

}  // namespace tankostream::stream

Q_DECLARE_METATYPE(tankostream::stream::DownloadRow)
