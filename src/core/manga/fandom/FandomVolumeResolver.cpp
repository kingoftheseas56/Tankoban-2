// src/core/manga/fandom/FandomVolumeResolver.cpp

#include "FandomVolumeResolver.h"

#include "FandomCatalogCache.h"
#include "FandomClient.h"
#include "WikiManifestRegistry.h"
#include "core/manga/fandom/extractors/TableExtractor.h"
#include "core/manga/wikidata/WikidataClient.h"

#include <QDateTime>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcResolver, "tankoban.manga.fandom.resolver")

namespace tankoban::manga::fandom {

FandomVolumeResolver::FandomVolumeResolver(wikidata::WikidataClient* wd,
                                           FandomClient* fc,
                                           WikiManifestRegistry* registry,
                                           QObject* parent)
    : QObject(parent), m_wd(wd), m_fc(fc), m_registry(registry)
{
    if (m_wd) {
        connect(m_wd, &wikidata::WikidataClient::resolved,
                this, &FandomVolumeResolver::onWikidataResolved);
        connect(m_wd, &wikidata::WikidataClient::resolveFailed,
                this, &FandomVolumeResolver::onWikidataFailed);
    }
    if (m_fc) {
        connect(m_fc, &FandomClient::pageFetched,
                this, &FandomVolumeResolver::onPageFetched);
        connect(m_fc, &FandomClient::pageFetchFailed,
                this, &FandomVolumeResolver::onPageFetchFailed);
    }
}

void FandomVolumeResolver::resolveForSeries(const QString& seriesId,
                                            const QString& wikidataQidHint,
                                            const QString& /*englishTitleHint*/)
{
    if (!m_registry || !m_fc) {
        emit unresolved(seriesId, QStringLiteral("dependencies-missing"));
        return;
    }

    WikiManifest manifest = m_registry->find(seriesId);
    const QString qid = manifest.isValid() && !manifest.wikidataQid.isEmpty()
                            ? manifest.wikidataQid
                            : wikidataQidHint;

    // (1) Cache lookup.
    if (!qid.isEmpty()) {
        if (auto cached = FandomCatalogCache::loadByQid(qid)) {
            qCInfo(lcResolver).noquote()
                << "cache hit for" << seriesId << "(qid=" << qid << ")";
            emit resolved(seriesId, *cached);
            return;
        }
    }

    Pending p;
    p.seriesId = seriesId;
    p.manifest = manifest;
    p.qid      = qid;

    // (2) Manifest carries everything we need → skip Wikidata, go fetch.
    if (manifest.isValid()
        && !manifest.fandomWikiId.isEmpty()
        && !manifest.volumePagePath.isEmpty())
    {
        p.subdomain = manifest.fandomWikiId;
        const int requestId = issueRequestId();
        m_pending.insert(requestId, p);
        fetchPageFor(requestId, p);
        return;
    }

    // (3) Need to resolve subdomain via Wikidata.
    if (!qid.isEmpty() && m_wd) {
        const int requestId = issueRequestId();
        m_pending.insert(requestId, p);
        m_wd->resolveQid(requestId, qid);
        return;
    }

    emit unresolved(seriesId, QStringLiteral("no-manifest-no-qid"));
}

void FandomVolumeResolver::fetchPageFor(int requestId, Pending pending)
{
    if (!m_fc) {
        m_pending.remove(requestId);
        emit unresolved(pending.seriesId, QStringLiteral("fandom-client-null"));
        return;
    }
    if (pending.subdomain.isEmpty() || pending.manifest.volumePagePath.isEmpty()) {
        m_pending.remove(requestId);
        emit unresolved(pending.seriesId, QStringLiteral("missing-subdomain-or-page-path"));
        return;
    }
    qCInfo(lcResolver).noquote()
        << "fetching" << pending.subdomain << pending.manifest.volumePagePath
        << "for" << pending.seriesId;
    m_fc->fetchPage(requestId, pending.subdomain, pending.manifest.volumePagePath);
}

void FandomVolumeResolver::onWikidataResolved(int requestId,
                                              const FandomReference& ref)
{
    if (!m_pending.contains(requestId))
        return;  // not ours
    Pending p = m_pending.take(requestId);
    p.subdomain = ref.subdomain;

    // We still need a page path. The manifest provides it; if the manifest
    // was missing entirely we can't proceed without a heuristic discovery
    // step (out of v1 scope).
    if (!p.manifest.isValid() || p.manifest.volumePagePath.isEmpty()) {
        emit unresolved(p.seriesId,
                        QStringLiteral("wikidata-resolved-but-no-manifest-pagepath"));
        return;
    }

    const int newRequestId = issueRequestId();
    m_pending.insert(newRequestId, p);
    fetchPageFor(newRequestId, p);
}

void FandomVolumeResolver::onWikidataFailed(int requestId, const QString& reason)
{
    if (!m_pending.contains(requestId))
        return;
    Pending p = m_pending.take(requestId);
    emit unresolved(p.seriesId,
                    QStringLiteral("wikidata-failed:") + reason);
}

void FandomVolumeResolver::onPageFetched(int requestId, const ParsedPage& page)
{
    if (!m_pending.contains(requestId))
        return;
    Pending p = m_pending.take(requestId);

    if (!page.isValid()) {
        emit unresolved(p.seriesId, QStringLiteral("empty-page"));
        return;
    }

    QList<FandomVolume> volumes;
    if (p.manifest.extractorType == ExtractorType::Table) {
        volumes = TableExtractor::extract(page.rawHtml, p.manifest);
    } else if (p.manifest.extractorType == ExtractorType::Infobox) {
        // Hierarchy / Infobox routes require multi-page iteration over
        // per-volume URLs (Vol.1 .. Vol.N). The single-fetch shape here
        // can't service that without a crawl loop. Deliberately deferred:
        // ComicsPage wiring (Task 19) is the natural consumer and will
        // own the iteration once the UI layer lands.
        emit unresolved(p.seriesId,
                        QStringLiteral("infobox-multi-page-crawl-deferred-to-task-19"));
        return;
    } else if (p.manifest.extractorType == ExtractorType::Mixed) {
        emit unresolved(p.seriesId, QStringLiteral("mixed-extractor-not-supported"));
        return;
    }

    if (volumes.isEmpty()) {
        emit unresolved(p.seriesId,
                        QStringLiteral("extractor-returned-zero-volumes"));
        return;
    }

    completeWithCatalog(p, std::move(volumes));
}

void FandomVolumeResolver::onPageFetchFailed(int requestId, const QString& reason)
{
    if (!m_pending.contains(requestId))
        return;
    Pending p = m_pending.take(requestId);
    emit unresolved(p.seriesId, QStringLiteral("fandom-fetch-failed:") + reason);
}

void FandomVolumeResolver::completeWithCatalog(const Pending& p,
                                               QList<FandomVolume> volumes)
{
    FandomCatalog catalog;
    catalog.seriesId         = p.seriesId;
    catalog.wikidataQid      = p.qid;
    catalog.fandomWikiId     = p.manifest.fandomWikiId.isEmpty()
                                  ? p.subdomain
                                  : p.manifest.fandomWikiId;
    catalog.fandomVolumePath = p.manifest.volumePagePath;
    catalog.volumes          = std::move(volumes);
    catalog.fetchedAt        = QDateTime::currentDateTimeUtc();
    catalog.schemaVersion    = kFandomCatalogSchemaVersion;

    if (!p.qid.isEmpty())
        FandomCatalogCache::storeByQid(p.qid, catalog);

    qCInfo(lcResolver).noquote()
        << "resolved" << p.seriesId << "with" << catalog.volumes.size() << "volumes";
    emit resolved(p.seriesId, catalog);
}

} // namespace tankoban::manga::fandom
