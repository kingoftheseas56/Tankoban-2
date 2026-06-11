// DOWNLOADS_OVERHAUL_V2 (2026-06-11) — pure aggregation for the Downloads
// command center. See DownloadsCommandModel.h for the full contract.
#include "core/stream/DownloadsCommandModel.h"
#include <algorithm>

namespace tankostream::stream {

const tankoban::queue::TransferItem* laneItemFor(
    const QHash<QString, tankoban::queue::TransferLane>& lanes,
    const QString& imdbId, int season, int episode)
{
    const auto it = lanes.constFind(QStringLiteral("imdb:") + imdbId);
    if (it == lanes.constEnd()) return nullptr;
    for (const auto& item : it->items) {
        const int s = item.seasonNumber.value_or(0);
        if (s != season) continue;
        if (!item.episodeNumber.has_value()) return &item;   // season pack
        if (item.episodeNumber.value() == episode) return &item;
    }
    return nullptr;
}

QList<DownloadRow> buildDownloadRows(const DownloadsSnapshot& snap,
                                     qint64 nowMs, qint64 maxCompletedAgeMs)
{
    using tankoban::queue::TransferState;
    QList<DownloadRow> rows;
    rows.reserve(snap.indexEntries.size());

    for (const auto& e : snap.indexEntries) {
        DownloadRow r;
        r.imdbId = e.imdbId; r.showTitle = e.imdbId; r.type = e.type;
        r.season = e.season; r.episode = e.episode;
        r.canonicalPath = e.canonicalPath; r.pct = e.progressPct;
        r.addedAt = e.addedAt;
        r.sourceGroupId = e.sourceGroupId;

        const auto* li = laneItemFor(snap.lanes, e.imdbId, e.season, e.episode);
        if (li) r.infoHash = li->transferId;

        if (e.state == StreamDownloadIndex::Entry::Complete) {
            if (maxCompletedAgeMs > 0 && nowMs - e.addedAt > maxCompletedAgeMs)
                continue;   // display trim only — the index record stays
            r.section = DownloadSection::Completed;
        } else if (li && li->state == TransferState::Failed) {
            r.section = DownloadSection::Failed;
        } else if (li && li->state == TransferState::Paused) {
            r.section = DownloadSection::Active;
            r.paused = true;
        } else if (li && li->state == TransferState::Queued) {
            r.section = DownloadSection::Queued;
        } else if (e.state == StreamDownloadIndex::Entry::Downloading
                   || (li && li->state == TransferState::Running)) {
            // Index Downloading with NO lane item is deliberate Active: after an
            // app restart resumed torrents download with an empty queue — the
            // transfer genuinely runs in the engine and progress keeps flowing
            // via updateEpisodeProgress (review I1, plan-owner decision).
            r.section = DownloadSection::Active;
        } else if (e.state == StreamDownloadIndex::Entry::Failed) {
            // Failure normally arrives via the INDEX, not the lane: TransferQueue
            // erases items on terminal states (finishCurrent/cancel), so lanes
            // snapshots never carry Failed in production — the lane-Failed branch
            // above is defensive only. Lane branches stay ABOVE this one so a
            // retry-re-queued episode (index still Failed, lane Queued/Running)
            // shows Queued/Active, not Failed. (Review C1.)
            r.section = DownloadSection::Failed;
        } else {
            r.section = DownloadSection::Queued;   // Pending, lane not visible yet
        }
        rows.append(r);
    }

    std::stable_sort(rows.begin(), rows.end(),
        [](const DownloadRow& a, const DownloadRow& b) {
            if (a.section != b.section) return a.section < b.section;
            if (a.imdbId != b.imdbId)   return a.imdbId < b.imdbId;
            if (a.season != b.season)   return a.season < b.season;
            return a.episode < b.episode;
        });
    return rows;
}

}  // namespace tankostream::stream
