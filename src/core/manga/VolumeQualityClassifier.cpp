#include "VolumeQualityClassifier.h"
#include <algorithm>
#include <cmath>

namespace tankoban::manga {

std::pair<int, int> volumeXChapterSpan(const QList<double>& chapterNumbers)
{
    if (chapterNumbers.isEmpty()) return {0, 0};
    double lo = chapterNumbers.first();
    double hi = chapterNumbers.first();
    for (double c : chapterNumbers) {
        lo = std::min(lo, c);
        hi = std::max(hi, c);
    }
    return {static_cast<int>(std::floor(lo)), static_cast<int>(std::ceil(hi))};
}

QList<ClassifiedVolume> VolumeQualityClassifier::classify(
    const QList<MangaVolume>& catalogVolumes,
    const QList<ChapterInfo>& chapters)
{
    QList<ClassifiedVolume> result;
    if (chapters.isEmpty()) return result;

    QList<MangaVolume> vols = catalogVolumes;
    std::sort(vols.begin(), vols.end(), [](const MangaVolume& a, const MangaVolume& b) {
        return a.volumeNumber < b.volumeNumber;
    });

    // A real tankobon volume carries a MangaFire cover. Auto-bucketed "fake"
    // volumes (chapters not yet collected into a tankobon — e.g. One Piece
    // "vol 116/117") have no cover; their chapters belong in Volume X, not in a
    // volume tile. Fallback: if the catalog carries NO covers at all, treat
    // every volume as real so we don't dump the whole series into Volume X.
    auto hasCover = [](const MangaVolume& v) {
        return !v.coverUrlJapanese.isEmpty() || !v.coverUrlEnglish.isEmpty();
    };
    bool anyCover = false;
    for (const auto& v : vols) if (hasCover(v)) { anyCover = true; break; }
    auto isRealVolume = [&](const MangaVolume& v) {
        return anyCover ? hasCover(v) : true;
    };

    int lastRealChapterEnd = 0;
    for (const auto& v : vols)
        if (isRealVolume(v))
            lastRealChapterEnd = std::max(lastRealChapterEnd, v.chapterRangeEnd);

    for (const auto& v : vols) {
        if (!isRealVolume(v)) continue;  // fake volume -> chapters fold into Volume X
        ClassifiedVolume cv;
        cv.volumeNumber = v.volumeNumber;
        bool allViolet = true;
        for (const auto& ch : chapters) {
            const int c = static_cast<int>(ch.chapterNumber);
            if (c >= v.chapterRangeStart && c <= v.chapterRangeEnd) {
                cv.chapterNumbers.append(ch.chapterNumber);
                if (!ch.isVolumeScanned) allViolet = false;
            }
        }
        if (cv.chapterNumbers.isEmpty()) continue;
        std::sort(cv.chapterNumbers.begin(), cv.chapterNumbers.end());
        cv.quality = allViolet ? VolumeQuality::Clean : VolumeQuality::Magazine;
        result.append(cv);
    }

    ClassifiedVolume volX;
    volX.isVolumeX = true;
    volX.volumeNumber = tankoban::manga::anilist::kVolumeXNumber;
    volX.quality = VolumeQuality::Magazine;
    for (const auto& ch : chapters) {
        if (static_cast<int>(ch.chapterNumber) > lastRealChapterEnd)
            volX.chapterNumbers.append(ch.chapterNumber);
    }
    if (!volX.chapterNumbers.isEmpty()) {
        std::sort(volX.chapterNumbers.begin(), volX.chapterNumbers.end());
        result.append(volX);
    }

    return result;
}

} // namespace tankoban::manga
