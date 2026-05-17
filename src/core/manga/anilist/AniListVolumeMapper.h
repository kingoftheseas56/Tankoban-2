// src/core/manga/anilist/AniListVolumeMapper.h
#pragma once

#include "AniListTypes.h"

#include <QList>

namespace tankoban::manga::anilist {

// Pure-function helper. Given a MediaDetail (AniList metadata + chapter
// list), produces a list of VolumeRow that the series view renders.
//
// Mapping strategy (since AniList does not expose per-chapter volume
// binding via the public GraphQL schema as of 2026-05):
//
//   if totalVolumes > 0 AND totalChapters > 0:
//     - chapters-per-bound-vol = totalChapters / totalVolumes (integer floor)
//     - vols 1..totalVolumes get `chapters-per-bound-vol` chapters each, in order
//     - if there are MORE chapters than totalVolumes * chapters-per-vol,
//       the residual goes into a single Vol X at the end
//     - ONLY for status == "RELEASING" (ongoing) do we create Vol X. For
//       FINISHED series we cap at totalVolumes (any extra chapters are
//       considered data noise and squeezed into the last bound vol).
//
//   if totalVolumes == 0 AND totalChapters == 0:
//     - empty result (series has no metadata yet)
//
//   if totalVolumes == 0 AND chapters.size() > 0:
//     - all chapters go into a single Vol X (pure-tail series)
//
// The mapper is pure: no I/O, no Qt UI, no logging. Thread-safe by
// construction (no shared state).
class AniListVolumeMapper
{
public:
    static QList<VolumeRow> map(const MediaDetail& detail);
    static QList<VolumeRow> map(const MediaDetail& detail,
                                int overrideVolumeCount,
                                int overrideChapterCount);

    // Exposed for unit tests: extract the numeric prefix from a chapter
    // number string. "12" -> 12; "12.5" -> 12; "Prologue 1" -> 1; ""
    // returns -1.
    static int extractChapterNumeric(const QString& chapterNumber);
};

} // namespace tankoban::manga::anilist
