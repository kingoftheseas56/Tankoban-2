// src/core/manga/anilist/AniListVolumeMapper.cpp
#include "AniListVolumeMapper.h"

#include <QRegularExpression>
#include <algorithm>

namespace tankoban::manga::anilist {

int AniListVolumeMapper::extractChapterNumeric(const QString& chapterNumber)
{
    static const QRegularExpression numRe(QStringLiteral("(\\d+)"));
    const auto m = numRe.match(chapterNumber);
    if (!m.hasMatch()) return -1;
    bool ok = false;
    const int n = m.captured(1).toInt(&ok);
    return ok ? n : -1;
}

QList<VolumeRow> AniListVolumeMapper::map(const MediaDetail& detail)
{
    QList<VolumeRow> out;
    QList<AniListChapter> input = detail.chapters;
    if (input.isEmpty()) {
        if (detail.totalChapters <= 0) {
            // Bug 4 fix (root cause: AniList returns totalChapters=0 +
            // totalVolumes=0 for very-long-running ongoing series like
            // One Piece / Bleach / Naruto where the "chapters" count
            // field isn't kept current). Without this fallback the
            // table renders zero rows for those series. Synthesize a
            // single placeholder Vol X so the user sees the series is
            // recognized + can drill into Sources (nyaa runtime works
            // even without a chapter list; WeebCentral packer needs
            // chapters and skips politely if chapterCount=0).
            const bool isOngoing =
                detail.preview.status == QStringLiteral("RELEASING") ||
                detail.preview.status == QStringLiteral("HIATUS");
            if (isOngoing) {
                VolumeRow placeholder;
                placeholder.volumeNumber      = kVolumeXNumber;
                placeholder.isVolumeX         = true;
                placeholder.chapterCount      = 0;
                placeholder.chapterRangeStart = 0;
                placeholder.chapterRangeEnd   = 0;
                out.append(placeholder);
            }
            return out;
        }
        input.reserve(detail.totalChapters);
        for (int i = 1; i <= detail.totalChapters; ++i) {
            AniListChapter ch;
            ch.number = QString::number(i);
            ch.title.clear();
            ch.boundVolume = -1;
            input.append(ch);
        }
    }

    const int chapterCount    = input.size();
    const int volumeCount     = detail.totalVolumes;
    const bool isOngoing      = (detail.preview.status == QStringLiteral("RELEASING") ||
                                  detail.preview.status == QStringLiteral("HIATUS"));

    // NOTE: detail.chapters[i].boundVolume is not read in v1 - AniList GraphQL
    // does not populate it via the public schema. We bucket by chapters/volumes
    // integer-floor heuristic instead. See header preamble for the strategy.

    // Sort chapters by numeric value so vol-bucket assignment is monotonic.
    // stable_sort: chapters whose extractChapterNumeric returns -1 (named
    // entries like "Prologue", "Extras") or duplicates ("12" + "12.5" both
    // -> 12) preserve their input order rather than getting unspecified
    // relative ordering from std::sort. Benign on the pure-integer v1
    // corpus; meaningful when curator data adds named chapters.
    QList<AniListChapter> sorted = input;
    std::stable_sort(sorted.begin(), sorted.end(),
              [](const AniListChapter& a, const AniListChapter& b) {
                  return extractChapterNumeric(a.number) <
                         extractChapterNumeric(b.number);
              });

    // Case 1: no bound vols at all -> single Vol X holds everything.
    if (volumeCount <= 0) {
        VolumeRow x;
        x.volumeNumber       = kVolumeXNumber;
        x.isVolumeX          = true;
        x.chapterCount       = chapterCount;
        x.chapterRangeStart  = extractChapterNumeric(sorted.first().number);
        x.chapterRangeEnd    = extractChapterNumeric(sorted.last().number);
        for (const auto& c : sorted) x.chapterNumbers.append(c.number);
        out.append(x);
        return out;
    }

    // Case 2: bound vols exist. Compute chapters per vol.
    const int chaptersPerVol = std::max(1, detail.totalChapters / volumeCount);

    int chapterIdx = 0;
    for (int v = 1; v <= volumeCount; ++v) {
        VolumeRow row;
        row.volumeNumber = v;
        row.isVolumeX    = false;

        // PHASE 7+: the FINISHED+leftover-stuffing branch (isLastBoundVol &&
        // !isOngoing) lacks dedicated test coverage; add a (13 vols, 113
        // chapters, FINISHED) case to AniListVolumeMapperTest if this path
        // ever lights up in production.
        const bool isLastBoundVol = (v == volumeCount);
        const int chaptersThisVol = isLastBoundVol && !isOngoing
            ? (chapterCount - chapterIdx)   // FINISHED: stuff remaining into last bound vol
            : chaptersPerVol;

        for (int n = 0; n < chaptersThisVol && chapterIdx < chapterCount; ++n) {
            row.chapterNumbers.append(sorted[chapterIdx].number);
            ++chapterIdx;
        }
        row.chapterCount       = row.chapterNumbers.size();
        if (!row.chapterNumbers.isEmpty()) {
            row.chapterRangeStart = extractChapterNumeric(row.chapterNumbers.first());
            row.chapterRangeEnd   = extractChapterNumeric(row.chapterNumbers.last());
        }

        // Per-vol art when available.
        if (v - 1 < detail.volumeArt.size()) row.art = detail.volumeArt.at(v - 1);

        out.append(row);
    }

    // Case 3: ongoing + leftover chapters past the bound vols -> Vol X.
    if (isOngoing && chapterIdx < chapterCount) {
        VolumeRow x;
        x.volumeNumber       = kVolumeXNumber;
        x.isVolumeX          = true;
        for (int i = chapterIdx; i < chapterCount; ++i) {
            x.chapterNumbers.append(sorted[i].number);
        }
        x.chapterCount       = x.chapterNumbers.size();
        if (!x.chapterNumbers.isEmpty()) {
            x.chapterRangeStart = extractChapterNumeric(x.chapterNumbers.first());
            x.chapterRangeEnd   = extractChapterNumeric(x.chapterNumbers.last());
        }
        out.append(x);
    }

    return out;
}

} // namespace tankoban::manga::anilist
