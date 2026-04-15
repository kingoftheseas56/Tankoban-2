#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <optional>

#include "StreamInfo.h"

namespace tankostream::addon {

enum class PosterShape {
    Poster,
    Square,
    Landscape,
};

struct SeriesInfo {
    int season = 0;
    int episode = 0;
};

struct MetaLink {
    QString name;
    QString category;
    QUrl url;
};

struct MetaItemBehaviorHints {
    QString defaultVideoId;
    QString featuredVideoId;
    bool hasScheduledVideos = false;
    QVariantMap other;
};

struct Video {
    QString id;
    QString title;
    QDateTime released;
    QString overview;
    QUrl thumbnail;
    QList<Stream> streams;
    std::optional<SeriesInfo> seriesInfo;
    QList<Stream> trailerStreams;
};

struct MetaItemPreview {
    QString id;
    QString type;
    QString name;

    QUrl poster;
    QUrl background;
    QUrl logo;

    QString description;
    QString releaseInfo;
    QString runtime;
    QDateTime released;
    PosterShape posterShape = PosterShape::Poster;

    QString imdbRating;
    QStringList genres;
    QList<MetaLink> links;

    QList<Stream> trailerStreams;
    MetaItemBehaviorHints behaviorHints;
};

struct MetaItem {
    MetaItemPreview preview;
    QList<Video> videos;
};

}
