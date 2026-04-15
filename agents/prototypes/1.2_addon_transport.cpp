// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 1.2 (Addon Protocol Foundation)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/addon_transport/http_transport/http_transport.rs:33
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/addon/request.rs:64
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/query_params_encode.rs:5
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/addon/manifest.rs:22
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/addon/descriptor.rs:8
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/CinemetaClient.cpp:36
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/TorrentioClient.cpp:158
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/pages/StreamPage.cpp:230
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 1.2.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace tankostream::addon {

// -----------------------------------------------------------------
// Minimal type slice from Batch 1.1 for a self-contained prototype.
// -----------------------------------------------------------------

struct ManifestBehaviorHints {
    bool adult = false;
    bool p2p = false;
    bool configurable = false;
    bool configurationRequired = false;
};

struct ManifestCatalog {
    QString id;
    QString type;
    QString name;
};

struct ManifestResource {
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
    QString description;
    QUrl logo;
    QUrl background;
    QStringList types;
    QList<ManifestResource> resources;
    QList<ManifestCatalog> catalogs;
    QStringList idPrefixes;
    bool hasIdPrefixes = false;
    ManifestBehaviorHints behaviorHints;
};

struct AddonDescriptorFlags {
    bool official = false;
    bool enabled = true;
    bool protectedAddon = false;
};

struct AddonDescriptor {
    AddonManifest manifest;
    QUrl transportUrl;   // canonical: .../manifest.json
    AddonDescriptorFlags flags;
};

struct ResourceRequest {
    QString resource;
    QString type;
    QString id;
    QList<QPair<QString, QString>> extra;
};

// -----------------------------------------------------------------
// AddonTransport prototype
// -----------------------------------------------------------------

class AddonTransport : public QObject {
    Q_OBJECT

public:
    explicit AddonTransport(QObject* parent = nullptr)
        : QObject(parent)
        , m_nam(new QNetworkAccessManager(this))
    {
    }

    void fetchManifest(const QUrl& base)
    {
        const QUrl manifestUrl = normalizeManifestUrl(base);
        if (!manifestUrl.isValid()) {
            emit manifestFailed("Invalid addon URL");
            return;
        }

        QNetworkRequest req(manifestUrl);
        req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
        req.setRawHeader("Accept", "application/json,*/*");
        req.setTransferTimeout(kTimeoutMs);

        QNetworkReply* reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, manifestUrl]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                emit manifestFailed(reply->errorString());
                return;
            }

            QJsonParseError parseErr;
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseErr);
            if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
                emit manifestFailed("Manifest JSON parse error: " + parseErr.errorString());
                return;
            }

            AddonDescriptor descriptor;
            descriptor.transportUrl = manifestUrl;
            descriptor.flags.official = false;
            descriptor.flags.enabled = true;
            descriptor.flags.protectedAddon = false;

            if (!parseManifest(doc.object(), descriptor.manifest)) {
                emit manifestFailed("Manifest validation failed");
                return;
            }

            emit manifestReady(descriptor);
        });
    }

    void fetchResource(const QUrl& base, const ResourceRequest& request)
    {
        const QUrl url = buildResourceUrl(base, request);
        if (!url.isValid()) {
            emit resourceFailed(request, "Invalid resource URL");
            return;
        }

        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
        req.setRawHeader("Accept", "application/json,*/*");
        req.setTransferTimeout(kTimeoutMs);

        QNetworkReply* reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, request]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                emit resourceFailed(request, reply->errorString());
                return;
            }

            QJsonParseError parseErr;
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseErr);
            if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
                emit resourceFailed(request, "Resource JSON parse error: " + parseErr.errorString());
                return;
            }

            emit resourceReady(request, doc.object());
        });
    }

signals:
    void manifestReady(const AddonDescriptor& descriptor);
    void manifestFailed(const QString& message);
    void resourceReady(const ResourceRequest& request, const QJsonObject& payload);
    void resourceFailed(const ResourceRequest& request, const QString& message);

