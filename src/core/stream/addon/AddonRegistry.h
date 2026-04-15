#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include "Descriptor.h"

namespace tankostream::addon {

class AddonTransport;

class AddonRegistry : public QObject
{
    Q_OBJECT

public:
    explicit AddonRegistry(const QString& dataDir,
                           AddonTransport* transport = nullptr,
                           QObject* parent = nullptr);

    QList<AddonDescriptor> list() const;
    QList<AddonDescriptor> findByResourceType(const QString& resource,
                                              const QString& type) const;

    void installByUrl(const QUrl& transportUrlInput);
    bool uninstall(const QString& addonId);
    bool setEnabled(const QString& addonId, bool enabled);

signals:
    void addonsChanged();
    void installSucceeded(const tankostream::addon::AddonDescriptor& descriptor);
    void installFailed(const QUrl& inputUrl, const QString& message);

private slots:
    void onManifestReady(const tankostream::addon::AddonDescriptor& fetched);
    void onManifestFailed(const QString& message);

private:
    static QUrl normalizeManifestUrl(QUrl input);
    static bool sameUrl(const QUrl& a, const QUrl& b);
    static bool supportsResourceType(const AddonManifest& manifest,
                                     const QString& resource,
                                     const QString& type);
    static bool validateFetchedDescriptor(const AddonDescriptor& descriptor);

    int indexOfId(const QString& addonId) const;

    void load();
    void save() const;
    void seedDefaults();

    QString storageFilePath() const;

    QString m_dataDir;
    AddonTransport* m_transport = nullptr;
    QList<AddonDescriptor> m_addons;
    QUrl m_pendingInstallUrl;
};

}
