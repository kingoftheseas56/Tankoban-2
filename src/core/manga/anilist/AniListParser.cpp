// src/core/manga/anilist/AniListParser.cpp
#include "AniListParser.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

namespace tankoban::manga::anilist {

namespace {

QString pickTitle(const QJsonObject& titleObj)
{
    // Prefer English, then romaji, then native.
    if (titleObj.contains("english") && !titleObj.value("english").isNull()) {
        const QString s = titleObj.value("english").toString();
        if (!s.isEmpty()) return s;
    }
    if (titleObj.contains("romaji") && !titleObj.value("romaji").isNull()) {
        const QString s = titleObj.value("romaji").toString();
        if (!s.isEmpty()) return s;
    }
    if (titleObj.contains("native") && !titleObj.value("native").isNull()) {
        const QString s = titleObj.value("native").toString();
        if (!s.isEmpty()) return s;
    }
    return QString();
}

QStringList collectAlternateTitles(const QJsonObject& mediaObj)
{
    QStringList out;
    const QJsonObject t = mediaObj.value("title").toObject();
    for (const auto& key : { "english", "romaji", "native", "userPreferred" }) {
        const QString s = t.value(key).toString();
        if (!s.isEmpty() && !out.contains(s)) out.append(s);
    }
    const QJsonArray syn = mediaObj.value("synonyms").toArray();
    for (const auto& v : syn) {
        const QString s = v.toString();
        if (!s.isEmpty() && !out.contains(s)) out.append(s);
    }
    return out;
}

} // anonymous namespace

MediaPreview parseMediaPreviewFromJson(const QJsonObject& mediaObj)
{
    MediaPreview p;
    p.anilistId       = mediaObj.value("id").toInt();
    p.title           = pickTitle(mediaObj.value("title").toObject());
    p.alternateTitles = collectAlternateTitles(mediaObj);
    const QJsonObject cover = mediaObj.value("coverImage").toObject();
    p.coverThumbUrl   = cover.value("medium").toString();
    p.coverFullUrl    = cover.value("large").toString();
    if (p.coverFullUrl.isEmpty()) {
        p.coverFullUrl = cover.value("extraLarge").toString();
    }
    p.bannerUrl       = mediaObj.value("bannerImage").toString();
    p.format          = mediaObj.value("format").toString();
    p.status          = mediaObj.value("status").toString();
    p.yearStarted     = mediaObj.value("startDate").toObject().value("year").toInt();
    const QJsonArray genres = mediaObj.value("genres").toArray();
    for (const auto& v : genres) p.genres.append(v.toString());
    p.description     = mediaObj.value("description").toString();
    return p;
}

} // namespace tankoban::manga::anilist
