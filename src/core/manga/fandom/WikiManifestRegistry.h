// src/core/manga/fandom/WikiManifestRegistry.h
//
// Per-app in-memory registry of all curated Wiki Manifests. Loaded at app
// start from resources/fandom_manifests/*.json. Provides fast lookup by
// seriesId (primary key) and fandomWikiId (secondary index).
//
// Spec: docs/superpowers/specs/2026-05-19-fandom-catalog-redesign-design.md §4.1
// Plan: docs/superpowers/plans/2026-05-19-fandom-catalog-redesign.md Task 3

#pragma once

#include "WikiManifest.h"
#include <QHash>
#include <QString>

namespace tankoban::manga::fandom {

class WikiManifestRegistry
{
public:
    // Load all *.json files from the given directory. Invalid manifests are
    // logged + skipped (per spec: "logs invalid manifests without aborting
    // load"). Clears any previously-loaded manifests first. Returns the
    // count of manifests successfully loaded.
    int loadFromDirectory(const QString& manifestsDir);

    // Returns the manifest matching the given Tankoban canonical seriesId,
    // or an invalid manifest (isValid() == false) if not found.
    WikiManifest find(const QString& seriesId) const;

    // Returns the manifest matching the given Fandom subdomain (P4073
    // value), or invalid if not found. Useful when the discovery layer
    // already resolved a subdomain and wants to look up its manifest.
    WikiManifest findByFandomWikiId(const QString& wikiId) const;

    int count() const { return m_bySeriesId.size(); }

private:
    QHash<QString, WikiManifest> m_bySeriesId;        // primary key
    QHash<QString, QString>      m_wikiIdToSeriesId;  // secondary index
};

} // namespace tankoban::manga::fandom
