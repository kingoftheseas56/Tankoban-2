#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

namespace tankostream::addon {

struct StreamSource {
    enum class Kind {
        Url,
        Magnet,
        YouTube,
        Http,
    };

    Kind kind = Kind::Url;

    QUrl url;

    QString infoHash;
    int fileIndex = -1;
    QString fileNameHint;
    QStringList trackers;

    QString youtubeId;

    static StreamSource urlSource(const QUrl& value)
    {
        StreamSource source;
        source.kind = Kind::Url;
        source.url = value;
        return source;
    }

    static StreamSource httpSource(const QUrl& value)
    {
        StreamSource source;
        source.kind = Kind::Http;
        source.url = value;
        return source;
    }

    static StreamSource magnetSource(const QString& hash,
                                     const QStringList& trackerList = {},
                                     int selectedFileIndex = -1,
                                     const QString& hint = {})
    {
        StreamSource source;
        source.kind = Kind::Magnet;
        source.infoHash = hash;
        source.trackers = trackerList;
        source.fileIndex = selectedFileIndex;
        source.fileNameHint = hint;
        return source;
    }

    static StreamSource youtubeSource(const QString& ytId)
    {
        StreamSource source;
        source.kind = Kind::YouTube;
        source.youtubeId = ytId;
        return source;
    }

    QString toMagnetUri() const
    {
        if (kind != Kind::Magnet || infoHash.isEmpty()) {
            return {};
        }
        QString uri = QStringLiteral("magnet:?xt=urn:btih:") + infoHash;
        for (const QString& tracker : trackers) {
            uri += QStringLiteral("&tr=") +
                   QString::fromUtf8(QUrl::toPercentEncoding(tracker));
        }
        return uri;
    }
};

}
