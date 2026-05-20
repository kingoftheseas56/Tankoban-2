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
    // Fandom catalog redesign Task 17 (Phase 7, 2026-05-20). Emit always;
    // empty strings round-trip cleanly. Pre-Fandom library records that
    // deserialize without these keys default-construct to empty (see
    // fromJson below) and re-serialize with the keys present + empty —
    // that's the desired forward-only schema migration shape.
    o["wikidataQid"]         = wikidataQid;
    o["fandomWikiId"]        = fandomWikiId;
    o["fandomVolumePath"]    = fandomVolumePath;
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
    // Fandom catalog redesign Task 17 (Phase 7, 2026-05-20). QJsonObject::value
    // returns Undefined for absent keys; .toString() collapses Undefined →
    // empty string, which is the backward-compat default for pre-Fandom
    // library entries.
    r.wikidataQid         = j.value("wikidataQid").toString();
    r.fandomWikiId        = j.value("fandomWikiId").toString();
    r.fandomVolumePath    = j.value("fandomVolumePath").toString();
    return r;
}
