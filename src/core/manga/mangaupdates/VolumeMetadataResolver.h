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

    void resolveForAnilist(int anilistId,
                           const tankoban::manga::anilist::MediaPreview& preview,
                           const QStringList& anilistAuthors);

signals:
    void resolved(int anilistId, int volumeCount, int chapterCount);
    void unresolved(int anilistId, const QString& reason);

private slots:
    void onSearchSucceeded(int requestId, const QList<MangaUpdatesSearchHit>& hits);
    void onSearchFailed(int requestId, const QString& reason);
    void onSeriesSucceeded(int requestId, const MangaUpdatesSeriesInfo& info);
    void onSeriesFailed(int requestId, const QString& reason);

private:
    struct PendingResolve {
        int anilistId = 0;
        tankoban::manga::anilist::MediaPreview preview;
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
