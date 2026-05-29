// src/core/manga/WeebCentralVolumePacker.cpp
//
// TANKOYOMI_VOLUME_PIVOT Phase 5 -- HTTP-fetch volume packer.

#include "WeebCentralVolumePacker.h"
#include "MangaScraper.h"
#include "MangaResult.h"            // PageInfo
#include "PremiumArchiveValidator.h"
#include "PremiumCoverExtractor.h"  // Phase 12
#include "anilist/AniListTypes.h"   // kVolumeXNumber (VOLUME_X_CHAPTER_PAIRING)

#include <QByteArray>
#include <QChar>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>
#include <QImage>
#include <QPainter>
#include <QBuffer>
#include <QMap>

#include <memory>   // std::shared_ptr

#ifdef HAS_QT_ZIP
#  include <QtCore/private/qzipwriter_p.h>
#endif

namespace tankoban::manga {

namespace {

constexpr qint64 kImageMaxBytes = 32LL * 1024 * 1024;  // 32 MiB per image safety bound
static const QByteArray kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36";

QString stagingDirFor(const QString& root, const QString& seriesId, int volNumber)
{
    return root + QStringLiteral("/wc_") + seriesId + QStringLiteral("_v")
         + QString::number(volNumber, 10).rightJustified(2, QChar('0'));
}

bool zipStagingDir(const QString& stagingDir, const QString& outPartPath)
{
#ifndef HAS_QT_ZIP
    // Portability fallback -- this build is configured with HAS_QT_ZIP=1
    // unconditionally (see CMakeLists.txt:428), so this branch is dead in
    // the Tankoban build, but kept for theoretical Qt-without-private-API
    // ports.
    Q_UNUSED(stagingDir)
    Q_UNUSED(outPartPath)
    return false;
#else
    QFile out(outPartPath);
    if (!out.open(QIODevice::WriteOnly)) return false;
    QZipWriter zw(&out);
    QDir dir(stagingDir);
    const QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo& fi : files) {
        QFile in(fi.absoluteFilePath());
        if (!in.open(QIODevice::ReadOnly)) continue;
        zw.addFile(fi.fileName(), in.readAll());
    }
    zw.close();
    return out.size() > 0;
#endif
}

} // anonymous namespace

WeebCentralVolumePacker::WeebCentralVolumePacker(MangaScraper* scraper,
                                                 QNetworkAccessManager* nam,
                                                 const QString& stagingRoot,
                                                 const QString& coversDir,
                                                 QObject* parent)
    : QObject(parent), m_scraper(scraper), m_nam(nam),
      m_stagingRoot(stagingRoot), m_coversDir(coversDir)
{
    QDir().mkpath(m_stagingRoot);
    if (!m_coversDir.isEmpty()) {
        QDir().mkpath(m_coversDir);
    }
}

WeebCentralVolumePacker::~WeebCentralVolumePacker() = default;

void WeebCentralVolumePacker::requestVolume(const VolumePackRequest& req)
{
    // Defensive-depth: validate seriesId before any filesystem concatenation.
    // Upstream is expected to pass a lowercase slug per plan, but the staging
    // dir + quarantine filename both interpolate seriesId raw. Reject paths
    // that could traverse out of m_stagingRoot.
    if (req.seriesId.isEmpty()
        || req.seriesId.contains(QChar('/'))
        || req.seriesId.contains(QChar('\\'))
        || req.seriesId.contains(QStringLiteral(".."))
        || req.seriesId.contains(QChar(':'))) {
        emit volumeFailed(req.seriesId, req.volumeNumber,
                          QStringLiteral("invalid_series_id"),
                          QStringLiteral("seriesId must be a slug (no path separators)"));
        return;
    }
    if (m_paused) {
        // v1 simple behavior: short-circuit by emitting Failed so the user
        // retries after resume. Phase 11 MangaTransferCoordinator wires
        // proper resume-queueing on top.
        emit volumeFailed(req.seriesId, req.volumeNumber,
                          QStringLiteral("packer_paused"),
                          QStringLiteral("WeebCentral packer is paused; resume transfers first"));
        return;
    }
    const QString staging = stagingDirFor(m_stagingRoot, req.seriesId, req.volumeNumber);
    if (!QDir().mkpath(staging)) {
        emit volumeFailed(req.seriesId, req.volumeNumber,
                          QStringLiteral("staging_dir_create_failed"), staging);
        return;
    }

    if (req.chapterIds.isEmpty()) {
        emit volumeFailed(req.seriesId, req.volumeNumber,
                          QStringLiteral("no_chapters_for_volume"),
                          QStringLiteral("chapter list is empty"));
        return;
    }
    // PHASE 11: when MangaTransferCoordinator wires this, promote `req` to
    // std::shared_ptr<VolumePackRequest> so the per-lambda capture-by-value
    // chain stops copy-on-writing the QStringList chapterIds on every step.
    startNextChapter(req, 0, req.chapterIds.size(), staging);
}

