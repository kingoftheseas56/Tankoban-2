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

    QJsonObject toJson() const;
    static ComicsLibraryRecord fromJson(const QJsonObject& j);

    static QString makeKey(const QString& sourceId, const QString& seriesId)
    { return sourceId + ":" + seriesId; }

    QString key() const { return makeKey(sourceId, seriesId); }
};
