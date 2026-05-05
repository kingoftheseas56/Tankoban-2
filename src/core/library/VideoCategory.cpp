#include "VideoCategory.h"

#include <QStringList>

const QList<VideoCategoryInfo>& videoCategoryInfos()
{
    static const QList<VideoCategoryInfo> infos = {
        {VideoCategory::TVShows,        "tv_shows",        "TV Shows"},
        {VideoCategory::Anime,          "anime",           "Anime"},
        {VideoCategory::Movies,         "movies",          "Movies"},
        {VideoCategory::Sports,         "sports",          "Sports"},
        {VideoCategory::Documentaries,  "documentaries",   "Documentaries"},
        {VideoCategory::PersonalVideos, "personal_videos", "Personal Videos"},
        {VideoCategory::Miscellaneous,  "miscellaneous",   "Miscellaneous"},
    };
    return infos;
}

QList<VideoCategory> allVideoCategories()
{
    QList<VideoCategory> out;
    for (const auto& info : videoCategoryInfos())
        out.append(info.category);
    return out;
}

QString videoCategoryKey(VideoCategory category)
{
    for (const auto& info : videoCategoryInfos()) {
        if (info.category == category)
            return QString::fromLatin1(info.key);
    }
    return QStringLiteral("miscellaneous");
}

QString videoCategoryLabel(VideoCategory category)
{
    for (const auto& info : videoCategoryInfos()) {
        if (info.category == category)
            return QString::fromLatin1(info.label);
    }
    return QStringLiteral("Miscellaneous");
}

QString videoCategoryHeading(VideoCategory category)
{
    return videoCategoryLabel(category).toUpper();
}

VideoCategory videoCategoryFromKey(const QString& key, VideoCategory fallback)
{
    for (const auto& info : videoCategoryInfos()) {
        if (key.compare(QString::fromLatin1(info.key), Qt::CaseInsensitive) == 0)
            return info.category;
    }
    return fallback;
}
