#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMetaType>
#include <QSet>

struct SeriesInfo {
    struct FileEntry {
        QString path;
        int pageCount = 0;
        qint64 mtimeMs = 0;
    };

    QString seriesName;
    QString seriesPath;
    QString coverThumbPath;
    int fileCount = 0;
    qint64 newestMtimeMs = 0;
    QList<FileEntry> files;
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 (Task 14) — provenance
    // tag distinguishing folder-imported series ("folder", default) from
    // Tankoyomi-origin series ("tankoyomi"). Folder-origin tiles render
    // with no badge; Tankoyomi-origin tiles render the [Tankoyomi] chip.
    QString provenance;
};
Q_DECLARE_METATYPE(SeriesInfo)

class LibraryScanner : public QObject {
    Q_OBJECT
public:
    explicit LibraryScanner(const QString& thumbsDir, QObject* parent = nullptr);

public slots:
    void scan(const QStringList& rootFolders);
    // COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — suppress emitting
    // folder-origin SeriesInfo for any folder whose canonical path
    // (display form, lowercased on Windows) is in this set. Used by
    // ComicsPage to hide Tankoyomi-origin folders that have a library
    // record. Empty set => unchanged behaviour. Declared as a slot so
    // callers can dispatch via QueuedConnection from the GUI thread.
    void setClaimedPaths(const QStringList& paths);

signals:
    void seriesFound(const SeriesInfo& series);
    void scanFinished(const QList<SeriesInfo>& allSeries);

private:
    QByteArray extractCoverFromCbz(const QString& cbzPath);
    int countPagesInCbz(const QString& cbzPath);
    QString saveThumbnail(const QByteArray& imageData, const QString& seriesPath);

    QString m_thumbsDir;
    // THREAD: written only by setClaimedPaths (delivered via QueuedConnection
    // from the GUI thread between scans); read only on the scanner worker
    // thread inside scan(). Safe today because scan() is non-reentrant on its
    // own thread and doesn't yield to the event loop, so queued writes can
    // only land between scans. If scan() is ever broken into event-loop-
    // yielding chunks, wrap m_claimedPaths in a QMutex (per-folder lookup is
    // cheap; lock cost negligible).
    QSet<QString> m_claimedPaths;  // normalised canonical paths (lower on Windows)
};
