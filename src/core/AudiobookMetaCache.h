#pragma once

#include <QString>
#include <QHash>
#include <QMutex>

// AudiobookMetaCache — per-folder audio duration cache for audiobook chapters
// (AUDIOBOOK_PAIRED_READING_FIX Phase 1.1).
//
// Responsibility: given an audiobook folder + one of its chapter audio files,
// return the duration in milliseconds. First call per file probes via
// resources/ffmpeg_sidecar/ffprobe.exe and persists into the folder's
// `.audiobook_meta.json` sidecar; subsequent calls use the cached value unless
// the file's mtime is newer than the cached entry's mtime (selective re-probe).
//
// Wrapper-folder audiobooks (Phase 1.2): `audioFilePath` may live in a subdir
// of `folderPath` — the cache key stores the relative path from folderPath so
// `{"0.5 Edgedancer/01.mp3": {...}}` coexists with
// `{"1 The Way Of Kings/01.mp3": {...}}` in the same `.audiobook_meta.json`.
//
// All public methods are thread-safe. The probe subprocess runs with a 15 s
// per-call timeout — sufficient for even large .m4b files; returns -1 on
// timeout, missing ffprobe binary, JSON parse failure, or zero/negative
// duration. Callers treat -1 as "unknown" and fall back to chapter count.
//
// Cache file shape (per audiobook folder, `<folder>/.audiobook_meta.json`):
//   {
//     "schemaVersion": 1,
//     "chapters": {
//       "<relpath>": { "durationMs": N, "mtimeMs": M },
//       ...
//     },
//     "updatedAt": <ms since epoch>
//   }
class AudiobookMetaCache {
public:
    // Returns duration in ms, or -1 if unavailable. Reads cache if fresh,
    // otherwise subprocess-probes ffprobe and updates cache. `audioFilePath`
    // must be absolute. `folderPath` is the audiobook folder (may be a
    // wrapper folder; audioFilePath may live in a subdir of folderPath).
    static qint64 durationMsFor(const QString& folderPath,
                                const QString& audioFilePath);

    // Path to the bundled ffprobe.exe resolved relative to QCoreApplication::
    // applicationDirPath(). Returns empty string if not found.
    static QString ffprobePath();

    // Forces a cache rebuild for `folderPath` on next durationMsFor call.
    // Used by unit tests; also safe to call if the user replaces audio files.
    static void invalidateFolder(const QString& folderPath);

    // Test-only: overrides the ffprobe resolution path. Pass empty string to
    // restore default resolution. Returns previous override (empty if none).
    static QString setFfprobePathOverrideForTest(const QString& override);

private:
    // Synchronous subprocess probe — no caching, no locking. Returns ms or -1.
    static qint64 probeDurationMsRaw(const QString& audioFilePath);

    static QString cacheFilePath(const QString& folderPath);

    struct FolderCache {
        QHash<QString, qint64> durationsMs;  // relpath-from-folder → ms
        QHash<QString, qint64> mtimesMs;     // relpath-from-folder → mtime at probe time
    };

    static FolderCache loadCache(const QString& folderPath);
    static void saveCache(const QString& folderPath, const FolderCache& cache);

    // Serializes cache-file access. Coarse but safe: the cost is dominated by
    // the ffprobe subprocess, and the mutex is released during the probe.
    static QMutex s_mutex;
    static QString s_ffprobeOverride;  // test-only, guarded by s_mutex
};
