// src/core/manga/fandom/extractors/TableExtractor.h
//
// Parses Fandom volume catalog pages whose body is a repeated <table>-driven
// per-volume record (monolith + chapter-branded URLs). The same class spans
// multiple grouping shapes; the manifest's `groupingSemantics` field routes
// the page to the correct branch:
//
//   - subsection-headers  → Death Note style (h3 per volume, table beneath)
//   - mathematical-buckets → One Piece style (h2 ranges → h3 per volume)
//   - narrative-arcs       → Berserk style (continuous wikitable, arc rowspan)
//   - multi-era            → Naruto style (top h2 era → per-era h3 volumes)
//
// Spec: docs/superpowers/specs/2026-05-19-fandom-catalog-redesign-design.md §5.1
// Plan: docs/superpowers/plans/2026-05-19-fandom-catalog-redesign.md Tasks 6-9

#pragma once

#include "core/manga/fandom/FandomTypes.h"
#include "core/manga/fandom/WikiManifest.h"
#include <QList>
#include <QString>

namespace tankoban::manga::fandom {

class TableExtractor
{
public:
    // Parse a fetched Fandom page (rawHtml) using the manifest's grouping
    // hint to pick a branch. Returns an empty list if the manifest's
    // groupingSemantics is unsupported (logged at warning level).
    static QList<FandomVolume> extract(const QString& rawHtml,
                                       const WikiManifest& manifest);
};

} // namespace tankoban::manga::fandom
