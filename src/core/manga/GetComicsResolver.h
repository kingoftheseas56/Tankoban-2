// src/core/manga/GetComicsResolver.h
#pragma once

#include "GetComicsParse.h"
#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace tankoban::manga {

// One resolved edition's downloads + cover, ready for WesternVolumeDownloader.
struct EditionDownload {
    QString postUrl;
    QString matchedTitle;                   // the collected-edition title we matched (for UI)
    QString coverUrl;                       // per-edition og:image (may be empty)
    QList<getcomics::DownloadLink> links;   // ordered as found; pickBest applied by caller
    getcomics::DownloadLink best;           // pickBest(links)
};

// Live GetComics resolver: search -> fuzzy-match -> fetch post -> emit downloads.
// QNAM is created via NetSeam (non-owning here; caller passes the shared one).
class GetComicsResolver : public QObject {
    Q_OBJECT
public:
    explicit GetComicsResolver(QNetworkAccessManager* nam, QObject* parent = nullptr);

    // COLLECTED-EDITION resolve (2026-06-02, Agent 1). seriesTitle is the SERIES
    // name (e.g. "Invincible"), not a per-TPB label — GetComics carries these
    // series only as collected editions. We fire a small set of series+keyword
    // queries (Compendium/Omnibus/Collection/...) and take the first that yields
    // a STRICT collected-edition match (pickBestCollectedEdition). year/tierLabel
    // are hints (year tie-breaks the match; tierLabel orders the first query).
    // Emits resolved() on a confident match with a usable download, else
    // resolveFailed() (fail safe).
    void resolve(const QString& seriesTitle, int year, const QString& tierLabel);

signals:
    void resolved(const EditionDownload& dl);
    void resolveFailed(const QString& reason);

private:
    // Try search query queries[idx]; on a strict match fetch the post, else
    // recurse to the next query. Exhausting the list (or all-errored) ->
    // resolveFailed. lastError carries a network error string for the final
    // message when no query ever returned usable HTML.
    void tryQuery(const QString& seriesTitle, int year,
                  const QStringList& queries, int idx, const QString& lastError);
    // Fetch the matched post page, assemble the EditionDownload, emit.
    void fetchPost(const getcomics::SearchResult& match);

    QNetworkAccessManager* m_nam = nullptr;
};

} // namespace tankoban::manga
