// STREAM_DOWNLOADED_LIBRARY 2026-05-10 Phase 6 — first-launch migration scanner.
// See header + spec §9 for the full contract.

#include "StreamRescueScanner.h"

#include "StreamDownloadIndex.h"
#include "StreamLibrary.h"
#include "MetaAggregator.h"
#include "addon/MetaItem.h"

#include "core/JsonStore.h"
#include "core/DebugLogBuffer.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

namespace {

// Spec §9.2 patterns.
const QRegularExpression kSeasonFolderRe(QStringLiteral("^Season \\d{2,3}$"));
const QRegularExpression kEpisodeFileRe(
    QStringLiteral("^(.+) - S(\\d{2,3})E(\\d{2,3}) - (.+)\\.(mkv|mp4|webm|m4v)$"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kYearSuffixRe(QStringLiteral("\\s*\\(\\d{4}\\)\\s*$"));

QString sanitizeShowName(const QString& raw)
{
    QString s = raw;
    s.remove(kYearSuffixRe);
    return s.trimmed();
}

struct EpisodeCandidate {
    QString showFolderName;
    QString showFolderPath;
    QString canonicalPath;
    int     season  = 0;
    int     episode = 0;
};

QList<EpisodeCandidate> findCandidatesUnderRoot(const QString& root,
                                                std::atomic<bool>& cancelled)
{
    QList<EpisodeCandidate> out;
    QDirIterator showIt(root, QDir::Dirs | QDir::NoDotAndDotDot,
                        QDirIterator::NoIteratorFlags);
    while (showIt.hasNext()) {
        if (cancelled.load()) return out;
        const QString showPath = showIt.next();
        const QFileInfo showInfo(showPath);
        const QString showFolder = showInfo.fileName();

        QDirIterator seasonIt(showPath, QDir::Dirs | QDir::NoDotAndDotDot,
                              QDirIterator::NoIteratorFlags);
        while (seasonIt.hasNext()) {
            if (cancelled.load()) return out;
            const QString seasonPath = seasonIt.next();
            const QFileInfo seasonInfo(seasonPath);
            if (!kSeasonFolderRe.match(seasonInfo.fileName()).hasMatch())
                continue;

            QDirIterator fileIt(seasonPath, QDir::Files,
                                QDirIterator::NoIteratorFlags);
            while (fileIt.hasNext()) {
                if (cancelled.load()) return out;
                const QString filePath = fileIt.next();
                const QFileInfo fi(filePath);
                const QRegularExpressionMatch m = kEpisodeFileRe.match(fi.fileName());
                if (!m.hasMatch()) continue;

                const QString fileShowTitle = m.captured(1);
                // Spec §9.2 sanity check — file's showTitle capture must
                // match the show folder name (after sanitization).
                if (sanitizeShowName(fileShowTitle).compare(
                        sanitizeShowName(showFolder), Qt::CaseInsensitive) != 0)
                    continue;

                EpisodeCandidate c;
                c.showFolderName = showFolder;
                c.showFolderPath = showPath;
                c.canonicalPath  = filePath;
                c.season  = m.captured(2).toInt();
                c.episode = m.captured(3).toInt();
                out.append(c);
            }
        }
    }
    return out;
}

} // namespace

StreamRescueScanner::StreamRescueScanner(StreamDownloadIndex* index,
                                         StreamLibrary* library,
                                         tankostream::stream::MetaAggregator* meta,
                                         JsonStore* metaStore,
                                         const QStringList& videoRoots,
                                         QObject* parent)
    : QObject(parent)
    , m_index(index)
    , m_library(library)
    , m_meta(meta)
    , m_metaStore(metaStore)
    , m_videoRoots(videoRoots)
{
}

void StreamRescueScanner::cancel()
{
    m_cancelled.store(true);
}

void StreamRescueScanner::start()
{
    DebugLogBuffer::instance().info(QStringLiteral("stream-rescue"),
        QString("scan-start roots=%1").arg(m_videoRoots.size()));

    (void) QtConcurrent::run([this]() {
        Stats stats;

        // ── Step 1 — discover candidates across all roots ──────────────────
        QList<EpisodeCandidate> all;
        for (const QString& root : m_videoRoots) {
            if (m_cancelled.load()) break;
            all.append(findCandidatesUnderRoot(root, m_cancelled));
        }

        // Group by show folder name → list of episodes.
        QHash<QString, QList<EpisodeCandidate>> byShow;
        for (const auto& c : all)
            byShow[c.showFolderName].append(c);

        const int totalShows = byShow.size();
        stats.showsScanned = totalShows;

        DebugLogBuffer::instance().info(QStringLiteral("stream-rescue"),
            QString("candidates discovered total_files=%1 unique_shows=%2")
                .arg(all.size()).arg(totalShows));

        int idx = 0;
        bool anyShowProcessed = false;

        // ── Step 2 — Cinemeta lookup + register per show ───────────────────
        for (auto it = byShow.constBegin(); it != byShow.constEnd(); ++it) {
            if (m_cancelled.load()) break;
            ++idx;
            const QString showFolderName = it.key();
            const QString cleanName      = sanitizeShowName(showFolderName);

            // Progress emit (marshalled to GUI thread).
            const int idxCopy = idx;
            const int totalCopy = totalShows;
            QMetaObject::invokeMethod(this, [this, idxCopy, totalCopy, showFolderName]() {
                emit progressUpdate(idxCopy, totalCopy, showFolderName);
            }, Qt::QueuedConnection);

            // Cinemeta search via the existing reentrant searchByTitle API.
            // We dispatch onto the MetaAggregator's owning thread (GUI thread)
            // and block this worker on a QEventLoop for completion. The
            // callback fires on the GUI thread; loop.quit() is thread-safe
            // to call from any thread, so the worker resumes correctly.
            QList<tankostream::addon::MetaItemPreview> results;
            QString searchError;
            std::atomic<bool> searchDone{false};

            QEventLoop loop;
            QTimer timeout;
            timeout.setSingleShot(true);
            QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

            QPointer<tankostream::stream::MetaAggregator> safeMeta(m_meta);
            QMetaObject::invokeMethod(m_meta, [safeMeta, cleanName,
                                                &results, &searchError,
                                                &searchDone, &loop]() {
                if (!safeMeta) {
                    searchDone.store(true);
                    loop.quit();
                    return;
                }
                safeMeta->searchByTitle(cleanName, QStringLiteral("series"),
                    [&results, &searchError, &searchDone, &loop](
                        const QList<tankostream::addon::MetaItemPreview>& res,
                        const QString& err) {
                        results = res;
                        searchError = err;
                        searchDone.store(true);
                        loop.quit();
                    });
            }, Qt::QueuedConnection);

            timeout.start(8000);
            loop.exec();

            if (!searchDone.load()) {
                ++stats.showsNetworkFailure;
                DebugLogBuffer::instance().info(QStringLiteral("stream-rescue"),
                    QString("net-timeout show=%1").arg(cleanName));
                continue;
            }
            if (results.isEmpty()) {
                if (!searchError.isEmpty()) {
                    ++stats.showsNetworkFailure;
                    DebugLogBuffer::instance().info(QStringLiteral("stream-rescue"),
                        QString("net-failure show=%1 err=%2").arg(cleanName, searchError));
                } else {
                    ++stats.showsUnmatched;
                    DebugLogBuffer::instance().info(QStringLiteral("stream-rescue"),
                        QString("unmatched show=%1").arg(cleanName));
                }
                continue;
            }

            // Pick highest-imdbRating series-type result. Spec §9.3.
            tankostream::addon::MetaItemPreview chosen = results.first();
            double bestRating = chosen.imdbRating.toDouble();
            for (const auto& r : results) {
                if (!r.type.isEmpty()
                    && r.type != QStringLiteral("series")) continue;
                const double rr = r.imdbRating.toDouble();
                if (rr > bestRating) {
                    bestRating = rr;
                    chosen = r;
                }
            }
            if (results.size() > 1) ++stats.showsAmbiguous;
            ++stats.showsMatched;
            anyShowProcessed = true;

            // ── Step 3 — register per-episode + StreamLibrary materialize ──
            // Both StreamDownloadIndex::registerEpisode and StreamLibrary::add
            // are conventionally called on the GUI thread (per index's
            // threading docs + library's QMutex protects but JsonStore
            // writes coalesce). Marshal a single batch.
            const QString chosenImdb = chosen.id;
            const QString chosenName = chosen.name;
            const QString chosenYear = chosen.releaseInfo;
            const QString chosenPoster = chosen.poster.toString();
            const QString chosenDesc = chosen.description;
            const QString chosenRating = chosen.imdbRating;
            const QList<EpisodeCandidate> episodes = it.value();

            int episodesAddedThisShow = 0;
            QMetaObject::invokeMethod(this, [this, chosenImdb, chosenName,
                                              chosenYear, chosenPoster,
                                              chosenDesc, chosenRating,
                                              episodes, &episodesAddedThisShow]() {
                if (m_index) {
                    for (const auto& c : episodes) {
                        const qint64 size = QFileInfo(c.canonicalPath).size();
                        if (size <= 0) continue;
                        m_index->registerEpisode(chosenImdb, c.season, c.episode,
                                                 c.canonicalPath,
                                                 QString(),  // empty groupId for migration
                                                 size);
                        ++episodesAddedThisShow;
                    }
                }
                if (m_library && !m_library->has(chosenImdb)) {
                    StreamLibraryEntry entry;
                    entry.imdb        = chosenImdb;
                    entry.type        = QStringLiteral("series");
                    entry.name        = chosenName;
                    entry.year        = chosenYear;
                    entry.poster      = chosenPoster;
                    entry.description = chosenDesc;
                    entry.imdbRating  = chosenRating;
                    entry.addedAt     = QDateTime::currentMSecsSinceEpoch();
                    m_library->add(entry);
                }
            }, Qt::BlockingQueuedConnection);
            stats.episodesRegistered += episodesAddedThisShow;
        }

        // ── Step 4 — write migration-version pin + emit complete ────────────
        // STREAM_BULK_DOWNLOAD_V2 2026-05-10 — pin on ANY non-cancelled
        // completion, not just runs that processed shows. Original gate
        // (`anyShowProcessed`) caused empty Videos folders / unmatchable
        // shows to re-fire the migration on every Tankoban launch — Hemanth
        // saw "Done. Added 0 shows / 0 episodes" repeatedly. A non-cancelled
        // walk that reached this line is a successful "nothing to migrate"
        // outcome; persisting the pin honors that.
        if (!m_cancelled.load() && m_metaStore) {
            QJsonObject meta;
            meta[QStringLiteral("migrationVersion")] = 1;
            meta[QStringLiteral("completedAt")] =
                static_cast<double>(QDateTime::currentMSecsSinceEpoch());
            QJsonObject st;
            st[QStringLiteral("shows_matched")]       = stats.showsMatched;
            st[QStringLiteral("shows_unmatched")]     = stats.showsUnmatched;
            st[QStringLiteral("shows_ambiguous")]     = stats.showsAmbiguous;
            st[QStringLiteral("shows_net_failure")]   = stats.showsNetworkFailure;
            st[QStringLiteral("episodes_registered")] = stats.episodesRegistered;
            meta[QStringLiteral("stats")] = st;
            m_metaStore->write(QStringLiteral("stream_downloads_meta.json"), meta);

            DebugLogBuffer::instance().info(QStringLiteral("stream-rescue"),
                QString("scan-pin matched=%1 unmatched=%2 ambig=%3 netfail=%4 eps=%5")
                    .arg(stats.showsMatched).arg(stats.showsUnmatched)
                    .arg(stats.showsAmbiguous).arg(stats.showsNetworkFailure)
                    .arg(stats.episodesRegistered));
        } else {
            DebugLogBuffer::instance().info(QStringLiteral("stream-rescue"),
                QString("scan-end-no-pin cancelled=%1 anyShow=%2")
                    .arg(m_cancelled.load()).arg(anyShowProcessed));
        }

        QMetaObject::invokeMethod(this, [this, stats]() {
            emit complete(stats);
        }, Qt::QueuedConnection);
    });
}
