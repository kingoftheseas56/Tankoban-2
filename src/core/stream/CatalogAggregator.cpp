#include "CatalogAggregator.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

#include "addon/AddonRegistry.h"
#include "addon/AddonTransport.h"
#include "addon/Descriptor.h"
#include "addon/ResourcePath.h"

using tankostream::addon::AddonDescriptor;
using tankostream::addon::AddonRegistry;
using tankostream::addon::AddonTransport;
using tankostream::addon::ManifestCatalog;
using tankostream::addon::ManifestExtraProp;
using tankostream::addon::MetaItemPreview;
using tankostream::addon::ResourceRequest;

namespace tankostream::stream {

namespace {

QList<QPair<QString, QString>> applyOptionsLimit(
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
            out.append(qMakePair(p.name, p.options.first()));
        }
    }

    for (auto& kv : out) {
        const ManifestExtraProp* prop = findProp(kv.first);
        if (!prop || prop->optionsLimit <= 0) {
            continue;
        }
        const QStringList pieces = kv.second.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (pieces.size() <= prop->optionsLimit) {
            continue;
        }
        kv.second = pieces.mid(0, prop->optionsLimit).join(QLatin1Char(','));
    }

    return out;
}

MetaItemPreview parseMetaPreview(const QJsonObject& metaObj, const QString& typeHint)
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

}

CatalogAggregator::CatalogAggregator(AddonRegistry* registry, QObject* parent)
    : QObject(parent)
    , m_registry(registry)
{
}

void CatalogAggregator::load(const CatalogQuery& query)
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

void CatalogAggregator::loadNextPage()
{
    if (m_query.catalogId.isEmpty()) {
        return;
    }

    bool any = false;
    for (auto it = m_activeByAddon.constBegin(); it != m_activeByAddon.constEnd(); ++it) {
        if (!it.value().hasMore) {
            continue;
        }
        if (it.value().inFlight) {
            continue;
        }
        any = true;
        break;
    }
    if (!any) {
        emit catalogPage({}, false);
        return;
    }
    dispatchCurrentPage();
}

QMap<QString, CatalogAggregator::AddonCursor>
CatalogAggregator::planRequests(const CatalogQuery& query) const
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

void CatalogAggregator::dispatchCurrentPage()
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
            req.extra.append(qMakePair(QStringLiteral("skip"),
                                       QString::number(cursor.skip)));
        }

        auto* worker = new AddonTransport(this);
        const QString addonId = cursor.addonId;

        connect(worker, &AddonTransport::resourceReady, this,
            [this, addonId, worker](const ResourceRequest&, const QJsonObject& payload) {
                worker->deleteLater();
                onAddonReady(addonId, payload);
            });

        connect(worker, &AddonTransport::resourceFailed, this,
            [this, addonId, worker](const ResourceRequest&, const QString& message) {
                worker->deleteLater();
                onAddonFailed(addonId, message);
            });

        worker->fetchResource(cursor.baseUrl, req);
    }

    if (m_pendingResponses == 0) {
        emit catalogPage({}, false);
    }
}

void CatalogAggregator::onAddonReady(const QString& addonId, const QJsonObject& payload)
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
        m_pageBuffer.append(item);
        ++addedFromAddon;
    }

    cursor.skip += addedFromAddon;

    if (payload.contains(QStringLiteral("hasMore"))) {
        cursor.hasMore = payload.value(QStringLiteral("hasMore")).toBool(false);
    } else {
        cursor.hasMore = (addedFromAddon > 0);
    }

    completeIfReady();
}

void CatalogAggregator::onAddonFailed(const QString& addonId, const QString& message)
{
    auto it = m_activeByAddon.find(addonId);
    if (it != m_activeByAddon.end()) {
        it.value().inFlight = false;
    }
    emit catalogError(addonId, message);
    completeIfReady();
}

void CatalogAggregator::completeIfReady()
{
    --m_pendingResponses;
    if (m_pendingResponses > 0) {
        return;
    }

    bool anyMore = false;
    for (auto it = m_activeByAddon.constBegin(); it != m_activeByAddon.constEnd(); ++it) {
        if (it.value().hasMore) {
            anyMore = true;
            break;
        }
    }

    emit catalogPage(m_pageBuffer, anyMore);
    m_pageBuffer.clear();
}

void CatalogAggregator::resetInternalState()
{
    m_query = {};
    m_seenMetaIds.clear();
    m_activeByAddon.clear();
    m_pageBuffer.clear();
    m_pendingResponses = 0;
}

}
