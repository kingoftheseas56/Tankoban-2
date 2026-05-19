#pragma once

#include "BookWalkerCacheTypes.h"
#include "BookWalkerTypes.h"

#include <QHash>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

namespace tankoban::manga::mangaupdates { class VolumeMetadataResolver; }

namespace tankoban::manga::premium {
class PremiumCatalog;
}

namespace tankoban::manga::bookwalker {

class BookWalkerClient;

// Orchestrates the full chain per WEEBCENTRAL_IDENTITY_PIVOT spec Tasks 6+7:
//   Premium short-circuit (anilistId optional) -> cache check (by seriesKey)
//   -> VolumeMetadataResolver::resolveBySeriesKey -> JapaneseTitlePicker on
//   returned altTitles -> BookWalker search -> series-page parse -> index align
//   -> cache write (by seriesKey) -> emit resolved(seriesKey, ...).
//
// AniListCache is NO LONGER a direct dependency. seriesKey is the
// WeebCentral sourceId:seriesId composite (e.g.
// "weebcentral:01J76XYAVE3FZ3YMHMTKEZGXM4").
class VolumeCoverResolver : public QObject
{
    Q_OBJECT
public:
    VolumeCoverResolver(BookWalkerClient* bwClient,
                        tankoban::manga::mangaupdates::VolumeMetadataResolver* muResolver,
                        tankoban::manga::premium::PremiumCatalog* premium,
                        QObject* parent = nullptr);
    ~VolumeCoverResolver() override;

    // Entry point. seriesKey is the WeebCentral sourceId:seriesId composite
    // (e.g. "weebcentral:01J76XYAVE3FZ3YMHMTKEZGXM4"). anilistIdOptional is
    // kept for Premium catalog backward-compat (PremiumCatalog::hasPremiumEntry
    // currently takes int anilistId; can be 0 if not known).
    void resolveForSeries(const QString& seriesKey,
                          const QString& englishTitle,
                          int anilistIdOptional = 0);

signals:
    void resolved(const QString& seriesKey, const QMap<int, QString>& volumeToCoverUrl);
    void unresolved(const QString& seriesKey, const QString& reason);
    void skipped(const QString& seriesKey, const QString& reason);  // Premium short-circuit

private slots:
    void onMuResolvedBySeriesKey(const QString& seriesKey,
                                 int volumeCount,
                                 int chapterCount,
                                 const QStringList& altTitles);
    void onMuUnresolvedBySeriesKey(const QString& seriesKey, const QString& reason);
    void onBwSearchSucceeded(int requestId, const QList<BookWalkerSearchHit>& hits);
    void onBwSearchFailed(int requestId, const QString& reason);
    void onBwCoversSucceeded(int requestId, const QList<QString>& orderedCoverUrls);
    void onBwCoversFailed(int requestId, const QString& reason);

private:
    struct PendingResolve {
        QString seriesKey;
        QString englishTitle;
        int     anilistIdOptional = 0;
        QString japaneseTitle;         // populated after MU resolves
        int     canonicalCount = 0;    // populated after MU resolves
        QString bookwalkerSeriesId;    // populated after BW search resolves
    };

    QPointer<BookWalkerClient>                                         m_bwClient;
    QPointer<tankoban::manga::mangaupdates::VolumeMetadataResolver>    m_muResolver;
    QPointer<tankoban::manga::premium::PremiumCatalog>                 m_premium;
    QHash<QString, PendingResolve> m_pendingBySeriesKey;   // seriesKey → pending
    QHash<int, QString>            m_bwRequestIdToSeriesKey; // bw requestId → seriesKey
    int m_nextBwRequestId = 1;
};

} // namespace tankoban::manga::bookwalker
