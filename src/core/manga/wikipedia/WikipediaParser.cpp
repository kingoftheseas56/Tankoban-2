// src/core/manga/wikipedia/WikipediaParser.cpp

#include "WikipediaParser.h"

#include <QDate>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

Q_LOGGING_CATEGORY(lcWikipediaParser, "tankoban.manga.wikipedia.parser")

namespace tankoban::manga::wikipedia {

namespace {

using tankoban::manga::fandom::FandomVolume;

// Volume anchor: <th scope="row" id="vol(\d+)" …>N</th>
const QRegularExpression kWpVolumeAnchorRe(
    R"RX(<th[^>]*id="vol(\d+)"[^>]*>\s*\d+\s*</th>)RX"
);

// Title cell: <td …><i><i>English</i></i><br /><i>Romaji</i> (kanji)</td>
const QRegularExpression kWpTitleCellRe(
    R"RX(<td[^>]*>\s*<i><i>([^<]+)</i></i>\s*<br\s*/?>\s*<i>([^<]+)</i>\s*\(([^)]+)\)\s*</td>)RX",
    QRegularExpression::DotMatchesEverythingOption
);

// Release date: starts a <td>, "Month Day, Year", may have trailing <sup> /
// <br /> / ISBN link. Capture just the date string.
const QRegularExpression kWpReleaseDateRe(
    R"RX(<td>\s*([A-Z][a-z]+\s+\d{1,2},?\s+\d{4}))RX"
);

// ISBN within a <td>: <a href="/wiki/Special:BookSources/<isbn>">…
const QRegularExpression kWpIsbnRe(
    R"RX(href="/wiki/Special:BookSources/([\d\-Xx]+)")RX"
);

QDate parseWpDate(const QString& raw)
{
    QDate d = QDate::fromString(raw, QStringLiteral("MMMM d, yyyy"));
    if (d.isValid())
        return d;
    d = QDate::fromString(raw, QStringLiteral("MMMM d yyyy"));
    return d;
}

} // anonymous

QList<FandomVolume> parseVolumeTable(const QString& rawHtml)
{
    QList<FandomVolume> volumes;

    QVector<QPair<int, int>> anchors;
    auto it = kWpVolumeAnchorRe.globalMatch(rawHtml);
    while (it.hasNext()) {
        auto m = it.next();
        anchors.append({ m.capturedStart(), m.captured(1).toInt() });
    }

    for (int i = 0; i < anchors.size(); ++i) {
        const int     sliceStart = anchors[i].first;
        const int     volNum     = anchors[i].second;
        const int     sliceEnd   = (i + 1 < anchors.size())
                                       ? anchors[i + 1].first
                                       : rawHtml.size();
        const QString slice = rawHtml.mid(sliceStart, sliceEnd - sliceStart);

        FandomVolume v;
        v.volumeNumber = volNum;

        auto title = kWpTitleCellRe.match(slice);
        if (title.hasMatch()) {
            v.titleEnglish  = title.captured(1).trimmed();
            v.titleRomaji   = title.captured(2).trimmed();
            v.titleJapanese = title.captured(3).trimmed();
        }

        auto dateIt = kWpReleaseDateRe.globalMatch(slice);
        if (dateIt.hasNext())
            v.releaseDateJp = parseWpDate(dateIt.next().captured(1));
        if (dateIt.hasNext())
            v.releaseDateEn = parseWpDate(dateIt.next().captured(1));

        auto isbnIt = kWpIsbnRe.globalMatch(slice);
        if (isbnIt.hasNext())
            v.isbnJp = isbnIt.next().captured(1);
        if (isbnIt.hasNext())
            v.isbnEn = isbnIt.next().captured(1);

        volumes.append(v);
    }

    qCInfo(lcWikipediaParser) << "parseVolumeTable extracted" << volumes.size()
                               << "volumes";
    return volumes;
}

} // namespace tankoban::manga::wikipedia
