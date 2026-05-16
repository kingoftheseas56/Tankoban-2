// src/core/manga/WeebCentralVolumePacker.h
//
// TANKOYOMI_VOLUME_PIVOT Phase 5 -- HTTP-fetch volume packer.
//
// Sibling to TorrentVolumeProvider on the source layer. Synthesizes a
// vol cbz from WeebCentral chapter HTTP fetches and emits the same
// volumeCompleted / volumeFailed / volumeProgress signal shape so
// downstream code (MangaDownloadIndex.registerVolume + Phase 12 cover
// extractor + MangaTransferCoordinator) does not branch on source type.
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class MangaScraper;
class QNetworkAccessManager;

namespace tankoban::manga {

namespace premium { class PremiumCoverExtractor; } // fwd-decl, Phase 12

// One pending vol-pack request.
struct VolumePackRequest {
    QString     seriesId;        // app-internal series id (lowercase slug)
    int         volumeNumber = 0;
    QString     destinationPath; // canonical .cbz path on disk
    QStringList chapterIds;      // chapter ids in scrape order
};

// Source provider that synthesizes a vol cbz from WeebCentral chapter
// fetches.
//
// Lifecycle:
//   requestVolume(req) ->
//     1. mkpath staging dir <stagingRoot>/wc_<seriesId>_v<NN>/
//     2. for each chapterId in req.chapterIds:
//          a. scraper->fetchPages(chapterId) -> pagesReady(List<PageInfo>)
//          b. download each image via NAM -> write to staging dir
//          c. emit volumeProgress per chapter complete
//     3. zip the staging dir into <destination>.tankoban-part
//     4. hand to PremiumArchiveValidator for Phase 4 finalize lifecycle
//     5. on validator success: atomic rename .tankoban-part -> .cbz, emit
//        volumeCompleted; on failure, move to quarantine, emit volumeFailed
//
// Signals match TorrentVolumeProvider's shape so downstream consumers
// (MangaDownloadIndex.registerVolume + PremiumCoverExtractor) do not
// branch on source type.
class WeebCentralVolumePacker : public QObject
{
    Q_OBJECT
public:
    WeebCentralVolumePacker(MangaScraper* scraper,
                            QNetworkAccessManager* nam,
                            const QString& stagingRoot,
                            const QString& coversDir = QString(),
                            QObject* parent = nullptr);
    ~WeebCentralVolumePacker() override;

    void requestVolume(const VolumePackRequest& req);

    // Coordinator integration.
    void pauseAll();
    void resumeAll();
    bool isPaused() const;

signals:
    void volumeProgress(QString seriesId, int volumeNumber, double pct);
    void volumeCompleted(QString seriesId, int volumeNumber, QString cbzPath);
    void volumeFailed(QString seriesId, int volumeNumber,
                      QString code, QString message);

    // TANKOYOMI_VOLUME_PIVOT Phase 12 -- per-volume cover thumbnail ready.
    // Emitted AFTER volumeCompleted (cover extraction does NOT gate
    // completion, mirroring TorrentVolumeProvider's Phase 10 wiring).
    void volumeCoverReady(QString seriesId, int volumeNumber, QString coverPath);

private:
    void startNextChapter(const VolumePackRequest& req, int chapterIdx,
                          int totalChapters, const QString& stagingDir);
    void finalizePack(const VolumePackRequest& req, const QString& stagingDir);

    QPointer<MangaScraper>          m_scraper;
    QPointer<QNetworkAccessManager> m_nam;
    QString                         m_stagingRoot;
    QString                         m_coversDir;     // Phase 12 cover output dir
    bool                            m_paused = false;

    // TANKOYOMI_VOLUME_PIVOT Phase 12 -- off-thread cover extractor. Lazily
    // created on first finalizePack when m_coversDir is non-empty; coverReady
    // is proxied out as volumeCoverReady via Qt::QueuedConnection. Lifetime
    // tied to this packer (QObject parent set to `this`).
    premium::PremiumCoverExtractor* m_coverExtractor = nullptr;
};

} // namespace tankoban::manga
