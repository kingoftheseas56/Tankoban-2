#include "BookWalkerSeriesPageParser.h"

#include <QRegularExpression>
#include <QRegularExpressionMatchIterator>
#include <QSet>

namespace tankoban::manga::bookwalker {

QList<QString> BookWalkerSeriesPageParser::extractCoverUrls(const QString& html)
{
    static const QRegularExpression re(
        QStringLiteral(R"(data-original=["'](https://rimg\.bookwalker\.jp/[^"']+\.(?:jpg|png|webp))["'])"),
        QRegularExpression::CaseInsensitiveOption);

    QList<QString> out;
    QSet<QString> seen;
    auto it = re.globalMatch(html);
    while (it.hasNext()) {
        const QString url = it.next().captured(1);
        if (!seen.contains(url)) {
            seen.insert(url);
            out.append(url);
        }
    }
    return out;
}

QList<BookWalkerSearchHit> BookWalkerSeriesPageParser::extractSearchHits(const QString& html)
{
    static const QRegularExpression re(
        QStringLiteral(R"(<a[^>]*\bdata-series-id=["'](\d+)["'][^>]*>[\s\S]*?<img[^>]*\balt=["']([^"']+)["'])"),
        QRegularExpression::CaseInsensitiveOption);

    QList<BookWalkerSearchHit> out;
    QSet<QString> seenIds;
    auto it = re.globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        const QString id = m.captured(1);
        if (seenIds.contains(id)) continue;
        seenIds.insert(id);
        BookWalkerSearchHit hit;
        hit.seriesId = id;
        hit.title = m.captured(2);
        out.append(hit);
    }
    return out;
}

QString BookWalkerSeriesPageParser::pickSeriesIdByTitle(const QList<BookWalkerSearchHit>& hits,
                                                        const QString& targetJapaneseTitle)
{
    static const QRegularExpression stripParens(
        QStringLiteral(R"([\((\x{FF08}][^\))\x{FF09}]*[\))\x{FF09}])"),
        QRegularExpression::UseUnicodePropertiesOption);

    if (hits.isEmpty()) return QString();

    const QString needle = targetJapaneseTitle.trimmed();
    if (needle.isEmpty()) return QString();

    // Pass 1: exact match (case-insensitive) after stripping parenthetical suffix.
    for (const auto& h : hits) {
        QString normalized = h.title;
        normalized.replace(stripParens, QString());
        normalized = normalized.trimmed();
        if (normalized.compare(needle, Qt::CaseInsensitive) == 0) {
            return h.seriesId;
        }
    }

    // Pass 2: starts-with match (case-insensitive). Handles "DEATH NOTE
    // モノクロ版" vs query "Death Note" — BookWalker editions append suffix
    // words without parentheses.
    for (const auto& h : hits) {
        const QString normalized = h.title.trimmed();
        if (normalized.length() >= needle.length() &&
            normalized.left(needle.length()).compare(needle, Qt::CaseInsensitive) == 0) {
            return h.seriesId;
        }
    }

    // Pass 3 (best-effort fallback): take the first hit. BookWalker ranks by
    // relevance for exact-query searches; the first result is usually the
    // canonical entry for the series. Better than emitting no result at all.
    return hits.first().seriesId;
}

} // namespace tankoban::manga::bookwalker
