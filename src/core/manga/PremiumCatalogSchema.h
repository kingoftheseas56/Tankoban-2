// src/core/manga/PremiumCatalogSchema.h
#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QVector>
#include <cstdint>

namespace tankoban::manga::premium {

// One chapter reference inside a volume. Canonical chapter key is derived
// at lookup time via CanonicalChapterKey::make(seriesId, chapterNumber).
struct PremiumChapterRef {
    QString chapterNumber;  // string to allow "12.5" half-chapters
    QString title;
};

// One volume inside a catalog entry. fileIndex / pieceStart / pieceEnd are
// captured at catalog-build time by the helper tool in Phase 2.
struct PremiumVolumeEntry {
    int                       vol           = 0;
    int                       fileIndex     = -1;     // index into the torrent's file list
    qint64                    fileSizeBytes = 0;
    int                       pieceStart    = -1;     // inclusive piece index where the file begins
    int                       pieceEnd      = -1;     // inclusive piece index where the file ends
    QString                   cbzFileName;            // the cbz's basename inside the torrent
    QString                   boundaryPolicy;         // "allow-piece-overlap" for v1
    int                       pageCount     = 0;      // catalog-provided; validator cross-checks
    QString                   coverPageHint;          // optional entryName preference for cover extraction
    QList<PremiumChapterRef>  chapters;               // chapters contained in this volume
};

// Forward-compat manifest fields, inspired by Stremio addon manifest shape
// per brainstorm section 24. Bundled-only in v1; community-catalog gating
// (signing + curator review) happens in v1.x.
struct PremiumCatalogManifest {
    QString id;             // catalog file id, e.g. "tankoyomi_premium_2026-05"
    QString name;
    QString version;        // semver string
    QString description;
    QString contact;
    bool    behaviorHintsP2P    = true;  // truthful: this is a P2P-backed source
    bool    behaviorHintsAdult  = false; // never true for v1
};

// One series entry.
struct PremiumCatalogEntry {
    QString                   seriesId;          // stable lowercase slug
    QString                   title;             // primary display title
    QStringList               alternateTitles;   // for dedup / matching
    int                       anilistId      = 0;
    QString                   status;            // "completed" | "ongoing"
    QString                   magnetUri;
    QString                   expectedInfoHash;  // bittorrent infoHash (lowercase hex);
                                                 // validator rejects mismatch
    QString                   trustedUploader;
    QString                   releaseEdition;    // e.g. "VIZ Digital", "1r0n", "Hox"
    QString                   format;            // v1 supports "one-cbz-per-volume" only
    QList<PremiumVolumeEntry> volumes;
    QString                   postCoverageWeebcentralSlug;  // empty for completed series
    int                       postCoverageStartsAfterVolume = 0;
};

// Validation severity per brainstorm section 20.
enum class ValidationSeverity {
    Warn,           // log + keep the entry usable
    RejectVolume,   // drop the offending volume from the entry; keep series
    RejectSeries,   // drop the series; keep other series in the file
    RejectFile      // drop the entire catalog file; keep other files
};

struct ValidationDiagnostic {
    QString                catalogFile;
    QString                seriesId;         // empty for file-level diagnostics
    int                    volumeNumber = 0; // 0 for non-volume diagnostics
    ValidationSeverity     severity     = ValidationSeverity::Warn;
    QString                code;             // e.g. "missing_magnet_uri", "infohash_mismatch"
    QString                message;
};

struct CatalogLoadResult {
    PremiumCatalogManifest          manifest;
    QList<PremiumCatalogEntry>      entries;
    QList<ValidationDiagnostic>     diagnostics;
};

} // namespace tankoban::manga::premium
