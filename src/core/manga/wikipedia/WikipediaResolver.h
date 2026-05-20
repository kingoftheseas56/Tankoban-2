// src/core/manga/wikipedia/WikipediaResolver.h
//
// Tier-2 fallback for series whose Fandom wiki coverage is incomplete or
// missing. Wikipedia's "List of <series> manga volumes" / "List of <series>
// chapters" pages use a more standardized wikitable schema than Fandom's
// per-wiki variance, so a single parser handles the typical case.
//
// Selection logic:
//   1. Try `List_of_<englishTitle>_manga_volumes` first.
//   2. Fall back to `List_of_<englishTitle>_chapters` on 404 (some series
//      consolidate volumes + chapters on the chapters page — Death Note is
//      the canonical example).
//
// We re-use FandomVolume as the canonical normalized record shape per
// spec §4.3 — the "Fandom" in the name is historical; the type is the
// project-wide volume value object. WikipediaResolver populates the same
// field set so downstream UI code stays single-shape.
//
// Spec: docs/superpowers/specs/2026-05-19-fandom-catalog-redesign-design.md §6
// Plan: docs/superpowers/plans/2026-05-19-fandom-catalog-redesign.md Task 15

#pragma once

#include "core/manga/fandom/FandomTypes.h"

#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace tankoban::manga::wikipedia {

// Result envelope for a Wikipedia resolution. seriesId + englishTitle are
// echo-back from the request; volumes is the parsed catalog; sourcePath is
// "volumes" or "chapters" depending on which tier-2 URL succeeded.
struct WikipediaCatalog {
    QString seriesId;
    QString englishTitle;
    QString sourcePath; // "volumes" or "chapters" — useful for diagnostics
    QList<tankoban::manga::fandom::FandomVolume> volumes;
};

class WikipediaResolver : public QObject
{
    Q_OBJECT
public:
    explicit WikipediaResolver(QNetworkAccessManager* nam, QObject* parent = nullptr);

    // Async resolution. Tries List_of_<englishTitle>_manga_volumes first,
    // falls back to List_of_<englishTitle>_chapters on 404. englishTitle
    // is URL-encoded with spaces → underscores by the implementation.
    void resolveForTitle(const QString& seriesId, const QString& englishTitle);

    // Sync parse helper, now lives in WikipediaParser.{h,cpp} to keep the
    // pure-logic translation unit free of Qt::Network dependencies (so the
    // test target can link it without pulling in QNetworkAccessManager).
    // Forward declaration retained here for callers that already include
    // the resolver header.
    static QList<tankoban::manga::fandom::FandomVolume>
        parseVolumeTable(const QString& rawHtml);

signals:
    void resolved(const tankoban::manga::wikipedia::WikipediaCatalog& catalog);
    void unresolved(const QString& seriesId, const QString& reason);

private:
    void fetchPath(int requestId, const QString& seriesId,
                   const QString& englishTitle, const QString& pageTitle,
                   const QString& sourcePathTag);

    QNetworkAccessManager* m_nam = nullptr; // non-owning
};

} // namespace tankoban::manga::wikipedia
