// src/core/manga/fandom/extractors/InfoboxExtractor.h
//
// Parses Fandom per-volume pages whose body is a <aside class="portable-infobox">
// block carrying release-date + ISBN + cover image + chapter list. Used by
// hierarchy-model wikis (Kingdom, JJK) where each volume lives at its own URL
// and the caller iterates over Vol.1 ... Vol.N via the manifest's pagination
// hints (paginationModel + pagePathPattern + pageRangeSize).
//
// Spec: docs/superpowers/specs/2026-05-19-fandom-catalog-redesign-design.md §5.2
// Plan: docs/superpowers/plans/2026-05-19-fandom-catalog-redesign.md Tasks 10-11

#pragma once

#include "core/manga/fandom/FandomTypes.h"
#include "core/manga/fandom/WikiManifest.h"
#include <QString>

namespace tankoban::manga::fandom {

class InfoboxExtractor
{
public:
    // Parse a single per-volume page. Returns one FandomVolume with the
    // given volumeNumber stamped on it (the caller knows N from the URL it
    // fetched — Vol.73, Volume_0, Volume_1, etc.).
    //
    // Empty fields signal "slot absent from this wiki" (the wiki doesn't
    // surface this field at all). Whitespace-only fields signal "slot
    // exists but empty" (e.g., Kingdom Vol.73 has a <h2>Synopsis</h2>
    // header followed immediately by <h2>Chapters</h2> — empty body).
    static FandomVolume extractSingle(const QString& rawHtml,
                                      int volumeNumber,
                                      const WikiManifest& manifest);
};

} // namespace tankoban::manga::fandom
