#pragma once

#include "BookWalkerCacheTypes.h"
#include "BookWalkerTypes.h"

#include <QHash>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>

namespace tankoban::manga::anilist { class AniListCache; }

namespace tankoban::manga::premium {
class PremiumCatalog;
}

namespace tankoban::manga::bookwalker {

class BookWalkerClient;

// Orchestrates the full chain per spec section 5:
//   Premium short-circuit -> AniList alt-title (Japanese) -> MangaUpdates
//   volume count (via AniListCache sidecar) -> cache check -> BookWalker
//   search -> BookWalker page parse -> index align -> cache write -> emit.
//
// MangaUpdatesClient is NOT a direct dependency: the canonical volume count
// is retrieved from AniListCache::getMangaUpdatesSidecar(), which stores the
// MangaUpdatesSeriesInfo fetched by VolumeMetadataResolver. This avoids a
// second live network dependency and reuses already-warmed sidecar data.
class VolumeCoverResolver : public QObject
{
    Q_OBJECT
public:
    VolumeCoverResolver(BookWalkerClient* bwClient,
                        tankoban::manga::anilist::AniListCache* anilistCache,
                        tankoban::manga::premium::PremiumCatalog* premium,
                        QObject* parent = nullptr);
    ~VolumeCoverResolver() override;

    // Entry point. Resolves per-volume covers for the given AniList ID.
    // Emits resolved(...) or unresolved(...) when complete.
    void resolveForAnilist(int anilistId);

signals:
    void resolved(int anilistId, const QMap<int, QString>& volumeToCoverUrl);
    void unresolved(int anilistId, const QString& reason);

private slots:
    void onSearchSucceeded(int requestId, const QList<BookWalkerSearchHit>& hits);
    void onSearchFailed(int requestId, const QString& reason);
    void onCoversSucceeded(int requestId, const QList<QString>& orderedCoverUrls);
    void onCoversFailed(int requestId, const QString& reason);

private:
    struct PendingResolve {
        int anilistId = 0;
        QString japaneseTitle;
        int canonicalCount = 0;
        QString bookwalkerSeriesId;
    };

    int nextRequestId();
    void serveCachedOrFallback(int anilistId, int canonicalCount,
                               const QString& failureReason);
    void emitFromCache(int anilistId, const BookWalkerCacheRecord& rec);

    QPointer<BookWalkerClient>                       m_bwClient;
    QPointer<tankoban::manga::anilist::AniListCache> m_anilistCache;
    QPointer<tankoban::manga::premium::PremiumCatalog> m_premium;

    QHash<int, PendingResolve> m_pending;
    int m_nextRequestId = 1;
};

} // namespace tankoban::manga::bookwalker
