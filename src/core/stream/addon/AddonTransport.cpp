#include "AddonTransport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QStringList>

namespace tankostream::addon {

namespace {

constexpr int kTimeoutMs = 10000;
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36";

QString encodeComponent(const QString& value)
{
    // Match Stremio's URI_COMPONENT_ENCODE_SET (stremio-core/src/constants.rs:54-63):
    // preserve the RFC 3986 sub-delim chars ! * ' ( ) that Stremio leaves unencoded,
    // plus ':' so Torrentio series ids like "tt0944947:1:1" pass through.
    // Qt already preserves the unreserved set [A-Za-z0-9-_.~]; the include set
    // below names only the additional Stremio-compatible preservations.
    return QString::fromUtf8(QUrl::toPercentEncoding(value, "!*'():"));
}

}

AddonTransport::AddonTransport(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void AddonTransport::fetchManifest(const QUrl& base)
{
    const QUrl manifestUrl = normalizeManifestUrl(base);
    if (!manifestUrl.isValid()) {
        emit manifestFailed(QStringLiteral("Invalid addon URL"));
        return;
    }

    QNetworkRequest req(manifestUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
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
            emit manifestFailed(QStringLiteral("Manifest JSON parse error: ") +
                                parseErr.errorString());
            return;
        }

        AddonDescriptor descriptor;
        descriptor.transportUrl = manifestUrl;

        if (!parseManifest(doc.object(), descriptor.manifest)) {
            emit manifestFailed(QStringLiteral("Manifest validation failed (missing id/name/version)"));
            return;
        }

        emit manifestReady(descriptor);
    });
}

void AddonTransport::fetchResource(const QUrl& base, const ResourceRequest& request)
{
    const QUrl url = buildResourceUrl(base, request);
    if (!url.isValid()) {
        emit resourceFailed(request, QStringLiteral("Invalid resource URL"));
        return;
    }

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
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
            emit resourceFailed(request, QStringLiteral("Resource JSON parse error: ") +
                                parseErr.errorString());
            return;
        }

        emit resourceReady(request, doc.object());
    });
}

