#include "BooksScanner.h"

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
    "*.mp3", "*.m4a", "*.m4b", "*.aac", "*.flac", "*.ogg", "*.opus", "*.wav"
};

static constexpr int THUMB_W = 180;
static constexpr int THUMB_H = 252;

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

    // ── Scan books ──
    QMap<QString, QStringList> seriesMap;

    for (const auto& root : bookRoots) {
        QDirIterator it(root, BOOK_EXTS, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString path = it.next();
            QString parentDir = QFileInfo(path).absolutePath();
            seriesMap[parentDir].append(path);
        }
    }

    QList<BookSeriesInfo> allBooks;

    for (auto it = seriesMap.begin(); it != seriesMap.end(); ++it) {
        const QString& seriesPath = it.key();
        QStringList& files = it.value();

        std::sort(files.begin(), files.end(), [&collator](const QString& a, const QString& b) {
            return collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
        });

        BookSeriesInfo info;
        info.seriesName = QDir(seriesPath).dirName();
        info.seriesPath = seriesPath;
        info.fileCount = files.size();

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
    QList<AudiobookInfo> allAudiobooks;

    for (const auto& root : audiobookRoots) {
        // Each immediate subdirectory is an audiobook
        QDir rootDir(root);
        for (const auto& entry : rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            int trackCount = 0;
            QDirIterator it(entry.absoluteFilePath(), AUDIO_EXTS,
                           QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                trackCount++;
            }

            if (trackCount > 0) {
                AudiobookInfo ab;
                ab.name = entry.fileName();
                ab.path = entry.absoluteFilePath();
                ab.trackCount = trackCount;
                allAudiobooks.append(ab);
                emit audiobookFound(ab);
            }
        }

        // Also check for loose audio files directly in root
        int looseCount = 0;
        QDirIterator looseIt(root, AUDIO_EXTS, QDir::Files);
        while (looseIt.hasNext()) {
            looseIt.next();
            looseCount++;
        }
        if (looseCount > 0) {
            AudiobookInfo ab;
            ab.name = rootDir.dirName();
            ab.path = root;
            ab.trackCount = looseCount;
            allAudiobooks.append(ab);
            emit audiobookFound(ab);
        }
    }

    emit scanFinished(allBooks, allAudiobooks);
}
