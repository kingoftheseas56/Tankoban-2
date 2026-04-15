// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 1.1 (Addon Protocol Foundation)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/addon/manifest.rs:22
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/addon/descriptor.rs:8
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/addon/request.rs:64
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/resource/meta_item.rs:152
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/resource/stream.rs:68
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/resource/subtitles.rs:9
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/CinemetaClient.h:13
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/TorrentioClient.h:10
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:230
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 1.1.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <optional>

namespace tankostream::addon {

// -----------------------------------------------------------------
// Manifest.h (AddonManifest + support types)
// -----------------------------------------------------------------

struct ManifestBehaviorHints {
    bool adult = false;
    bool p2p = false;
    bool configurable = false;
    bool configurationRequired = false;
};

struct ManifestExtraProp {
    QString name;
    bool isRequired = false;
    QStringList options;
    int optionsLimit = 1;
};

struct ManifestCatalog {
    QString id;
    QString type;
    QString name;
    QList<ManifestExtraProp> extra;
};

struct ManifestResource {
    // `name` is always present. `types`/`idPrefixes` are optional and
    // model the long-form resource variant from Stremio manifests.
    QString name;
    QStringList types;
    QStringList idPrefixes;
    bool hasTypes = false;
    bool hasIdPrefixes = false;
};

struct AddonManifest {
    QString id;
    QString version;
    QString name;
    QString contactEmail;

    QString description;       // optional
    QUrl logo;                 // optional
    QUrl background;           // optional

    QStringList types;
    QList<ManifestResource> resources;
    QList<ManifestCatalog> catalogs;
    QStringList idPrefixes;    // optional
    bool hasIdPrefixes = false;

    ManifestBehaviorHints behaviorHints;
};

// -----------------------------------------------------------------
// Descriptor.h (AddonDescriptor + flags)
// -----------------------------------------------------------------

struct AddonDescriptorFlags {
    bool official = false;
    bool enabled = true;       // Tankoban persistence flag
    bool protectedAddon = false;
};

struct AddonDescriptor {
    AddonManifest manifest;
    QUrl transportUrl;
    AddonDescriptorFlags flags;
};

// -----------------------------------------------------------------
// ResourcePath.h (ResourceRequest)
// -----------------------------------------------------------------

struct ResourceRequest {
    QString resource;
    QString type;
    QString id;
    QList<QPair<QString, QString>> extra;

    static ResourceRequest withoutExtra(
        const QString& resourceName,
        const QString& itemType,
        const QString& itemId)
    {
        return {resourceName, itemType, itemId, {}};
    }

    static ResourceRequest withExtra(
        const QString& resourceName,
        const QString& itemType,
        const QString& itemId,
        const QList<QPair<QString, QString>>& extraValues)
    {
        return {resourceName, itemType, itemId, extraValues};
    }
};

// -----------------------------------------------------------------
// SubtitleInfo.h
// -----------------------------------------------------------------

struct SubtitleTrack {
    QString id;
    QString lang;
    QUrl url;
    QString label;  // optional
};

// -----------------------------------------------------------------
// StreamSource.h + StreamInfo.h
// -----------------------------------------------------------------

struct StreamSource {
    enum class Kind {
        Url,
        Magnet,
        YouTube,
        Http,
    };

    Kind kind = Kind::Url;

    // Url/Http
    QUrl url;

    // Magnet
    QString magnetUri;
    QString infoHash;
    int fileIndex = -1;
    QString fileNameHint;
    QStringList trackers;

    // YouTube
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

    static StreamSource magnetSource(
        const QString& magnet,
        const QString& hash = {},
        int selectedFileIndex = -1,
        const QString& hint = {})
    {
        StreamSource source;
        source.kind = Kind::Magnet;
        source.magnetUri = magnet;
        source.infoHash = hash;
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
};

struct StreamBehaviorHints {
    bool notWebReady = false;
    QString bingeGroup;
    QStringList countryWhitelist;

    QHash<QString, QString> proxyRequestHeaders;
    QHash<QString, QString> proxyResponseHeaders;

    QString filename;
    QString videoHash;
    qint64 videoSize = 0;
};

struct Stream {
    StreamSource source;
    QString name;           // optional
    QString description;    // optional
    QUrl thumbnail;         // optional
    StreamBehaviorHints behaviorHints;
    QList<SubtitleTrack> subtitles;
};

// -----------------------------------------------------------------
// MetaItem.h
// -----------------------------------------------------------------

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

}  // namespace tankostream::addon