void WeebCentralVolumePacker::startNextChapter(const VolumePackRequest& req,
                                                int chapterIdx, int totalChapters,
                                                const QString& stagingDir)
{
    if (chapterIdx >= totalChapters) {
        finalizePack(req, stagingDir);
        return;
    }
    if (!m_scraper) {
        emit volumeFailed(req.seriesId, req.volumeNumber,
                          QStringLiteral("scraper_unavailable"), QString());
        return;
    }
    const QString chapterId = req.chapterIds.at(chapterIdx);

    // The scraper emits pagesReady(QList<PageInfo>) on completion. We
    // connect once per chapter, disconnect inside the lambda on first
    // fire to avoid cross-chapter cross-talk on subsequent fetchPages
    // calls. Stored in a heap-allocated QMetaObject::Connection so the
    // lambda can reach it after copy-into-slot. Plan-adaptation:
    // MangaScraper::fetchPages takes ONE arg (chapterId), and pagesReady
    // yields QList<PageInfo>, not a (seriesId, chapterId, QStringList)
    // triple -- see plan §"CRITICAL PLAN ADAPTATIONS" in dispatch brief.
    // PHASE 11: if the scraper dies (or pagesReady never fires for any other
    // reason) the heap-allocated Connection below leaks ~16 bytes. Refactor to
    // std::shared_ptr<QMetaObject::Connection> captured by value so the
    // lambda's destruction frees it automatically. Minor for v1; matters when
    // MangaTransferCoordinator wires multi-volume serialization.
    auto* conn = new QMetaObject::Connection();
    *conn = connect(m_scraper, &MangaScraper::pagesReady,
        this, [this, conn, req, chapterIdx, totalChapters, stagingDir]
              (const QList<PageInfo>& pages) {
            QObject::disconnect(*conn);
            delete conn;

            const int total = pages.size();
            if (total == 0) {
                emit volumeFailed(req.seriesId, req.volumeNumber,
                                  QStringLiteral("zero_pages_in_chapter"),
                                  QStringLiteral("chapter returned 0 image URLs"));
                return;
            }

            // Buffer every downloaded image by its flat index; stitch per group
            // once all have arrived. shared_ptr gives heap lifetime across the
            // async per-image finished lambdas.
            auto bytesByIndex = std::make_shared<QMap<int, QByteArray>>();
            auto finished = std::make_shared<int>(0);
            auto aborted  = std::make_shared<bool>(false);

            for (int p = 0; p < total; ++p) {
                if (!m_nam) {
                    if (!*aborted) {
                        *aborted = true;
                        emit volumeFailed(req.seriesId, req.volumeNumber,
                                          QStringLiteral("nam_unavailable"), QString());
                    }
                    return;
                }
                QNetworkRequest httpReq(QUrl(pages.at(p).imageUrl));
                httpReq.setRawHeader("User-Agent", kUserAgent);
                httpReq.setRawHeader("Referer", "https://weebcentral.com/");
                httpReq.setRawHeader("Accept", "image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8");
                auto* reply = m_nam->get(httpReq);
                connect(reply, &QNetworkReply::finished,
                    this, [this, reply, p, total, pages, stagingDir, chapterIdx,
                           totalChapters, bytesByIndex, finished, aborted, req]() {
                        reply->deleteLater();
                        if (*aborted) return;
                        if (reply->error() != QNetworkReply::NoError) {
                            *aborted = true;
                            emit volumeFailed(req.seriesId, req.volumeNumber,
                                              QStringLiteral("image_fetch_failed"),
                                              reply->errorString());
                            return;
                        }
                        const QByteArray data = reply->readAll();
                        if (data.size() > kImageMaxBytes) {
                            *aborted = true;
                            emit volumeFailed(req.seriesId, req.volumeNumber,
                                              QStringLiteral("image_oversize"),
                                              QString::number(data.size()));
                            return;
                        }
                        bytesByIndex->insert(p, data);

                        if (++(*finished) != total) return;

                        // All images in; stitch per facing-pair group, in
                        // document order. One output file per group.
                        QMap<int, QList<int>> groupToIndices;  // pageGroup -> flat indices (ordered)
                        QList<int> groupOrder;                 // first-seen order of groups
                        for (int i = 0; i < pages.size(); ++i) {
                            const int g = pages.at(i).pageGroup >= 0
                                              ? pages.at(i).pageGroup
                                              : i;  // ungrouped: each its own group
                            if (!groupToIndices.contains(g)) groupOrder.append(g);
                            groupToIndices[g].append(i);
                        }

                        int seq = 0;
                        for (const int g : groupOrder) {
                            const QList<int>& members = groupToIndices[g];
                            const QString outName = QStringLiteral("%1_%2.jpg")
                                .arg(chapterIdx, 4, 10, QChar('0'))
                                .arg(seq++,      4, 10, QChar('0'));
                            const QString outPath = stagingDir + QChar('/') + outName;

                            if (members.size() == 1) {
                                // Single (cover / natively-wide spread): write bytes as-is.
                                QFile f(outPath);
                                if (!f.open(QIODevice::WriteOnly)) {
                                    if (!*aborted) { *aborted = true;
                                        emit volumeFailed(req.seriesId, req.volumeNumber,
                                                          QStringLiteral("write_failed"), outName); }
                                    return;
                                }
                                f.write(bytesByIndex->value(members.first()));
                                f.close();
                                continue;
                            }

                            // Pair: decode both halves and composite left-to-right
                            // in document order (WeebCentral renders the group in
                            // visual L->R order for the reader's RTL layout).
                            QImage left, right;
                            left.loadFromData(bytesByIndex->value(members.at(0)));
                            right.loadFromData(bytesByIndex->value(members.at(1)));
                            if (left.isNull() || right.isNull()) {
                                // Fallback: a half failed to decode — write the
                                // decodable one, or the raw first half, rather
                                // than abort the whole volume.
                                QFile f(outPath);
                                if (f.open(QIODevice::WriteOnly)) {
                                    f.write(bytesByIndex->value(members.first()));
                                    f.close();
                                }
                                continue;
                            }
                            const int h = qMax(left.height(), right.height());
                            const int w = left.width() + right.width();
                            QImage combined(w, h, QImage::Format_RGB32);
                            combined.fill(Qt::white);
                            QPainter painter(&combined);
                            painter.drawImage(QPoint(0, (h - left.height()) / 2), left);
                            painter.drawImage(QPoint(left.width(), (h - right.height()) / 2), right);
                            painter.end();
                            if (!combined.save(outPath, "JPEG", 92)) {
                                if (!*aborted) { *aborted = true;
                                    emit volumeFailed(req.seriesId, req.volumeNumber,
                                                      QStringLiteral("stitch_save_failed"), outName); }
                                return;
                            }
                        }

                        const double pct = static_cast<double>(chapterIdx + 1)
                                         / static_cast<double>(totalChapters);
                        emit volumeProgress(req.seriesId, req.volumeNumber, pct);
                        startNextChapter(req, chapterIdx + 1, totalChapters, stagingDir);
                    });
            }
        });

    // Plan adaptation: fetch in MangaPlus paired mode so PageInfo.pageGroup is
    // populated and we can stitch facing-pairs into one spread image.
    m_scraper->fetchPagesPaired(chapterId);
}

