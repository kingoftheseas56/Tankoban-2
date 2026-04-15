// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 1.3 (Addon Protocol Foundation)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:71
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/addon/descriptor.rs:8
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/addon/manifest.rs:22
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/installed_addons_with_filters.rs:14
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/addon_details.rs:13
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/JsonStore.cpp:12
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/CoreBridge.cpp:31
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/StreamLibrary.cpp:56
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 1.3.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QObject>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace tankostream::addon {

// -----------------------------------------------------------------
// Minimal type surface (kept local so this prototype is standalone).
// -----------------------------------------------------------------

struct ManifestResource {
    QString name;
    QStringList types;
    QStringList idPrefixes;
    bool hasTypes = false;
    bool hasIdPrefixes = false;
};

struct ManifestCatalog {
    QString id;
    QString type;
    QString name;
};

struct ManifestBehaviorHints {
    bool adult = false;
    bool p2p = false;
    bool configurable = false;
    bool configurationRequired = false;
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
    QUrl transportUrl;   // canonical /manifest.json
    AddonDescriptorFlags flags;
};

// Prototype surface from Batch 1.2.
class AddonTransport : public QObject {
    Q_OBJECT
public:
    explicit AddonTransport(QObject* parent = nullptr) : QObject(parent) {}
    void fetchManifest(const QUrl& base);

signals:
    void manifestReady(const AddonDescriptor& descriptor);
    void manifestFailed(const QString& message);
};

// -----------------------------------------------------------------
// AddonRegistry prototype
// -----------------------------------------------------------------

class AddonRegistry : public QObject {
    Q_OBJECT

public:
    explicit AddonRegistry(AddonTransport* transport, QObject* parent = nullptr)
        : QObject(parent)
        , m_transport(transport ? transport : new AddonTransport(this))
        , m_ownsTransport(transport == nullptr)
    {
        connect(m_transport, &AddonTransport::manifestReady,
                this, &AddonRegistry::onManifestReady);
        connect(m_transport, &AddonTransport::manifestFailed,
                this, &AddonRegistry::onManifestFailed);

        load();
    }

    ~AddonRegistry() override
    {
        if (m_ownsTransport)
            m_transport->deleteLater();
    }

    QList<AddonDescriptor> list() const
    {
        return m_addons;
    }

    QList<AddonDescriptor> findByResourceType(
        const QString& resource,
        const QString& type) const
    {
        QList<AddonDescriptor> out;
        for (const AddonDescriptor& addon : m_addons) {
            if (!addon.flags.enabled)
                continue;
            if (supportsResourceType(addon.manifest, resource, type))
                out.push_back(addon);
        }
        return out;
    }

    void installByUrl(const QUrl& transportUrlInput)
    {
        if (!m_pendingInstallUrl.isEmpty()) {
            emit installFailed(transportUrlInput, "Another addon install is already running");
            return;
        }

        const QUrl transportUrl = normalizeManifestUrl(transportUrlInput);
        if (!transportUrl.isValid()) {
            emit installFailed(transportUrlInput, "Invalid addon URL");
            return;
        }

        for (const AddonDescriptor& existing : m_addons) {
            if (sameUrl(existing.transportUrl, transportUrl)) {
                emit installFailed(transportUrl, "Addon already installed");
                return;
            }
        }

        m_pendingInstallUrl = transportUrl;
        m_transport->fetchManifest(transportUrl);
    }

    bool uninstall(const QString& addonId)
    {
        const int index = indexOfId(addonId);
        if (index < 0)
            return false;
        if (m_addons[index].flags.protectedAddon)
            return false;

        m_addons.removeAt(index);
        save();
        emit addonsChanged();
        return true;
    }

    bool setEnabled(const QString& addonId, bool enabled)
    {
        const int index = indexOfId(addonId);
        if (index < 0)
            return false;
        if (m_addons[index].flags.enabled == enabled)
            return true;

        m_addons[index].flags.enabled = enabled;
        save();
        emit addonsChanged();
        return true;
    }

signals:
    void addonsChanged();
    void installSucceeded(const AddonDescriptor& descriptor);
    void installFailed(const QUrl& inputUrl, const QString& message);

private slots:
    void onManifestReady(const AddonDescriptor& fetched)
    {
        if (m_pendingInstallUrl.isEmpty())
            return;

        const QUrl installUrl = m_pendingInstallUrl;
        m_pendingInstallUrl = QUrl();

        if (!validateFetchedDescriptor(fetched)) {
            emit installFailed(installUrl, "Manifest missing id/name/version");
            return;
        }

        AddonDescriptor normalized = fetched;
        normalized.transportUrl = normalizeManifestUrl(installUrl);
        normalized.flags.official = false;
        normalized.flags.enabled = true;
        normalized.flags.protectedAddon = false;

        const int byId = indexOfId(normalized.manifest.id);
        if (byId >= 0) {
            // Replace manifest + URL, keep existing immutable flags if protected.
            const bool preserveOfficial = m_addons[byId].flags.official;
            const bool preserveProtected = m_addons[byId].flags.protectedAddon;
            const bool preserveEnabled = m_addons[byId].flags.enabled;

            m_addons[byId] = normalized;
            m_addons[byId].flags.official = preserveOfficial;
            m_addons[byId].flags.protectedAddon = preserveProtected;
            m_addons[byId].flags.enabled = preserveEnabled;
        } else {
            m_addons.push_back(normalized);
        }

        save();
        emit addonsChanged();
        emit installSucceeded(normalized);
    }

    void onManifestFailed(const QString& message)
    {
        if (m_pendingInstallUrl.isEmpty())
            return;
        const QUrl installUrl = m_pendingInstallUrl;
        m_pendingInstallUrl = QUrl();
        emit installFailed(installUrl, message);
    }

private:
    static constexpr int kSchemaVersion = 1;

    static QString storageFilePath()
    {
        // TODO contract wants: {AppData}/Tankoban/stream_addons.json
        const QString appDataBase =
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        QDir tankobanDir(appDataBase + "/Tankoban");
        tankobanDir.mkpath(".");
        return tankobanDir.filePath("stream_addons.json");
    }

    static bool sameUrl(const QUrl& a, const QUrl& b)
    {
        return a.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash)
            == b.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash);
    }

    static QUrl normalizeManifestUrl(QUrl input)
    {
        QString s = input.toString().trimmed();
        if (s.startsWith("stremio://", Qt::CaseInsensitive))
            s.replace(0, QString("stremio://").size(), "https://");

        if (s.endsWith("/manifest.json", Qt::CaseInsensitive))
            return QUrl(s);

        if (s.endsWith('/'))
            s.chop(1);

        return QUrl(s + "/manifest.json");
    }

    static bool supportsResourceType(
        const AddonManifest& manifest,
        const QString& resource,
        const QString& type)
    {
        if (resource.compare("catalog", Qt::CaseInsensitive) == 0) {
            for (const ManifestCatalog& catalog : manifest.catalogs) {
                if (catalog.type.compare(type, Qt::CaseInsensitive) == 0)
                    return true;
            }
            return false;
        }

        for (const ManifestResource& item : manifest.resources) {
            if (item.name.compare(resource, Qt::CaseInsensitive) != 0)
                continue;

            if (item.hasTypes)
                return item.types.contains(type, Qt::CaseInsensitive);

            // Stremio short-form resource => use global manifest.types.
            return manifest.types.contains(type, Qt::CaseInsensitive);
        }
        return false;
    }

    static bool validateFetchedDescriptor(const AddonDescriptor& descriptor)
    {
        if (!descriptor.transportUrl.isValid())
            return false;

        const AddonManifest& m = descriptor.manifest;
        return !m.id.trimmed().isEmpty()
            && !m.name.trimmed().isEmpty()
            && !m.version.trimmed().isEmpty();
    }

    int indexOfId(const QString& addonId) const
    {
        for (int i = 0; i < m_addons.size(); ++i) {
            if (m_addons[i].manifest.id == addonId)
                return i;
        }
        return -1;
    }

    static QJsonObject toJson(const AddonDescriptor& descriptor)
    {
        QJsonObject root;
        root["transportUrl"] = descriptor.transportUrl.toString();

        QJsonObject flags;
        flags["official"] = descriptor.flags.official;
        flags["enabled"] = descriptor.flags.enabled;
        flags["protected"] = descriptor.flags.protectedAddon;
        root["flags"] = flags;

        const AddonManifest& m = descriptor.manifest;
        QJsonObject manifest;
        manifest["id"] = m.id;
        manifest["version"] = m.version;
        manifest["name"] = m.name;
        manifest["description"] = m.description;
        manifest["logo"] = m.logo.toString();
        manifest["background"] = m.background.toString();

        QJsonArray types;
        for (const QString& type : m.types)
            types.append(type);
        manifest["types"] = types;

        if (m.hasIdPrefixes) {
            QJsonArray idPrefixes;
            for (const QString& prefix : m.idPrefixes)
                idPrefixes.append(prefix);
            manifest["idPrefixes"] = idPrefixes;
        }

        QJsonArray resources;
        for (const ManifestResource& r : m.resources) {
            if (!r.hasTypes && !r.hasIdPrefixes) {
                resources.append(r.name);
                continue;
            }

            QJsonObject ro;
            ro["name"] = r.name;
            if (r.hasTypes) {
                QJsonArray rt;
                for (const QString& type : r.types)
                    rt.append(type);
                ro["types"] = rt;
            }
            if (r.hasIdPrefixes) {
                QJsonArray rp;
                for (const QString& prefix : r.idPrefixes)
                    rp.append(prefix);
                ro["idPrefixes"] = rp;
            }
            resources.append(ro);
        }
        manifest["resources"] = resources;

        QJsonArray catalogs;
        for (const ManifestCatalog& c : m.catalogs) {
            QJsonObject co;
            co["id"] = c.id;
            co["type"] = c.type;
            co["name"] = c.name;
            catalogs.append(co);
        }
        manifest["catalogs"] = catalogs;

        QJsonObject hints;
        hints["adult"] = m.behaviorHints.adult;
        hints["p2p"] = m.behaviorHints.p2p;
        hints["configurable"] = m.behaviorHints.configurable;
        hints["configurationRequired"] = m.behaviorHints.configurationRequired;
        manifest["behaviorHints"] = hints;

        root["manifest"] = manifest;
        return root;
    }

    static AddonDescriptor fromJson(const QJsonObject& obj)
    {
        AddonDescriptor descriptor;
        descriptor.transportUrl = normalizeManifestUrl(QUrl(obj.value("transportUrl").toString()));

        const QJsonObject flags = obj.value("flags").toObject();
        descriptor.flags.official = flags.value("official").toBool(false);
        descriptor.flags.enabled = flags.value("enabled").toBool(true);
        descriptor.flags.protectedAddon = flags.value("protected").toBool(false);

        const QJsonObject m = obj.value("manifest").toObject();
        descriptor.manifest.id = m.value("id").toString().trimmed();
        descriptor.manifest.version = m.value("version").toString().trimmed();
        descriptor.manifest.name = m.value("name").toString().trimmed();
        descriptor.manifest.description = m.value("description").toString().trimmed();
        descriptor.manifest.logo = QUrl(m.value("logo").toString().trimmed());
        descriptor.manifest.background = QUrl(m.value("background").toString().trimmed());

        for (const QJsonValue& t : m.value("types").toArray()) {
            const QString type = t.toString().trimmed();
            if (!type.isEmpty())
                descriptor.manifest.types.push_back(type);
        }

        if (m.contains("idPrefixes")) {
            descriptor.manifest.hasIdPrefixes = true;
            for (const QJsonValue& p : m.value("idPrefixes").toArray()) {
                const QString prefix = p.toString().trimmed();
                if (!prefix.isEmpty())
                    descriptor.manifest.idPrefixes.push_back(prefix);
            }
        }

        for (const QJsonValue& rv : m.value("resources").toArray()) {
            ManifestResource resource;
            if (rv.isString()) {
                resource.name = rv.toString().trimmed();
            } else {
                const QJsonObject ro = rv.toObject();
                resource.name = ro.value("name").toString().trimmed();
                if (ro.contains("types")) {
                    resource.hasTypes = true;
                    for (const QJsonValue& tv : ro.value("types").toArray()) {
                        const QString type = tv.toString().trimmed();
                        if (!type.isEmpty())
                            resource.types.push_back(type);
                    }
                }
                if (ro.contains("idPrefixes")) {
                    resource.hasIdPrefixes = true;
                    for (const QJsonValue& pv : ro.value("idPrefixes").toArray()) {
                        const QString prefix = pv.toString().trimmed();
                        if (!prefix.isEmpty())
                            resource.idPrefixes.push_back(prefix);
                    }
                }
            }
            if (!resource.name.isEmpty())
                descriptor.manifest.resources.push_back(resource);
        }

        for (const QJsonValue& cv : m.value("catalogs").toArray()) {
            const QJsonObject co = cv.toObject();
            ManifestCatalog catalog;
            catalog.id = co.value("id").toString().trimmed();
            catalog.type = co.value("type").toString().trimmed();
            catalog.name = co.value("name").toString().trimmed();
            if (!catalog.id.isEmpty() && !catalog.type.isEmpty())
                descriptor.manifest.catalogs.push_back(catalog);
        }

        const QJsonObject hints = m.value("behaviorHints").toObject();
        descriptor.manifest.behaviorHints.adult = hints.value("adult").toBool(false);
        descriptor.manifest.behaviorHints.p2p = hints.value("p2p").toBool(false);
        descriptor.manifest.behaviorHints.configurable = hints.value("configurable").toBool(false);
        descriptor.manifest.behaviorHints.configurationRequired =
            hints.value("configurationRequired").toBool(false);

        return descriptor;
    }

    void load()
    {
        QFile file(storageFilePath());
        if (!file.exists()) {
            seedDefaults();
            save();
            return;
        }

        if (!file.open(QIODevice::ReadOnly)) {
            seedDefaults();
            save();
            return;
        }

        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            seedDefaults();
            save();
            return;
        }

        const QJsonObject root = doc.object();
        const QJsonArray addons = root.value("addons").toArray();

        m_addons.clear();
        for (const QJsonValue& value : addons) {
            if (!value.isObject())
                continue;

            AddonDescriptor descriptor = fromJson(value.toObject());
            if (descriptor.manifest.id.isEmpty())
                continue;
            m_addons.push_back(descriptor);
        }

        if (m_addons.isEmpty()) {
            seedDefaults();
            save();
        }
    }

    void save() const
    {
        QJsonObject root;
        root["version"] = kSchemaVersion;

        QJsonArray addons;
        for (const AddonDescriptor& descriptor : m_addons)
            addons.append(toJson(descriptor));
        root["addons"] = addons;

        QSaveFile file(storageFilePath());
        if (!file.open(QIODevice::WriteOnly))
            return;
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.commit();
    }

    void seedDefaults()
    {
        m_addons.clear();

        AddonDescriptor cinemeta;
        cinemeta.transportUrl = QUrl("https://v3-cinemeta.strem.io/manifest.json");
        cinemeta.flags.official = true;
        cinemeta.flags.enabled = true;
        cinemeta.flags.protectedAddon = true;
        cinemeta.manifest.id = "com.linvo.cinemeta";
        cinemeta.manifest.version = "3.0.14";
        cinemeta.manifest.name = "Cinemeta";
        cinemeta.manifest.types = {"movie", "series"};
        cinemeta.manifest.resources = {
            ManifestResource{"catalog"},
            ManifestResource{"meta"},
            ManifestResource{"addon_catalog"},
        };
        m_addons.push_back(cinemeta);

        AddonDescriptor torrentio;
        torrentio.transportUrl = QUrl("https://torrentio.strem.fun/manifest.json");
        torrentio.flags.official = true;
        torrentio.flags.enabled = true;
        torrentio.flags.protectedAddon = true;
        torrentio.manifest.id = "com.stremio.torrentio.addon";
        torrentio.manifest.version = "0.0.15";
        torrentio.manifest.name = "Torrentio";
        torrentio.manifest.types = {"movie", "series", "anime", "other"};
        ManifestResource streamRes;
        streamRes.name = "stream";
        streamRes.hasTypes = true;
        streamRes.types = {"movie", "series", "anime"};
        streamRes.hasIdPrefixes = true;
        streamRes.idPrefixes = {"tt", "kitsu"};
        torrentio.manifest.resources = {streamRes};
        m_addons.push_back(torrentio);
    }

    AddonTransport* m_transport = nullptr;
    bool m_ownsTransport = false;
    QList<AddonDescriptor> m_addons;
    QUrl m_pendingInstallUrl;
};

} // namespace tankostream::addon

