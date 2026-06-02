// src/core/manga/WesternVolumeDownloader.cpp
#include "WesternVolumeDownloader.h"

#include "core/net/HttpFileDownloader.h"
#include "core/torrent/TorrentClient.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace tankoban::manga {

// ── helpers ──────────────────────────────────────────────────────────────────

static QString stateKey(const QString& seriesId, int vol)
{
    return seriesId + QLatin1Char(':') + QString::number(vol);
}

// True if the file begins with a known comic-archive magic number — ZIP (.cbz)
// or RAR (.cbr). Guards the DDL path against a hoster that serves an HTML
// landing page instead of the file (e.g. a MEGA / Mediafire interstitial):
// HttpFileDownloader would otherwise save that HTML as a bogus ".cbz" and we
// would report a finished comic that is unreadable (smoke 2026-06-02).
static bool looksLikeArchive(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray head = f.read(4);
    f.close();
    if (head.size() < 4) return false;
    if (head.startsWith("PK"))   return true;   // ZIP family (CBZ)
    if (head.startsWith("Rar!")) return true;   // RAR (CBR)
    return false;
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
                                             const QString& seriesTitle,
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

    // Record state and enqueue for the (serialised) resolve step.
    ReqState state;
    state.key             = {seriesId, volumeNumber};
    state.seriesTitle     = seriesTitle;
    state.year            = year;
    state.tierLabel       = tierLabel;
    state.destinationPath = destinationPath;
    m_states.insert(key, state);

    m_resolveQueue.append({seriesId, volumeNumber});
    pumpResolveQueue();
}

// ── serialised resolve pump ─────────────────────────────────────────────────

void WesternVolumeDownloader::pumpResolveQueue()
{
    // At most one resolve in flight — GetComicsResolver does not serialise and
    // its signals carry no identity, so we serialise here to keep onResolved
    // unambiguous (Codex review 2026-06-02).
    if (m_resolveInFlight || m_resolveQueue.isEmpty()) return;

    // Skip any queued keys whose state was already removed (defensive).
    while (!m_resolveQueue.isEmpty()) {
        const ReqKey rk = m_resolveQueue.takeFirst();
        const QString key = stateKey(rk.seriesId, rk.volumeNumber);
        if (!m_states.contains(key)) continue;
        const ReqState& st = m_states[key];
        m_activeResolveKey = rk;
        m_resolveInFlight  = true;
        m_resolver.resolve(st.seriesTitle, st.year, st.tierLabel);
        return;
    }
}

// ── resolver callbacks ────────────────────────────────────────────────────────

void WesternVolumeDownloader::onResolved(const EditionDownload& dl)
{
    if (!m_resolveInFlight) return;  // defensive — no resolve was in flight
    const ReqKey rk   = m_activeResolveKey;
    const QString key = stateKey(rk.seriesId, rk.volumeNumber);
    m_resolveInFlight = false;       // this resolve is done; allow the next

    if (!m_states.contains(key)) { pumpResolveQueue(); return; }

    // Forward cover if available.
    if (!dl.coverUrl.isEmpty())
        emit coverReady(rk.seriesId, rk.volumeNumber, dl.coverUrl);

    // Surface the matched collected-edition title so the UI can show exactly
    // what is being downloaded (it is the collected edition, not the per-TPB).
    if (!dl.matchedTitle.isEmpty())
        emit volumeResolved(rk.seriesId, rk.volumeNumber, dl.matchedTitle);

    // ── Magnet path ──────────────────────────────────────────────────────────
    if (dl.best.kind == QLatin1String("magnet") && m_torrent) {
        // Duplicate-infoHash guard (Codex review): if another active request
        // already owns this infoHash, fail this one rather than overwrite the
        // hash->key map and orphan the first.
        const QString destinationPath = m_states[key].destinationPath;
        const QString infoHash =
            m_torrent->addMagnetHeadless(dl.best.url,
                                         QStringLiteral("comics"),
                                         destinationPath);
        if (infoHash.isEmpty()) {
            // Remove state BEFORE emitting (Codex review r2): a synchronous retry
            // from the volumeFailed handler must not hit the in-progress guard.
            m_states.remove(key);
            emit volumeFailed(rk.seriesId, rk.volumeNumber,
                              QStringLiteral("magnet add failed"));
            pumpResolveQueue();
            return;
        }
        if (m_hashToKey.contains(infoHash)) {
            m_states.remove(key);
            emit volumeFailed(rk.seriesId, rk.volumeNumber,
                              QStringLiteral("this torrent is already downloading for another edition"));
            pumpResolveQueue();
            return;
        }
        m_states[key].infoHash = infoHash;
        m_hashToKey.insert(infoHash, rk);
        emit volumeProgress(rk.seriesId, rk.volumeNumber, 1);
        pumpResolveQueue();          // resolve done — start the next queued resolve
        return;
    }

    // ── DDL path ─────────────────────────────────────────────────────────────
    // Try ALL non-magnet links in order (pickBest priority); fall through on
    // each failure. Magnets are skipped here (no torrent client / not best).
    QList<getcomics::DownloadLink> links;
    for (const auto& lnk : std::as_const(dl.links))
        if (lnk.kind != QLatin1String("magnet"))
            links.append(lnk);

    if (links.isEmpty()) {
        m_states.remove(key);   // remove before emit (Codex review r2 — retry-safe)
        emit volumeFailed(rk.seriesId, rk.volumeNumber,
                          QStringLiteral("no usable download links"));
        pumpResolveQueue();
        return;
    }

    m_states[key].remainingLinks = links;
    m_states[key].ddlLinkIndex   = 0;
    pumpResolveQueue();              // resolve done — let the next resolve start
    tryNextDdlLink(key);             // downloads run concurrently with later resolves
}