void WeebCentralVolumePacker::finalizePack(const VolumePackRequest& req, const QString& stagingDir)
{
    const QString partPath = req.destinationPath + QStringLiteral(".tankoban-part");
    if (!zipStagingDir(stagingDir, partPath)) {
        emit volumeFailed(req.seriesId, req.volumeNumber,
                          QStringLiteral("zip_failed"), stagingDir);
        return;
    }

    using namespace tankoban::manga::premium;
    const ArchiveValidationResult vr = PremiumArchiveValidator::validate(partPath, /*expectedPageCount=*/0);
    if (vr.code != ArchiveValidationCode::Ok) {
        const QString quarantineDir = QStandardPaths::writableLocation(
                                          QStandardPaths::AppDataLocation)
                                    + QStringLiteral("/manga_premium_quarantine");
        QDir().mkpath(quarantineDir);
        const QString quarantineName = QStringLiteral("%1_v%2_%3.cbz.bad")
            .arg(req.seriesId)
            .arg(req.volumeNumber, 2, 10, QChar('0'))
            .arg(QDateTime::currentMSecsSinceEpoch());
        QFile::rename(partPath, quarantineDir + QChar('/') + quarantineName);
        emit volumeFailed(req.seriesId, req.volumeNumber,
                          QStringLiteral("validation_failed"), vr.detail);
        return;
    }

    if (!QFile::rename(partPath, req.destinationPath)) {
        emit volumeFailed(req.seriesId, req.volumeNumber,
                          QStringLiteral("final_rename_failed"), req.destinationPath);
        return;
    }

    // VOLUME_X_QUALITY 2026-05-28 (Agent 1). Magazine-sourced volumes (any gray
    // chapter) AND Volume X stitch chapters that were never laid out as a
    // cohesive volume, so the reader must break pairing at each chapter
    // boundary (cover-alone). Clean (all-violet) volumes are real tankobon
    // scans split into chapters and flow correctly without breaks, so they get
    // no marker. The caller sets needsChapterPairing from the volume's quality
    // classification (was Volume-X-only before the quality-aware cutover). The
    // marker is a sidecar beside the cbz (NOT inside it) because
    // PremiumArchiveValidator rejects any non-image entry within a Premium cbz.
    // The reader reads the chapter boundaries themselves from the
    // <chapter>_<page> image filenames; this sidecar only carries the
    // needs-pairing yes/no bit.
    if (req.needsChapterPairing) {
        QFile marker(req.destinationPath + QStringLiteral(".volx"));
        if (marker.open(QIODevice::WriteOnly))
            marker.close();
    }

    // Cleanup staging dir (best-effort).
    QDir(stagingDir).removeRecursively();

    emit volumeCompleted(req.seriesId, req.volumeNumber, req.destinationPath);

    // TANKOYOMI_VOLUME_PIVOT Phase 12 -- kick off off-thread cover extraction
    // AFTER volumeCompleted, mirroring TorrentVolumeProvider's Phase 10
    // wiring. Cover extraction does NOT gate completion (per PremiumCoverExtractor
    // header note + Codex section 21); the volume is immediately readable, and
    // the cover thumbnail trickles in via volumeCoverReady.
    if (!m_coversDir.isEmpty()) {
        if (!m_coverExtractor) {
            m_coverExtractor = new premium::PremiumCoverExtractor(this);
            connect(m_coverExtractor, &premium::PremiumCoverExtractor::coverReady,
                    this, [this](const QString& s, int v, const QString& p) {
                        emit volumeCoverReady(s, v, p);
                    },
                    Qt::QueuedConnection);
            // coverFailed: log to debug + fall through silently. UI keeps the
            // AniList per-vol thumb until/unless a future re-pack succeeds.
            connect(m_coverExtractor, &premium::PremiumCoverExtractor::coverFailed,
                    this, [](const QString& s, int v, const QString& reason) {
                        qWarning("WeebCentralVolumePacker: cover extract failed seriesId=%s vol=%d reason=%s",
                                 s.toUtf8().constData(), v, reason.toUtf8().constData());
                    },
                    Qt::QueuedConnection);
        }
        // Empty coverPageHint + empty precomputed imageEntries -- extractor
        // walks the freshly-finalized cbz itself.
        m_coverExtractor->extract(req.destinationPath, req.seriesId, req.volumeNumber,
                                  m_coversDir, /*coverPageHint=*/QString(),
                                  /*precomputedImageEntries=*/QStringList{});
    }
}

// PHASE 11: real abort/cancel API. v1's m_paused only short-circuits the
// next requestVolume call; in-flight per-image lambdas continue writing to
// the staging dir until the chapter finishes. MangaTransferCoordinator
// integration is the proper home for true cancellation.
void WeebCentralVolumePacker::pauseAll()  { m_paused = true; }
void WeebCentralVolumePacker::resumeAll() { m_paused = false; }
bool WeebCentralVolumePacker::isPaused() const { return m_paused; }

} // namespace tankoban::manga
