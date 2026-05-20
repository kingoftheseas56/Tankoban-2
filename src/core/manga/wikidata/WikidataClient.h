// src/core/manga/wikidata/WikidataClient.h
//
// SPARQL query wrapper for the Wikidata Query Service. Given a Wikidata
// Q-ID (the canonical cross-source identity for a series), resolves to a
// FandomReference{subdomain} by reading property P4073 (Fandom wiki ID).
// Also reads P6262 (Fandom article ID) for drift detection — logs a
// warning if P4073 + P6262 disagree on the subdomain (the One Piece →
// tropedia drift class from audit §2.3 / spec §3.1).
//
// Trust hierarchy locked by spec §3.1:
//   1. WikiManifest (when present) is the source of truth.
//   2. ComicsLibraryRecord cached fields are second.
//   3. P4073 from this client is third.
//   4. P6262 is a confidence signal only — NEVER overrides the subdomain.
//
// Spec: docs/superpowers/specs/2026-05-19-fandom-catalog-redesign-design.md §3.1, §4.1
// Plan: docs/superpowers/plans/2026-05-19-fandom-catalog-redesign.md Task 5

#pragma once

#include "core/manga/fandom/FandomTypes.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

namespace tankoban::manga::wikidata {

class WikidataClient : public QObject
{
    Q_OBJECT
public:
    explicit WikidataClient(QNetworkAccessManager* nam, QObject* parent = nullptr);

    // Resolve a Wikidata Q-ID (e.g. "Q14559") to a FandomReference. Emits
    // resolved(requestId, ref) with ref.subdomain populated from P4073, or
    // resolveFailed(requestId, reason) on any failure. P4073/P6262 drift
    // logged via Q_LOGGING_CATEGORY "tankoban.manga.wikidata".
    //
    // ref.volumePagePath is intentionally left empty — only the subdomain
    // resolves here; page-path resolution happens at the manifest layer.
    void resolveQid(int requestId, const QString& qid);

signals:
    void resolved(int requestId, const tankoban::manga::fandom::FandomReference& ref);
    void resolveFailed(int requestId, const QString& reason);

private:
    QNetworkAccessManager* m_nam = nullptr;  // non-owning
};

} // namespace tankoban::manga::wikidata
