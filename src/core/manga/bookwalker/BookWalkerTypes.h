#pragma once

#include <QList>
#include <QString>
#include <QDateTime>

namespace tankoban::manga::bookwalker {

struct BookWalkerSearchHit {
    QString seriesId;
    QString title;
};

struct BookWalkerCoverEntry {
    int volume = 0;
    QString url;
};

struct BookWalkerCacheRecord {
    int schemaVersion = 1;
    QDateTime fetchedAt;
    int canonicalCount = 0;
    QString bookwalkerSeriesId;
    QList<BookWalkerCoverEntry> volumes;
};

} // namespace tankoban::manga::bookwalker
