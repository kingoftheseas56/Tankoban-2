#pragma once

#include <QObject>
#include <QRunnable>
#include <QImage>
#include <QString>
#include <QSet>
#include <QPair>
#include <QThreadPool>

// P6-1: per-page thumbnail decode + on-disk JPEG cache for the modal grid
// overlay. Async + lazy + single-flight. Reuses ArchiveReader for format-
// transparent (CBZ + CBR + RAR) page-data extraction.

class ThumbnailGenerator;

class ThumbnailTaskSignals : public QObject {
    Q_OBJECT
signals:
    void thumbnailDecoded(int pageIndex, const QString& cbzPath, const QImage& thumb);
};

class ThumbnailTask : public QRunnable {
public:
    ThumbnailTask(ThumbnailTaskSignals* notifier, int pageIndex,
                  const QString& cbzPath, const QString& pageName,
                  const QString& cachePath);
    void run() override;

private:
    ThumbnailTaskSignals* m_notifier;  // borrowed pointer; outlives the task
    int m_pageIndex;
    QString m_cbzPath;
    QString m_pageName;
    QString m_cachePath;
};

class ThumbnailGenerator : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailGenerator(QObject* parent = nullptr);

    // Thumbnail dimensions on disk (post-crop).
    static constexpr int THUMB_W = 160;
    static constexpr int THUMB_H = 246;

    // Compute the on-disk cache path for a given page. No IO — pure path math.
    // Caller can stat it before calling requestThumbnail to avoid the round-trip.
    QString cachePathForPage(const QString& cbzPath, int pageIdx) const;

    // Enqueue a thumbnail decode job. No-op if already on disk OR already in-flight.
    // Emits thumbnailReady on completion (queued from worker thread).
    void requestThumbnail(const QString& cbzPath, int pageIdx, const QString& pageName);

signals:
    // Fired on Qt main thread (queued connection from worker pool).
    void thumbnailReady(int pageIndex, const QImage& thumb);

private slots:
    void onTaskDecoded(int pageIndex, const QString& cbzPath, const QImage& thumb);

private:
    QString seriesCacheDir(const QString& cbzPath) const;

    QString m_baseDir;                       // {AppLocalDataLocation}/data/comic_thumbs
    QSet<QPair<QString, int>> m_inflight;    // (cbzPath, pageIdx) — main-thread only
    QThreadPool m_pool;
    ThumbnailTaskSignals m_taskNotifier;
};
