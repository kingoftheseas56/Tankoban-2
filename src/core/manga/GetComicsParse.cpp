// src/core/manga/GetComicsParse.cpp
#include "GetComicsParse.h"

#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace tankoban::manga::getcomics {
namespace {

QString classifyKind(const QString& href, const QString& text) {
    QString t = text;
    t.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
    t = t.trimmed().toLower();
    if (href.startsWith(QLatin1String("magnet:"))) return QStringLiteral("magnet");
    if (t.contains(QLatin1String("main server"))) return QStringLiteral("main_server");
    if (t.contains(QLatin1String("pixeldrain")))  return QStringLiteral("pixeldrain");
    if (t.contains(QLatin1String("mediafire")))   return QStringLiteral("mediafire");
    if (t.contains(QLatin1String("mega")))        return QStringLiteral("mega");
    return QString();
}

// Lowercase alnum tokens, dropping common edition-noise words so the series
// name carries the match (year handled separately).
QStringList tokens(const QString& s) {
    static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9]+"));
    static const QSet<QString> noise = {
        QStringLiteral("vol"), QStringLiteral("volume"), QStringLiteral("the"),
        QStringLiteral("edition"), QStringLiteral("collection")};
    QStringList out;
    for (const QString& w : s.toLower().split(nonAlnum, Qt::SkipEmptyParts))
        if (!noise.contains(w)) out.push_back(w);
    return out;
}

const QStringList& priority() {
    static const QStringList p = {QStringLiteral("magnet"), QStringLiteral("main_server"),
                                  QStringLiteral("pixeldrain"), QStringLiteral("mediafire"),
                                  QStringLiteral("mega")};
    return p;
}
} // namespace

QList<DownloadLink> extractDownloads(const QString& postHtml) {
    // Delimiter changed to R"rx(...)rx" because pattern contains )" from ([^"]+)"
    static const QRegularExpression anchor(
        QStringLiteral(R"rx(<a\s+[^>]*?href="([^"]+)"[^>]*>(.*?)</a>)rx"),
        QRegularExpression::DotMatchesEverythingOption);
    QList<DownloadLink> out;
    auto it = anchor.globalMatch(postHtml);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString href = m.captured(1);
        const QString kind = classifyKind(href, m.captured(2));
        if (kind.isEmpty()) continue;
        if (href.startsWith(QLatin1String("magnet:")) ||
            href.contains(QLatin1String("getcomics.org/dls/")))
            out.push_back({kind, href});
    }
    return out;
}

DownloadLink pickBest(const QList<DownloadLink>& links) {
    for (const QString& k : priority())
        for (const auto& d : links)
            if (d.kind == k) return d;
    return {};
}

QString parsePostCover(const QString& postHtml) {
    // Delimiter changed to R"rx(...)rx" because pattern contains )" from ([^"]+)"
    static const QRegularExpression og(
        QStringLiteral(R"rx(<meta\s+property="og:image"\s+content="([^"]+)")rx"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = og.match(postHtml);
    return m.hasMatch() ? m.captured(1) : QString();
}

QList<SearchResult> parseSearchResults(const QString& searchHtml) {
    // GetComics search results: each post is an <article> whose title links the
    // post: <h1 class="post-title"><a href="<postUrl>">Title</a></h1>. The exact
    // class is pinned against a captured fixture in Task 2 — adjust there if the
    // live markup differs. Deduped by postUrl, first-seen order.
    // Delimiter changed to R"rx(...)rx" because pattern contains )" from [^"]*"
    static const QRegularExpression row(
        QStringLiteral(R"rx(<h1[^>]*class="[^"]*post-title[^"]*"[^>]*>\s*<a\s+href="([^"]+)"[^>]*>(.*?)</a>)rx"),
        QRegularExpression::DotMatchesEverythingOption);
    QList<SearchResult> out;
    QSet<QString> seen;
    auto it = row.globalMatch(searchHtml);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString url = m.captured(1);
        if (seen.contains(url)) continue;
        seen.insert(url);
        QString title = m.captured(2);
        title.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
        out.push_back({title.trimmed(), url});
    }
    return out;
}

int scoreMatch(const QString& editionTitle, int year,
               const QString& tierLabel, const QString& candidateTitle) {
    const QStringList want = tokens(editionTitle);
    if (want.isEmpty()) return 0;
    const QString cand = candidateTitle.toLower();
    const QStringList candToks = tokens(candidateTitle);
    const QSet<QString> candSet(candToks.begin(), candToks.end());

    int shared = 0;
    for (const QString& w : want)
        if (candSet.contains(w)) ++shared;
    if (shared == 0) return 0;                       // no series overlap -> reject

    int score = shared * 10;
    if (year > 0 && cand.contains(QString::number(year))) score += 8;   // year match
    if (!tierLabel.isEmpty() && cand.contains(tierLabel.toLower())) score += 6;  // tier match
    return score;
}

SearchResult pickBestMatch(const QString& editionTitle, int year,
                           const QString& tierLabel,
                           const QList<SearchResult>& results) {
    // Confidence floor: at least half the edition's significant tokens must be
    // shared (encoded as score >= ceil(half)*10). Fail safe — empty if unsure.
    const int wantCount = tokens(editionTitle).size();
    if (wantCount == 0) return {};
    const int minShared = (wantCount + 1) / 2;
    const int floor = minShared * 10;

    SearchResult best;
    int bestScore = 0;
    for (const auto& r : results) {
        const int s = scoreMatch(editionTitle, year, tierLabel, r.title);
        if (s > bestScore) { bestScore = s; best = r; }
    }
    return (bestScore >= floor) ? best : SearchResult{};
}

} // namespace tankoban::manga::getcomics
