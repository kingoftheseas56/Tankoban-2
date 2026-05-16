// src/core/manga/anilist/AniListTypes.h
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace tankoban::manga::anilist {

// Single chapter as AniList tracks it. AniList exposes chapter metadata on
// Media via the `chapters` count (when available) but per-chapter detail
// (number + title + bound-volume) is sparser. For series with explicit
// per-chapter volume mappings we use those; for others we use the
// series-level `volumes` count as the binding boundary heuristic.
struct AniListChapter {
    QString number;           // string to allow "12.5" half-chapters
    QString title;            // can be empty
    int     boundVolume = -1; // -1 when unbound; 1..N when bound to a vol
};

// Per-volume art reference. AniList exposes `Media.coverImage` (series-
// level) but per-volume art lives on related Volume records (not always
// populated by the database). When per-volume art is unavailable, fall
// back to series-level coverImage.
struct AniListVolumeArt {
    QString thumbnailUrl;  // 256px-ish, fast for grid rendering
    QString fullUrl;       // higher-res; used in the detail-view hero
};

// AniList Media node, slimmed to fields we use.
struct MediaPreview {
    int         anilistId      = 0;
    QString     title;             // primary display title
    QStringList alternateTitles;   // romaji + native + synonyms (English-first ordering applied client-side)
    QString     coverThumbUrl;     // small cover for search-result tile
    QString     coverFullUrl;      // larger cover for detail-view hero
    QString     bannerUrl;         // wide banner for detail-view hero; may be empty
    QString     format;            // "MANGA" / "MANHWA" / "MANHUA" / "ONE_SHOT" / "NOVEL"
    QString     status;            // "FINISHED" / "RELEASING" / "HIATUS" / "CANCELLED" / "NOT_YET_RELEASED"
    int         yearStarted       = 0;
    QStringList genres;
    QString     description;       // raw HTML/BBCode; stripped to plain text by display layer
};

// Full series detail. Includes per-chapter binding info needed by
// AniListVolumeMapper.
struct MediaDetail {
    MediaPreview            preview;
    int                     totalChapters = 0;  // 0 when unknown / ongoing
    int                     totalVolumes  = 0;  // 0 when unknown / ongoing
    QList<AniListChapter>   chapters;            // sorted ascending by chapter number
    QList<AniListVolumeArt> volumeArt;           // optional, indexed by vol number - 1; empty entries = use series cover
    qint64                  fetchedAtMs   = 0;  // for cache freshness checks
};

// Output of AniListVolumeMapper. The series view renders one row per
// VolumeRow.
struct VolumeRow {
    int                     volumeNumber      = 0;     // 1..N for bound vols; sentinel kVolumeXNumber for the un-bound tail
    bool                    isVolumeX         = false; // true when this row is the synthesized Vol X
    int                     chapterRangeStart = 0;     // first chapter number (numeric extract) in this vol
    int                     chapterRangeEnd   = 0;     // last chapter number in this vol
    int                     chapterCount      = 0;     // count of AniListChapter entries in this vol
    QStringList             chapterNumbers;            // the raw chapterNumber strings (preserves "12.5" etc.)
    AniListVolumeArt        art;                       // per-vol art when available; else fall back to series cover
};

// Sentinel for VolumeRow.volumeNumber when isVolumeX is true. Chosen as
// a high integer so any normal vol comparison still works.
constexpr int kVolumeXNumber = 99999;

} // namespace tankoban::manga::anilist
