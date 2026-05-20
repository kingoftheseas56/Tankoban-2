#include "SeriesDetector.h"

#include <QHash>
#include <QRegularExpression>

namespace {

constexpr int kMinSeriesSize = 2; // Need 2+ siblings to call it a series.

QString trimTitleEdges(const QString& s)
{
    QString t = s;
    while (!t.isEmpty() && (t.endsWith(QLatin1Char(':')) ||
                            t.endsWith(QLatin1Char(',')) ||
                            t.endsWith(QLatin1Char('-')) ||
                            t.endsWith(QLatin1Char(' ')))) {
        t.chop(1);
    }
    return t.trimmed();
}

} // namespace

int SeriesDetector::romanToInt(const QString& s)
{
    static const QHash<QString, int> table = {
        {QStringLiteral("I"),    1}, {QStringLiteral("II"),   2},
        {QStringLiteral("III"),  3}, {QStringLiteral("IV"),   4},
        {QStringLiteral("V"),    5}, {QStringLiteral("VI"),   6},
        {QStringLiteral("VII"),  7}, {QStringLiteral("VIII"), 8},
        {QStringLiteral("IX"),   9}, {QStringLiteral("X"),   10},
    };
    auto it = table.constFind(s.toUpper());
    return it == table.constEnd() ? 0 : it.value();
}

SeriesDetector::TitleParse SeriesDetector::parseSeriesTitlePattern(const QString& title)
{
    TitleParse p;

    // Patterns ordered from most-specific to least, all using greedy base capture.
    // Stop at the first match.

    // 1. "<base> #N[: subtitle]"
    {
        static const QRegularExpression re(
            R"(^(?<base>.+?)\s*#(?<n>\d+)(?:\s*[:\-].*)?$)");
        auto m = re.match(title);
        if (m.hasMatch()) {
            p.matched = true;
            p.base = trimTitleEdges(m.captured(QStringLiteral("base")));
            p.position = m.captured(QStringLiteral("n")).toInt();
            return p;
        }
    }

    // 2. "<base>, Book N[: subtitle]" / "<base> Book N[: subtitle]"
    {
        static const QRegularExpression re(
            R"(^(?<base>.+?)\s*,?\s*Book\s+(?<n>\d+)(?:\s*[:\-].*)?$)",
            QRegularExpression::CaseInsensitiveOption);
        auto m = re.match(title);
        if (m.hasMatch()) {
            p.matched = true;
            p.base = trimTitleEdges(m.captured(QStringLiteral("base")));
            p.position = m.captured(QStringLiteral("n")).toInt();
            return p;
        }
    }

    // 3. "<base> (N)[: subtitle]"
    {
        static const QRegularExpression re(
            R"(^(?<base>.+?)\s*\((?<n>\d+)\)(?:\s*[:\-].*)?$)");
        auto m = re.match(title);
        if (m.hasMatch()) {
            p.matched = true;
            p.base = trimTitleEdges(m.captured(QStringLiteral("base")));
            p.position = m.captured(QStringLiteral("n")).toInt();
            return p;
        }
    }

    // 4. "<base> <ROMAN>[: subtitle]" (I..X)
    //    Anchored so ROMAN token must be at a word boundary before optional
    //    subtitle separator. The alternation order matches longest roman first
    //    (VIII before VII before VI, IX before I, etc.) to avoid early exit.
    {
        static const QRegularExpression re(
            R"(^(?<base>.+?)\s+(?<r>VIII|VII|VI|IV|IX|III|II|X|V|I)(?:\s*[:\-].*)?$)");
        auto m = re.match(title);
        if (m.hasMatch()) {
            const int n = romanToInt(m.captured(QStringLiteral("r")));
            if (n > 0) {
                p.matched = true;
                p.base = trimTitleEdges(m.captured(QStringLiteral("base")));
                p.position = n;
                return p;
            }
        }
    }

    // 5. "<base>: <subtitle>" — base is everything before the FIRST colon.
    //    No position signal; only useful when a sibling has the same base.
    //    Position stays 0; grouping fills it in arrival order.
    {
        const int colon = title.indexOf(QLatin1Char(':'));
        if (colon > 0) {
            p.matched = true;
            p.base = trimTitleEdges(title.left(colon));
            p.position = 0; // unknown — sibling-ordered fill at grouping time
            return p;
        }
    }

    p.matched = false;
    return p;
}

