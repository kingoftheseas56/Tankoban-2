// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 3.1 (Catalog Browsing)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:138
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:141
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:142
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:143
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/AddonRegistry.h:24
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/AddonTransport.h:22
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/AddonTransport.h:27
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/Manifest.h:19
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/Manifest.h:23
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/ResourcePath.h:9
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/core/stream/addon/MetaItem.h:51
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/catalog_with_filters.rs:90
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/catalog_with_filters.rs:118
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/catalog_with_filters.rs:191
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/catalogs_with_extra.rs:173
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/models/catalogs_with_extra.rs:178
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 4, Batch 3.1.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace tankostream::addon {

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

struct AddonManifest {
    QString id;
    QString name;
    QStringList types;
    QList<ManifestCatalog> catalogs;
};

struct AddonDescriptorFlags {
    bool official = false;
    bool enabled = true;
    bool protectedAddon = false;
};

struct AddonDescriptor {
    AddonManifest manifest;
    QUrl transportUrl;
    AddonDescriptorFlags flags;
};

struct ResourceRequest {
    QString resource;
    QString type;
    QString id;
    QList<QPair<QString, QString>> extra;
};

struct MetaItemPreview {
    QString id;
    QString type;
    QString name;
    QUrl poster;
    QString description;
    QString releaseInfo;
    QString imdbRating;
};

class AddonRegistry : public QObject {
    Q_OBJECT
public:
    explicit AddonRegistry(QObject* parent = nullptr) : QObject(parent) {}
    QList<AddonDescriptor> list() const;
};

class AddonTransport : public QObject {
    Q_OBJECT
public:
    explicit AddonTransport(QObject* parent = nullptr) : QObject(parent) {}
    void fetchResource(const QUrl& base, const ResourceRequest& request);

signals:
    void resourceReady(const ResourceRequest& request, const QJsonObject& payload);
    void resourceFailed(const ResourceRequest& request, const QString& message);
};

} // namespace tankostream::addon

namespace tankostream::stream {

using tankostream::addon::AddonDescriptor;
using tankostream::addon::AddonRegistry;
using tankostream::addon::AddonTransport;
using tankostream::addon::ManifestCatalog;
using tankostream::addon::ManifestExtraProp;
using tankostream::addon::MetaItemPreview;
using tankostream::addon::ResourceRequest;

struct CatalogQuery {
    // Optional: constrain to one addon row (used by StreamHomeBoard).
    QString addonId;
    // Required: movie / series / etc.
    QString type;
    // Required: top / popular / etc.
    QString catalogId;
    // Optional user filters (genre, year, sort, search...).
    QList<QPair<QString, QString>> extra;
};

class CatalogAggregator : public QObject {
    Q_OBJECT

public:
    explicit CatalogAggregator(AddonRegistry* registry, QObject* parent = nullptr)
        : QObject(parent)
        , m_registry(registry)
    {
    }

    void load(const CatalogQuery& query)
    {
        resetInternalState();
        m_query = query;
        m_activeByAddon = planRequests(query);
        if (m_activeByAddon.isEmpty()) {
            emit catalogPage({}, false);
            return;
        }
        dispatchCurrentPage();
    }

    void loadNextPage()
    {
        if (m_query.catalogId.isEmpty()) {
            return;
        }

        bool any = false;
        for (auto it = m_activeByAddon.begin(); it != m_activeByAddon.end(); ++it) {
            if (!it.value().hasMore) {
                continue;
            }
            if (it.value().inFlight) {
                continue;
            }
            any = true;
        }
        if (!any) {
            emit catalogPage({}, false);
            return;
        }
        dispatchCurrentPage();
    }

signals:
    void catalogPage(const QList<MetaItemPreview>& items, bool hasMore);
    void catalogError(const QString& addonId, const QString& message);

private:
    struct AddonCursor {
        QString addonId;
        QUrl baseUrl;
        ManifestCatalog catalog;
        int skip = 0;
        bool hasMore = true;
        bool inFlight = false;
    };

    static QList<QPair<QString, QString>> applyOptionsLimit(
        const ManifestCatalog& catalog,
        const QList<QPair<QString, QString>>& input)
    {
        QList<QPair<QString, QString>> out = input;

        auto findProp = [&](const QString& name) -> const ManifestExtraProp* {
            for (const ManifestExtraProp& p : catalog.extra) {
                if (p.name == name) {
                    return &p;
                }
            }
            return nullptr;
        };

        // Ensure required extras have at least one value.
        for (const ManifestExtraProp& p : catalog.extra) {
            if (!p.isRequired) {
                continue;
            }
            bool exists = false;
            for (const auto& kv : out) {
                if (kv.first == p.name) {
                    exists = true;
                    break;
                }
            }
            if (!exists && !p.options.isEmpty()) {
                out.push_back(qMakePair(p.name, p.options.first()));
            }
        }

        // Respect optionsLimit for comma-separated multi-values.
        for (auto& kv : out) {
            const ManifestExtraProp* prop = findProp(kv.first);
            if (!prop || prop->optionsLimit <= 0) {
                continue;
            }
            const QStringList pieces =
                kv.second.split(',', Qt::SkipEmptyParts);
            if (pieces.size() <= prop->optionsLimit) {
                continue;
            }
            kv.second = pieces.mid(0, prop->optionsLimit).join(',');
        }

        return out;
    }

