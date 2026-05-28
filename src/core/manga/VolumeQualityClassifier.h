#pragma once
#include <QList>
#include <utility>
#include "MangaCatalogTypes.h"        // MangaVolume
#include "MangaResult.h"              // ChapterInfo
#include "anilist/AniListTypes.h"     // kVolumeXNumber

namespace tankoban::manga {

enum class VolumeQuality { Clean, Magazine };

struct ClassifiedVolume {
    int           volumeNumber = 0;     // catalog number; kVolumeXNumber for the tail bucket
    bool          isVolumeX    = false;
    VolumeQuality quality      = VolumeQuality::Clean;
    QList<double> chapterNumbers;        // member chapters, ascending (for compilation)
};

// [rangeStart, rangeEnd] integer span covering a Volume X bucket's member
// chapters: floor(min) .. ceil(max). {0,0} when the list is empty. Used to
// drive MangaWeebCentralResolver::resolveChapterRange for the un-bound tail.
std::pair<int, int> volumeXChapterSpan(const QList<double>& chapterNumbers);

class VolumeQualityClassifier {
public:
    // One ClassifiedVolume per catalog volume that has >=1 member chapter,
    // ascending by volumeNumber, plus a trailing Volume X (isVolumeX,
    // volumeNumber=kVolumeXNumber) if any chapters fall past the last catalog
    // volume's chapterRangeEnd. Clean iff ALL member chapters are volume-scanned.
    static QList<ClassifiedVolume> classify(
        const QList<MangaVolume>& catalogVolumes,
        const QList<ChapterInfo>& chapters);
};

} // namespace tankoban::manga
