#include "VolumeCoverAlignment.h"

namespace tankoban::manga::bookwalker {

QMap<int, QString> VolumeCoverAlignment::align(const QList<QString>& orderedCoverUrls,
                                               int canonicalCount)
{
    QMap<int, QString> out;
    const int rawCount = orderedCoverUrls.size();
    if (rawCount == 0) return out;

    const int take = (canonicalCount > 0)
        ? qMin(canonicalCount, rawCount)
        : rawCount;

    for (int i = 0; i < take; ++i) {
        out.insert(i + 1, orderedCoverUrls[i]);
    }
    return out;
}

} // namespace tankoban::manga::bookwalker