    static MetaItemPreview parseMetaPreview(const QJsonObject& metaObj, const QString& typeHint)
    {
        MetaItemPreview item;
        item.id = metaObj.value(QStringLiteral("id")).toString().trimmed();
        if (item.id.isEmpty()) {
            item.id = metaObj.value(QStringLiteral("imdb_id")).toString().trimmed();
        }
        item.type = metaObj.value(QStringLiteral("type")).toString().trimmed();
        if (item.type.isEmpty()) {
            item.type = typeHint;
        }
        item.name = metaObj.value(QStringLiteral("name")).toString().trimmed();
        item.poster = QUrl(metaObj.value(QStringLiteral("poster")).toString().trimmed());
        item.description = metaObj.value(QStringLiteral("description")).toString().trimmed();
        item.releaseInfo = metaObj.value(QStringLiteral("releaseInfo")).toString().trimmed();
        item.imdbRating = metaObj.value(QStringLiteral("imdbRating")).toString().trimmed();
        return item;
    }

    QMap<QString, AddonCursor> planRequests(const CatalogQuery& query) const
    {
        QMap<QString, AddonCursor> out;
        if (!m_registry) {
            return out;
        }

        const QList<AddonDescriptor> addons = m_registry->list();
        for (const AddonDescriptor& addon : addons) {
            if (!addon.flags.enabled) {
                continue;
            }
            if (!query.addonId.isEmpty() && addon.manifest.id != query.addonId) {
                continue;
            }

            for (const ManifestCatalog& catalog : addon.manifest.catalogs) {
                if (catalog.type.compare(query.type, Qt::CaseInsensitive) != 0) {
                    continue;
                }
                if (catalog.id != query.catalogId) {
                    continue;
                }

                AddonCursor c;
                c.addonId = addon.manifest.id;
                c.baseUrl = addon.transportUrl;
                c.catalog = catalog;
                out.insert(c.addonId, c);
                break;
            }
        }
        return out;
    }

    void dispatchCurrentPage()
    {
        m_pendingResponses = 0;
        m_pageBuffer.clear();

        for (auto it = m_activeByAddon.begin(); it != m_activeByAddon.end(); ++it) {
            AddonCursor& cursor = it.value();
            if (!cursor.hasMore || cursor.inFlight) {
                continue;
            }
            cursor.inFlight = true;
            ++m_pendingResponses;

            ResourceRequest req;
            req.resource = QStringLiteral("catalog");
            req.type = cursor.catalog.type;
            req.id = cursor.catalog.id;
            req.extra = applyOptionsLimit(cursor.catalog, m_query.extra);
            if (cursor.skip > 0) {
                req.extra.push_back(qMakePair(QStringLiteral("skip"),
                                              QString::number(cursor.skip)));
            }

            // One transport instance per addon request keeps source attribution explicit
            // even though AddonTransport signals do not include base URL.
            auto* worker = new AddonTransport(this);

            connect(worker, &AddonTransport::resourceReady, this,
                [this, addonId = cursor.addonId, worker](
                    const ResourceRequest&,
                    const QJsonObject& payload) {
                    worker->deleteLater();
                    onAddonReady(addonId, payload);
                });

            connect(worker, &AddonTransport::resourceFailed, this,
                [this, addonId = cursor.addonId, worker](
                    const ResourceRequest&,
                    const QString& message) {
                    worker->deleteLater();
                    onAddonFailed(addonId, message);
                });

            worker->fetchResource(cursor.baseUrl, req);
        }

        if (m_pendingResponses == 0) {
            emit catalogPage({}, false);
        }
    }

    void onAddonReady(const QString& addonId, const QJsonObject& payload)
    {
        auto it = m_activeByAddon.find(addonId);
        if (it == m_activeByAddon.end()) {
            completeIfReady();
            return;
        }

        AddonCursor& cursor = it.value();
        cursor.inFlight = false;

        const QJsonArray metas = payload.value(QStringLiteral("metas")).toArray();
        int addedFromAddon = 0;
        for (const QJsonValue& value : metas) {
            const MetaItemPreview item = parseMetaPreview(value.toObject(), cursor.catalog.type);
            if (item.id.isEmpty() || item.name.isEmpty()) {
                continue;
            }
            if (m_seenMetaIds.contains(item.id)) {
                continue;
            }
            m_seenMetaIds.insert(item.id);
            m_pageBuffer.push_back(item);
            ++addedFromAddon;
        }

        cursor.skip += addedFromAddon;

        // Explicit hasMore (preferred) or fallback: non-empty page means maybe more.
        if (payload.contains(QStringLiteral("hasMore"))) {
            cursor.hasMore = payload.value(QStringLiteral("hasMore")).toBool(false);
        } else {
            cursor.hasMore = (addedFromAddon > 0);
        }

        completeIfReady();
    }

    void onAddonFailed(const QString& addonId, const QString& message)
    {
        auto it = m_activeByAddon.find(addonId);
        if (it != m_activeByAddon.end()) {
            it.value().inFlight = false;
            // Allow retry on next page load; do not permanently close cursor on one timeout.
        }
        emit catalogError(addonId, message);
        completeIfReady();
    }

    void completeIfReady()
    {
        --m_pendingResponses;
        if (m_pendingResponses > 0) {
            return;
        }

        bool anyMore = false;
        for (auto it = m_activeByAddon.cbegin(); it != m_activeByAddon.cend(); ++it) {
            if (it.value().hasMore) {
                anyMore = true;
                break;
            }
        }

        emit catalogPage(m_pageBuffer, anyMore);
        m_pageBuffer.clear();
    }

    void resetInternalState()
    {
        m_query = {};
        m_seenMetaIds.clear();
        m_activeByAddon.clear();
        m_pageBuffer.clear();
        m_pendingResponses = 0;
    }

    AddonRegistry* m_registry = nullptr;

    CatalogQuery m_query;
    QSet<QString> m_seenMetaIds;
    QMap<QString, AddonCursor> m_activeByAddon;
    QList<MetaItemPreview> m_pageBuffer;
    int m_pendingResponses = 0;
};

} // namespace tankostream::stream

