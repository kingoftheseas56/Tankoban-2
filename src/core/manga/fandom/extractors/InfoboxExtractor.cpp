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

    // Release date.
    auto date = kInfoboxReleaseDateRe.match(rawHtml);
    if (date.hasMatch())
        v.releaseDateJp = parseInfoboxDate(date.captured(1).trimmed());

    // ISBN (single — most hierarchy-model wikis only carry the JP edition's
    // ISBN per page).
    auto isbn = kInfoboxIsbnRe.match(rawHtml);
    if (isbn.hasMatch())
        v.isbnJp = isbn.captured(1);

    // Synopsis (may be empty — Kingdom Vol.73 case).
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
