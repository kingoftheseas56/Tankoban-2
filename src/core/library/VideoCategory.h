#pragma once

#include <QList>
#include <QString>

enum class VideoCategory {
    TVShows,
    Anime,
    Movies,
    Sports,
    Documentaries,
    PersonalVideos,
    Miscellaneous,
};

inline bool operator<(VideoCategory lhs, VideoCategory rhs)
{
    return static_cast<int>(lhs) < static_cast<int>(rhs);
}

struct VideoCategoryInfo {
    VideoCategory category;
    const char* key;
    const char* label;
};

const QList<VideoCategoryInfo>& videoCategoryInfos();
QList<VideoCategory> allVideoCategories();
QString videoCategoryKey(VideoCategory category);
QString videoCategoryLabel(VideoCategory category);
QString videoCategoryHeading(VideoCategory category);
VideoCategory videoCategoryFromKey(const QString& key,
                                   VideoCategory fallback = VideoCategory::Miscellaneous);
