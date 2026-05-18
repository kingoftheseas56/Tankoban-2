#pragma once

#include "BookWalkerTypes.h"

#include <QList>
#include <QString>

namespace tankoban::manga::bookwalker {

class BookWalkerSeriesPageParser
{
public:
    // Series-page parsing: extract ordered, deduplicated cover URLs from data-original attrs.
    static QList<QString> extractCoverUrls(const QString& html);

    // Search-results parsing: extract (series-id, title) pairs from data-series-id + img alt attrs.
    static QList<BookWalkerSearchHit> extractSearchHits(const QString& html);

    // Disambiguation: pick the series-id whose title (after stripping parenthetical
    // publisher suffix like "（ヤングアニマル）") equals targetJapaneseTitle exactly.
    // Returns empty QString if no exact match.
    static QString pickSeriesIdByTitle(const QList<BookWalkerSearchHit>& hits,
                                       const QString& targetJapaneseTitle);
};

} // namespace tankoban::manga::bookwalker
