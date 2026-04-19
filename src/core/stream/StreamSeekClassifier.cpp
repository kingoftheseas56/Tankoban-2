#include "StreamSeekClassifier.h"

#include <QtGlobal>

namespace StreamSeekClassifier {

qint64 containerMetadataStart(qint64 fileSize)
{
    if (fileSize <= 0) return 0;

    // Stremio `priorities.rs:16-20`:
    //   file_size.saturating_sub(10 MB).min(file_size * 95 / 100)
    //
    // saturating_sub means "clamp at 0 instead of underflowing" — for files
    // smaller than 10 MB the first term is 0. The `.min` then picks the
    // earlier-starting threshold: for a 100 MB file, 95 % = 95 MB, minus
    // 10 MB = 90 MB, so 90 MB wins; for a 1 GB file, 95 % = 950 MB, minus
    // 10 MB = 1014 MB, so 950 MB wins.
    constexpr qint64 kTenMb = 10LL * 1024 * 1024;
    const qint64 minusTenMb = (fileSize > kTenMb) ? (fileSize - kTenMb) : 0;
    const qint64 ninetyFivePct = (fileSize / 100) * 95;
    return qMin(minusTenMb, ninetyFivePct);
}

StreamSeekType classifySeek(qint64 targetByteOffset,
                            qint64 fileSize,
                            bool isFirstClassification)
{
    // Cold-open first read always maps to InitialPlayback so the caller can
    // drive Stremio's URGENT tier (0 ms deadline, MIN..MAX_STARTUP_PIECES
    // window) on the first piece. Subsequent reads take the offset-based
    // path.
    if (isFirstClassification && targetByteOffset == 0) {
        return StreamSeekType::InitialPlayback;
    }

    if (fileSize <= 0) return StreamSeekType::Sequential;

    const qint64 threshold = containerMetadataStart(fileSize);
    if (threshold > 0 && targetByteOffset >= threshold) {
        return StreamSeekType::ContainerMetadata;
    }

    // Every non-first classification that isn't in the tail region is either
    // Sequential (the caller passes the same offset as last time) or a
    // UserScrub (the caller passes a new offset). The classifier itself
    // can't distinguish those without last-offset state; that's the caller's
    // job. We conservatively return UserScrub here — StreamSession resolves
    // the Sequential / UserScrub split by comparing against its cached
    // `lastPlaybackByteOffset`.
    return StreamSeekType::UserScrub;
}

}  // namespace StreamSeekClassifier
