#include "ThumbnailGenerator.h"
#include "core/ArchiveReader.h"

#include <QBuffer>
#include <QImageReader>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QCryptographicHash>

// ── ThumbnailTask ────────────────────────────────────────────────────────────

ThumbnailTask::ThumbnailTask(ThumbnailTaskSignals* notifier, int pageIndex,
                             const QString& cbzPath, const QString& pageName,
                             const QString& cachePath)
    : m_notifier(notifier)
    , m_pageIndex(pageIndex)
    , m_cbzPath(cbzPath)
    , m_pageName(pageName)
    , m_cachePath(cachePath)
{
    setAutoDelete(true);
}

void ThumbnailTask::run()
{
    QByteArray data = ArchiveReader::pageData(m_cbzPath, m_pageName);
    if (data.isEmpty()) {
        emit m_notifier->thumbnailDecoded(m_pageIndex, m_cbzPath, QImage());
        return;
    }

    QBuffer buf(&data);
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        emit m_notifier->thumbnailDecoded(m_pageIndex, m_cbzPath, QImage());
        return;
    }

    // Match LibraryScanner cover pattern: scale-with-expand + center-crop to
    // exact thumbnail dimensions (preserves aspect, fills the cell).
    QImage scaled = img.scaled(ThumbnailGenerator::THUMB_W,
                               ThumbnailGenerator::THUMB_H,
                               Qt::KeepAspectRatioByExpanding,
                               Qt::SmoothTransformation);
    int x = (scaled.width()  - ThumbnailGenerator::THUMB_W) / 2;
    int y = (scaled.height() - ThumbnailGenerator::THUMB_H) / 2;
    QImage cropped = scaled.copy(x, y,
                                 ThumbnailGenerator::THUMB_W,
                                 ThumbnailGenerator::THUMB_H);

    // Persist to disk. Worker-thread IO (avoids blocking main).
    // Best-effort: a failed save still emits the in-memory thumbnail so the
    // current grid open paints; a follow-up request will retry the disk write.
    cropped.save(m_cachePath, "JPEG", 80);

    emit m_notifier->thumbnailDecoded(m_pageIndex, m_cbzPath, cropped);
}

// ── ThumbnailGenerator ───────────────────────────────────────────────────────

ThumbnailGenerator::ThumbnailGenerator(QObject* parent)
    : QObject(parent)
{
    m_baseDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
              + "/data/comic_thumbs";
    QDir().mkpath(m_baseDir);

    // Cap pool concurrency so thumbnail bursts don't starve the page-display
    // decode pool. 2 workers is enough for fast grid-fill on JPEG-heavy CBZs.
    m_pool.setMaxThreadCount(2);

    connect(&m_taskNotifier, &ThumbnailTaskSignals::thumbnailDecoded,
            this, &ThumbnailGenerator::onTaskDecoded,
            Qt::QueuedConnection);
}

QString ThumbnailGenerator::seriesCacheDir(const QString& cbzPath) const
{
    QString hash = QString(QCryptographicHash::hash(
        cbzPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
    return m_baseDir + "/" + hash;
}

QString ThumbnailGenerator::cachePathForPage(const QString& cbzPath, int pageIdx) const
{
    return seriesCacheDir(cbzPath) + "/" + QString::asprintf("%04d.jpg", pageIdx);
}

void ThumbnailGenerator::requestThumbnail(const QString& cbzPath, int pageIdx,
                                          const QString& pageName)
{
    if (cbzPath.isEmpty() || pageName.isEmpty()) return;
    if (pageIdx < 0) return;

    const QString cachePath = cachePathForPage(cbzPath, pageIdx);

    // Already on disk — caller can read directly. Don't re-decode.
    if (QFile::exists(cachePath)) return;

    // Already in flight — single-flight dedup.
    QPair<QString, int> key(cbzPath, pageIdx);
    if (m_inflight.contains(key)) return;
    m_inflight.insert(key);

    // Ensure series subdirectory exists before the worker tries to save.
    QDir().mkpath(seriesCacheDir(cbzPath));

    auto* task = new ThumbnailTask(&m_taskNotifier, pageIdx, cbzPath, pageName, cachePath);
    m_pool.start(task);
}

void ThumbnailGenerator::onTaskDecoded(int pageIndex, const QString& cbzPath,
                                       const QImage& thumb)
{
    m_inflight.remove({cbzPath, pageIndex});
    // Forward the result regardless of whether it's null — callers can check
    // and substitute placeholder. Null means decode failed (corrupt page,
    // unsupported format, or empty archive entry).
    emit thumbnailReady(pageIndex, thumb);
}
