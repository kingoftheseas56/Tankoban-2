// src/core/manga/fandom/extractors/InfoboxExtractor.cpp

#include "InfoboxExtractor.h"

#include <QDate>
#include <QLoggingCategory>
#include <QRegularExpression>

Q_LOGGING_CATEGORY(lcInfoboxExtractor, "tankoban.manga.fandom.infobox")

namespace tankoban::manga::fandom {

namespace {

// Portable infobox cover image: <a href="<full-res>" class="image image-thumbnail">.
// Falls back to <figure class="pi-item pi-image"> <img src=...> when the link
// wrapper carries a different class set (some wikis use "pi-image-link").
const QRegularExpression kInfoboxCoverHrefRe(
    R"RX(<a href="(https://static\.wikia\.nocookie\.net/[^"]+\.(?:png|jpg|jpeg|webp)[^"]*)"\s+class="image image-thumbnail")RX"
);

const QRegularExpression kInfoboxCoverImgFallbackRe(
    R"RX(<figure class="pi-item pi-image"[^>]*>.*?<img\s+src="(https://static\.wikia\.nocookie\.net/[^"]+\.(?:png|jpg|jpeg|webp))[^"]*")RX",
    QRegularExpression::DotMatchesEverythingOption
);

// data-source="release date" + pi-data-value contains the date.
// Match anywhere in the rawHtml — pi-* class names are unique to portable
// infobox blocks so collisions are unlikely.
const QRegularExpression kInfoboxReleaseDateRe(
    R"RX(data-source="release date"[^>]*>.*?<div class="pi-data-value pi-font">\s*([^<]+?)\s*</div>)RX",
    QRegularExpression::DotMatchesEverythingOption
);

// ISBN cell: capture digits + dashes, stop at any non-ISBN character.
// Trailing <sup> footnotes ([1]) get excluded by the [^<] termination.
const QRegularExpression kInfoboxIsbnRe(
    R"RX(data-source="isbn"[^>]*>.*?<div class="pi-data-value pi-font">\s*([\d\-Xx]+))RX",
    QRegularExpression::DotMatchesEverythingOption
);

// Synopsis section body: everything between <h2 id="Synopsis"> and the next
// <h2 ... id="…"> (Chapters / References / etc.) or document end.
const QRegularExpression kInfoboxSynopsisRe(
    R"RX(<h2><span class="mw-headline" id="Synopsis"[^>]*>.*?</h2>(.*?)(?=<h2><span class="mw-headline"|$))RX",
    QRegularExpression::DotMatchesEverythingOption
);

// ────────────────────────────────────────────────────────────────────────────
// JJK-shape variants (Task 11): Jujutsu Kaisen's portable-infobox splits
// the JP and EN release into TWO data-source fields ("jp release" / "eng
// release") with the ISBN embedded as a Special:BookSources link inside
// each pi-data-value block. Title fields are explicit data-source fields
// rather than h2 headers. The english volume title lives in a pi-header h2.

const QRegularExpression kInfoboxJjkEnglishTitleRe(
    R"RX(<h2 class="pi-item pi-header[^"]*"[^>]*>([^<]+)</h2>)RX"
);

const QRegularExpression kInfoboxJjkJpTitleRe(
    R"RX(data-source="jp title"[^>]*>.*?<span lang="ja">([^<]+)</span>)RX",
    QRegularExpression::DotMatchesEverythingOption
);

const QRegularExpression kInfoboxJjkRomajiRe(
    R"RX(data-source="romaji title"[^>]*>.*?<div class="pi-data-value pi-font"><i>([^<]+)</i>)RX",
    QRegularExpression::DotMatchesEverythingOption
);

const QRegularExpression kInfoboxJjkJpReleaseDateRe(
    R"RX(data-source="jp release"[^>]*>.*?<div class="pi-data-value pi-font">\s*([A-Z][a-z]+\s+\d{1,2},?\s+\d{4}))RX",
    QRegularExpression::DotMatchesEverythingOption
);

const QRegularExpression kInfoboxJjkEnReleaseDateRe(
    R"RX(data-source="eng release"[^>]*>.*?<div class="pi-data-value pi-font">\s*([A-Z][a-z]+\s+\d{1,2},?\s+\d{4}))RX",
    QRegularExpression::DotMatchesEverythingOption
);

const QRegularExpression kInfoboxJjkJpIsbnRe(
    R"RX(data-source="jp release"[^>]*>.*?Special:BookSources/([\d\-Xx]+))RX",
    QRegularExpression::DotMatchesEverythingOption
);

const QRegularExpression kInfoboxJjkEnIsbnRe(
    R"RX(data-source="eng release"[^>]*>.*?Special:BookSources/([\d\-Xx]+))RX",
    QRegularExpression::DotMatchesEverythingOption
);

// Strip HTML tags + whitespace from a captured snippet to reveal whether
// the slot has body content. mw-empty-elt paragraphs render as <p
// class="mw-empty-elt"></p> which would survive a naive isEmpty() check.
QString stripTagsAndNormalize(const QString& html)
{
    QString s = html;
    s.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
    return s.trimmed();
}

QDate parseInfoboxDate(const QString& raw)
{
    // Kingdom uses "September 19, 2024" (US comma form). JJK + others may
    // differ — keep the same three try-formats as TableExtractor.
    QDate d = QDate::fromString(raw, QStringLiteral("MMMM d, yyyy"));
    if (d.isValid())
        return d;
    d = QDate::fromString(raw, QStringLiteral("MMMM d yyyy"));
    if (d.isValid())
        return d;
    d = QDate::fromString(raw, QStringLiteral("d MMMM yyyy"));
    return d;
}

} // anonymous

FandomVolume InfoboxExtractor::extractSingle(const QString& rawHtml,
                                             int volumeNumber,
                                             const WikiManifest& manifest)
{
    FandomVolume v;
    v.volumeNumber = volumeNumber;

    // Cover URL — try the canonical <a class="image image-thumbnail"> first,
    // fall back to the <figure>/<img> shape if the wiki uses a different
    // anchor class.
    auto coverHref = kInfoboxCoverHrefRe.match(rawHtml);
    if (coverHref.hasMatch()) {
        v.coverUrlJapanese = coverHref.captured(1);
    } else {
        auto coverImg = kInfoboxCoverImgFallbackRe.match(rawHtml);
        if (coverImg.hasMatch())
            v.coverUrlJapanese = coverImg.captured(1);
    }

    // Release date — try Kingdom-shape first (single "release date" field).
    auto date = kInfoboxReleaseDateRe.match(rawHtml);
    if (date.hasMatch()) {
        v.releaseDateJp = parseInfoboxDate(date.captured(1).trimmed());
    } else {
        // Fall back to JJK-shape ("jp release" / "eng release").
        auto jpDate = kInfoboxJjkJpReleaseDateRe.match(rawHtml);
        if (jpDate.hasMatch())
            v.releaseDateJp = parseInfoboxDate(jpDate.captured(1).trimmed());

        auto enDate = kInfoboxJjkEnReleaseDateRe.match(rawHtml);
        if (enDate.hasMatch())
            v.releaseDateEn = parseInfoboxDate(enDate.captured(1).trimmed());
    }

    // ISBN — Kingdom has a single "isbn" data-source; JJK embeds them in the
    // release-date data-source blocks.
    auto isbn = kInfoboxIsbnRe.match(rawHtml);
    if (isbn.hasMatch()) {
        v.isbnJp = isbn.captured(1);
    } else {
        auto jpIsbn = kInfoboxJjkJpIsbnRe.match(rawHtml);
        if (jpIsbn.hasMatch())
            v.isbnJp = jpIsbn.captured(1);

        auto enIsbn = kInfoboxJjkEnIsbnRe.match(rawHtml);
        if (enIsbn.hasMatch())
            v.isbnEn = enIsbn.captured(1);
    }

    // Title fields — JJK exposes English title in pi-header h2, JP kanji in
    // "jp title" data-source, romaji in "romaji title". Kingdom doesn't
    // surface these, so missing matches simply leave fields empty.
    auto enTitle = kInfoboxJjkEnglishTitleRe.match(rawHtml);
    if (enTitle.hasMatch())
        v.titleEnglish = enTitle.captured(1).trimmed();

    auto jpTitle = kInfoboxJjkJpTitleRe.match(rawHtml);
    if (jpTitle.hasMatch())
        v.titleJapanese = jpTitle.captured(1).trimmed();

    auto romaji = kInfoboxJjkRomajiRe.match(rawHtml);
    if (romaji.hasMatch())
        v.titleRomaji = romaji.captured(1).trimmed();

    // Synopsis (may be empty — Kingdom Vol.73 case; absent on JJK).
    auto syn = kInfoboxSynopsisRe.match(rawHtml);
    if (syn.hasMatch())
        v.synopsis = stripTagsAndNormalize(syn.captured(1));

    qCInfo(lcInfoboxExtractor) << "extractSingle: vol" << volumeNumber
                                << "for" << manifest.seriesId
                                << "cover=" << !v.coverUrlJapanese.isEmpty()
                                << "date=" << v.releaseDateJp
                                << "isbn=" << v.isbnJp
                                << "synopsisLen=" << v.synopsis.size();
    return v;
}

} // namespace tankoban::manga::fandom
