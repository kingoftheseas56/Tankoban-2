#pragma once

// COMICS_TANKOYOMI_STREAM_MERGER 2026-05-14 — forked from
// src/core/stream/StreamDownloadIndex.{h,cpp} per brainstorm §6.1.
// Same threadsafe canonical-key-keyed JSON-backed shape, different
// keying (sourceId:seriesId:chapterId instead of imdbId:season:episode).
//
// JSON file: <appDataDir>/manga_downloads_index.json. Schema v1.

#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <optional>

class JsonStore;

class MangaDownloadIndex : public QObject
{
    Q_OBJECT
public:
    struct Entry {
        QString sourceId;
        QString seriesId;
        QString chapterId;          // first chapter that registered the entry; legacy single-chapter path
        QString canonicalPath;
        qint64  addedAt        = 0;
        qint64  fileSizeBytes  = 0;

        // Phase 5 (Premium): chapter keys served by this entry. For
        // legacy single-chapter cbz files (from MangaDownloader), this
        // set holds exactly one element matching computeChapterKey(
        // sourceId, seriesId, chapterId). For Premium volume cbz files,
        // it holds N elements - one per chapter contained in the volume.
        // evictByChapter removes one element at a time; the m_byPath
        // entry is only dropped when the set becomes empty.
        QSet<QString> servedChapterKeys;
    };

    explicit MangaDownloadIndex(JsonStore* store, QObject* parent = nullptr);

    // ── Thread safety contract ──────────────────────────────────────────────
    // All mutating methods (registerChapter/registerVolume/evictBySeries/
    // evictByChapter/validateAll/load) execute synchronously on the calling
    // thread. They acquire m_mutex around in-memory map mutations, then call
    // save() and emit entriesChanged() OFF the lock. JsonStore::write is
    // internally thread-safe. Subscribers MUST use Qt::QueuedConnection if
    // they want delivery on the GUI thread regardless of which thread called
    // the mutating method.
    //
    // fileSizeBytes is monotone-increasing: a re-register with a smaller
    // value is ignored (treats partial-finalize artifacts as never shrinking
    // the recorded size). Applies to both registerChapter and registerVolume.
    void registerChapter(const QString& sourceId, const QString& seriesId,
                          const QString& chapterId, const QString& canonicalPath,
                          qint64 fileSizeBytes);

    // Phase 5 (Premium): register a volume cbz that serves multiple chapters.
    // sourceId is the catalog source ("tankoyomi_premium"); chapterIds is
    // the list of chapter identifiers contained in this volume. The same
    // canonicalPath gets ONE m_byPath entry whose servedChapterKeys holds
    // N elements. evictByChapter removes one chapter at a time; the entry
    // only disappears when servedChapterKeys is empty.
    //
    // Calling registerVolume twice with the same canonicalPath and an
    // identical chapterIds set is idempotent (no-op). Calling with a
    // different chapterIds set extends the served set.
    //
    // fileSizeBytes is monotone-increasing: a re-register with a smaller
    // value is ignored (treats partial-finalize artifacts as never
    // shrinking the recorded size).
    void registerVolume(const QString&       sourceId,
                         const QString&       seriesId,
                         int                  volumeNumber,
                         const QString&       canonicalPath,
                         qint64               fileSizeBytes,
                         const QStringList&   chapterIds);

    void evictBySeries(const QString& sourceId, const QString& seriesId);
    void evictByChapter(const QString& sourceId, const QString& seriesId,
                         const QString& chapterId);
    void validateAll();

    // Read API — mutex-guarded. Safe from any thread.
    bool isComicsOwned(const QString& canonicalKey) const;
    std::optional<QString> filePathFor(const QString& sourceId, const QString& seriesId,
                                        const QString& chapterId) const;
    bool hasAnyForSeries(const QString& sourceId, const QString& seriesId) const;
    QList<Entry> entriesForSeries(const QString& sourceId, const QString& seriesId) const;

    // Static helpers.
    static QString computeCanonicalKey(const QString& anyPath);
    static QString computeChapterKey(const QString& sourceId, const QString& seriesId,
                                      const QString& chapterId);
    static QString computeSeriesKey(const QString& sourceId, const QString& seriesId);

signals:
    void entriesChanged();

private:
    void load();
    void save();

    // Recompute m_seriesHasAny for a single (sourceId, seriesId) after an
    // entry was evicted. MUST be called with m_mutex already held.
    void recomputeSeriesHasAnyLocked(const QString& sourceId, const QString& seriesId);

    JsonStore* m_store;
    mutable QMutex m_mutex;

    QHash<QString, Entry>   m_byPath;     // canonicalKey -> Entry
    QHash<QString, QString> m_byChapter;  // "source:series:chapter" -> canonicalKey
    QSet<QString>           m_seriesHasAny; // "source:series" if at least one entry exists

    static constexpr const char* FILENAME = "manga_downloads_index.json";
    static constexpr int kSchemaVersion = 2;
};
