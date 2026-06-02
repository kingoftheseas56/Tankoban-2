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

// A token that looks like a publication year (4 digits, 1900-2099). Excluded
// from the volume-number identity gate — a year must never satisfy a volume
// number (Codex review 2026-06-02).
bool isYearToken(const QString& t) {
    bool ok = false;
    const int n = t.toInt(&ok);
    return ok && t.size() == 4 && n >= 1900 && n <= 2099;
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

// HARD IDENTITY GATES, not a score floor (Codex review 2026-06-02). A score
// floor let a wrong-series ("Invincible Iron Man Vol 1") or wrong-volume
// ("...Compendium Vol 2") candidate pass by sharing common tokens + the year.
// Now EVERY distinctive token of the wanted edition must be present, the format
// tier must match, and the year is never allowed to stand in for a volume
// number. Any gate miss => 0 (rejected). Fail-safe: a near-miss returns no
// download rather than the wrong one.
int scoreMatch(const QString& editionTitle, int year,
               const QString& tierLabel, const QString& candidateTitle) {
    const QStringList want = tokens(editionTitle);
    if (want.isEmpty()) return 0;
    const QString cand = candidateTitle.toLower();
    const QStringList candToks = tokens(candidateTitle);
    const QSet<QString> candSet(candToks.begin(), candToks.end());
    const QString tier = tierLabel.toLower();

    // Gate 1 — format tier (Compendium/Omnibus/TPB/...) must appear as a WHOLE
    // WORD, not a substring (Codex review 2026-06-02 r2: "contains" let
    // "compendium" match inside "CompendiumX"). A word-boundary match also
    // handles the "Vol" tier correctly, which tokens() drops as a noise word so
    // a token-set check would wrongly reject every Vol-tier edition.
    if (!tier.isEmpty()) {
        const QRegularExpression tierRe(
            QStringLiteral("\\b") + QRegularExpression::escape(tier) + QStringLiteral("\\b"));
        if (!tierRe.match(cand).hasMatch()) return 0;
    }

    // Gate 2 — every wanted token must be present in the candidate, EXCEPT the
    // tier token (gated above) and any year-like token. This forces both the
    // series identity (e.g. "invincible") AND the volume number (e.g. "1") to
    // match — so "Invincible Iron Man" fails (missing nothing? it shares
    // "invincible" but lacks "compendium" -> caught by gate 1) and
    // "Compendium Vol 2" fails (missing the wanted volume "1").
    for (const QString& w : want) {
        if (w == tier) continue;
        if (isYearToken(w)) continue;
        if (!candSet.contains(w)) return 0;
    }

    // Eligible — score only to rank among gated-pass candidates + drive tie
    // detection in pickBestMatch. Year match breaks ties toward the right printing.
    int score = want.size() * 10;
    if (year > 0 && cand.contains(QString::number(year))) score += 8;
    if (!tier.isEmpty()) score += 6;   // tier already confirmed present by gate 1
    return score;
}

SearchResult pickBestMatch(const QString& editionTitle, int year,
                           const QString& tierLabel,
                           const QList<SearchResult>& results) {
    // Only gated-pass candidates (scoreMatch > 0) are eligible. Among them take
    // the unique top score; an AMBIGUOUS TIE (>=2 sharing the top score) returns
    // empty — fail safe, never guess between two equally-plausible posts.
    SearchResult best;
    int bestScore = 0;
    int bestCount = 0;
    for (const auto& r : results) {
        const int s = scoreMatch(editionTitle, year, tierLabel, r.title);
        if (s == 0) continue;
        if (s > bestScore) { bestScore = s; best = r; bestCount = 1; }
        else if (s == bestScore) { ++bestCount; }
    }
    if (bestScore == 0 || bestCount > 1) return {};
    return best;
}

} // namespace tankoban::manga::getcomics
