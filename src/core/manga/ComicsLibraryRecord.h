#pragma once

#include "MangaSeriesDetail.h"
#include <QJsonObject>
#include <QString>

// Schema for one entry in comics_library.json. Records exist ONLY for
// Tankoyomi-origin series; folder-imported series are represented purely
// by LibraryScanner's on-disk walk + cover cache. Source-of-truth
// invariant per Codex §16: this record is authoritative for
// "is this series Tankoyomi-origin?"; the sidecar is a recovery hint.
struct ComicsLibraryRecord {
    QString sourceId;            // "weebcentral" | "readcomicsonline"
    QString seriesId;            // scraper-local series id (from MangaResult.id)
    QString title;
    QString origin;              // always "tankoyomi" in v1 (folder rows live elsewhere)
    QString rootFolder;          // absolute path, the Comics root the series lives under
    QString seriesFolderName;    // sanitised series folder name on disk
    QString canonicalSeriesPath; // rootFolder + "/" + seriesFolderName (display form)
    QString coverPath;           // absolute path to cached cover image
    MangaSeriesDetail detailCache; // last successful fetchDetail payload
    qint64  addedAt = 0;
    qint64  lastValidatedAt = 0;

    // Fandom catalog redesign (Phase 7 Task 17, 2026-05-20). All three fields
    // are optional/nullable. They persist the cross-source identity + Fandom
    // lookup keys for Tankoyomi/catalog-origin series so future opens skip
    // the Wikidata SPARQL hop and go straight into the FandomVolumeResolver.
    // Pre-Fandom library records deserialize cleanly with empty defaults
    // (round-trip verified). Folder-imported series do NOT get these fields
    // persisted through this struct — folder rows live entirely in
    // LibraryScanner's on-disk walk + cover cache and resolve catalog data
    // by title/anilist hint at open time. A separate scanner-side metadata
    // sidecar would be needed to extend Fandom identity to folder rows;
    // out-of-scope here per Codex review-and-expand pass on 2026-05-19.
    QString wikidataQid;       // optional; canonical cross-source identity (e.g. "Q14559")
    QString fandomWikiId;      // optional; Fandom subdomain key (e.g. "deathnote")
    QString fandomVolumePath;  // optional; Fandom volume-list path (e.g. "/wiki/Volumes")

    QJsonObject toJson() const;
    static ComicsLibraryRecord fromJson(const QJsonObject& j);

    static QString makeKey(const QString& sourceId, const QString& seriesId)
    { return sourceId + ":" + seriesId; }

    QString key() const { return makeKey(sourceId, seriesId); }
};
