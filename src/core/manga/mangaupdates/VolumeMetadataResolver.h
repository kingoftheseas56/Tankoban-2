#pragma once

#include "MangaUpdatesTypes.h"
#include "core/manga/anilist/AniListTypes.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

namespace tankoban::manga::anilist { class AniListCache; }

namespace tankoban::manga::mangaupdates {

class MangaUpdatesClient;

class VolumeMetadataResolver : public QObject
{
    Q_OBJECT
public:
    VolumeMetadataResolver(MangaUpdatesClient* client,
                           tankoban::manga::anilist::AniListCache* cache,
                           QObject* parent = nullptr);
    ~VolumeMetadataResolver() override;

    // Existing entry retained for backward compatibility with library entries
    // that already store anilistId (currently none, but future cleanup may
    // reuse this signature).
    void resolveForAnilist(int anilistId,
                           const tankoban::manga::anilist::MediaPreview& preview,
                           const QStringList& anilistAuthors);

    // New entry — used by the post-WEEBCENTRAL_IDENTITY_PIVOT chain.
    // Searches MangaUpdates by englishTitle, caches by seriesKey
    // (e.g. "weebcentral:01J76XYAVE3FZ3YMHMTKEZGXM4").
    void resolveBySeriesKey(const QString& seriesKey,
                            const QString& englishTitle,
                            const QStringList& authorsHint = {});

signals:
    void resolved(int anilistId, int volumeCount, int chapterCount);
    void unresolved(int anilistId, const QString& reason);

    // New signals — richer payload for the by-seriesKey flow.
    void resolvedBySeriesKey(const QString& seriesKey,
                             int volumeCount,
                             int chapterCount,
                             const QStringList& altTitles);
    void unresolvedBySeriesKey(const QString& seriesKey, const QString& reason);

private slots:
    void onSearchSucceeded(int requestId, const QList<MangaUpdatesSearchHit>& hits);
    void onSearchFailed(int requestId, const QString& reason);
    void onSeriesSucceeded(int requestId, const MangaUpdatesSeriesInfo& info);
    void onSeriesFailed(int requestId, const QString& reason);

private:
    struct PendingResolve {
        int anilistId = 0;                                // 0 when by-seriesKey
        QString seriesKey;                                 // empty when by-anilist (legacy)
        QString englishTitle;
        tankoban::manga::anilist::MediaPreview preview;    // empty when by-seriesKey
        QStringList anilistAuthors;
    };

    int nextRequestId();
    bool isSidecarFresh(const MangaUpdatesSeriesInfo& info) const;

    QPointer<MangaUpdatesClient> m_client;
    QPointer<tankoban::manga::anilist::AniListCache> m_cache;
    QHash<int, PendingResolve> m_pending;
    int m_nextRequestId = 1;
};

} // namespace tankoban::manga::mangaupdates
