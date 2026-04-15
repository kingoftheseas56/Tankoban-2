#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

namespace tankostream::addon {

struct ManifestBehaviorHints {
    bool adult = false;
    bool p2p = false;
    bool configurable = false;
    bool configurationRequired = false;
    QVariantMap other;
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

}
