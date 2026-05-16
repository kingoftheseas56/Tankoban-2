// src/core/manga/PremiumCoverExtractor.h
//
// TANKOYOMI_PREMIUM Phase 10 -- off-thread cover extractor for Premium volume
// cbz files. Each request runs on QThreadPool::globalInstance() via a
// QRunnable; results are dispatched back to the requester's thread via
// QMetaObject::invokeMethod(Qt::QueuedConnection).
//
// Per Codex section 21 the cover extraction does NOT gate volumeCompleted --
// a volume with a valid cbz and no cover thumbnail is still readable. The
// caller (UI) falls back to the series-level poster when coverFailed is
// emitted (or when no cover ever arrives for an otherwise-completed volume).
//
// Lookup heuristic per Codex section 21:
//   1. catalog coverPageHint.entryName if present and found in the archive
//   2. "cover.*" or "folder.*" basename
//   3. natural-sort first image bypassing junk needles
//      ("credit", "scan", "blank", "advert", "back", "spread")
//   4. else coverFailed
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace tankoban::manga::premium {

class PremiumCoverExtractor : public QObject
{
    Q_OBJECT
public:
    explicit PremiumCoverExtractor(QObject* parent = nullptr);
    ~PremiumCoverExtractor() override;

    // Output: <outputDir>/premium_<seriesId>_v<NN>.jpg (zero-padded NN).
    // precomputedImageEntries may be empty -- the runnable walks the archive
    // itself in that case (Phase 4 validator's imageEntries are not currently
    // threaded through to finalizeCompletion's scope, so the extractor defends
    // by re-walking).
    void extract(const QString&     cbzPath,
                 const QString&     seriesId,
                 int                volumeNumber,
                 const QString&     outputDir,
                 const QString&     coverPageHintEntryName,
                 const QStringList& precomputedImageEntries);

signals:
    void coverReady(QString seriesId, int volumeNumber, QString coverPath);
    void coverFailed(QString seriesId, int volumeNumber, QString reason);
};

} // namespace tankoban::manga::premium
