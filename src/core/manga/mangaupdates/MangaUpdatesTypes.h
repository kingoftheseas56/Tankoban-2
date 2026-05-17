#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace tankoban::manga::mangaupdates {

struct MangaUpdatesSearchHit {
    qint64      seriesId    = 0;
    QString     title;
    QStringList altTitles;
    QStringList authors;
    int         yearStarted = 0;
    QString     description;
    QString     imageUrl;
};

struct MangaUpdatesSeriesInfo {
    qint64    seriesId      = 0;
    QString   title;
    QString   rawStatus;
    int       volumeCount   = 0;
    int       latestChapter = 0;
    bool      completed     = false;
    QString   description;
    QString   imageUrl;
    QDateTime lastUpdated;
    qint64    fetchedAtMs   = 0;
};

} // namespace tankoban::manga::mangaupdates
