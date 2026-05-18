// src/core/manga/anilist/AniListCache.h
#pragma once

#include "AniListTypes.h"
#include "core/manga/mangaupdates/MangaUpdatesTypes.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <optional>

namespace tankoban::manga::anilist {

// File-backed cache for AniList responses.
//   <cacheDir>/series_<anilistId>.json   - one MediaDetail per file
//   <cacheDir>/_bookmarks.json           - QSet<int> of bookmarked anilistIds (never evicted)
//   <cacheDir>/_index.json               - reserved for future cross-series index; v1 uses per-series fetchedAtMs for staleness
//
// Thread safety: all mutating methods acquire m_mutex; readers also acquire
// m_mutex (the in-memory hot copy m_byId may be touched from any thread).
// save/flush always happen off-lock.
class AniListCache : public QObject
{
    Q_OBJECT
public:
    explicit AniListCache(const QString& cacheDir, QObject* parent = nullptr);
    ~AniListCache() override;

    // Read API.
    std::optional<MediaDetail> get(int anilistId) const;
    bool isBookmarked(int anilistId) const;
    QSet<int> bookmarkedIds() const;
    QList<MediaPreview> bookmarkedPreviews() const;  // for offline browse landing

    // Write API.
    void put(const MediaDetail& detail);
    void addBookmark(int anilistId);
    void removeBookmark(int anilistId);
    std::optional<tankoban::manga::mangaupdates::MangaUpdatesSeriesInfo>
        getMangaUpdatesSidecar(int anilistId) const;
    void putMangaUpdatesSidecar(
        int anilistId,
        const tankoban::manga::mangaupdates::MangaUpdatesSeriesInfo& info);

    // Returns true if a cached entry exists AND its fetchedAtMs is within
    // `maxAgeMs` of now. Used by the series view to decide whether to
    // fire a background refetch.
    bool isFresh(int anilistId, qint64 maxAgeMs) const;

    // Returns the Japanese (native) title for anilistId from the cached
    // MediaPreview.alternateTitles list, or empty if uncached or not found.
    // Scans alternateTitles for the first string that contains CJK Unified
    // Ideographs, Hiragana, or Katakana -- the ordering in alternateTitles
    // is english/romaji/native/userPreferred (see AniListClient.cpp
    // collectAlternateTitles), so this reliably isolates the native JP title.
    // Used by VolumeCoverResolver before hitting BookWalker JP search.
    QString japaneseTitleFor(int anilistId) const;

signals:
    void cacheChanged(int anilistId);
    void bookmarksChanged();

private:
    void loadFromDisk();
    void saveSeriesToDisk(const MediaDetail& d) const;
    void saveBookmarksToDisk() const;
    QString seriesFilePath(int anilistId) const;
    QString mangaUpdatesSidecarFilePath(int anilistId) const;
    QString bookmarksFilePath() const;
    QString indexFilePath() const;

    const QString m_cacheDir;
    mutable QMutex m_mutex;
    QHash<int, MediaDetail> m_byId;
    QHash<int, tankoban::manga::mangaupdates::MangaUpdatesSeriesInfo>
        m_mangaUpdatesByAnilistId;
    QSet<int>               m_bookmarks;
};

} // namespace tankoban::manga::anilist
