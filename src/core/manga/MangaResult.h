#pragma once

#include <QString>
#include <QMetaType>

struct MangaResult {
    QString id;
    QString url;
    QString title;
    QString author;
    QString thumbnailUrl;
    QString source;       // "weebcentral" or "readcomicsonline"
    QString status;       // "Ongoing", "Completed", etc.
    QString type;         // "manga" or "comic"
};
Q_DECLARE_METATYPE(MangaResult)

struct ChapterInfo {
    QString id;
    QString url;
    QString name;
    double  chapterNumber = 0.0;
    qint64  dateUpload    = 0;   // ms epoch
    QString source;
};
Q_DECLARE_METATYPE(ChapterInfo)

struct PageInfo {
    int     index = 0;
    QString imageUrl;
};
Q_DECLARE_METATYPE(PageInfo)
