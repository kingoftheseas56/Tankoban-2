#include "CatalogueRecord.h"

QJsonObject CatalogueRecord::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("catalogueId")]   = catalogueId;
    o[QStringLiteral("isbn")]          = isbn;
    o[QStringLiteral("md5")]           = md5;
    o[QStringLiteral("title")]         = title;
    o[QStringLiteral("author")]        = author;
    o[QStringLiteral("publisher")]     = publisher;
    o[QStringLiteral("year")]          = year;
    o[QStringLiteral("language")]      = language;
    o[QStringLiteral("description")]   = description;
    QJsonArray g; for (const auto& s : genres) g.append(s);
    o[QStringLiteral("genres")]        = g;
    o[QStringLiteral("coverUrl")]      = coverUrl;
    o[QStringLiteral("cachedCoverPath")] = cachedCoverPath;
    if (!seriesId.isEmpty()) {
        o[QStringLiteral("seriesId")]       = seriesId;
        o[QStringLiteral("seriesName")]     = seriesName;
        o[QStringLiteral("seriesPosition")] = seriesPosition;
        o[QStringLiteral("seriesTotal")]    = seriesTotal;
    }
    o[QStringLiteral("filePath")]      = filePath;
    o[QStringLiteral("format")]        = format;
    o[QStringLiteral("fileSize")]      = fileSize;
    o[QStringLiteral("addedAt")]       = addedAt;
    o[QStringLiteral("readProgress")]  = readProgress;
    o[QStringLiteral("lastReadAt")]    = lastReadAt;
    o[QStringLiteral("lastReadCfi")]   = lastReadCfi;
    return o;
}

CatalogueRecord CatalogueRecord::fromJson(const QJsonObject& o)
{
    CatalogueRecord r;
    r.catalogueId     = o.value(QStringLiteral("catalogueId")).toString();
    r.isbn            = o.value(QStringLiteral("isbn")).toString();
    r.md5             = o.value(QStringLiteral("md5")).toString();
    r.title           = o.value(QStringLiteral("title")).toString();
    r.author          = o.value(QStringLiteral("author")).toString();
    r.publisher       = o.value(QStringLiteral("publisher")).toString();
    r.year            = o.value(QStringLiteral("year")).toString();
    r.language        = o.value(QStringLiteral("language")).toString();
    r.description     = o.value(QStringLiteral("description")).toString();
    QJsonArray g      = o.value(QStringLiteral("genres")).toArray();
    for (const auto& v : g) r.genres << v.toString();
    r.coverUrl        = o.value(QStringLiteral("coverUrl")).toString();
    r.cachedCoverPath = o.value(QStringLiteral("cachedCoverPath")).toString();
    r.seriesId        = o.value(QStringLiteral("seriesId")).toString();
    r.seriesName      = o.value(QStringLiteral("seriesName")).toString();
    r.seriesPosition  = o.value(QStringLiteral("seriesPosition")).toInt(0);
    r.seriesTotal     = o.value(QStringLiteral("seriesTotal")).toInt(0);
    r.filePath        = o.value(QStringLiteral("filePath")).toString();
    r.format          = o.value(QStringLiteral("format")).toString();
    r.fileSize        = o.value(QStringLiteral("fileSize")).toString();
    r.addedAt         = static_cast<qint64>(o.value(QStringLiteral("addedAt")).toDouble(0));
    r.readProgress    = o.value(QStringLiteral("readProgress")).toDouble(0.0);
    r.lastReadAt      = static_cast<qint64>(o.value(QStringLiteral("lastReadAt")).toDouble(0));
    r.lastReadCfi     = o.value(QStringLiteral("lastReadCfi")).toString();
    return r;
}
