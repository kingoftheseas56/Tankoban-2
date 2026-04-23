#pragma once

#include "BookScraper.h"

#include <QHash>
#include <QString>
#include <QStringList>

class QNetworkReply;

// AudioBookBay scraper - QNetworkAccessManager-backed, NO QWebEngineView.
//
// Reachability verified 2026-04-22 per curl-only reachability + DOM probe
// (agents/prototypes/tankolibrary_abb_probe_2026-04-22/FINDINGS.md):
//   HEAD https://audiobookbay.lu/              -> HTTP 200, no CF challenge
//   GET  https://audiobookbay.lu/?s=<query>    -> WordPress search HTML
//   GET  https://audiobookbay.lu/abss/<slug>/  -> detail page with Info Hash
//   GET  https://audiobookbay.lu/js/main.js    -> magnet construction recipe
//
// Search flow (M1):
//   GET /?s=<urlencoded query>
//   Parse <div class="post"> blocks (EXACT class match, NOT class-contains -
//   decoy <div class="post re-ab" style="display:none;"> are base64
//   honeypots targeting blind scrapers).
//   Each real row surfaces: title / slug / detail-url / category / language /
//   posted date / format (M4B/MP3) / file size / cover URL. Rich enough
//   that we don't need a detail round-trip for the grid listing view.
//
// Detail flow (M2 - M1 ships a stub):
//   GET /abss/<slug>/
//   Extract Info Hash from <td>Info Hash:</td><td>{{40_HEX}}</td>.
//
// Download resolution (M2 - M1 ships a failing stub):
//   No HTTP download path. Construct magnet URI server-side replicating
//   ABB's own main.js logic verbatim: xt=urn:btih:<hash> + 7 hardcoded
//   trackers. Emit downloadResolved(infoHash, { magnetUri }) as a single-
//   element list reusing BookScraper's signal shape; TankoLibraryPage
//   branches on sourceId to route to TorrentEngine::addMagnet instead of
//   BookDownloader.
//
// Anti-scraper caveats honored in this scraper:
//   1. Honeypot rows use class="post re-ab" with display:none. The EXACT
//      class="post" match (trailing-quote discriminator) filters them out.
//   2. Slug misspellings (rhytuhm/sgtormlight/etc) are deliberate anti-
//      scraper to make URLs non-guessable. Doesn't affect us - we extract
//      href from anchor directly, never construct URLs from titles.
//   3. HTTP download paths (Torrent Free / Direct / Secured) route through
//      filehost ad-walls with captcha + wait timers + throttled speeds.
//      Never touched; magnet-only at M2.
class AbbScraper : public BookScraper
{
    Q_OBJECT

public:
    explicit AbbScraper(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~AbbScraper() override;

    QString sourceId()   const override { return QStringLiteral("audiobookbay"); }
    QString sourceName() const override { return QStringLiteral("AudioBookBay"); }

    void search(const QString& query, int limit = 30) override;
    void fetchDetail(const QString& md5OrId) override;
    void resolveDownload(const QString& md5OrId) override;

private slots:
    void onSearchReplyFinished();
    void onDetailReplyFinished();

private:
    enum class Mode { Idle, Searching, FetchingDetail, ResolvingDownload };

    void cancelActiveReply();
    void reset();
    void fail(const QString& reason);
    void failResolve(const QString& slug, const QString& reason);

    QList<BookResult> parseSearchHtml(const QByteArray& html) const;

    // Detail-page parsing. Extracts the torrent info hash from the
    // "Info Hash:" <td> row; returns empty string on no match.
    QString parseInfoHash(const QByteArray& html) const;

    // Detail-page parsing — file-list summary. Walks the rows after the
    // "This is a Multifile/Singlefile Torrent" marker, counts extensions,
    // returns a compact human summary like "Contents: 1 × .m4b, 1 × .jpg,
    // 1 × .nfo". Empty string when no file-list block found. TANKOLIBRARY_ABB
    // Track B — stuffed into BookResult.description for detail-view render.
    QString parseFileListSummary(const QByteArray& html) const;

    // Magnet URI construction - replicates ABB's own /js/main.js client-
    // side logic verbatim. Uses the info hash + 7 hardcoded trackers. The
    // title is url-encoded into the magnet's `dn=` display-name param so
    // libtorrent has a fallback name before metadata arrives.
    QString constructMagnet(const QString& infoHash, const QString& title) const;

    Mode    m_mode = Mode::Idle;
    QString m_currentQuery;
    int     m_currentLimit = 30;
    QString m_currentSlug;    // populated during FetchingDetail / ResolvingDownload

    QNetworkReply* m_activeReply = nullptr;
};
