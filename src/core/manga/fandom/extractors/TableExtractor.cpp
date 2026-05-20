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
    // Fandom dates come in three shapes across wikis:
    //   - "April 2, 2004"        (Death Note, One Piece — US style)
    //   - "April 2 2004"         (rare — comma stripped)
    //   - "26 November 1990"     (Berserk — DD Month YYYY)
    QDate d = QDate::fromString(raw, QStringLiteral("MMMM d, yyyy"));
    if (d.isValid())
        return d;
    d = QDate::fromString(raw, QStringLiteral("MMMM d yyyy"));
    if (d.isValid())
        return d;
    d = QDate::fromString(raw, QStringLiteral("d MMMM yyyy"));
    return d;
}

// ────────────────────────────────────────────────────────────────────────────
// mathematical-buckets — One Piece style (Task 7)
//
// Page layout: top-level Volume List with a sequence of range
// <h3 id="Volume_N_To_M">Volume N To M</h3> subsections, each enclosing one
// <table id="Volume&#95;K"> per volume. Each table has a "Japan" row + a "US"
// row with title / release-date / pages / ISBN cells, plus a single cover
// image (the JP tankobon).
//
// HTML entity gotcha: the table id attribute uses &#95; rather than literal
// underscore in the source, i.e., <table id="Volume&#95;1">. Regex matches
// the raw source bytes, not the entity-decoded view.

const QRegularExpression kOnePieceVolumeTableRe(
    R"RX(<table id="Volume&#95;(\d+)")RX"
);

const QRegularExpression kOnePieceJapanRowRe(
    R"RX(<th[^>]*>Japan\s*</th>\s*<td>([^<]+?)\s*</td>\s*<td>([A-Z][a-z]+\s+\d{1,2},?\s+\d{4}))RX",
    QRegularExpression::DotMatchesEverythingOption
);

const QRegularExpression kOnePieceUsRowRe(
    R"RX(<th[^>]*>US\s*</th>\s*<td>([^<]+?)\s*</td>\s*<td>([A-Z][a-z]+\s+\d{1,2},?\s+\d{4}))RX",
    QRegularExpression::DotMatchesEverythingOption
);

// One Piece BookSources links appear twice per ISBN (href + title attribute).
// Bind to the href attribute so each ISBN is captured exactly once.
const QRegularExpression kOnePieceIsbnRe(
    R"RX(href="/wiki/Special:BookSources/([\d\-Xx]+)")RX"
);

