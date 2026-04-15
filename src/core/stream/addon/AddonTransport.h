#pragma once

#include <QJsonObject>
#include <QObject>
#include <QUrl>

#include "Descriptor.h"
#include "ResourcePath.h"

class QNetworkAccessManager;

namespace tankostream::addon {

class AddonTransport : public QObject
{
    Q_OBJECT

public:
    explicit AddonTransport(QObject* parent = nullptr);

    void fetchManifest(const QUrl& base);
    void fetchResource(const QUrl& base, const ResourceRequest& request);

signals:
    void manifestReady(const tankostream::addon::AddonDescriptor& descriptor);
    void manifestFailed(const QString& message);
    void resourceReady(const tankostream::addon::ResourceRequest& request,
                       const QJsonObject& payload);
    void resourceFailed(const tankostream::addon::ResourceRequest& request,
                        const QString& message);

private:
    static QUrl normalizeManifestUrl(QUrl base);
    static QUrl baseRoot(const QUrl& base);
    static QUrl buildResourceUrl(const QUrl& base, const ResourceRequest& request);
    static QString encodeExtraSegment(const QList<QPair<QString, QString>>& extra);
    static bool parseManifest(const QJsonObject& obj, AddonManifest& out);

    QNetworkAccessManager* m_nam = nullptr;
};

}
