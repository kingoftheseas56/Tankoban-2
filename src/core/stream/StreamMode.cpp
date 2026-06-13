#include "core/stream/StreamMode.h"
#include <QRegularExpression>

bool isAnimeTitle(const QStringList& genres, const QString& country) {
    bool hasAnimation = false;
    for (const QString& g : genres)
        if (g.compare(QStringLiteral("Animation"), Qt::CaseInsensitive) == 0) { hasAnimation = true; break; }
    if (!hasAnimation) return false;
    const QStringList toks = country.split(QRegularExpression(QStringLiteral("[,/]")),
                                           Qt::SkipEmptyParts);
    for (QString t : toks) {
        t = t.trimmed();
        if (t.compare(QStringLiteral("Japan"), Qt::CaseInsensitive) == 0 ||
            t.compare(QStringLiteral("JP"),    Qt::CaseInsensitive) == 0 ||
            t.compare(QStringLiteral("JPN"),   Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QString streamModeKey(StreamMode mode) {
    switch (mode) {
        case StreamMode::Anime:  return QStringLiteral("anime");
        case StreamMode::TV:     return QStringLiteral("tv");
        case StreamMode::Movies: return QStringLiteral("movies");
    }
    return QStringLiteral("movies");
}
StreamMode streamModeFromKey(const QString& key) {
    const QString k = key.toLower();
    if (k == QStringLiteral("anime")) return StreamMode::Anime;
    if (k == QStringLiteral("tv"))    return StreamMode::TV;
    return StreamMode::Movies;
}
QString streamLibraryFilename(StreamMode mode) {
    return streamModeKey(mode) + QStringLiteral("_library.json");
}
