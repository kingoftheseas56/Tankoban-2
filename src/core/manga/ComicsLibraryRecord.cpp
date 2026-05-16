#include "ComicsLibraryRecord.h"

QJsonObject ComicsLibraryRecord::toJson() const
{
    QJsonObject o;
    o["sourceId"]            = sourceId;
    o["seriesId"]            = seriesId;
    o["title"]               = title;
    o["origin"]              = origin;
    o["rootFolder"]          = rootFolder;
    o["seriesFolderName"]    = seriesFolderName;
    o["canonicalSeriesPath"] = canonicalSeriesPath;
    o["coverPath"]           = coverPath;
    o["detailCache"]         = detailCache.toJson();
    // Phase 1 polish lesson applied: native qint64 for ms-epoch timestamps
    // rather than QString::number/.toString().toLongLong() round-trip.
    o["addedAt"]             = static_cast<qint64>(addedAt);
    o["lastValidatedAt"]     = static_cast<qint64>(lastValidatedAt);
    return o;
}

ComicsLibraryRecord ComicsLibraryRecord::fromJson(const QJsonObject& j)
{
    ComicsLibraryRecord r;
    r.sourceId            = j.value("sourceId").toString();
    r.seriesId            = j.value("seriesId").toString();
    r.title               = j.value("title").toString();
    r.origin              = j.value("origin").toString();
    r.rootFolder          = j.value("rootFolder").toString();
    r.seriesFolderName    = j.value("seriesFolderName").toString();
    r.canonicalSeriesPath = j.value("canonicalSeriesPath").toString();
    r.coverPath           = j.value("coverPath").toString();
    r.detailCache         = MangaSeriesDetail::fromJson(j.value("detailCache").toObject());
    r.addedAt             = j.value("addedAt").toInteger(0);
    r.lastValidatedAt     = j.value("lastValidatedAt").toInteger(0);
    return r;
}
