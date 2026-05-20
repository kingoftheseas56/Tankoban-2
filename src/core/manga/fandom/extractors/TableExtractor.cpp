// src/core/manga/fandom/extractors/TableExtractor.cpp

#include "TableExtractor.h"

#include <QDate>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStringList>

Q_LOGGING_CATEGORY(lcTableExtractor, "tankoban.manga.fandom.table")

namespace tankoban::manga::fandom {

namespace {

// Death Note header shape (Task 6, subsection-headers).
//
// The raw HTML wraps the visible title in <small><b><a>…</a></b>(JP, Romaji)</small>,
// so we anchor on the mw-headline span's `id` attribute instead — it always
// carries the un-rendered "Volume_N:_Title_(JP,_Romaji)" form that's stable
// across visual presentation tweaks.
//
// Group 1: volume number
// Group 2: id-suffix (e.g., "Boredom_(退屈,_Taikutsu)" or "How_to_Read")
const QRegularExpression kSubsectionHeaderRe(
    R"RX(<span class="mw-headline"\s+id="Volume_(\d+):_([^"]+)")RX",
    QRegularExpression::CaseInsensitiveOption
);

// Pull the JP kanji + romaji out of a title-id suffix like "Boredom_(退屈,_Taikutsu)".
// Group 1: english title (id-encoded with underscores)
// Group 2: JP kanji
// Group 3: romaji
const QRegularExpression kIdSuffixJapaneseRe(
    R"(^(.+?)_\(([^,]+),_(.+)\)$)"
);

// Release-date list-item with country-flag image marker.
// We match the localized flag filename to decide region, then capture the
// "Month D, YYYY" date that follows. Death Note uses Japan + UK flags; we
// also accept United_States flags for series like Naruto.
const QRegularExpression kReleaseDateJpRe(
    R"(Flag_of_Japan\.svg[^<]*</a></span>\s*([A-Z][a-z]+\s+\d{1,2},?\s+\d{4}))"
);

const QRegularExpression kReleaseDateEnRe(
    R"(Flag_of_(?:the_United_Kingdom|the_United_States|Canada)\.svg[^<]*</a></span>\s*([A-Z][a-z]+\s+\d{1,2},?\s+\d{4}))"
);

// Special:BookSources is Fandom's universal ISBN link target — much more
// reliable than free-form ISBN regex on the rendered text. Region inferred
// from the flag image that precedes the link.
// Tightened so the ISBN regex only matches the flag IMG immediately followed
// by a BookSources link (i.e., the ISBN section, not the Release Date section
// — both sections carry the same country-flag IMG).
const QRegularExpression kIsbnJpRe(
    R"RX(Flag_of_Japan\.svg[^<]*</a></span>\s*<a href="/wiki/Special:BookSources/([\d\-Xx]+)")RX",
    QRegularExpression::DotMatchesEverythingOption
);

const QRegularExpression kIsbnEnRe(
    R"RX(Flag_of_(?:the_United_Kingdom|the_United_States|Canada)\.svg[^<]*</a></span>\s*<a href="/wiki/Special:BookSources/([\d\-Xx]+)")RX",
    QRegularExpression::DotMatchesEverythingOption
);

// Cover URLs live on the <a class="mw-file-description image" href="<full-res>">
// wrapper (the inner <img src=...> is the scaled-down thumbnail). Death Note
// puts English cover in the first wds-tab__content (current) and Japanese in
// the second; we extract in document order and let the JP-first-or-EN-first
// per-wiki convention be encoded by `manifest.notes` for v1.x.
const QRegularExpression kFullResCoverRe(
    R"RX(<a href="(https://static\.wikia\.nocookie\.net/[^"]+\.(?:jpg|jpeg|png|webp)[^"]*)"\s+class="mw-file-description image")RX"
);

bool sectionMatchesEditionFilter(const QString& sectionTitle,
                                 const QStringList& filters)
{
    for (const QString& filter : filters) {
        if (sectionTitle.contains(filter, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

QDate parseFandomDate(const QString& raw)
{
    // Fandom dates come in both "April 2, 2004" and "April 2 2004" shapes.
    // Try the comma form first, fall back to no-comma.
    QDate d = QDate::fromString(raw, QStringLiteral("MMMM d, yyyy"));
    if (d.isValid())
        return d;
    d = QDate::fromString(raw, QStringLiteral("MMMM d yyyy"));
    return d;
}

QList<FandomVolume> extractSubsectionHeaders(const QString& rawHtml,
                                             const WikiManifest& manifest)
{
    QList<FandomVolume> volumes;

    auto headerIt = kSubsectionHeaderRe.globalMatch(rawHtml);
    QVector<QPair<int, QRegularExpressionMatch>> headers;
    while (headerIt.hasNext()) {
        auto m = headerIt.next();
        headers.append({ m.capturedStart(), m });
    }

    for (int i = 0; i < headers.size(); ++i) {
        const auto& match = headers[i].second;

        const int     volNum    = match.captured(1).toInt();
        const QString idSuffix  = match.captured(2);

        // Decode title + JP kanji + romaji from the id-suffix.
        QString english;
        QString jpKanji;
        QString romaji;

        auto jpMatch = kIdSuffixJapaneseRe.match(idSuffix);
        if (jpMatch.hasMatch()) {
            english = jpMatch.captured(1);
            jpKanji = jpMatch.captured(2);
            romaji  = jpMatch.captured(3);
        } else {
            english = idSuffix;
        }
        english.replace('_', ' ');
        romaji.replace('_', ' ');

        const QString sectionTitle = QStringLiteral("Volume %1: %2")
                                         .arg(volNum)
                                         .arg(english);

        if (sectionMatchesEditionFilter(sectionTitle, manifest.editionFilters))
            continue;

        FandomVolume v;
        v.volumeNumber  = volNum;
        v.titleEnglish  = english;
        v.titleJapanese = jpKanji;
        v.titleRomaji   = romaji;

        // Slice = from this header to the next header (or document end).
        const int sliceStart = match.capturedEnd();
        const int sliceEnd   = (i + 1 < headers.size())
                                   ? headers[i + 1].first
                                   : rawHtml.size();
        const QString slice = rawHtml.mid(sliceStart, sliceEnd - sliceStart);

        // Release dates.
        auto dJp = kReleaseDateJpRe.match(slice);
        if (dJp.hasMatch())
            v.releaseDateJp = parseFandomDate(dJp.captured(1));

        auto dEn = kReleaseDateEnRe.match(slice);
        if (dEn.hasMatch())
            v.releaseDateEn = parseFandomDate(dEn.captured(1));

        // ISBNs.
        auto iJp = kIsbnJpRe.match(slice);
        if (iJp.hasMatch())
            v.isbnJp = iJp.captured(1);

        auto iEn = kIsbnEnRe.match(slice);
        if (iEn.hasMatch())
            v.isbnEn = iEn.captured(1);

        // Covers (first = English tab, second = Japanese tab on Death Note).
        auto coverIt = kFullResCoverRe.globalMatch(slice);
        if (coverIt.hasNext())
            v.coverUrlEnglish = coverIt.next().captured(1);
        if (coverIt.hasNext())
            v.coverUrlJapanese = coverIt.next().captured(1);

        volumes.append(v);
    }

    qCInfo(lcTableExtractor) << "extractSubsectionHeaders:" << volumes.size()
                              << "volumes for" << manifest.seriesId;
    return volumes;
}

} // anonymous

QList<FandomVolume> TableExtractor::extract(const QString& rawHtml,
                                            const WikiManifest& manifest)
{
    if (manifest.groupingSemantics == QStringLiteral("subsection-headers"))
        return extractSubsectionHeaders(rawHtml, manifest);

    // mathematical-buckets / narrative-arcs / multi-era land in Tasks 7-9.
    qCWarning(lcTableExtractor)
        << "TableExtractor: unsupported groupingSemantics"
        << manifest.groupingSemantics
        << "for series" << manifest.seriesId;
    return {};
}

} // namespace tankoban::manga::fandom
