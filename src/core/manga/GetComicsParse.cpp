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

// A pure-number token (any run of digits) — dropped from series identity so a
// volume/range number never participates in the series-name comparison.
bool isNumberToken(const QString& t) {
    for (const QChar c : t) if (!c.isDigit()) return false;
    return !t.isEmpty();
}

// Edition-noise + collected-edition tier words. Stripped from series identity
// (so "Invincible Compendium Vol. 1" reduces to {invincible}) and, separately,
// detected as collected-edition MARKERS in isCollectedEditionOf.
const QSet<QString>& identityNoise() {
    static const QSet<QString> noise = {
        // grammatical noise
        QStringLiteral("the"), QStringLiteral("a"), QStringLiteral("an"),
        QStringLiteral("of"), QStringLiteral("and"),
        // volume/edition framing
        QStringLiteral("vol"), QStringLiteral("volume"), QStringLiteral("edition"),
        QStringLiteral("editions"), QStringLiteral("part"), QStringLiteral("parts"),
        // collected-edition tier words (also markers)
        QStringLiteral("compendium"), QStringLiteral("omnibus"),
        QStringLiteral("collection"), QStringLiteral("collected"),
        QStringLiteral("complete"), QStringLiteral("deluxe"),
        QStringLiteral("book"), QStringLiteral("books"),
        QStringLiteral("tpb"), QStringLiteral("tpbs"),
        QStringLiteral("hc"), QStringLiteral("hardcover"),
        QStringLiteral("paperback"), QStringLiteral("set"), QStringLiteral("box")};
    return noise;
}

// Decode the HTML entities GetComics emits in post titles. Numeric (&#NNNN;) +
// the few named ones that appear, so display and identity matching see real text.
QString decodeEntities(QString s) {
    static const QRegularExpression numeric(QStringLiteral("&#(x?[0-9a-fA-F]+);"));
    QString out;
    int last = 0;
    auto it = numeric.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        const QString cap = m.captured(1);
        bool ok = false;
        const uint cp = cap.startsWith(QLatin1Char('x'), Qt::CaseInsensitive)
                            ? cap.mid(1).toUInt(&ok, 16)
                            : cap.toUInt(&ok, 10);
        if (ok && cp) {
            if (cp <= 0xFFFF) {
                out += QChar(static_cast<char16_t>(cp));
            } else {
                const char32_t c = cp;
                out += QString::fromUcs4(&c, 1);
            }
        } else {
            // Unparseable numeric entity — preserve it verbatim rather than drop.
            out += m.captured(0);
        }
        last = m.capturedEnd();
    }
    out += s.mid(last);
    out.replace(QLatin1String("&amp;"),  QLatin1String("&"));
    out.replace(QLatin1String("&quot;"), QLatin1String("\""));
    out.replace(QLatin1String("&#039;"), QLatin1String("'"));
    out.replace(QLatin1String("&apos;"), QLatin1String("'"));
    return out;
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
        out.push_back({decodeEntities(title.trimmed()), url});
    }
    return out;
}

QStringList identityTokens(const QString& title) {
    static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9]+"));
    QStringList out;
    for (const QString& w : title.toLower().split(nonAlnum, Qt::SkipEmptyParts)) {
        if (identityNoise().contains(w)) continue;
        if (isYearToken(w) || isNumberToken(w)) continue;
        out.push_back(w);
    }
    return out;
}

namespace {
// A collected-edition MARKER in the candidate: a tier keyword as a whole word
// (Compendium / Omnibus / Collection / Complete / Deluxe / Book / TPB / HC).
// Distinguishes a collected edition from a single issue ("Spawn #375") which has
// none. Deliberately NOT keyed on a bare number range — a publication-year span
// like "(2009-2013)" would false-positive a single issue; every real collected
// edition carries one of these tier words, so the keyword signal is enough.
bool hasCollectedMarker(const QString& candLower) {
    static const QRegularExpression tierRe(QStringLiteral(
        "\\b(compendium|omnibus|collection|collected|complete|deluxe|"
        "book|books|tpb|tpbs|hardcover|hc|edition)\\b"));
    return tierRe.match(candLower).hasMatch();
}

// Tier rank: the more canonical/complete the collected form, the higher. Picks
// "Compendium" over a raw issue-run when both qualify for the same series.
int tierRank(const QString& candLower) {
    struct T { const char* w; int bonus; };
    static const T tiers[] = {
        {"compendium", 60}, {"omnibus", 50}, {"complete", 40},
        {"collected", 35}, {"collection", 35}, {"deluxe", 30},
        {"hardcover", 25}, {"hc", 25}, {"books", 20}, {"book", 20},
        {"tpbs", 15}, {"tpb", 15}, {"edition", 10}};
    int best = 0;
    for (const T& t : tiers) {
        const QRegularExpression re(
            QStringLiteral("\\b") + QLatin1String(t.w) + QStringLiteral("\\b"));
        if (re.match(candLower).hasMatch()) best = std::max(best, t.bonus);
    }
    return best > 0 ? best : 5;   // range-only collected edition
}
} // namespace

bool isCollectedEditionOf(const QString& seriesTitle, const QString& candidateTitle) {
    const QStringList series = identityTokens(seriesTitle);
    if (series.isEmpty()) return false;
    const QStringList cand = identityTokens(candidateTitle);
    // Gate 1 — series identity must match EXACTLY: same set, no extra series
    // words (rejects "Invincible Iron Man", "Spawn Origins"), none missing.
    const QSet<QString> seriesSet(series.begin(), series.end());
    const QSet<QString> candSet(cand.begin(), cand.end());
    if (seriesSet != candSet) return false;
    // Gate 2 — it must be a COLLECTED edition, not a single issue.
    return hasCollectedMarker(candidateTitle.toLower());
}

SearchResult pickBestCollectedEdition(const QString& seriesTitle, int year,
                                      const QList<SearchResult>& results) {
    // Rank qualifiers by collected-edition tier (+ year tie-break). Take the
    // UNIQUE top; an ambiguous tie (>=2 sharing the top rank) returns empty —
    // fail safe, never guess between two equally-canonical collected editions.
    SearchResult best;
    int bestRank = 0;
    int bestCount = 0;
    for (const auto& r : results) {
        if (!isCollectedEditionOf(seriesTitle, r.title)) continue;
        const QString candLower = r.title.toLower();
        int rank = 100 + tierRank(candLower);
        if (year > 0 && candLower.contains(QString::number(year))) rank += 8;
        if (rank > bestRank) { bestRank = rank; best = r; bestCount = 1; }
        else if (rank == bestRank) { ++bestCount; }
    }
    if (bestRank == 0 || bestCount > 1) return {};
    return best;
}

} // namespace tankoban::manga::getcomics
