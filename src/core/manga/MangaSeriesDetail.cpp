#include "MangaSeriesDetail.h"
#include <QJsonArray>

QJsonObject MangaSeriesDetail::toJson() const
{
    QJsonObject o;
    // Schema version for forward-compat. Bump on breaking shape change.
    o["schema_version"] = 1;

    // Inline preview fields (don't nest — keeps the JSON flat for the library record):
    o["id"]            = preview.id;
    o["url"]           = preview.url;
    o["title"]         = preview.title;
    o["author"]        = preview.author;
    o["thumbnailUrl"]  = preview.thumbnailUrl;
    o["source"]        = preview.source;
    o["status"]        = preview.status;
    o["type"]          = preview.type;

    // Detail-level overrides (may differ from preview when the detail-page
    // scrape returns richer/fresher author/status than the search hit did).
    // fromJson restores both sides; consumers that want effective values
    // should read detail.author/detail.status and fall back to preview if
    // empty.
    o["detailAuthor"]  = author.isEmpty() ? preview.author : author;
    o["detailStatus"]  = status.isEmpty() ? preview.status : status;

    // Detail-only fields:
    o["synopsis"]      = synopsis;
    o["year"]          = year;
    o["heroCoverUrl"]  = heroCoverUrl;
    o["sourceUrl"]     = sourceUrl;

    QJsonArray genresArr;
    for (const auto& g : genres) genresArr.append(g);
    o["genres"] = genresArr;

    QJsonArray chaptersArr;
    for (const auto& ch : cachedChapters) {
        QJsonObject c;
        c["id"]            = ch.id;
        c["name"]          = ch.name;
        c["chapterNumber"] = ch.chapterNumber;
        c["dateUpload"]    = static_cast<qint64>(ch.dateUpload);
        chaptersArr.append(c);
    }
    o["cachedChapters"] = chaptersArr;
    return o;
}

MangaSeriesDetail MangaSeriesDetail::fromJson(const QJsonObject& j)
{
    MangaSeriesDetail d;
    // schema_version intentionally not branched on for v1; reserved.

    d.preview.id           = j.value("id").toString();
    d.preview.url          = j.value("url").toString();
    d.preview.title        = j.value("title").toString();
    d.preview.author       = j.value("author").toString();
    d.preview.thumbnailUrl = j.value("thumbnailUrl").toString();
    d.preview.source       = j.value("source").toString();
    d.preview.status       = j.value("status").toString();
    d.preview.type         = j.value("type").toString();

    // Detail-level overrides — fall back to preview if the key is absent
    // (older payloads that predate the detailAuthor/detailStatus split).
    d.author        = j.contains("detailAuthor")
                       ? j.value("detailAuthor").toString()
                       : d.preview.author;
    d.status        = j.contains("detailStatus")
                       ? j.value("detailStatus").toString()
                       : d.preview.status;
    d.synopsis      = j.value("synopsis").toString();
    d.year          = j.value("year").toString();
    d.heroCoverUrl  = j.value("heroCoverUrl").toString();
    d.sourceUrl     = j.value("sourceUrl").toString();

    for (const auto& v : j.value("genres").toArray()) d.genres.append(v.toString());

    for (const auto& v : j.value("cachedChapters").toArray()) {
        const auto co = v.toObject();
        ChapterInfo c;
        c.id            = co.value("id").toString();
        c.name          = co.value("name").toString();
        c.chapterNumber = co.value("chapterNumber").toDouble();
        c.dateUpload    = co.value("dateUpload").toInteger(0);
        d.cachedChapters.append(c);
    }
    return d;
}
