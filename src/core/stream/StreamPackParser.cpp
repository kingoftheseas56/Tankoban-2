#include "StreamPackParser.h"

#include "BulkPackVerifier.h"

#include <QDir>
#include <QJsonObject>
#include <QJsonValue>
#include <algorithm>

namespace tankostream::stream {

ParsedPack StreamPackParser::parsePack(const QJsonArray& files,
                                       const QString& imdbId,
                                       int configSeason)
{
    ParsedPack pack;
    pack.imdbId = imdbId;
    pack.type = QStringLiteral("series");

    for (int fileIdx = 0; fileIdx < files.size(); ++fileIdx) {
        QJsonObject file = files.at(fileIdx).toObject();
        // Defensive: mirror BulkPackVerifier's own internal backfill pattern
        // at BulkPackVerifier.cpp:189-190 — ensure "index" is present.
        if (!file.contains(QStringLiteral("index"))) {
            file.insert(QStringLiteral("index"), fileIdx);
        }

        int detectedSeason = configSeason;
        int episodeNum = 0;
        int matchedFileIdx = 0;

        if (configSeason > 0) {
            const bool ok = BulkPackVerifier::matchEpisodeFileForSeason(
                file, configSeason, &episodeNum, &matchedFileIdx);
            if (!ok || episodeNum <= 0)
                continue;
        } else {
            for (int probeSeason = 1; probeSeason <= kMaxSeasonProbe; ++probeSeason) {
                if (BulkPackVerifier::matchEpisodeFileForSeason(
                        file, probeSeason, &episodeNum, &matchedFileIdx)
                    && episodeNum > 0) {
                    detectedSeason = probeSeason;
                    break;
                }
            }
            if (episodeNum <= 0)
                continue;
        }

        ParsedFile pf;
        pf.season = detectedSeason;
        pf.episode = episodeNum;
        pf.fileIndex = fileIdx;
        pf.relName = file.value(QStringLiteral("name")).toString();
        pf.sizeBytes = file.value(QStringLiteral("size")).toVariant().toLongLong();
        if (pf.relName.isEmpty())
            continue;
        pack.episodes.append(pf);
    }

    // Sort episodes by (season, episode) so consumers can rely on episode order.
    std::sort(pack.episodes.begin(), pack.episodes.end(),
              [](const ParsedFile& a, const ParsedFile& b) {
                  if (a.season != b.season) return a.season < b.season;
                  return a.episode < b.episode;
              });

    // Movie fallback: no episodes parsed; pick largest video file.
    if (pack.episodes.isEmpty()) {
        qint64 largestSize = 0;
        ParsedFile candidate;
        for (int fileIdx = 0; fileIdx < files.size(); ++fileIdx) {
            const QJsonObject file = files.at(fileIdx).toObject();
            const QString relName = file.value(QStringLiteral("name")).toString();
            const QString lowerName = relName.toLower();
            if (!(lowerName.endsWith(QStringLiteral(".mkv"))
                  || lowerName.endsWith(QStringLiteral(".mp4"))
                  || lowerName.endsWith(QStringLiteral(".webm"))
                  || lowerName.endsWith(QStringLiteral(".m4v"))
                  || lowerName.endsWith(QStringLiteral(".avi")))) {
                continue;
            }
            const qint64 size =
                file.value(QStringLiteral("size")).toVariant().toLongLong();
            if (size > largestSize) {
                largestSize = size;
                candidate.fileIndex = fileIdx;
                candidate.relName = relName;
                candidate.sizeBytes = size;
            }
        }
        if (largestSize > 0) {
            pack.type = QStringLiteral("movie");
            pack.movieFile = candidate;
        }
    }

    return pack;
}

}  // namespace tankostream::stream
