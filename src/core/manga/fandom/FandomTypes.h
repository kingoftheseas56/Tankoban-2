// src/core/manga/fandom/FandomTypes.h
//
// Minimal type stubs kept for wikidata/ and wikipedia/ module compatibility
// after COMICS_MANGAFIRE_PIVOT Phase B (2026-05-23).
//
// REMOVED in Phase B: FandomCatalog, kFandomCatalogSchemaVersion (replaced
// by MangaCatalog + kMangaCatalogSchemaVersion in core/manga/MangaCatalogTypes.h).
//
// KEPT: FandomReference (WikidataClient/WikidataCache domain type — Q-ID →
// Fandom wiki subdomain resolution) and FandomVolume (WikipediaParser/
// WikipediaResolver return type — Wikipedia volume-table rows).
// Neither type is a candidate for the MangaCatalog rename; they are specific
// to their respective resolution layers and do not flow into ComicsSeriesView.

#pragma once

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

namespace tankoban::manga::fandom {

// How a series's volume data is laid out across Fandom wiki pages.
enum class PageModel {
    Monolith,        // one page = all volumes (One Piece, Berserk, Naruto, Spy x Family)
    Hierarchy,       // central index → atomized per-volume pages (Kingdom, JJK)
    ChapterBranded,  // chapter-branded URL serving volume-record body (Death Note, AoT)
};

// Where a series's catalog lives on Fandom. Produced by the Discovery layer
// (WikidataClient → manifest lookup → final resolution).
struct FandomReference {
    QString   subdomain;        // e.g., "deathnote"
    QString   volumePagePath;   // e.g., "/wiki/List_of_Death_Note_chapters"
    PageModel pageModel = PageModel::Monolith;
    bool      isValid() const { return !subdomain.isEmpty() && !volumePagePath.isEmpty(); }
};

// One volume's worth of normalized data as parsed by WikipediaParser.
// "Fandom" in the name is historical; this is a generic volume-row shape
// reused by the Wikipedia fallback layer.
struct FandomVolume {
    int         volumeNumber      = 0;
    QString     titleEnglish;
    QString     titleJapanese;
    QString     titleRomaji;
    int         chapterRangeStart = 0;
    int         chapterRangeEnd   = 0;
    QStringList chapterList;
    QDate       releaseDateJp;
    QDate       releaseDateEn;
    QString     isbnJp;
    QString     isbnEn;
    QString     synopsis;
    QString     coverUrlEnglish;
    QString     coverUrlJapanese;
    QString     groupingLabel;
};

} // namespace tankoban::manga::fandom
