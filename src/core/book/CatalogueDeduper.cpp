#include "CatalogueDeduper.h"

#include <QSet>
#include <QStringList>

namespace {

QSet<QString> isbnsOf(const BookCatalogueResult& r)
{
    QSet<QString> out;
    const auto parts = r.isbn.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const auto& p : parts) {
        const QString clean = p.trimmed();
        if (!clean.isEmpty()) out.insert(clean);
    }
    return out;
}

bool isbnsOverlap(const BookCatalogueResult& a, const BookCatalogueResult& b)
{
    const auto sa = isbnsOf(a);
    if (sa.isEmpty()) return false;
    const auto sb = isbnsOf(b);
    if (sb.isEmpty()) return false;
    for (const auto& v : sa) {
        if (sb.contains(v)) return true;
    }
    return false;
}

bool fuzzyTitleAuthorEqual(const BookCatalogueResult& a, const BookCatalogueResult& b)
{
    return CatalogueDeduper::normalize(a.title)  == CatalogueDeduper::normalize(b.title)
        && CatalogueDeduper::normalize(a.author) == CatalogueDeduper::normalize(b.author);
}

void fillMissingFromLoser(BookCatalogueResult& winner, const BookCatalogueResult& loser)
{
    if (winner.description.isEmpty()) winner.description = loser.description;
    if (winner.coverUrl.isEmpty())    winner.coverUrl    = loser.coverUrl;
    if (winner.publisher.isEmpty())   winner.publisher   = loser.publisher;
    if (winner.year.isEmpty())        winner.year        = loser.year;
    if (winner.pages.isEmpty())       winner.pages       = loser.pages;
    if (winner.language.isEmpty())    winner.language    = loser.language;
    if (winner.genres.isEmpty())      winner.genres      = loser.genres;
    if (winner.isbn.isEmpty())        winner.isbn        = loser.isbn;
}

} // namespace

QString CatalogueDeduper::normalize(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        if (c.isLetterOrNumber()) out.append(c.toLower());
        else if (c.isSpace())     out.append(QLatin1Char(' '));
        // punctuation discarded
    }
    // Collapse runs of spaces.
    QString collapsed;
    bool prevSpace = false;
    for (const QChar c : out) {
        if (c == QLatin1Char(' ')) {
            if (!prevSpace) collapsed.append(c);
            prevSpace = true;
        } else {
            collapsed.append(c);
            prevSpace = false;
        }
    }
    return collapsed.trimmed();
}

QList<BookCatalogueResult> CatalogueDeduper::merge(
    const QList<BookCatalogueResult>& openlib,
    const QList<BookCatalogueResult>& googlebooks)
{
    QList<BookCatalogueResult> out = openlib;
    QList<bool> consumed(googlebooks.size(), false);

    // Pass 1: For each OpenLibrary record, look for a Google match.
    for (auto& winner : out) {
        for (int j = 0; j < googlebooks.size(); ++j) {
            if (consumed[j]) continue;
            const auto& loser = googlebooks[j];
            if (isbnsOverlap(winner, loser) || fuzzyTitleAuthorEqual(winner, loser)) {
                fillMissingFromLoser(winner, loser);
                consumed[j] = true;
                break;
            }
        }
    }

    // Pass 2: Append non-consumed Google records.
    for (int j = 0; j < googlebooks.size(); ++j) {
        if (!consumed[j]) out.append(googlebooks[j]);
    }

    return out;
}
