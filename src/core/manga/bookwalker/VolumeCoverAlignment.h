#pragma once

#include <QList>
#include <QMap>
#include <QString>

namespace tankoban::manga::bookwalker {

class VolumeCoverAlignment
{
public:
    // Map BookWalker URLs to canonical volume indices [1..N].
    // - canonicalCount > 0: take first canonicalCount URLs (drop overflow); shortfall is honest.
    // - canonicalCount == 0: degraded mode -- map every URL as-is at indices [1..rawCount].
    // - urls empty: returns empty map.
    static QMap<int, QString> align(const QList<QString>& orderedCoverUrls,
                                    int canonicalCount);
};

} // namespace tankoban::manga::bookwalker
