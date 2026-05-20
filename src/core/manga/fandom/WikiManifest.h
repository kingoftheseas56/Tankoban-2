// src/core/manga/fandom/WikiManifest.h
//
// Per-series JSON-backed configuration for the Fandom-driven catalog
// redesign. Each manifest is one file at
// resources/fandom_manifests/<seriesId>.json and pins everything an
// extractor needs to parse that series' Fandom wiki without heuristic
// guessing: which subdomain, which page path, which page model, which
// extractor type, and which schema variances the wiki happens to use.
//
// Spec: docs/superpowers/specs/2026-05-19-fandom-catalog-redesign-design.md §5
// Plan: docs/superpowers/plans/2026-05-19-fandom-catalog-redesign.md Task 2
//
// JSON schema variance handled (per Codex Trigger C review-and-expand §5):
//   - paginationModel + pagePathPattern + pageRangeSize for hierarchy-style
//     range-page navigation (Kingdom Volumes_71-80 / JJK per-volume pages)
//   - maxVolumeProbe upper bound for hierarchy crawls without an explicit
//     final-volume marker
//   - nextLinkSelector for wikis exposing "next page" anchors

#pragma once

#include "FandomTypes.h"
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace tankoban::manga::fandom {

// Which extraction module a wiki manifest should route to.
enum class ExtractorType {
    Table,    // monolith + chapter-branded URL pages with repeated <table> blocks
    Infobox,  // hierarchy-model per-volume pages with <aside class="portable-infobox">
    Mixed,    // rare — series whose list page is Table but per-volume detail uses Infobox
};

// Per-field expectation declared by the manifest. Used by extractors to
// decide whether a missing value is normal, surprising, or an error.
enum class FieldExpectation {
    Required,  // fail extraction if missing
    Expected,  // warn if missing
    Optional,  // silent if missing
    Absent,    // don't even try to extract (wiki doesn't surface this field)
};

// Pagination strategy for wikis that split their volume list across multiple
// pages. Added per Codex §5 expansion to keep range-page navigation out of
// the extractors' core path.
enum class PaginationModel {
    None,         // single page contains the whole catalog
    RangePages,   // navigate /Volume_71-80 → /Vol.73 (Kingdom)
    NextLink,     // follow "next page" anchors found via nextLinkSelector
    DetailLinks,  // per-volume detail link from a flat index page (JJK)
};

struct WikiManifest {
    // Identity
    QString          seriesId;
    QString          wikidataQid;       // canonical cross-source identity (Q-ID)
    QString          fandomWikiId;      // subdomain, from Wikidata P4073

    // Where the data lives
    QString          volumePagePath;    // canonical entry path
    PageModel        pageModel        = PageModel::Monolith;
    ExtractorType    extractorType    = ExtractorType::Table;

    // Schema variance hints
    QString          chapterKeyword   = QStringLiteral("Chapter"); // "Chapter" | "Mission" | "Episode"
    QString          groupingSemantics;                            // "mathematical-buckets" | "narrative-arcs" | "subsection-headers" | "flat"
    QStringList      editionFilters;                               // section headers to EXCLUDE
    QStringList      unitHierarchy;                                // e.g., ["chapter", "volume"] or ["episode", "chapter", "arc", "volume"]
    QHash<QString, FieldExpectation> expectedFields;

    // Pagination (Codex §5 expansion)
    PaginationModel  paginationModel  = PaginationModel::None;
    QString          pagePathPattern;   // e.g., "/wiki/Volumes_and_Chapters/Volume_%1-%2" or "/wiki/Vol.%1"
    int              pageRangeSize    = 0;    // e.g., 10 for Kingdom-style range pages
    int              maxVolumeProbe   = 0;    // upper bound for hierarchy crawls without a final-volume marker
    QString          nextLinkSelector;        // CSS-ish hint for next-link wikis

    QString          notes;             // human-readable hints for manifest maintainers

    // Parse a WikiManifest from a JSON object. Returns an invalid manifest
    // (isValid() == false) if seriesId or fandomWikiId is missing.
    static WikiManifest fromJson(const QJsonObject& obj);
    bool isValid() const { return !seriesId.isEmpty() && !fandomWikiId.isEmpty(); }
};

} // namespace tankoban::manga::fandom