private:
    static constexpr int kTimeoutMs = 10000;
    static constexpr const char* kUserAgent =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36";

    static QUrl normalizeManifestUrl(QUrl base)
    {
        if (!base.isValid())
            return {};

        QString s = base.toString(QUrl::FullyDecoded).trimmed();
        if (s.isEmpty())
            return {};

        if (s.endsWith("/manifest.json", Qt::CaseInsensitive))
            return QUrl(s);

        if (s.endsWith('/'))
            s.chop(1);

        return QUrl(s + "/manifest.json");
    }

    static QUrl baseWithoutManifest(const QUrl& base)
    {
        QUrl manifest = normalizeManifestUrl(base);
        QString s = manifest.toString(QUrl::FullyDecoded);
        if (s.endsWith("/manifest.json", Qt::CaseInsensitive))
            s.chop(QString("/manifest.json").size());
        return QUrl(s);
    }

    static QString encodeComponent(const QString& value)
    {
        return QString::fromUtf8(QUrl::toPercentEncoding(value));
    }

    // Batch TODO contract:
    //   {base}/{resource}/{type}/{id}[/{extra}].json
    // extras segment is comma-joined key=value pairs with percent-encoding.
    static QString encodeExtraSegment(const QList<QPair<QString, QString>>& extra)
    {
        QStringList parts;
        parts.reserve(extra.size());
        for (const auto& kv : extra) {
            const QString encodedKey = encodeComponent(kv.first);
            const QString encodedValue = encodeComponent(kv.second);
            parts.push_back(encodedKey + "=" + encodedValue);
        }
        return parts.join(",");
    }

    static QUrl buildResourceUrl(const QUrl& base, const ResourceRequest& request)
    {
        const QUrl root = baseWithoutManifest(base);
        if (!root.isValid())
            return {};

        QString path = "/" + encodeComponent(request.resource)
            + "/" + encodeComponent(request.type)
            + "/" + encodeComponent(request.id);

        if (!request.extra.isEmpty())
            path += "/" + encodeExtraSegment(request.extra);

        path += ".json";

        QString rootString = root.toString(QUrl::FullyDecoded);
        if (rootString.endsWith('/'))
            rootString.chop(1);
        return QUrl(rootString + path);
    }

    static bool parseManifest(const QJsonObject& obj, AddonManifest& out)
    {
        out.id = obj.value("id").toString().trimmed();
        out.version = obj.value("version").toString().trimmed();
        out.name = obj.value("name").toString().trimmed();
        out.description = obj.value("description").toString().trimmed();
        out.logo = QUrl(obj.value("logo").toString().trimmed());
        out.background = QUrl(obj.value("background").toString().trimmed());

        if (out.id.isEmpty() || out.name.isEmpty() || out.version.isEmpty())
            return false;

        for (const QJsonValue& item : obj.value("types").toArray()) {
            const QString type = item.toString().trimmed();
            if (!type.isEmpty())
                out.types.push_back(type);
        }

        if (obj.contains("idPrefixes")) {
            out.hasIdPrefixes = true;
            for (const QJsonValue& item : obj.value("idPrefixes").toArray()) {
                const QString prefix = item.toString().trimmed();
                if (!prefix.isEmpty())
                    out.idPrefixes.push_back(prefix);
            }
        }

        for (const QJsonValue& item : obj.value("resources").toArray()) {
            ManifestResource resource;
            if (item.isString()) {
                resource.name = item.toString().trimmed();
            } else {
                const QJsonObject ro = item.toObject();
                resource.name = ro.value("name").toString().trimmed();
                if (ro.contains("types")) {
                    resource.hasTypes = true;
                    for (const QJsonValue& t : ro.value("types").toArray()) {
                        const QString type = t.toString().trimmed();
                        if (!type.isEmpty())
                            resource.types.push_back(type);
                    }
                }
                if (ro.contains("idPrefixes")) {
                    resource.hasIdPrefixes = true;
                    for (const QJsonValue& p : ro.value("idPrefixes").toArray()) {
                        const QString prefix = p.toString().trimmed();
                        if (!prefix.isEmpty())
                            resource.idPrefixes.push_back(prefix);
                    }
                }
            }

            if (!resource.name.isEmpty())
                out.resources.push_back(resource);
        }

        for (const QJsonValue& item : obj.value("catalogs").toArray()) {
            const QJsonObject catObj = item.toObject();
            ManifestCatalog catalog;
            catalog.id = catObj.value("id").toString().trimmed();
            catalog.type = catObj.value("type").toString().trimmed();
            catalog.name = catObj.value("name").toString().trimmed();
            if (!catalog.id.isEmpty() && !catalog.type.isEmpty())
                out.catalogs.push_back(catalog);
        }

        const QJsonObject hints = obj.value("behaviorHints").toObject();
        out.behaviorHints.adult = hints.value("adult").toBool(false);
        out.behaviorHints.p2p = hints.value("p2p").toBool(false);
        out.behaviorHints.configurable = hints.value("configurable").toBool(false);
        out.behaviorHints.configurationRequired = hints.value("configurationRequired").toBool(false);

        return true;
    }

    QNetworkAccessManager* m_nam = nullptr;
};

}  // namespace tankostream::addon