QUrl AddonTransport::normalizeManifestUrl(QUrl base)
{
    if (!base.isValid() || base.scheme().isEmpty()) {
        return {};
    }

    QString path = base.path();
    if (path.endsWith(QLatin1String("/manifest.json"), Qt::CaseInsensitive)) {
        return base;
    }
    if (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    path += QStringLiteral("/manifest.json");
    base.setPath(path);
    return base;
}

QUrl AddonTransport::baseRoot(const QUrl& base)
{
    if (!base.isValid() || base.scheme().isEmpty()) {
        return {};
    }

    QUrl root = base;
    QString path = root.path();
    if (path.endsWith(QLatin1String("/manifest.json"), Qt::CaseInsensitive)) {
        path.chop(QString(QStringLiteral("/manifest.json")).size());
    }
    if (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    root.setPath(path);
    return root;
}

QString AddonTransport::encodeExtraSegment(const QList<QPair<QString, QString>>& extra)
{
    QStringList parts;
    parts.reserve(extra.size());
    for (const auto& kv : extra) {
        parts.push_back(encodeComponent(kv.first) + QLatin1Char('=') +
                        encodeComponent(kv.second));
    }
    return parts.join(QLatin1Char('&'));
}

QUrl AddonTransport::buildResourceUrl(const QUrl& base, const ResourceRequest& request)
{
    const QUrl root = baseRoot(base);
    if (!root.isValid()) {
        return {};
    }

    QString path = root.path();
    path += QLatin1Char('/') + encodeComponent(request.resource);
    path += QLatin1Char('/') + encodeComponent(request.type);
    path += QLatin1Char('/') + encodeComponent(request.id);
    if (!request.extra.isEmpty()) {
        path += QLatin1Char('/') + encodeExtraSegment(request.extra);
    }
    path += QStringLiteral(".json");

    QUrl url = root;
    url.setPath(path, QUrl::TolerantMode);
    return url;
}

bool AddonTransport::parseManifest(const QJsonObject& obj, AddonManifest& out)
{
    out.id = obj.value(QStringLiteral("id")).toString().trimmed();
    out.version = obj.value(QStringLiteral("version")).toString().trimmed();
    out.name = obj.value(QStringLiteral("name")).toString().trimmed();
    out.contactEmail = obj.value(QStringLiteral("contactEmail")).toString().trimmed();
    out.description = obj.value(QStringLiteral("description")).toString().trimmed();
    out.logo = QUrl(obj.value(QStringLiteral("logo")).toString().trimmed());
    out.background = QUrl(obj.value(QStringLiteral("background")).toString().trimmed());

    if (out.id.isEmpty() || out.name.isEmpty() || out.version.isEmpty()) {
        return false;
    }

    for (const QJsonValue& item : obj.value(QStringLiteral("types")).toArray()) {
        const QString type = item.toString().trimmed();
        if (!type.isEmpty()) {
            out.types.push_back(type);
        }
    }

    if (obj.contains(QStringLiteral("idPrefixes"))) {
        out.hasIdPrefixes = true;
        for (const QJsonValue& item : obj.value(QStringLiteral("idPrefixes")).toArray()) {
            const QString prefix = item.toString().trimmed();
            if (!prefix.isEmpty()) {
                out.idPrefixes.push_back(prefix);
            }
        }
    }

    for (const QJsonValue& item : obj.value(QStringLiteral("resources")).toArray()) {
        ManifestResource resource;
        if (item.isString()) {
            resource.name = item.toString().trimmed();
        } else {
            const QJsonObject ro = item.toObject();
            resource.name = ro.value(QStringLiteral("name")).toString().trimmed();
            if (ro.contains(QStringLiteral("types"))) {
                resource.hasTypes = true;
                for (const QJsonValue& t : ro.value(QStringLiteral("types")).toArray()) {
                    const QString type = t.toString().trimmed();
                    if (!type.isEmpty()) {
                        resource.types.push_back(type);
                    }
                }
            }
            if (ro.contains(QStringLiteral("idPrefixes"))) {
                resource.hasIdPrefixes = true;
                for (const QJsonValue& p : ro.value(QStringLiteral("idPrefixes")).toArray()) {
                    const QString prefix = p.toString().trimmed();
                    if (!prefix.isEmpty()) {
                        resource.idPrefixes.push_back(prefix);
                    }
                }
            }
        }
        if (!resource.name.isEmpty()) {
            out.resources.push_back(resource);
        }
    }

    for (const QJsonValue& item : obj.value(QStringLiteral("catalogs")).toArray()) {
        const QJsonObject catObj = item.toObject();
        ManifestCatalog catalog;
        catalog.id = catObj.value(QStringLiteral("id")).toString().trimmed();
        catalog.type = catObj.value(QStringLiteral("type")).toString().trimmed();
        catalog.name = catObj.value(QStringLiteral("name")).toString().trimmed();

        for (const QJsonValue& extraItem : catObj.value(QStringLiteral("extra")).toArray()) {
            const QJsonObject propObj = extraItem.toObject();
            ManifestExtraProp prop;
            prop.name = propObj.value(QStringLiteral("name")).toString().trimmed();
            if (prop.name.isEmpty()) {
                continue;
            }
            prop.isRequired = propObj.value(QStringLiteral("isRequired")).toBool(false);
            for (const QJsonValue& opt : propObj.value(QStringLiteral("options")).toArray()) {
                const QString optStr = opt.toString().trimmed();
                if (!optStr.isEmpty()) {
                    prop.options.push_back(optStr);
                }
            }
            if (propObj.contains(QStringLiteral("optionsLimit"))) {
                prop.optionsLimit = propObj.value(QStringLiteral("optionsLimit")).toInt(1);
            }
            catalog.extra.push_back(prop);
        }

        if (!catalog.id.isEmpty() && !catalog.type.isEmpty()) {
            out.catalogs.push_back(catalog);
        }
    }

    const QJsonObject hints = obj.value(QStringLiteral("behaviorHints")).toObject();
    out.behaviorHints.adult = hints.value(QStringLiteral("adult")).toBool(false);
    out.behaviorHints.p2p = hints.value(QStringLiteral("p2p")).toBool(false);
    out.behaviorHints.configurable = hints.value(QStringLiteral("configurable")).toBool(false);
    out.behaviorHints.configurationRequired =
        hints.value(QStringLiteral("configurationRequired")).toBool(false);

    static const QSet<QString> kKnownHints = {
        QStringLiteral("adult"),
        QStringLiteral("p2p"),
        QStringLiteral("configurable"),
        QStringLiteral("configurationRequired"),
    };
    for (auto it = hints.constBegin(); it != hints.constEnd(); ++it) {
        if (!kKnownHints.contains(it.key())) {
            out.behaviorHints.other.insert(it.key(), it.value().toVariant());
        }
    }

    return true;
}

}