SeriesDetector::DetectionResult
SeriesDetector::detect(const QList<BookCatalogueResult>& flatResults)
{
    DetectionResult out;

    // Step 1: Bucket by (author, candidate-series-name).
    //   - First try Open Library `seriesName` field (high-confidence).
    //   - Else try title-pattern parse.
    //   - Else: standalone (author alone is not enough to group).
    struct Bucket {
        QList<BookCatalogueResult> books;
        bool fromSeriesField = false; // priority signal
    };

    // Key = author + \x1f + series-base (unit separator is a non-printable
    // control char that cannot appear in author or series name data).
    QHash<QString, Bucket> buckets;
    QList<BookCatalogueResult> unbucketed;

    for (const auto& r : flatResults) {
        if (r.author.isEmpty()) {
            unbucketed.append(r);
            continue;
        }
        // High-priority: explicit seriesName field present.
        if (!r.seriesName.isEmpty()) {
            const QString k = r.author + QLatin1Char('\x1f') + r.seriesName;
            buckets[k].books.append(r);
            buckets[k].fromSeriesField = true;
            continue;
        }
        // Title-pattern parse.
        auto tp = parseSeriesTitlePattern(r.title);
        if (tp.matched && !tp.base.isEmpty()) {
            const QString k = r.author + QLatin1Char('\x1f') + tp.base;
            auto enriched = r;
            enriched.seriesPosition = tp.position;
            if (enriched.seriesName.isEmpty()) enriched.seriesName = tp.base;
            buckets[k].books.append(enriched);
            continue;
        }
        unbucketed.append(r);
    }

    // Step 2: Promote buckets with >= kMinSeriesSize siblings to SeriesGroup.
    //         Singletons fall back to standalone.
    for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it) {
        if (it.value().books.size() < kMinSeriesSize) {
            for (const auto& r : it.value().books) {
                // Strip the speculative series fields when promoting back to standalone.
                auto bareback = r;
                bareback.isSeries = false;
                bareback.seriesName.clear();
                bareback.seriesPosition = 0;
                bareback.seriesTotal = 0;
                out.standalones.append(bareback);
            }
            continue;
        }
        SeriesGroup g;
        g.books = it.value().books;
        g.author = g.books.first().author;
        g.seriesName = g.books.first().seriesName.isEmpty()
                           ? it.key().section(QLatin1Char('\x1f'), 1, 1)
                           : g.books.first().seriesName;

        // If positions are missing (0), fill in arrival order (1-indexed).
        // Otherwise sort by position.
        bool anyMissing = false;
        for (const auto& b : g.books) {
            if (b.seriesPosition == 0) { anyMissing = true; break; }
        }
        if (anyMissing) {
            int pos = 1;
            for (auto& b : g.books) b.seriesPosition = pos++;
        } else {
            std::sort(g.books.begin(), g.books.end(),
                      [](const BookCatalogueResult& a, const BookCatalogueResult& b) {
                          return a.seriesPosition < b.seriesPosition;
                      });
        }

        // Stamp isSeries + seriesName + seriesTotal on each book.
        const int total = g.books.size();
        for (auto& b : g.books) {
            b.isSeries = true;
            b.seriesName = g.seriesName;
            b.seriesTotal = total;
        }
        out.seriesGroups.append(g);
    }

    // Step 3: Unbucketed go to standalones.
    for (const auto& r : unbucketed) {
        auto clean = r;
        clean.isSeries = false;
        out.standalones.append(clean);
    }

    return out;
}
