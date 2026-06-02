// src/core/manga/WesternVolumeDownloader.cpp
#include "WesternVolumeDownloader.h"

#include "core/net/HttpFileDownloader.h"
#include "core/torrent/TorrentClient.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>

namespace tankoban::manga {

// ── helpers ──────────────────────────────────────────────────────────────────

static QString stateKey(const QString& seriesId, int vol)
{
    return seriesId + QLatin1Char(':') + QString::number(vol);
}

// Replace any character that is unsafe in a filename with '_'.
static QString sanitiseName(const QString& raw)
{
    QString out = raw;
    // Replace path separators and common FS-unsafe chars with underscore.
    static const QRegularExpression unsafe(QStringLiteral(R"rx([\\/:*?"<>|])rx"));
    out.replace(unsafe, QStringLiteral("_"));
    return out.trimmed();
}

// ── ctor / dtor ───────────────────────────────────────────────────────────────

WesternVolumeDownloader::WesternVolumeDownloader(QNetworkAccessManager* nam,
                                                  TorrentClient*         torrent,
                                                  QObject*               parent)
    : QObject(parent)
    , m_nam(nam)
    , m_torrent(torrent)
    , m_resolver(nam, this)
{
    // Wire resolver signals ONCE in the ctor. Correlation is done by the FIFO
    // m_pendingKeys queue (see class comment in the header).
    connect(&m_resolver, &GetComicsResolver::resolved,
            this, &WesternVolumeDownloader::onResolved);
    connect(&m_resolver, &GetComicsResolver::resolveFailed,
            this, &WesternVolumeDownloader::onResolveFailed);

    // Wire torrent signals ONCE for the magnet path.
    if (m_torrent) {
        connect(m_torrent, &TorrentClient::torrentUpdated,
                this, &WesternVolumeDownloader::onTorrentUpdated);
        connect(m_torrent, &TorrentClient::torrentCompleted,
                this, &WesternVolumeDownloader::onTorrentCompleted);
    }
}

WesternVolumeDownloader::~WesternVolumeDownloader() = default;

// ── public API ────────────────────────────────────────────────────────────────

void WesternVolumeDownloader::requestVolume(const QString& seriesId,
                                             int            volumeNumber,
                                             const QString& editionTitle,
                                             int            year,
                                             const QString& tierLabel,
                                             const QString& destinationPath)
{
    const QString key = stateKey(seriesId, volumeNumber);

    // Guard: reject duplicate in-flight request for the same (series, vol).
    if (m_states.contains(key)) {
        emit volumeFailed(seriesId, volumeNumber,
                          QStringLiteral("download already in progress"));
        return;
    }

    // Announce "resolving" state immediately.
    emit volumeProgress(seriesId, volumeNumber, 0);

    // Record state and enqueue the resolve key.
    ReqState state;
    state.key             = {seriesId, volumeNumber};
    state.editionTitle    = editionTitle;
    state.destinationPath = destinationPath;
    m_states.insert(key, state);

    m_pendingKeys.append({seriesId, volumeNumber});
    m_resolver.resolve(editionTitle, year, tierLabel);
}

// ── resolver callbacks ────────────────────────────────────────────────────────

void WesternVolumeDownloader::onResolved(const EditionDownload& dl)
{
    if (m_pendingKeys.isEmpty()) return;  // defensive
    const ReqKey rk  = m_pendingKeys.takeFirst();
    const QString key = stateKey(rk.seriesId, rk.volumeNumber);

    ReqState* state = &m_states[key];

    // Forward cover if available.
    if (!dl.coverUrl.isEmpty())
        emit coverReady(rk.seriesId, rk.volumeNumber, dl.coverUrl);

    // ── Magnet path ──────────────────────────────────────────────────────────
    if (dl.best.kind == QLatin1String("magnet") && m_torrent) {
        const QString infoHash =
            m_torrent->addMagnetHeadless(dl.best.url,
                                         QStringLiteral("comics"),
                                         state->destinationPath);
        if (infoHash.isEmpty()) {
            emit volumeFailed(rk.seriesId, rk.volumeNumber,
                              QStringLiteral("magnet add failed"));
            m_states.remove(key);
            return;
        }
        state->infoHash = infoHash;
        m_hashToKey.insert(infoHash, rk);
        // Progress will be driven by onTorrentUpdated.
        emit volumeProgress(rk.seriesId, rk.volumeNumber, 1);
        return;
    }

    // ── DDL path ─────────────────────────────────────────────────────────────
    // Walk dl.links in pickBest priority order (they are already ordered
    // as found; pickBest selected the first usable one, but for retries we
    // try ALL links). Skip the best link if it was a magnet but we have no
    // torrent client — fall through to the rest of the list.
    QList<getcomics::DownloadLink> links = dl.links;
    // Filter magnets when no torrent client is available.
    if (!m_torrent) {
        QList<getcomics::DownloadLink> filtered;
        for (const auto& lnk : std::as_const(links)) {
            if (lnk.kind != QLatin1String("magnet"))
                filtered.append(lnk);
        }
        links = filtered;
    }

    if (links.isEmpty()) {
        emit volumeFailed(rk.seriesId, rk.volumeNumber,
                          QStringLiteral("no usable download links"));
        m_states.remove(key);
        return;
    }

    state->remainingLinks = links;
    state->ddlLinkIndex   = 0;
    tryNextDdlLink(*state);
}

void WesternVolumeDownloader::onResolveFailed(const QString& reason)
{
    if (m_pendingKeys.isEmpty()) return;
    const ReqKey rk  = m_pendingKeys.takeFirst();
    const QString key = stateKey(rk.seriesId, rk.volumeNumber);
    m_states.remove(key);
    emit volumeFailed(rk.seriesId, rk.volumeNumber, reason);
}

// ── DDL helpers ───────────────────────────────────────────────────────────────

void WesternVolumeDownloader::tryNextDdlLink(ReqState& state)
{
    const ReqKey rk  = state.key;
    const QString key = stateKey(rk.seriesId, rk.volumeNumber);

    while (state.ddlLinkIndex < state.remainingLinks.size()) {
        const auto& link = state.remainingLinks.at(state.ddlLinkIndex);
        ++state.ddlLinkIndex;

        // Skip magnet links in the DDL path (we would have taken the magnet
        // branch above if m_torrent were available).
        if (link.kind == QLatin1String("magnet")) continue;

        const QString destFile =
            QDir(state.destinationPath).absoluteFilePath(
                sanitiseName(state.editionTitle) + QStringLiteral(".cbz"));

        // Create an HttpFileDownloader as a child of this (auto-cleaned up).
        auto* dl = new tankoban::net::HttpFileDownloader(m_nam, this);

        connect(dl, &tankoban::net::HttpFileDownloader::progress,
                this, [this, rk](qint64 received, qint64 total) {
                    const int pct = (total > 0)
                        ? static_cast<int>((received * 100) / total)
                        : 0;
                    emit volumeProgress(rk.seriesId, rk.volumeNumber, pct);
                });

        connect(dl, &tankoban::net::HttpFileDownloader::finished,
                this, [this, rk, key, dl](const QString& path) {
                    dl->deleteLater();
                    m_states.remove(key);
                    emit volumeCompleted(rk.seriesId, rk.volumeNumber, path);
                });

        connect(dl, &tankoban::net::HttpFileDownloader::failed,
                this, [this, rk, key, dl](const QString& /*reason*/) {
                    dl->deleteLater();
                    // Try the next link if any remain.
                    if (m_states.contains(key)) {
                        tryNextDdlLink(m_states[key]);
                    }
                });

        dl->start(link.url, destFile);
        return;  // handed off; wait for finished/failed
    }

    // All links exhausted.
    m_states.remove(key);
    emit volumeFailed(rk.seriesId, rk.volumeNumber,
                      QStringLiteral("all downloads failed"));
}

// ── torrent callbacks ─────────────────────────────────────────────────────────

void WesternVolumeDownloader::onTorrentUpdated(const QString& infoHash)
{
    if (!m_hashToKey.contains(infoHash)) return;
    if (!m_torrent) return;

    const ReqKey rk  = m_hashToKey.value(infoHash);
    const QString key = stateKey(rk.seriesId, rk.volumeNumber);
    if (!m_states.contains(key)) return;

    const QString& destPath = m_states[key].destinationPath;
    const float prog = m_torrent->downloadProgress(destPath);
    const int pct = qBound(1, static_cast<int>(prog * 100.f), 99);
    emit volumeProgress(rk.seriesId, rk.volumeNumber, pct);
}

void WesternVolumeDownloader::onTorrentCompleted(const QString& infoHash)
{
    if (!m_hashToKey.contains(infoHash)) return;

    const ReqKey rk  = m_hashToKey.take(infoHash);
    const QString key = stateKey(rk.seriesId, rk.volumeNumber);
    if (!m_states.contains(key)) return;

    const QString destPath = m_states[key].destinationPath;
    m_states.remove(key);

    // Locate the downloaded archive. Strategy:
    // 1. Use listActive() to find the TorrentInfo for this infoHash — it
    //    carries savePath + name, giving us the exact content folder
    //    (savePath/name). Scan that folder first.
    // 2. Fall back to scanning destinationPath directly.
    QString contentRoot;
    if (m_torrent) {
        const QList<TorrentInfo> active = m_torrent->listActive();
        for (const TorrentInfo& info : active) {
            if (info.infoHash == infoHash && !info.savePath.isEmpty()) {
                // Single-file torrents land directly in savePath; multi-file
                // in savePath/name. Try the compound path first; if name is
                // empty or not a dir, fall back to savePath itself.
                if (!info.name.isEmpty()) {
                    const QString compound =
                        QDir(info.savePath).absoluteFilePath(info.name);
                    if (QDir(compound).exists())
                        contentRoot = compound;
                }
                if (contentRoot.isEmpty())
                    contentRoot = info.savePath;
                break;
            }
        }
    }
    if (contentRoot.isEmpty())
        contentRoot = destPath;

    const QString archivePath = findArchiveIn(contentRoot);
    if (archivePath.isEmpty()) {
        // Last resort: scan the destinationPath itself.
        const QString fallback = findArchiveIn(destPath);
        if (fallback.isEmpty()) {
            emit volumeFailed(rk.seriesId, rk.volumeNumber,
                              QStringLiteral("torrent completed but no archive found"));
            return;
        }
        emit volumeCompleted(rk.seriesId, rk.volumeNumber, fallback);
        return;
    }
    emit volumeCompleted(rk.seriesId, rk.volumeNumber, archivePath);
}

// ── static helper ─────────────────────────────────────────────────────────────

/*static*/ QString WesternVolumeDownloader::findArchiveIn(const QString& dirPath)
{
    if (dirPath.isEmpty()) return QString();

    // Walk the directory recursively; prefer .cbz, then .cbr.
    QDirIterator it(dirPath,
                    QStringList{QStringLiteral("*.cbz"), QStringLiteral("*.cbr")},
                    QDir::Files,
                    QDirIterator::Subdirectories);

    QString bestCbz;
    QString bestCbr;
    while (it.hasNext()) {
        const QString path = it.next();
        // Split on both separators per libtorrent Windows convention.
        const QStringList parts = path.split(QRegularExpression(QStringLiteral(R"rx([\\/])rx")));
        const QString fname = parts.isEmpty() ? path : parts.last();
        if (fname.endsWith(QStringLiteral(".cbz"), Qt::CaseInsensitive)) {
            if (bestCbz.isEmpty()) bestCbz = path;
        } else if (fname.endsWith(QStringLiteral(".cbr"), Qt::CaseInsensitive)) {
            if (bestCbr.isEmpty()) bestCbr = path;
        }
    }

    return bestCbz.isEmpty() ? bestCbr : bestCbz;
}

} // namespace tankoban::manga
