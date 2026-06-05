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

    // VOLUME-AWARE resolve (COMICS_WESTERN_GCD 2026-06-05, Agent 1). seriesTitle
    // is the SERIES name (e.g. "Saga"); volumeNumber is the specific TPB volume to
    // fetch. We browse GetComics' clean per-series tag page
    // (getcomics.org/tag/<slug>/, paginated), pick the post that CARRIES this
    // volume (pickPostForVolume — a standalone "<series> Vol. N" post, else a range
    // post covering N), and extract THAT volume's download (extractVolumeDownload).
    // If the tag page yields nothing, fall back to the noisy /?s= search (also
    // volume-aware). year/tierLabel are legacy hints (tierLabel seeds the fallback
    // queries). Emits resolved() on a usable per-volume download, else
    // resolveFailed() (fail safe).
    void resolve(const QString& seriesTitle, int volumeNumber, int year,
                 const QString& tierLabel);

signals:
    void resolved(const EditionDownload& dl);
    void resolveFailed(const QString& reason);

private:
    // Fetch tag page `page` for slug; pickPostForVolume over its results. On a
    // match, fetch the post; else fetch the next page (up to kMaxTagPages); if the
    // tag page is exhausted/empty, fall back to the /?s= search path.
    void fetchTagPage(const QString& seriesTitle, int volumeNumber,
                      const QString& slug, int page);
    // /?s= fallback: try queries[idx]; pickPostForVolume on results; on a match
    // fetch the post, else recurse. Exhausting -> resolveFailed.
    void trySearchQueries(const QString& seriesTitle, int volumeNumber,
                          const QStringList& queries, int idx, const QString& lastError);
    // Fetch the matched post, extract THIS volume's download, assemble + emit.
    void fetchPostForVolume(const getcomics::SearchResult& match,
                            const QString& seriesTitle, int volumeNumber);

    static constexpr int kMaxTagPages = 5;
    QNetworkAccessManager* m_nam = nullptr;
};

} // namespace tankoban::manga
