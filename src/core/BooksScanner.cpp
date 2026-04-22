#include "BooksScanner.h"
#include "AudiobookMetaCache.h"
#include "ScannerUtils.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImage>
#include <QCryptographicHash>
#include <QCollator>
#include <algorithm>

#ifdef HAS_QT_ZIP
#include <QtCore/private/qzipreader_p.h>
#endif

const QStringList BooksScanner::BOOK_EXTS = {
    "*.epub", "*.pdf", "*.mobi", "*.fb2", "*.azw3", "*.djvu", "*.txt"
};

const QStringList BooksScanner::AUDIO_EXTS = {
    "*.mp3", "*.m4a", "*.m4b", "*.aac", "*.flac", "*.ogg", "*.opus", "*.wav", "*.wma"
};

static const QStringList AUDIOBOOK_COVER_NAMES = {
    "cover.jpg", "cover.jpeg", "cover.png",
    "folder.jpg", "folder.jpeg", "folder.png",
    "front.jpg", "front.jpeg", "front.png"
};

static QString findAudiobookCover(const QDir& dir)
{
    for (const QString& name : AUDIOBOOK_COVER_NAMES) {
        if (dir.exists(name))
            return dir.absoluteFilePath(name);
    }
    for (const auto& entry : dir.entryInfoList({"*.jpg", "*.jpeg", "*.png"}, QDir::Files)) {
        return entry.absoluteFilePath();
    }
    return {};
}

static constexpr int THUMB_W = 240;
static constexpr int THUMB_H = 369;  // int(240 / 0.65) — matches groundwork aspect ratio

static QByteArray extractEpubCover(const QString& epubPath)
{
#ifdef HAS_QT_ZIP
    QZipReader zip(epubPath);
    if (!zip.exists())
        return {};

    // Look for common cover image paths in EPUB
    static const QStringList coverNames = {
        "cover.jpg", "cover.jpeg", "cover.png",
        "OEBPS/cover.jpg", "OEBPS/cover.jpeg", "OEBPS/cover.png",
        "OEBPS/images/cover.jpg", "OEBPS/images/cover.jpeg", "OEBPS/images/cover.png",
        "OPS/cover.jpg", "OPS/cover.jpeg", "OPS/cover.png",
        "OPS/images/cover.jpg", "OPS/images/cover.jpeg", "OPS/images/cover.png",
        "images/cover.jpg", "images/cover.jpeg", "images/cover.png",
    };

    const auto entries = zip.fileInfoList();

    // Try exact cover names first
    for (const auto& name : coverNames) {
        for (const auto& entry : entries) {
            if (entry.filePath.compare(name, Qt::CaseInsensitive) == 0) {
                QByteArray data = zip.fileData(entry.filePath);
                if (data.size() > 100)
                    return data;
            }
        }
    }

    // Fall back to any image file with "cover" in the name
    for (const auto& entry : entries) {
        if (entry.isDir) continue;
        QString lower = entry.filePath.toLower();
        if (lower.contains("cover") &&
            (lower.endsWith(".jpg") || lower.endsWith(".jpeg") ||
             lower.endsWith(".png") || lower.endsWith(".webp"))) {
            QByteArray data = zip.fileData(entry.filePath);
            if (data.size() > 100)
                return data;
        }
    }

    // Fall back to basename starting with "folder."
    for (const auto& entry : entries) {
        if (entry.isDir) continue;
        QString lower = entry.filePath.toLower();
        QString base = lower.mid(lower.lastIndexOf('/') + 1);
        if (base.startsWith("folder.") &&
            (lower.endsWith(".jpg") || lower.endsWith(".jpeg") ||
             lower.endsWith(".png") || lower.endsWith(".webp"))) {
            QByteArray data = zip.fileData(entry.filePath);
            if (data.size() > 100)
                return data;
        }
    }

    // Fall back to any image with "front" in the path
    for (const auto& entry : entries) {
        if (entry.isDir) continue;
        QString lower = entry.filePath.toLower();
        if (lower.contains("front") &&
            (lower.endsWith(".jpg") || lower.endsWith(".jpeg") ||
             lower.endsWith(".png") || lower.endsWith(".webp"))) {
            QByteArray data = zip.fileData(entry.filePath);
            if (data.size() > 100)
                return data;
        }
    }

    // Fall back to first image in the archive
    for (const auto& entry : entries) {
        if (entry.isDir) continue;
        QString lower = entry.filePath.toLower();
        if (lower.endsWith(".jpg") || lower.endsWith(".jpeg") ||
            lower.endsWith(".png") || lower.endsWith(".webp")) {
            QByteArray data = zip.fileData(entry.filePath);
            if (data.size() > 100)
                return data;
        }
    }
#else
    Q_UNUSED(epubPath);
#endif
    return {};
}

