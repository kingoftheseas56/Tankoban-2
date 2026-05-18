#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

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
    QDateTime fetchedAt{};
    int canonicalCount = 0;
    QString bookwalkerSeriesId;
    QList<BookWalkerCoverEntry> volumes;
};

} // namespace tankoban::manga::bookwalker
