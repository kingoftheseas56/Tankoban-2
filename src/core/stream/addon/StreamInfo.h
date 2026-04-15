#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include "StreamSource.h"
#include "SubtitleInfo.h"

namespace tankostream::addon {

struct StreamBehaviorHints {
    bool notWebReady = false;
    QString bingeGroup;
    QStringList countryWhitelist;

    QHash<QString, QString> proxyRequestHeaders;
    QHash<QString, QString> proxyResponseHeaders;

    QString filename;
    QString videoHash;
    qint64 videoSize = 0;

    QVariantMap other;
};

struct Stream {
    StreamSource source;
    QString name;
    QString description;
    QUrl thumbnail;
    StreamBehaviorHints behaviorHints;
    QList<SubtitleTrack> subtitles;
};

}
