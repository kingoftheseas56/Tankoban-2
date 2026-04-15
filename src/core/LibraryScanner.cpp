#include "LibraryScanner.h"
#include "ScannerUtils.h"
#include "ArchiveReader.h"  // P4-2: CBR/RAR cover + page-count via libarchive

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImage>
#include <QBuffer>
#include <QCryptographicHash>
#include <QCollator>
#include <algorithm>

#ifdef HAS_QT_ZIP
#include <QtCore/private/qzipreader_p.h>
#endif

// P4-2: comic format dispatch — CBZ uses fast QZipReader path; CBR/RAR
// route through ArchiveReader (which already wraps libarchive transparently).
static bool isLibarchiveFormat(const QString& path)
{
    return path.endsWith(".cbr", Qt::CaseInsensitive)
        || path.endsWith(".rar", Qt::CaseInsensitive);
}

static constexpr int THUMB_W = 240;
static constexpr int THUMB_H = 369;  // int(240 / 0.65) — matches groundwork aspect ratio
static constexpr int THUMB_QUALITY = 85;

static const QStringList IMAGE_EXTS = {"jpg", "jpeg", "png", "webp", "gif", "bmp"};

static bool isImageFile(const QString& name)
{
    for (const auto& ext : IMAGE_EXTS) {
        if (name.endsWith("." + ext, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

LibraryScanner::LibraryScanner(const QString& thumbsDir, QObject* parent)
    : QObject(parent)
    , m_thumbsDir(thumbsDir)
{
    QDir().mkpath(m_thumbsDir);
}

void LibraryScanner::scan(const QStringList& rootFolders)
{
    // P4-2: enumerate all comic formats — CBZ + CBR + RAR. Engine
    // (ArchiveReader) handles all three; this scan path now matches.
    QMap<QString, QStringList> seriesMap =
        ScannerUtils::groupByFirstLevelSubdir(rootFolders, {"*.cbz", "*.cbr", "*.rar"});

    // Natural sort the files within each series
    QCollator collator;
    collator.setNumericMode(true);

    QList<SeriesInfo> allSeries;

    for (auto it = seriesMap.begin(); it != seriesMap.end(); ++it) {
        QString seriesPath = it.key();
        QStringList& files = it.value();

        std::sort(files.begin(), files.end(), [&collator](const QString& a, const QString& b) {
            return collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
        });

        // Skip loose files in Comics — avoids a ghost tile named after the
        // root folder for stray archives sitting directly at the root.
        if (seriesPath.endsWith("::LOOSE"))
            continue;

        SeriesInfo info;
        info.seriesName = ScannerUtils::cleanMediaFolderTitle(QDir(seriesPath).dirName());
        info.seriesPath = seriesPath;
        info.fileCount = files.size();

        // Compute newest modification time + per-file data
        qint64 newest = 0;
        for (const auto& f : files) {
            QFileInfo fi(f);
            qint64 mt = fi.lastModified().toMSecsSinceEpoch();
            if (mt > newest) newest = mt;

            SeriesInfo::FileEntry fe;
            fe.path = f;
            fe.mtimeMs = mt;
            fe.pageCount = countPagesInCbz(f);
            info.files.append(fe);
        }
        info.newestMtimeMs = newest;

        // Series-level thumbnail (for grid tiles)
        QString seriesHash = QString(QCryptographicHash::hash(
            seriesPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
        QString seriesThumb = m_thumbsDir + "/" + seriesHash + ".jpg";

        if (QFile::exists(seriesThumb)) {
            info.coverThumbPath = seriesThumb;
        } else {
            QByteArray coverData = extractCoverFromCbz(files.first());
            if (!coverData.isEmpty())
                info.coverThumbPath = saveThumbnail(coverData, seriesPath);
        }

        // Per-file thumbnails (for continue strip — each volume gets its own cover)
        for (const auto& cbzPath : files) {
            QFileInfo fi(cbzPath);
            QString fileKey = cbzPath + "::" + QString::number(fi.size())
                            + "::" + QString::number(fi.lastModified().toMSecsSinceEpoch());
            QString fileHash = QString(QCryptographicHash::hash(
                fileKey.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
            QString fileThumb = m_thumbsDir + "/" + fileHash + ".jpg";
            if (!QFile::exists(fileThumb)) {
                QByteArray coverData = extractCoverFromCbz(cbzPath);
                if (!coverData.isEmpty()) {
                    QImage img;
                    if (img.loadFromData(coverData)) {
                        QImage scaled = img.scaled(THUMB_W, THUMB_H,
                                                   Qt::KeepAspectRatioByExpanding,
                                                   Qt::SmoothTransformation);
                        int x = (scaled.width() - THUMB_W) / 2;
                        int y = (scaled.height() - THUMB_H) / 2;
                        QImage cropped = scaled.copy(x, y, THUMB_W, THUMB_H);
                        cropped.save(fileThumb, "JPEG", THUMB_QUALITY);
                    }
                }
            }
        }

        allSeries.append(info);
        emit seriesFound(info);
    }

    emit scanFinished(allSeries);
}

int LibraryScanner::countPagesInCbz(const QString& cbzPath)
{
    // P4-2: CBR/RAR via libarchive (ArchiveReader::pageList already filters
    // to image entries + sorts naturally — its size IS the page count).
    if (isLibarchiveFormat(cbzPath))
        return ArchiveReader::pageList(cbzPath).size();

#ifdef HAS_QT_ZIP
    QZipReader zip(cbzPath);
    if (!zip.exists())
        return 0;

    int count = 0;
    const auto entries = zip.fileInfoList();
    for (const auto& entry : entries) {
        if (!entry.isDir && isImageFile(entry.filePath))
            ++count;
    }
    return count;
#else
    Q_UNUSED(cbzPath);
    return 0;
#endif
}

QByteArray LibraryScanner::extractCoverFromCbz(const QString& cbzPath)
{
    // P4-2: CBR/RAR via libarchive — pageList is already natural-sorted +
    // filtered to image entries. Cover priority logic mirrors the CBZ path.
    if (isLibarchiveFormat(cbzPath)) {
        const QStringList pages = ArchiveReader::pageList(cbzPath);
        if (pages.isEmpty()) return {};

        // Cover priority — "cover.*" or "folder.*" basename wins
        for (const auto& name : pages) {
            QString basename = QFileInfo(name).fileName().toLower();
            if (basename.startsWith("cover.") || basename.startsWith("folder.")) {
                QByteArray data = ArchiveReader::pageData(cbzPath, name);
                if (data.size() > 100) return data;
            }
        }
        // Fallback to first sorted image
        QByteArray data = ArchiveReader::pageData(cbzPath, pages.first());
        return data.size() > 100 ? data : QByteArray{};
    }

#ifdef HAS_QT_ZIP
    QZipReader zip(cbzPath);
    if (!zip.exists())
        return {};

    // Collect image entries
    QStringList imageNames;
    const auto entries = zip.fileInfoList();
    for (const auto& entry : entries) {
        if (entry.isDir)
            continue;
        QString name = entry.filePath;
        if (isImageFile(name))
            imageNames.append(name);
    }

    if (imageNames.isEmpty())
        return {};

    // Natural sort
    QCollator collator;
    collator.setNumericMode(true);
    std::sort(imageNames.begin(), imageNames.end(), [&collator](const QString& a, const QString& b) {
        return collator.compare(a, b) < 0;
    });

    // Prioritize "cover." or "folder." files
    for (const auto& name : imageNames) {
        QString baseName = QFileInfo(name).fileName().toLower();
        if (baseName.startsWith("cover.") || baseName.startsWith("folder.")) {
            QByteArray data = zip.fileData(name);
            if (data.size() > 100)
                return data;
        }
    }

    // Fall back to first image
    QByteArray data = zip.fileData(imageNames.first());
    return data.size() > 100 ? data : QByteArray{};
#else
    Q_UNUSED(cbzPath);
    return {};
#endif
}

QString LibraryScanner::saveThumbnail(const QByteArray& imageData, const QString& seriesPath)
{
    QImage img;
    if (!img.loadFromData(imageData))
        return {};

    // Scale to fill 180x252, then center-crop
    QImage scaled = img.scaled(THUMB_W, THUMB_H,
                               Qt::KeepAspectRatioByExpanding,
                               Qt::SmoothTransformation);

    // Center crop to exact dimensions
    int x = (scaled.width() - THUMB_W) / 2;
    int y = (scaled.height() - THUMB_H) / 2;
    QImage cropped = scaled.copy(x, y, THUMB_W, THUMB_H);

    // Save
    QString hash = QString(QCryptographicHash::hash(
        seriesPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
    QString thumbPath = m_thumbsDir + "/" + hash + ".jpg";

    if (cropped.save(thumbPath, "JPEG", THUMB_QUALITY))
        return thumbPath;

    return {};
}