void WesternVolumeDownloader::onResolveFailed(const QString& reason)
{
    if (!m_resolveInFlight) return;
    const ReqKey rk   = m_activeResolveKey;
    const QString key = stateKey(rk.seriesId, rk.volumeNumber);
    m_resolveInFlight = false;
    m_states.remove(key);
    emit volumeFailed(rk.seriesId, rk.volumeNumber, reason);
    pumpResolveQueue();
}

// ── DDL helpers ───────────────────────────────────────────────────────────────

void WesternVolumeDownloader::tryNextDdlLink(const QString& key)
{
    // Operate BY KEY (Codex review): never hold a ReqState& across an erase.
    if (!m_states.contains(key)) return;
    ReqState& state   = m_states[key];
    const ReqKey rk   = state.key;                 // copy for emits + post-erase use
    const QString destFolder = state.destinationPath;
    // The DDL file is named after the series (the collected edition is the unit).
    const QString seriesTitle = state.seriesTitle;

    while (state.ddlLinkIndex < state.remainingLinks.size()) {
        const getcomics::DownloadLink link = state.remainingLinks.at(state.ddlLinkIndex);  // copy
        ++state.ddlLinkIndex;

        if (link.kind == QLatin1String("magnet")) continue;   // DDL path only

        const QString destFile =
            QDir(destFolder).absoluteFilePath(
                sanitiseName(seriesTitle) + QStringLiteral(".cbz"));

        // HttpFileDownloader as a child of this (auto-cleaned up if this dies).
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
                    dl->disconnect(this);     // no re-entry through this downloader
                    dl->deleteLater();
                    // Content guard: a hoster may serve an HTML landing page (MEGA
                    // / Mediafire interstitial) rather than the archive. Discard it
                    // and try the next link instead of reporting a bogus comic.
                    if (!looksLikeArchive(path)) {
                        QFile::remove(path);
                        if (m_states.contains(key)) tryNextDdlLink(key);
                        return;
                    }
                    m_states.remove(key);
                    emit volumeCompleted(rk.seriesId, rk.volumeNumber, path);
                });

        connect(dl, &tankoban::net::HttpFileDownloader::failed,
                this, [this, key, dl](const QString& /*reason*/) {
                    dl->disconnect(this);
                    dl->deleteLater();
                    // Try the next link if the request is still alive (by key).
                    if (m_states.contains(key))
                        tryNextDdlLink(key);
                });

        dl->start(link.url, destFile);
        return;  // handed off; wait for finished/failed
    }

    // All links exhausted — rk/key already copied, safe to erase then emit.
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
