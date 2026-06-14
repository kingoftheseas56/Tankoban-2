#include "core/stream/StreamLibraryCodec.h"

#include <QJsonObject>

// SIX_MODE_RESTRUCTURE Arc 2 (2026-06-07), Task 2. Pure JSON <-> struct mapping
// lifted verbatim from the former StreamLibrary::{to,from}Json, plus the two new
// fields (animeFlag/kitsuId). Legacy rows (pre-split JSON) lack the new keys, so
// they default to false / -1 — keeping every existing library entry non-anime
// until classification re-resolves it.

QJsonObject streamLibraryEntryToJson(const StreamLibraryEntry& entry)
{
    QJsonObject obj;
    obj["imdb"]        = entry.imdb;
    obj["type"]        = entry.type;
    obj["name"]        = entry.name;
    obj["year"]        = entry.year;
    obj["poster"]      = entry.poster;
    obj["description"] = entry.description;
    obj["imdbRating"]  = entry.imdbRating;
    obj["addedAt"]     = entry.addedAt;
    obj["animeFlag"]   = entry.animeFlag;
    obj["kitsuId"]     = entry.kitsuId;
    return obj;
}

StreamLibraryEntry streamLibraryEntryFromJson(const QJsonObject& obj)
{
    StreamLibraryEntry e;
    e.imdb        = obj.value("imdb").toString().trimmed();
    e.type        = obj.value("type").toString().trimmed();
    e.name        = obj.value("name").toString().trimmed();
    e.year        = obj.value("year").toString().trimmed();
    e.poster      = obj.value("poster").toString().trimmed();
    e.description = obj.value("description").toString().trimmed();
    e.imdbRating  = obj.value("imdbRating").toString().trimmed();
    e.addedAt     = obj.value("addedAt").toInteger(0);
    e.animeFlag   = obj.value("animeFlag").toBool(false);
    e.kitsuId     = obj.value("kitsuId").toInt(-1);
    return e;
}
