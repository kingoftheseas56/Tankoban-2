#pragma once
#include <QString>
#include <QStringList>

// Six-mode restructure (2026-06-07), Arc 2. anime-flag wins for films AND series;
// otherwise series -> TV, movie/unknown -> Movies.
enum class StreamMode { Anime, TV, Movies };

inline StreamMode classifyStreamMode(bool isAnime, const QString& type) {
    if (isAnime) return StreamMode::Anime;
    if (type.compare(QStringLiteral("series"), Qt::CaseInsensitive) == 0)
        return StreamMode::TV;
    return StreamMode::Movies;
}

// Animation genre AND a Japan-origin country token. country is normalized so
// "Japan", "JP", and lists like "Japan, China" / "Japan / USA" all match.
bool       isAnimeTitle(const QStringList& genres, const QString& country);
QString    streamModeKey(StreamMode mode);          // "anime" | "tv" | "movies"
StreamMode streamModeFromKey(const QString& key);   // inverse; unknown -> Movies
QString    streamLibraryFilename(StreamMode mode);  // "<key>_library.json"
