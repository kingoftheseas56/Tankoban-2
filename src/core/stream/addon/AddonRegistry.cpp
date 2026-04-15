#include "AddonRegistry.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>

#include "AddonTransport.h"
#include "Manifest.h"

namespace tankostream::addon {

namespace {

// v2: seeded Cinemeta manifest now carries a populated `manifest.catalogs`
// (top/movie + top/series with search extra) so MetaAggregator's manifest-
// driven search finds it. v1 stream_addons.json files predate that seed —
// bump triggers a reseed on next load.
constexpr int kSchemaVersion = 2;

QJsonObject manifestToJson(const AddonManifest& m)
{
    QJsonObject out;
    out[QStringLiteral("id")] = m.id;
    out[QStringLiteral("version")] = m.version;
    out[QStringLiteral("name")] = m.name;
    if (!m.contactEmail.isEmpty()) {
        out[QStringLiteral("contactEmail")] = m.contactEmail;
    }
    if (!m.description.isEmpty()) {
        out[QStringLiteral("description")] = m.description;
    }
    if (!m.logo.isEmpty()) {
        out[QStringLiteral("logo")] = m.logo.toString();
    }
    if (!m.background.isEmpty()) {
        out[QStringLiteral("background")] = m.background.toString();
    }

    QJsonArray types;
    for (const QString& type : m.types) {
        types.append(type);
    }
    out[QStringLiteral("types")] = types;

    if (m.hasIdPrefixes) {
        QJsonArray idPrefixes;
        for (const QString& prefix : m.idPrefixes) {
            idPrefixes.append(prefix);
        }
        out[QStringLiteral("idPrefixes")] = idPrefixes;
    }

    QJsonArray resources;
    for (const ManifestResource& r : m.resources) {
        if (!r.hasTypes && !r.hasIdPrefixes) {
            resources.append(r.name);
            continue;
        }
        QJsonObject ro;
        ro[QStringLiteral("name")] = r.name;
        if (r.hasTypes) {
            QJsonArray rt;
            for (const QString& t : r.types) {
                rt.append(t);
            }
            ro[QStringLiteral("types")] = rt;
        }
        if (r.hasIdPrefixes) {
            QJsonArray rp;
            for (const QString& p : r.idPrefixes) {
                rp.append(p);
            }
            ro[QStringLiteral("idPrefixes")] = rp;
        }
        resources.append(ro);
    }
    out[QStringLiteral("resources")] = resources;

    QJsonArray catalogs;
    for (const ManifestCatalog& c : m.catalogs) {
        QJsonObject co;
        co[QStringLiteral("id")] = c.id;
        co[QStringLiteral("type")] = c.type;
        co[QStringLiteral("name")] = c.name;
        if (!c.extra.isEmpty()) {
            QJsonArray extras;
            for (const ManifestExtraProp& prop : c.extra) {
                QJsonObject po;
                po[QStringLiteral("name")] = prop.name;
                if (prop.isRequired) {
                    po[QStringLiteral("isRequired")] = true;
                }
                if (!prop.options.isEmpty()) {
                    QJsonArray opts;
                    for (const QString& opt : prop.options) {
                        opts.append(opt);
                    }
                    po[QStringLiteral("options")] = opts;
                }
                po[QStringLiteral("optionsLimit")] = prop.optionsLimit;
                extras.append(po);
            }
            co[QStringLiteral("extra")] = extras;
        }
        catalogs.append(co);
    }
    out[QStringLiteral("catalogs")] = catalogs;

    QJsonObject hints;
    hints[QStringLiteral("adult")] = m.behaviorHints.adult;
    hints[QStringLiteral("p2p")] = m.behaviorHints.p2p;
    hints[QStringLiteral("configurable")] = m.behaviorHints.configurable;
    hints[QStringLiteral("configurationRequired")] = m.behaviorHints.configurationRequired;
    for (auto it = m.behaviorHints.other.constBegin();
         it != m.behaviorHints.other.constEnd(); ++it) {
        hints.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }
    out[QStringLiteral("behaviorHints")] = hints;

    return out;
}

AddonManifest manifestFromJson(const QJsonObject& m)
{
    AddonManifest out;
    out.id = m.value(QStringLiteral("id")).toString().trimmed();
    out.version = m.value(QStringLiteral("version")).toString().trimmed();
    out.name = m.value(QStringLiteral("name")).toString().trimmed();
    out.contactEmail = m.value(QStringLiteral("contactEmail")).toString().trimmed();
    out.description = m.value(QStringLiteral("description")).toString().trimmed();
    out.logo = QUrl(m.value(QStringLiteral("logo")).toString().trimmed());
    out.background = QUrl(m.value(QStringLiteral("background")).toString().trimmed());

    for (const QJsonValue& t : m.value(QStringLiteral("types")).toArray()) {
        const QString type = t.toString().trimmed();
        if (!type.isEmpty()) {
            out.types.push_back(type);
        }
    }

    if (m.contains(QStringLiteral("idPrefixes"))) {
        out.hasIdPrefixes = true;
        for (const QJsonValue& p : m.value(QStringLiteral("idPrefixes")).toArray()) {
            const QString prefix = p.toString().trimmed();
            if (!prefix.isEmpty()) {
                out.idPrefixes.push_back(prefix);
            }
        }
    }

    for (const QJsonValue& rv : m.value(QStringLiteral("resources")).toArray()) {
        ManifestResource resource;
        if (rv.isString()) {
            resource.name = rv.toString().trimmed();
        } else {
            const QJsonObject ro = rv.toObject();
            resource.name = ro.value(QStringLiteral("name")).toString().trimmed();
            if (ro.contains(QStringLiteral("types"))) {
                resource.hasTypes = true;
                for (const QJsonValue& tv : ro.value(QStringLiteral("types")).toArray()) {
                    const QString type = tv.toString().trimmed();
                    if (!type.isEmpty()) {
                        resource.types.push_back(type);
                    }
                }
            }
            if (ro.contains(QStringLiteral("idPrefixes"))) {
                resource.hasIdPrefixes = true;
                for (const QJsonValue& pv : ro.value(QStringLiteral("idPrefixes")).toArray()) {
                    const QString prefix = pv.toString().trimmed();
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

    for (const QJsonValue& cv : m.value(QStringLiteral("catalogs")).toArray()) {
        const QJsonObject co = cv.toObject();
        ManifestCatalog catalog;
        catalog.id = co.value(QStringLiteral("id")).toString().trimmed();
        catalog.type = co.value(QStringLiteral("type")).toString().trimmed();
        catalog.name = co.value(QStringLiteral("name")).toString().trimmed();
        for (const QJsonValue& extraVal : co.value(QStringLiteral("extra")).toArray()) {
            const QJsonObject po = extraVal.toObject();
            ManifestExtraProp prop;
            prop.name = po.value(QStringLiteral("name")).toString().trimmed();
            if (prop.name.isEmpty()) {
                continue;
            }
            prop.isRequired = po.value(QStringLiteral("isRequired")).toBool(false);
            for (const QJsonValue& opt : po.value(QStringLiteral("options")).toArray()) {
                const QString s = opt.toString().trimmed();
                if (!s.isEmpty()) {
                    prop.options.push_back(s);
                }
            }
            if (po.contains(QStringLiteral("optionsLimit"))) {
                prop.optionsLimit = po.value(QStringLiteral("optionsLimit")).toInt(1);
            }
            catalog.extra.push_back(prop);
        }
        if (!catalog.id.isEmpty() && !catalog.type.isEmpty()) {
            out.catalogs.push_back(catalog);
        }
    }

    const QJsonObject hints = m.value(QStringLiteral("behaviorHints")).toObject();
    out.behaviorHints.adult = hints.value(QStringLiteral("adult")).toBool(false);
    out.behaviorHints.p2p = hints.value(QStringLiteral("p2p")).toBool(false);
    out.behaviorHints.configurable = hints.value(QStringLiteral("configurable")).toBool(false);
    out.behaviorHints.configurationRequired =
        hints.value(QStringLiteral("configurationRequired")).toBool(false);

    static const QStringList kKnownHints = {
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

    return out;
}

QJsonObject descriptorToJson(const AddonDescriptor& descriptor)
{
    QJsonObject root;
    root[QStringLiteral("transportUrl")] = descriptor.transportUrl.toString();

    QJsonObject flags;
    flags[QStringLiteral("official")] = descriptor.flags.official;
    flags[QStringLiteral("enabled")] = descriptor.flags.enabled;
    flags[QStringLiteral("protected")] = descriptor.flags.protectedAddon;
    root[QStringLiteral("flags")] = flags;

    root[QStringLiteral("manifest")] = manifestToJson(descriptor.manifest);
    return root;
}

AddonDescriptor descriptorFromJson(const QJsonObject& obj)
{
    AddonDescriptor descriptor;
    descriptor.transportUrl = QUrl(obj.value(QStringLiteral("transportUrl")).toString().trimmed());

    const QJsonObject flags = obj.value(QStringLiteral("flags")).toObject();
    descriptor.flags.official = flags.value(QStringLiteral("official")).toBool(false);
    descriptor.flags.enabled = flags.value(QStringLiteral("enabled")).toBool(true);
    descriptor.flags.protectedAddon = flags.value(QStringLiteral("protected")).toBool(false);

    descriptor.manifest = manifestFromJson(obj.value(QStringLiteral("manifest")).toObject());
    return descriptor;
}

}

AddonRegistry::AddonRegistry(const QString& dataDir,
                             AddonTransport* transport,
                             QObject* parent)
    : QObject(parent)
    , m_dataDir(dataDir)
    , m_transport(transport ? transport : new AddonTransport(this))
{
    connect(m_transport, &AddonTransport::manifestReady,
            this, &AddonRegistry::onManifestReady);
    connect(m_transport, &AddonTransport::manifestFailed,
            this, &AddonRegistry::onManifestFailed);

    load();
}

QList<AddonDescriptor> AddonRegistry::list() const
{
    return m_addons;
}

QList<AddonDescriptor> AddonRegistry::findByResourceType(const QString& resource,
                                                         const QString& type) const
{
    QList<AddonDescriptor> out;
    for (const AddonDescriptor& addon : m_addons) {
        if (!addon.flags.enabled) {
            continue;
        }
        if (supportsResourceType(addon.manifest, resource, type)) {
            out.push_back(addon);
        }
    }
    return out;
}

void AddonRegistry::installByUrl(const QUrl& transportUrlInput)
{
    if (!m_pendingInstallUrl.isEmpty()) {
        emit installFailed(transportUrlInput,
                           QStringLiteral("Another addon install is already running"));
        return;
    }

    const QUrl transportUrl = normalizeManifestUrl(transportUrlInput);
    if (!transportUrl.isValid() || transportUrl.scheme().isEmpty()) {
        emit installFailed(transportUrlInput, QStringLiteral("Invalid addon URL"));
        return;
    }

    for (const AddonDescriptor& existing : m_addons) {
        if (sameUrl(existing.transportUrl, transportUrl)) {
            emit installFailed(transportUrl, QStringLiteral("Addon already installed"));
            return;
        }
    }

    m_pendingInstallUrl = transportUrl;
    m_transport->fetchManifest(transportUrl);
}

bool AddonRegistry::uninstall(const QString& addonId)
{
    const int index = indexOfId(addonId);
    if (index < 0) {
        return false;
    }
    if (m_addons[index].flags.protectedAddon) {
        return false;
    }

    m_addons.removeAt(index);
    save();
    emit addonsChanged();
    return true;
}

bool AddonRegistry::setEnabled(const QString& addonId, bool enabled)
{
    const int index = indexOfId(addonId);
    if (index < 0) {
        return false;
    }
    if (m_addons[index].flags.enabled == enabled) {
        return true;
    }

    m_addons[index].flags.enabled = enabled;
    save();
    emit addonsChanged();
    return true;
}

void AddonRegistry::onManifestReady(const AddonDescriptor& fetched)
{
    if (m_pendingInstallUrl.isEmpty()) {
        return;
    }

    const QUrl installUrl = m_pendingInstallUrl;
    m_pendingInstallUrl = QUrl();

    if (!validateFetchedDescriptor(fetched)) {
        emit installFailed(installUrl, QStringLiteral("Manifest missing id/name/version"));
        return;
    }

    AddonDescriptor normalized = fetched;
    normalized.transportUrl = normalizeManifestUrl(installUrl);
    normalized.flags.official = false;
    normalized.flags.enabled = true;
    normalized.flags.protectedAddon = false;

    const int byId = indexOfId(normalized.manifest.id);
    if (byId >= 0 && m_addons[byId].flags.protectedAddon) {
        emit installFailed(installUrl,
                           QStringLiteral("An official addon with this id is already installed"));
        return;
    }

    int storedIndex = byId;
    if (byId >= 0) {
        const AddonDescriptorFlags preserve = m_addons[byId].flags;
        m_addons[byId] = normalized;
        m_addons[byId].flags = preserve;
    } else {
        m_addons.push_back(normalized);
        storedIndex = m_addons.size() - 1;
    }

    save();
    emit addonsChanged();
    emit installSucceeded(m_addons[storedIndex]);
}

void AddonRegistry::onManifestFailed(const QString& message)
{
    if (m_pendingInstallUrl.isEmpty()) {
        return;
    }
    const QUrl installUrl = m_pendingInstallUrl;
    m_pendingInstallUrl = QUrl();
    emit installFailed(installUrl, message);
}

QUrl AddonRegistry::normalizeManifestUrl(QUrl input)
{
    if (!input.isValid()) {
        return {};
    }

    if (input.scheme().compare(QStringLiteral("stremio"), Qt::CaseInsensitive) == 0) {
        input.setScheme(QStringLiteral("https"));
    }

    if (input.scheme().isEmpty()) {
        return {};
    }

    QString path = input.path();
    if (path.endsWith(QStringLiteral("/manifest.json"), Qt::CaseInsensitive)) {
        return input;
    }
    if (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    path += QStringLiteral("/manifest.json");
    input.setPath(path);
    return input;
}

bool AddonRegistry::sameUrl(const QUrl& a, const QUrl& b)
{
    const auto normFlags = QUrl::NormalizePathSegments | QUrl::StripTrailingSlash;
    return a.adjusted(normFlags) == b.adjusted(normFlags);
}

bool AddonRegistry::supportsResourceType(const AddonManifest& manifest,
                                         const QString& resource,
                                         const QString& type)
{
    if (resource.compare(QStringLiteral("catalog"), Qt::CaseInsensitive) == 0) {
        for (const ManifestCatalog& catalog : manifest.catalogs) {
            if (catalog.type.compare(type, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    }

    for (const ManifestResource& item : manifest.resources) {
        if (item.name.compare(resource, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (item.hasTypes) {
            return item.types.contains(type, Qt::CaseInsensitive);
        }
        return manifest.types.contains(type, Qt::CaseInsensitive);
    }
    return false;
}

bool AddonRegistry::validateFetchedDescriptor(const AddonDescriptor& descriptor)
{
    if (!descriptor.transportUrl.isValid()) {
        return false;
    }
    const AddonManifest& m = descriptor.manifest;
    return !m.id.trimmed().isEmpty()
        && !m.name.trimmed().isEmpty()
        && !m.version.trimmed().isEmpty();
}

int AddonRegistry::indexOfId(const QString& addonId) const
{
    for (int i = 0; i < m_addons.size(); ++i) {
        if (m_addons[i].manifest.id == addonId) {
            return i;
        }
    }
    return -1;
}

QString AddonRegistry::storageFilePath() const
{
    QDir dir(m_dataDir);
    dir.mkpath(QStringLiteral("."));
    return dir.filePath(QStringLiteral("stream_addons.json"));
}

void AddonRegistry::load()
{
    QFile file(storageFilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        seedDefaults();
        save();
        return;
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
    file.close();

    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        seedDefaults();
        save();
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonArray addons = root.value(QStringLiteral("addons")).toArray();

    m_addons.clear();
    for (const QJsonValue& value : addons) {
        if (!value.isObject()) {
            continue;
        }
        AddonDescriptor descriptor = descriptorFromJson(value.toObject());
        if (descriptor.manifest.id.isEmpty()) {
            continue;
        }
        m_addons.push_back(descriptor);
    }

    // Schema migration. When kSchemaVersion bumps, reseed protected defaults
    // (Cinemeta, Torrentio) from the code-embedded seed while preserving any
    // user-installed non-protected addons. Without this, an on-disk file
    // written under an older seed shape sticks around forever — e.g. the v1
    // seed left Cinemeta's manifest.catalogs empty which broke manifest-driven
    // search even after the code was fixed.
    const int storedVersion = root.value(QStringLiteral("version")).toInt(0);
    if (storedVersion != kSchemaVersion) {
        QList<AddonDescriptor> userInstalled;
        for (const AddonDescriptor& a : m_addons) {
            if (!a.flags.protectedAddon) {
                userInstalled.push_back(a);
            }
        }
        seedDefaults();  // clears m_addons + pushes fresh protected defaults
        for (const AddonDescriptor& a : userInstalled) {
            m_addons.push_back(a);
        }
        save();
        return;
    }

    if (m_addons.isEmpty()) {
        seedDefaults();
        save();
    }
}

void AddonRegistry::save() const
{
    QJsonObject root;
    root[QStringLiteral("version")] = kSchemaVersion;

    QJsonArray addons;
    for (const AddonDescriptor& descriptor : m_addons) {
        addons.append(descriptorToJson(descriptor));
    }
    root[QStringLiteral("addons")] = addons;

    QSaveFile file(storageFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.commit();
}

void AddonRegistry::seedDefaults()
{
    m_addons.clear();

    AddonDescriptor cinemeta;
    cinemeta.transportUrl = QUrl(QStringLiteral("https://v3-cinemeta.strem.io/manifest.json"));
    cinemeta.flags.official = true;
    cinemeta.flags.enabled = true;
    cinemeta.flags.protectedAddon = true;
    cinemeta.manifest.id = QStringLiteral("com.linvo.cinemeta");
    cinemeta.manifest.version = QStringLiteral("3.0.14");
    cinemeta.manifest.name = QStringLiteral("Cinemeta");
    cinemeta.manifest.types = {QStringLiteral("movie"), QStringLiteral("series")};
    {
        ManifestResource catalog;
        catalog.name = QStringLiteral("catalog");
        ManifestResource meta;
        meta.name = QStringLiteral("meta");
        ManifestResource addonCatalog;
        addonCatalog.name = QStringLiteral("addon_catalog");
        cinemeta.manifest.resources = {catalog, meta, addonCatalog};
    }
    {
        // Cinemeta's upstream manifest declares top/movie + top/series catalogs
        // both supporting the `search` extra prop. Without these entries,
        // MetaAggregator::searchCatalog finds no catalog with matching type
        // and returns an empty queue — breaks Stream-mode search end-to-end.
        // Seed matches v3-cinemeta.strem.io/manifest.json shape so the manifest
        // is usable offline; a future refresh-on-startup pass will replace
        // this with a live fetch if one is ever implemented.
        ManifestExtraProp searchExtra;
        searchExtra.name = QStringLiteral("search");
        searchExtra.isRequired = false;
        searchExtra.optionsLimit = 1;

        ManifestExtraProp genreExtra;
        genreExtra.name = QStringLiteral("genre");
        genreExtra.isRequired = false;
        genreExtra.optionsLimit = 1;

        ManifestExtraProp skipExtra;
        skipExtra.name = QStringLiteral("skip");
        skipExtra.isRequired = false;
        skipExtra.optionsLimit = 1;

        ManifestCatalog topMovie;
        topMovie.id    = QStringLiteral("top");
        topMovie.type  = QStringLiteral("movie");
        topMovie.name  = QStringLiteral("Popular");
        topMovie.extra = {searchExtra, genreExtra, skipExtra};

        ManifestCatalog topSeries;
        topSeries.id    = QStringLiteral("top");
        topSeries.type  = QStringLiteral("series");
        topSeries.name  = QStringLiteral("Popular");
        topSeries.extra = {searchExtra, genreExtra, skipExtra};

        cinemeta.manifest.catalogs = {topMovie, topSeries};
    }
    m_addons.push_back(cinemeta);

    AddonDescriptor torrentio;
    torrentio.transportUrl = QUrl(QStringLiteral("https://torrentio.strem.fun/manifest.json"));
    torrentio.flags.official = true;
    torrentio.flags.enabled = true;
    torrentio.flags.protectedAddon = true;
    torrentio.manifest.id = QStringLiteral("com.stremio.torrentio.addon");
    torrentio.manifest.version = QStringLiteral("0.0.15");
    torrentio.manifest.name = QStringLiteral("Torrentio");
    torrentio.manifest.types = {
        QStringLiteral("movie"), QStringLiteral("series"),
        QStringLiteral("anime"), QStringLiteral("other"),
    };
    {
        ManifestResource streamRes;
        streamRes.name = QStringLiteral("stream");
        streamRes.hasTypes = true;
        streamRes.types = {QStringLiteral("movie"), QStringLiteral("series"), QStringLiteral("anime")};
        streamRes.hasIdPrefixes = true;
        streamRes.idPrefixes = {QStringLiteral("tt"), QStringLiteral("kitsu")};
        torrentio.manifest.resources = {streamRes};
    }
    m_addons.push_back(torrentio);
}

}
