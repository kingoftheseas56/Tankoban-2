// src/core/manga/fandom/FandomTypes.h
//
// Foundational data types for the Fandom-driven catalog redesign per
// docs/superpowers/specs/2026-05-19-fandom-catalog-redesign-design.md § 4.3.
//
// These structs are POD-ish (Qt value types). JSON serialization for the
// 7-day disk cache lands separately in FandomCatalogCache (Task 12 in the
// implementation plan).
//
// Consumers (created in later tasks):
//   - FandomClient / WikidataClient / extractors PRODUCE FandomVolume +
//     FandomCatalog + FandomReference values.
//   - FandomVolumeResolver / FallbackChainResolver COMPOSE them.
//   - ComicsSeriesView::populateVolumeRowsFromFandom CONSUMES FandomCatalog.

#pragma once

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace tankoban::manga::fandom {

// How a series's volume data is laid out across Fandom wiki pages.
// Locked by the spec's variance-axis "Topology" + extended for chapter-branded
// URLs (Death Note, AoT) whose page title says "chapters" but whose body is
// a repeated per-volume record.
enum class PageModel {
    Monolith,        // one page = all volumes (One Piece, Berserk, Naruto, Spy x Family)
    Hierarchy,       // central index → atomized per-volume pages (Kingdom, JJK)
    ChapterBranded,  // chapter-branded URL serving volume-record body (Death Note, AoT)
};

// Where a series's catalog lives on Fandom. Produced by the Discovery layer
// (WikidataClient → manifest lookup → final resolution).
struct FandomReference {
    QString    subdomain;        // e.g., "deathnote"
    QString    volumePagePath;   // e.g., "/wiki/List_of_Death_Note_chapters"
    PageModel  pageModel = PageModel::Monolith;
    bool       isValid() const { return !subdomain.isEmpty() && !volumePagePath.isEmpty(); }
};

// One volume's worth of normalized catalog data. Empty strings/dates mean
// "field not present on this wiki" — distinct from "field exists but is
// empty" (Codex Trigger C expansion §4.3 — Kingdom Vol.73 case).
struct FandomVolume {
    int          volumeNumber     = 0;
    QString      titleEnglish;        // e.g., "Romance Dawn" / "Boredom"
    QString      titleJapanese;       // kanji form, e.g., "ROMANCE DAWN—冒険の夜明け—"
    QString      titleRomaji;         // e.g., "Romansu Dōn -Bōken no Yoake-"
    int          chapterRangeStart = 0;
    int          chapterRangeEnd   = 0;
    QStringList  chapterList;         // optional richer per-chapter strings if scraped
    QDate        releaseDateJp;
    QDate        releaseDateEn;
    QString      isbnJp;
    QString      isbnEn;
    QString      synopsis;            // empty if not on this wiki (e.g., Berserk volume blocks)
    QString      coverUrlEnglish;     // empty if no English-edition cover surfaced
    QString      coverUrlJapanese;
};

// Schema version stamp baked into every cached FandomCatalog. Bumping this
// constant invalidates all on-disk caches — used when FandomVolume's field
// shape changes (e.g., adding hierarchy-display fields per Codex §4.3).
constexpr int kFandomCatalogSchemaVersion = 1;

// One series's worth of normalized catalog data. Cached on disk by
// FandomCatalogCache (Task 12) with a 7-day TTL.
struct FandomCatalog {
    QString             seriesId;                       // Tankoban's canonical seriesId
    QString             wikidataQid;                    // canonical cross-source identity
    QString             fandomWikiId;                   // cached subdomain (e.g., "deathnote")
    QString             fandomVolumePath;               // cached page path
    QString             seriesSynopsis;                 // optional fallback when per-volume synopsis empty
    QList<FandomVolume> volumes;
    QDateTime           fetchedAt;                      // for 7d TTL
    int                 schemaVersion = kFandomCatalogSchemaVersion;
    bool                isValid() const { return !seriesId.isEmpty() && !volumes.isEmpty(); }
};

} // namespace tankoban::manga::fandom
