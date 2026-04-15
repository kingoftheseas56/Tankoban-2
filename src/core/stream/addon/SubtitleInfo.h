#pragma once

#include <QString>
#include <QUrl>

namespace tankostream::addon {

struct SubtitleTrack {
    QString id;
    QString lang;
    QUrl url;
    QString label;
};

}
