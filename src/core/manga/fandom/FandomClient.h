// src/core/manga/fandom/FandomClient.h
//
// HTTP wrapper around the MediaWiki action=parse API on Fandom subdomains.
// Returns rendered HTML + section structure for a given (subdomain, pagePath)
// pair. Consumed by FandomVolumeResolver (Task 14) to feed the Table /
// Infobox extractors.
//
// Spec: docs/superpowers/specs/2026-05-19-fandom-catalog-redesign-design.md §4.1
// Plan: docs/superpowers/plans/2026-05-19-fandom-catalog-redesign.md Task 4
//
// Codex Trigger C review (§4.1.1) expanded the return shape to include
// canonicalPageTitle / redirectTrail so callers can detect moved pages +
// log loops. Pagination crawl guards live in FandomVolumeResolver (Task 14),
// not here — this client is single-page-fetch only.

#pragma once

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>

namespace tankoban::manga::fandom {

// Result of one action=parse fetch.
struct ParsedPage {
    QString     rawHtml;             // parse.text["*"] — the rendered page body
    QJsonArray  sections;            // parse.sections — TOC structure
    QString     pageTitle;           // parse.title — as MediaWiki returned it
    QString     canonicalPageTitle;  // resolved title after redirects (per Codex §4.1.1)
    QStringList redirectTrail;       // list of intermediate redirect targets
    bool        isValid() const { return !rawHtml.isEmpty(); }
};

class FandomClient : public QObject
{
    Q_OBJECT
public:
    explicit FandomClient(QNetworkAccessManager* nam, QObject* parent = nullptr);

    // Async fetch. requestId identifies the call across the (fetched, fail)
    // signal pair. Subdomain is the Fandom wiki ID (e.g., "deathnote"),
    // pagePath is either "/wiki/<title>" or a bare title (the leading
    // "/wiki/" is stripped before being passed as the action=parse `page`
    // parameter).
    void fetchPage(int requestId, const QString& subdomain, const QString& pagePath);

signals:
    void pageFetched(int requestId, const tankoban::manga::fandom::ParsedPage& page);
    void pageFetchFailed(int requestId, const QString& reason);

private:
    QNetworkAccessManager* m_nam = nullptr;  // non-owning
};

} // namespace tankoban::manga::fandom
