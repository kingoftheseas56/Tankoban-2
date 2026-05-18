#pragma once

// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — pure-logic parser lifted from
// publishTankorentItemsForTorrent. Maps a torrent's file list to parsed episode
// or movie tuples. Stateless; no Qt signals; unit-testable in isolation.
// Spec: docs/superpowers/specs/2026-05-18-tankorent-cinemeta-pack-mapping-design.md

#include <QJsonArray>
#include <QList>
#include <QString>

namespace tankostream::stream {

struct ParsedFile {
    int     season = 0;
    int     episode = 0;
    int     fileIndex = -1;    // index in libtorrent's file list
    QString relName;           // e.g. "Daredevil.S01E03.1080p.WEB-DL.mkv"
    qint64  sizeBytes = 0;
};

struct ParsedPack {
    QString             imdbId;
    QString             type;       // "series" or "movie"
    QList<ParsedFile>   episodes;   // empty for movies
    ParsedFile          movieFile;  // valid only when type=="movie"
};

class StreamPackParser
{
public:
    // Parse a torrent's file array (from TorrentEngine::torrentFiles()) into a
    // ParsedPack. configSeason == 0 triggers multi-season probe over seasons
    // 1..kMaxSeasonProbe. Returns type=="movie" + movieFile populated if no
    // episode parses but a clear largest-video-file candidate exists.
    static ParsedPack parsePack(
        const QJsonArray& files,
        const QString& imdbId,
        int configSeason
    );

    static constexpr int kMaxSeasonProbe = 50;
};

}  // namespace tankostream::stream
