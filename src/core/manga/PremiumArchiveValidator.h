// src/core/manga/PremiumArchiveValidator.h
//
// TANKOYOMI_PREMIUM Phase 4 -- pre-finalization archive validator for the
// .tankoban-part file produced by TorrentVolumeProvider::finalizeCompletion.
// Enforces Codex section 24 v1 trust requirements:
//   - .cbz extension only (no rar/7z/tar/nested archives)
//   - every entry is an image (no executables, no nested archives, no scripts)
//   - page count within absolute bound and within tolerance of catalog hint
//   - bounded first-image decompression as a zip-bomb defense
//
// Phase 10 will reuse the imageEntries list (in scanner-natural sort order)
// for cover extraction.
#pragma once

#include <QString>
#include <QStringList>
#include <cstdint>

namespace tankoban::manga::premium {

enum class ArchiveValidationCode {
    Ok,
    NotCbzExtension,
    OpenFailed,
    Empty,
    NonImageEntry,
    NestedArchiveEntry,
    ExecutableEntry,
    PageCountExceedsBound,
    PageCountMismatchCatalog,
    DecompressedFirstImageTooLarge,
    ReadFailed
};

struct ArchiveValidationResult {
    ArchiveValidationCode  code        = ArchiveValidationCode::Ok;
    QString                detail;
    int                    pageCount   = 0;
    QStringList            imageEntries; // in scanner-natural sort order, used by Phase 10 cover extractor
};

// Pre-finalization validator for Premium-downloaded cbz files. Bounds:
//   - kMaxPagesPerVolume   = 1000 (validator hard rejects above; catalog warns above 600)
//   - kMaxFirstImageBytes  = 64 MiB (zip-bomb defense per Codex section 24.5)
//   - kMaxEntryCount       = 2000
//
// `expectedPageCount` is 0 when the catalog did not provide a hint; the
// validator then only enforces the absolute bound. When non-zero, validator
// fails on a mismatch outside [-3, +3] tolerance.
class PremiumArchiveValidator
{
public:
    static constexpr int    kMaxPagesPerVolume         = 1000;
    static constexpr qint64 kMaxFirstImageBytes        = 64LL * 1024 * 1024;
    static constexpr int    kMaxEntryCount             = 2000;
    static constexpr int    kCatalogPageCountTolerance = 3;

    static ArchiveValidationResult validate(const QString& cbzPath,
                                            int expectedPageCount = 0);
};

} // namespace tankoban::manga::premium
