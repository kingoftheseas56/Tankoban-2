// src/core/manga/mangafire/MangaWeebCentralResolver.h
//
// COMICS_WC_VOLUME_WIRING 2026-05-24 (Agent 1).
//
// Bridge between MangaFire-cataloged volume metadata and WeebCentral's
// chapter scrape. The resolver is lazy: a volume click triggers WC seriesId
// resolution, chapter enumeration, then integer range filtering.
//
// PRIVATE scraper requirement: do not swap m_scraper to the shared scraper
// from MangaSourceRegistry. The shared instance is used by search/detail/
// packing flows and its signals carry no request id.

#pragma once

#include "core/manga/MangaCatalogTypes.h"
#include "core/manga/VolumeQualityClassifier.h"  // ClassifiedVolume

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <memory>

class WeebCentralScraper;
class QNetworkAccessManager;

namespace tankoban::manga::mangafire {

class MangaWeebCentralResolver : public QObject
{
    Q_OBJECT
public:
    struct ResolveKey {
        QString seriesId;       // MangaFire slug
        int     volumeNumber = 0;
        quint64 requestSerial = 0;

        bool operator==(const ResolveKey& other) const {
            return seriesId == other.seriesId
                && volumeNumber == other.volumeNumber
                && requestSerial == other.requestSerial;
        }
    };

    struct ChapterRef {
        int number = 0;      // Parsed WeebCentral chapter number.
        QString id;          // Opaque WeebCentral chapter id for fetchPages().
        bool isVolumeScanned = false;  // WeebCentral violet tick (volume-scan quality)
    };

    enum class SkipReason {
        NoSeriesMatch,
        NoChapterOverlap,
        IncompleteCoverage,
        NetworkError,
    };
    static QString reasonCode(SkipReason reason);

    explicit MangaWeebCentralResolver(QNetworkAccessManager* nam,
                                      QObject* parent = nullptr);
    ~MangaWeebCentralResolver() override;

    void resolve(const tankoban::manga::MangaCatalog& catalog,
                 int volumeNumber,
                 const ResolveKey& key);

    // VOLUME_X_QUALITY 2026-05-28 (Agent 1, DeepSeek V4-Pro).
    // Classify every catalog volume as Clean or Magazine from the WeebCentral
    // chapter-quality ticks, plus a trailing Volume X bucket for chapters past
    // the last catalog volume. If the chapter cache is warm (prior resolve()
    // call), emits seriesClassified synchronously; otherwise fetches chapters
    // via the private scraper and emits asynchronously.
    void classifySeries(const tankoban::manga::MangaCatalog& catalog);

    static QStringList filterChaptersToRange(const QList<ChapterRef>& chapters,
                                             int rangeStart,
                                             int rangeEnd,
                                             bool* outIncomplete = nullptr);

signals:
    void viable(tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey key,
                QStringList chapterIds);
    void skip(tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey key,
              QString reasonCode);

    // VOLUME_X_QUALITY 2026-05-28 (Agent 1, DeepSeek V4-Pro).
    // Emitted after classifySeries() completes. mangaFireSeriesId is the
    // catalog slug; classified has one entry per catalog volume with member
    // chapters plus a trailing Volume X when chapters exist past the last
    // catalog volume.
    void seriesClassified(const QString& mangaFireSeriesId,
                          QList<tankoban::manga::ClassifiedVolume> classified);

private:
    struct PendingResolve;
    using PendingResolvePtr = std::shared_ptr<PendingResolve>;

    void stepFetchChapters(PendingResolvePtr pending);
    void filterAndEmit(PendingResolvePtr pending);
    void emitSkip(PendingResolvePtr pending, SkipReason reason);
    void emitViable(PendingResolvePtr pending, const QStringList& chapterIds);

    // VOLUME_X_QUALITY 2026-05-28 (Agent 1, DeepSeek V4-Pro).
    // Runs VolumeQualityClassifier against the cached chapter list for
    // wcSeriesId, using volumes as the volume-boundary input. Emits
    // seriesClassified on completion.
    void runClassification(const QString& mangaFireSeriesId,
                           const QString& wcSeriesId,
                           const QList<tankoban::manga::MangaVolume>& volumes);

    QPointer<QNetworkAccessManager> m_nam;
    WeebCentralScraper*             m_scraper = nullptr;  // owned, private instance

    QHash<QString, QList<ChapterRef>> m_chapterCache; // WC seriesId -> chapter refs
    QHash<QString, QList<PendingResolvePtr>> m_pendingByMangafireSeriesId;

    // VOLUME_X_QUALITY 2026-05-28 (Agent 1, DeepSeek V4-Pro).
    // MangaFire seriesId waiting for chapter fetch before classification.
    // Cleared when chaptersReady runs classification or on error.
    QString m_inflightClassifyMangaFireId;
    QList<tankoban::manga::MangaVolume> m_pendingClassifyVolumes;

    // VOLUME_X_QUALITY 2026-05-28 (Agent 1, Opus, seam fix).
    // The WeebCentral seriesId the pending classify is waiting on. Empty until
    // known (a cold-open classify learns it from searchFinished). chaptersReady
    // only runs classification when the just-fetched WC seriesId matches this,
    // so a concurrent resolve fetch for a *different* series can never feed the
    // wrong chapter list into the classifier.
    QString m_inflightClassifyWcSeriesId;

    // The private WeebCentralScraper signal surface is request-id-less, so
    // serialize each async phase explicitly.
    QString m_inflightSearch; // MangaFire seriesId
    QString m_inflightFetch;  // WeebCentral seriesId
};

} // namespace tankoban::manga::mangafire

Q_DECLARE_METATYPE(tankoban::manga::mangafire::MangaWeebCentralResolver::ResolveKey)