BooksScanner::BooksScanner(const QString& thumbsDir, QObject* parent)
    : QObject(parent)
    , m_thumbsDir(thumbsDir)
{
    QDir().mkpath(m_thumbsDir);
}

void BooksScanner::scan(const QStringList& bookRoots, const QStringList& audiobookRoots)
{
    QCollator collator;
    collator.setNumericMode(true);

    // ── Scan books ── group by first-level subdirectory under each root
    QMap<QString, QStringList> seriesMap =
        ScannerUtils::groupByFirstLevelSubdir(bookRoots, BOOK_EXTS);

    QList<BookSeriesInfo> allBooks;

    for (auto it = seriesMap.begin(); it != seriesMap.end(); ++it) {
        QString seriesPath = it.key();
        QStringList& files = it.value();

        std::sort(files.begin(), files.end(), [&collator](const QString& a, const QString& b) {
            return collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
        });

        // Strip loose files marker
        bool isLoose = seriesPath.endsWith("::LOOSE");
        if (isLoose)
            seriesPath = seriesPath.chopped(7);

        BookSeriesInfo info;
        info.seriesName = ScannerUtils::cleanMediaFolderTitle(QDir(seriesPath).dirName());
        info.seriesPath = seriesPath;
        info.fileCount = files.size();

        qint64 newest = 0;
        for (const auto& f : files) {
            qint64 mt = QFileInfo(f).lastModified().toMSecsSinceEpoch();
            if (mt > newest) newest = mt;
        }
        info.newestMtimeMs = newest;

        // Thumbnail cache check
        QString hash = QString(QCryptographicHash::hash(
            seriesPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
        QString thumbPath = m_thumbsDir + "/" + hash + ".jpg";

        if (QFile::exists(thumbPath)) {
            info.coverThumbPath = thumbPath;
        } else {
            // Try to extract cover from first EPUB
            for (const auto& file : files) {
                if (file.endsWith(".epub", Qt::CaseInsensitive)) {
                    QByteArray coverData = extractEpubCover(file);
                    if (!coverData.isEmpty()) {
                        QImage img;
                        if (img.loadFromData(coverData)) {
                            QImage scaled = img.scaled(THUMB_W, THUMB_H,
                                                       Qt::KeepAspectRatioByExpanding,
                                                       Qt::SmoothTransformation);
                            int x = (scaled.width() - THUMB_W) / 2;
                            int y = (scaled.height() - THUMB_H) / 2;
                            QImage cropped = scaled.copy(x, y, THUMB_W, THUMB_H);
                            if (cropped.save(thumbPath, "JPEG", 85))
                                info.coverThumbPath = thumbPath;
                        }
                    }
                    break;
                }
            }
        }

        allBooks.append(info);
        emit bookSeriesFound(info);
    }

    // ── Scan audiobooks ──
    //
    // Max/Groundwork parity: recursive walk where each folder containing direct
    // audio files becomes one audiobook. Handles nested series layouts like
    // /Root/Series Wrapper/Book A/*.mp3 where the wrapper has no direct audio
    // but each leaf does — each leaf registers separately, wrapper is skipped.
    //
    // Cross-domain: we also walk bookRoots for audio content. Matches Max's
    // "drop an audiobook folder anywhere in your library and it's discovered"
    // behavior. Users with a dedicated audiobook root still get prioritized
    // coverage; users who organize everything under one book root also work.

    QList<AudiobookInfo> allAudiobooks;
    QSet<QString> seenAudiobookPaths;

    QStringList audiobookScanRoots = audiobookRoots;
    for (const QString& br : bookRoots) {
        if (!audiobookScanRoots.contains(br))
            audiobookScanRoots.append(br);
    }

    for (const auto& root : audiobookScanRoots) {
        walkAudiobooks(QDir(root), collator, allAudiobooks, seenAudiobookPaths, 6);
    }

    emit scanFinished(allBooks, allAudiobooks);
}

namespace {

// Returns true if `dir` contains at least one direct audio file (non-recursive).
bool hasDirectAudio(const QDir& dir, const QStringList& exts)
{
    return !dir.entryInfoList(exts, QDir::Files).isEmpty();
}

}  // namespace

void BooksScanner::walkAudiobooks(const QDir& dir,
                                  const QCollator& collator,
                                  QList<AudiobookInfo>& out,
                                  QSet<QString>& seenPaths,
                                  int maxDepth)
{
    if (maxDepth < 0) return;
    if (ScannerUtils::isIgnoredDir(dir.dirName())) return;

    const QString absPath = dir.absolutePath();
    if (seenPaths.contains(absPath)) return;

    // Case A — this folder has direct audio files. It IS a leaf audiobook.
    // Don't descend (leaves don't contain wrappers in our model).
    QFileInfoList audioEntries = dir.entryInfoList(AUDIO_EXTS, QDir::Files);
    if (!audioEntries.isEmpty()) {
        seenPaths.insert(absPath);

        QStringList tracks;
        tracks.reserve(audioEntries.size());
        for (const auto& fi : audioEntries)
            tracks.append(fi.absoluteFilePath());

        std::sort(tracks.begin(), tracks.end(),
                  [&collator](const QString& a, const QString& b) {
                      return collator.compare(QFileInfo(a).fileName(),
                                              QFileInfo(b).fileName()) < 0;
                  });

        AudiobookInfo ab;
        ab.name = ScannerUtils::cleanMediaFolderTitle(dir.dirName());
        ab.path = absPath;
        ab.trackCount = tracks.size();
        ab.tracks = tracks;
        ab.coverPath = findAudiobookCover(dir);

        // Phase 1.3 — populate per-chapter + total duration via ffprobe
        // (cache-first; first scan on a fresh pack probes each file, cached
        // into .audiobook_meta.json in the folder so subsequent scans hit
        // in microseconds).
        qint64 sum = 0;
        for (const QString& trackPath : tracks) {
            const qint64 ms = AudiobookMetaCache::durationMsFor(absPath, trackPath);
            if (ms > 0) sum += ms;
        }
        ab.totalDurationMs = sum;

        out.append(ab);
        emit audiobookFound(ab);
        return;
    }

    // Case B — no direct audio. Inspect immediate subdirs and classify.
    // AUDIOBOOK_PAIRED_READING_FIX Phase 1.2: wrapper-flatten detection.
    // A folder whose immediate subdirs are ALL leaf-audiobooks (each has
    // direct audio) AND has NO non-leaf siblings is treated as a "wrapper"
    // — emits ONE AudiobookInfo with chapters natural-sorted across all
    // leaf subdirs. This is Hemanth's override of Max-parity: "the folder I
    // downloaded" should be one tile even if it has sub-folders internally
    // (e.g. Stormlight Archive 0.5-4/{0.5 Edgedancer, 1 Way of Kings, ...}).
    QFileInfoList subEntries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);

    QList<QDir> leafSubdirs;
    QList<QDir> nonLeafSubdirs;
    for (const auto& sub : subEntries) {
        if (ScannerUtils::isIgnoredDir(sub.fileName())) continue;
        QDir subDir(sub.absoluteFilePath());
        if (hasDirectAudio(subDir, AUDIO_EXTS))
            leafSubdirs.append(subDir);
        else
            nonLeafSubdirs.append(subDir);
    }

    // Sort leaves naturally so chapters appear in volume order.
    std::sort(leafSubdirs.begin(), leafSubdirs.end(),
              [&collator](const QDir& a, const QDir& b) {
                  return collator.compare(a.dirName(), b.dirName()) < 0;
              });

    // Case B1 — wrapper pattern: ≥2 leaf subdirs AND no non-leaf siblings.
    // Emit ONE AudiobookInfo; claim all leaves' audio as this audiobook's
    // flat chapter list. Do NOT recurse into the claimed leaves.
    if (leafSubdirs.size() >= 2 && nonLeafSubdirs.isEmpty()) {
        seenPaths.insert(absPath);

        AudiobookInfo ab;
        ab.name = ScannerUtils::cleanMediaFolderTitle(dir.dirName());
        ab.path = absPath;
        ab.coverPath = findAudiobookCover(dir);

        QStringList tracks;
        QString fallbackCover;
        for (const QDir& leafDir : leafSubdirs) {
            const QString leafAbs = leafDir.absolutePath();
            seenPaths.insert(leafAbs);

            QFileInfoList leafAudio =
                leafDir.entryInfoList(AUDIO_EXTS, QDir::Files);
            for (const auto& fi : leafAudio)
                tracks.append(fi.absoluteFilePath());

            // Capture first-leaf's cover as a fallback if the wrapper itself
            // has no cover.
            if (fallbackCover.isEmpty())
                fallbackCover = findAudiobookCover(leafDir);
        }

        // Natural-sort the full track list using paths RELATIVE to the
        // wrapper so `0.5 Edgedancer/01.mp3` < `1 The Way Of Kings/01.mp3`
        // (QCollator sorts the subdir component first, then the filename
        // within the subdir — the natural order users expect).
        const QDir wrapperDir(absPath);
        std::sort(tracks.begin(), tracks.end(),
                  [&collator, &wrapperDir](const QString& a, const QString& b) {
                      return collator.compare(wrapperDir.relativeFilePath(a),
                                              wrapperDir.relativeFilePath(b)) < 0;
                  });

        if (ab.coverPath.isEmpty())
            ab.coverPath = fallbackCover;

        ab.tracks = tracks;
        ab.trackCount = tracks.size();

        // Phase 1.3 — populate total duration for the wrapper (sum across
        // all chapters spanning all leaf subdirs). Cache keys use relative
        // paths from the wrapper folder, so cross-subdir chapters coexist
        // in the single `.audiobook_meta.json` written at wrapper level.
        qint64 wrapperSum = 0;
        for (const QString& trackPath : tracks) {
            const qint64 ms = AudiobookMetaCache::durationMsFor(absPath, trackPath);
            if (ms > 0) wrapperSum += ms;
        }
        ab.totalDurationMs = wrapperSum;

        out.append(ab);
        emit audiobookFound(ab);
        return;
    }

    // Case B2 — not a wrapper. Recurse into leaf subdirs (each becomes its
    // own standalone AudiobookInfo via Case A) + non-leaf subdirs (to find
    // deeper leaves or nested wrappers).
    for (const QDir& leafDir : leafSubdirs) {
        walkAudiobooks(leafDir, collator, out, seenPaths, maxDepth - 1);
    }
    for (const QDir& subDir : nonLeafSubdirs) {
        walkAudiobooks(subDir, collator, out, seenPaths, maxDepth - 1);
    }
}