const QRegularExpression kOnePieceCoverRe(
    R"RX(<a href="(https://static\.wikia\.nocookie\.net/[^"]+\.(?:jpg|jpeg|png|webp)[^"]*)"\s+class="mw-file-description image")RX"
);

// Section-header scan for edition-filter pre-trim.
const QRegularExpression kAnyHeaderRe(
    R"RX(<h[23][^>]*>.*?<span class="mw-headline"[^>]*>([^<]+)</span>)RX",
    QRegularExpression::DotMatchesEverythingOption
);

int findFirstEditionFilterOffset(const QString& rawHtml,
                                 const QStringList& filters)
{
    int firstHit = -1;
    auto it = kAnyHeaderRe.globalMatch(rawHtml);
    while (it.hasNext()) {
        auto m = it.next();
        const QString displayed = m.captured(1).trimmed();
        if (sectionMatchesEditionFilter(displayed, filters)) {
            const int start = m.capturedStart();
            if (firstHit < 0 || start < firstHit)
                firstHit = start;
        }
    }
    return firstHit;
}

QList<FandomVolume> extractMathematicalBuckets(const QString& rawHtml,
                                               const WikiManifest& manifest)
{
    QList<FandomVolume> volumes;

    // Trim at first edition-filter header so Special Volumes / Stampede etc.
    // don't leak past the canon volume range.
    int cutoff = findFirstEditionFilterOffset(rawHtml, manifest.editionFilters);
    const QString body = (cutoff > 0) ? rawHtml.left(cutoff) : rawHtml;

    auto tableIt = kOnePieceVolumeTableRe.globalMatch(body);
    QVector<QPair<int, int>> tables; // (offset, volumeNumber)
    while (tableIt.hasNext()) {
        auto m = tableIt.next();
        tables.append({ m.capturedStart(), m.captured(1).toInt() });
    }

    for (int i = 0; i < tables.size(); ++i) {
        const int     sliceStart = tables[i].first;
        const int     volNum     = tables[i].second;
        const int     sliceEnd   = (i + 1 < tables.size())
                                       ? tables[i + 1].first
                                       : body.size();
        const QString slice = body.mid(sliceStart, sliceEnd - sliceStart);

        FandomVolume v;
        v.volumeNumber = volNum;

        // Titles + release dates from the Japan / US rows.
        auto jpRow = kOnePieceJapanRowRe.match(slice);
        if (jpRow.hasMatch()) {
            v.titleJapanese = jpRow.captured(1).trimmed();
            v.releaseDateJp = parseFandomDate(jpRow.captured(2));
        }

        auto usRow = kOnePieceUsRowRe.match(slice);
        if (usRow.hasMatch()) {
            v.titleEnglish  = usRow.captured(1).trimmed();
            v.releaseDateEn = parseFandomDate(usRow.captured(2));
        }

        // ISBNs (first = JP, second = US in document order).
        auto isbnIt = kOnePieceIsbnRe.globalMatch(slice);
        if (isbnIt.hasNext())
            v.isbnJp = isbnIt.next().captured(1);
        if (isbnIt.hasNext())
            v.isbnEn = isbnIt.next().captured(1);

        // Single tankobon cover image per volume.
        auto cover = kOnePieceCoverRe.match(slice);
        if (cover.hasMatch())
            v.coverUrlJapanese = cover.captured(1);

        volumes.append(v);
    }

    qCInfo(lcTableExtractor) << "extractMathematicalBuckets:" << volumes.size()
                              << "volumes for" << manifest.seriesId
                              << "(trimmed at offset" << cutoff << ")";
    return volumes;
}

// ────────────────────────────────────────────────────────────────────────────
// narrative-arcs — Berserk style (Task 8)
//
// Page layout: single continuous <table class="wikitable"> with TWO <tr>
// rows per volume:
//   top row    — <td id="volN"><b>N</b></td>
//                <td>arc(s) links</td>
//                <td>DD Month YYYY (JP)</td>
//                <td>DD Month YYYY (EN)</td>
//   bottom row — <td colspan=2>episode list</td>
//                <td>ISBN block: (ja) <a>…</a> (en) <a>…</a></td>
//                <td>cover image + page counts</td>
//
// No volume titles surfaced; "title" lives only at the arc level. We populate
// FandomVolume::groupingLabel with the arc text (multiple arcs joined by " / ")
// so the UI can render an arc breadcrumb without inventing fake titles.

const QRegularExpression kBerserkVolumeAnchorRe(
    R"RX(<td id="vol(\d+)"><b>\d+</b>)RX"
);

const QRegularExpression kBerserkArcLinkRe(
    R"RX(<a href="/wiki/[^"]+_Arc[^"]*"[^>]*>([^<]+)</a>)RX"
);

// "26 November 1990" — DD Month YYYY. Anchored to <td> so it only matches
// the dedicated date cells (not page counts or page numbers in episode lists).
const QRegularExpression kBerserkDateRe(
    R"RX(<td>\s*(\d{1,2}\s+[A-Z][a-z]+\s+\d{4})\s*\n?\s*</td>)RX"
);

const QRegularExpression kBerserkIsbnJaRe(
    R"RX(\(ja\)</tt></sup>\s*<a href="/wiki/Special:BookSources/([\d\-Xx]+)")RX",
    QRegularExpression::DotMatchesEverythingOption
);

const QRegularExpression kBerserkIsbnEnRe(
    R"RX(\(en\)</tt></sup>\s*<a href="/wiki/Special:BookSources/([\d\-Xx]+)")RX",
    QRegularExpression::DotMatchesEverythingOption
);

QList<FandomVolume> extractNarrativeArcs(const QString& rawHtml,
                                        const WikiManifest& manifest)
{
    QList<FandomVolume> volumes;

    // Trim at first edition-filter header (Unvolumized Episodes / Deluxe Edition).
    int cutoff = findFirstEditionFilterOffset(rawHtml, manifest.editionFilters);
    const QString body = (cutoff > 0) ? rawHtml.left(cutoff) : rawHtml;

    auto anchorIt = kBerserkVolumeAnchorRe.globalMatch(body);
    QVector<QPair<int, int>> anchors; // (offset, volumeNumber)
    while (anchorIt.hasNext()) {
        auto m = anchorIt.next();
        anchors.append({ m.capturedStart(), m.captured(1).toInt() });
    }

    for (int i = 0; i < anchors.size(); ++i) {
        const int     sliceStart = anchors[i].first;
        const int     volNum     = anchors[i].second;
        const int     sliceEnd   = (i + 1 < anchors.size())
                                       ? anchors[i + 1].first
                                       : body.size();
        const QString slice = body.mid(sliceStart, sliceEnd - sliceStart);

        FandomVolume v;
        v.volumeNumber = volNum;
        // titleEnglish / titleJapanese intentionally empty per manifest.

        // Arc(s) — possibly multiple, join with " / " for groupingLabel.
        QStringList arcs;
        auto arcIt = kBerserkArcLinkRe.globalMatch(slice);
        while (arcIt.hasNext()) {
            const QString name = arcIt.next().captured(1).trimmed();
            if (!arcs.contains(name))
                arcs.append(name);
        }
        v.groupingLabel = arcs.join(QStringLiteral(" / "));

        // Dates — first match = JP, second = EN.
        auto dateIt = kBerserkDateRe.globalMatch(slice);
        if (dateIt.hasNext())
            v.releaseDateJp = parseFandomDate(dateIt.next().captured(1));
        if (dateIt.hasNext())
            v.releaseDateEn = parseFandomDate(dateIt.next().captured(1));

        // ISBNs.
        auto iJa = kBerserkIsbnJaRe.match(slice);
        if (iJa.hasMatch())
            v.isbnJp = iJa.captured(1);

        auto iEn = kBerserkIsbnEnRe.match(slice);
        if (iEn.hasMatch())
            v.isbnEn = iEn.captured(1);

        // Cover — first mw-file-description image in slice (single tankobon cover).
        auto cover = kFullResCoverRe.match(slice);
        if (cover.hasMatch())
            v.coverUrlJapanese = cover.captured(1);

        volumes.append(v);
    }

    qCInfo(lcTableExtractor) << "extractNarrativeArcs:" << volumes.size()
                              << "volumes for" << manifest.seriesId
                              << "(trimmed at offset" << cutoff << ")";
    return volumes;
}

// ────────────────────────────────────────────────────────────────────────────
// multi-era — Naruto style (Task 9)
//
// Page layout: top-level <h2>Tankōbon</h2> with sequential <h3>Part I</h3>
// / <h3>Part II</h3> / <h3>Boruto: …</h3> / spinoff / alt-edition subsections.
// Each era restarts volume numbering, so a naive parser would collide them.
//
// v1 strategy: list all non-Part-I eras as edition-filter entries. The
// generic findFirstEditionFilterOffset() returns the offset of the first
// non-Part-I header in document order (Part II for Naruto), and we trim
// the body at that point. Everything left is Part I content.
//
// groupingLabel is the most-recent h3 era header before each volume's
// anchor — set to "Part I" for all Naruto v1 volumes.

const QRegularExpression kNarutoVolumeAnchorRe(
    R"RX(<td align="center"><b>(\d+)</b>)RX"
);

const QRegularExpression kNarutoTitleRe(
    R"RX(<td><i><a href="/wiki/[^"]+_\(volume\)"[^>]*>([^<]+?)\s*</a></i>\s*\(<span lang="ja">([^<]+)</span>&#44;\s*<i>([^<]+?)</i>\))RX",
    QRegularExpression::DotMatchesEverythingOption
);

const QRegularExpression kNarutoDateRe(
    R"RX(<td align="center">\s*(\d{1,2}\s+[A-Z][a-z]+\s+\d{4})\s*\n?\s*</td>)RX"
);

const QRegularExpression kNarutoCoverImgRe(
    R"RX(<img alt="[^"]*\(volume\)"\s+src="(https://static\.wikia\.nocookie\.net/naruto/[^"]+\.(?:png|jpg|jpeg|webp))[^"]*")RX"
);

QString findEraLabelAtOffset(const QString& body, int offset)
{
    static const QRegularExpression kH3HeaderRe(
        R"RX(<h3[^>]*>.*?<span class="mw-headline"[^>]*>([^<]+)</span>)RX",
        QRegularExpression::DotMatchesEverythingOption
    );

    QString label;
    auto it = kH3HeaderRe.globalMatch(body);
    while (it.hasNext()) {
        auto m = it.next();
        if (m.capturedStart() > offset)
            break;
        label = m.captured(1).trimmed();
    }
    return label;
}

QList<FandomVolume> extractMultiEra(const QString& rawHtml,
                                    const WikiManifest& manifest)
{
    QList<FandomVolume> volumes;

    int cutoff = findFirstEditionFilterOffset(rawHtml, manifest.editionFilters);
    const QString body = (cutoff > 0) ? rawHtml.left(cutoff) : rawHtml;

    auto anchorIt = kNarutoVolumeAnchorRe.globalMatch(body);
    QVector<QPair<int, int>> anchors;
    while (anchorIt.hasNext()) {
        auto m = anchorIt.next();
        anchors.append({ m.capturedStart(), m.captured(1).toInt() });
    }

    for (int i = 0; i < anchors.size(); ++i) {
        const int     sliceStart = anchors[i].first;
        const int     volNum     = anchors[i].second;
        const int     sliceEnd   = (i + 1 < anchors.size())
                                       ? anchors[i + 1].first
                                       : body.size();
        const QString slice = body.mid(sliceStart, sliceEnd - sliceStart);

        FandomVolume v;
        v.volumeNumber  = volNum;
        v.groupingLabel = findEraLabelAtOffset(body, sliceStart);

        auto title = kNarutoTitleRe.match(slice);
        if (title.hasMatch()) {
            v.titleEnglish  = title.captured(1).trimmed();
            v.titleJapanese = title.captured(2).trimmed();
            v.titleRomaji   = title.captured(3).trimmed();
        }

        auto dateIt = kNarutoDateRe.globalMatch(slice);
        if (dateIt.hasNext())
            v.releaseDateJp = parseFandomDate(dateIt.next().captured(1));
        if (dateIt.hasNext())
            v.releaseDateEn = parseFandomDate(dateIt.next().captured(1));

        auto cover = kNarutoCoverImgRe.match(slice);
        if (cover.hasMatch())
            v.coverUrlJapanese = cover.captured(1);

        volumes.append(v);
    }

    qCInfo(lcTableExtractor) << "extractMultiEra:" << volumes.size()
                              << "volumes for" << manifest.seriesId
                              << "(trimmed at offset" << cutoff << ")";
    return volumes;
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
    if (manifest.groupingSemantics == QStringLiteral("mathematical-buckets"))
        return extractMathematicalBuckets(rawHtml, manifest);
    if (manifest.groupingSemantics == QStringLiteral("narrative-arcs"))
        return extractNarrativeArcs(rawHtml, manifest);
    if (manifest.groupingSemantics == QStringLiteral("multi-era"))
        return extractMultiEra(rawHtml, manifest);

    // Anything else is genuinely unsupported.
    qCWarning(lcTableExtractor)
        << "TableExtractor: unsupported groupingSemantics"
        << manifest.groupingSemantics
        << "for series" << manifest.seriesId;
    return {};
}

} // namespace tankoban::manga::fandom
