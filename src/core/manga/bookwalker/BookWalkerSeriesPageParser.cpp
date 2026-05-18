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

    const QString needle = targetJapaneseTitle.trimmed();
    for (const auto& h : hits) {
        QString normalized = h.title;
        normalized.replace(stripParens, QString());
        normalized = normalized.trimmed();
        if (normalized == needle) {
            return h.seriesId;
        }
    }
    return QString();
}

} // namespace tankoban::manga::bookwalker
