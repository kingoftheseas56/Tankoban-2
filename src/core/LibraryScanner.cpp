#include "LibraryScanner.h"
#include "ScannerUtils.h"

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

static constexpr int THUMB_W = 180;
static constexpr int THUMB_H = 252;
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
    // Group .cbz files by first-level subdirectory under each root
    QMap<QString, QStringList> seriesMap =
        ScannerUtils::groupByFirstLevelSubdir(rootFolders, {"*.cbz"});

    // Natural sort the files within each series
    QCollator collator;
    collator.setNumericMode(true);

    QList<SeriesInfo> allSeries;

    for (auto it = seriesMap.begin(); it != seriesMap.end(); ++it) {
        const QString& seriesPath = it.key();
        QStringList& files = it.value();

        std::sort(files.begin(), files.end(), [&collator](const QString& a, const QString& b) {
            return collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
        });

        SeriesInfo info;
        info.seriesName = ScannerUtils::cleanMediaFolderTitle(QDir(seriesPath).dirName());
        info.seriesPath = seriesPath;
        info.fileCount = files.size();

        // Check thumbnail cache
        QString hash = QString(QCryptographicHash::hash(
            seriesPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
        QString thumbPath = m_thumbsDir + "/" + hash + ".jpg";

        if (QFile::exists(thumbPath)) {
            info.coverThumbPath = thumbPath;
        } else {
            // Extract cover from first CBZ
            QByteArray coverData = extractCoverFromCbz(files.first());
            if (!coverData.isEmpty()) {
                info.coverThumbPath = saveThumbnail(coverData, seriesPath);
            }
        }

        allSeries.append(info);
        emit seriesFound(info);
    }

    emit scanFinished(allSeries);
}

QByteArray LibraryScanner::extractCoverFromCbz(const QString& cbzPath)
{
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
