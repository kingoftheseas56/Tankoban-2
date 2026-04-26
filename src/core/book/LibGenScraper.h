#pragma once

#include "BookScraper.h"

#include <QHash>
#include <QString>
#include <QStringList>

class QNetworkReply;

// LibGen scraper - QNetworkAccessManager-backed, NO QWebEngineView required.
//
// LibGen's primary mirror (libgen.li) serves raw HTML directly with no
// Cloudflare challenge and no browser-verification interstitial - contrast
// with Anna's Archive's /books/<id> gate that blocks headless extraction.
// Reachability verified 2026-04-21 from this network:
//   curl libgen.li/index.php?req=sapiens -> HTTP 200, 89 KB real HTML in ~1s
//   curl libgen.li/ads.php?md5=<hash>     -> HTTP 200, direct mirror <a href>
//
// Search flow:
//   GET /index.php?req=<query>
//   Parse <table id="tablelibgen"> -> each <tbody><tr> is a result row
//   Columns vary but consistently include: md5 in hrefs + title anchor + author
//   + year + language + pages + extension + size.
//
// Detail flow (M2.3 scope - minimal):
//   Emit detailReady(snapshot) immediately; row metadata from search is
//   usually sufficient for the TankoLibraryPage detail view. Richer
//   /json.php?object=e&md5=<md5>&fields=* enrichment deferred to M2.4.
//
// Download resolution:
//   GET /ads.php?md5=<md5>
//   Parse <a href="get.php?md5=X&key=Y"> -> absolute URL is the direct file.
//   No slow-download, no countdown, no captcha, no wait handler needed.
//
// Cover resolution:
//   GET /ads.php?md5=<md5>
//   Parse <img src="/covers/..."> -> absolute URL is the detail-cover image.
//   Runs on a separate reply pool so it never blocks search/detail/download.
//
// Mirror fallback: primary libgen.li only in M2.3; .rs/.is/.st are
// unreachable from this network per 2026-04-21 probe. Adding mirror
// failover is deferred to a later polish batch when / if primary fails.
class LibGenScraper : public BookScraper
{
    Q_OBJECT

public:
    explicit LibGenScraper(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~LibGenScraper() override;

    QString sourceId()   const override { return QStringLiteral("libgen"); }
    QString sourceName() const override { return QStringLiteral("LibGen"); }

    void search(const QString& query, int limit = 30) override;
    void fetchDetail(const QString& md5OrId) override;
    void resolveDownload(const QString& md5OrId) override;
    void fetchCoverUrl(const QString& md5);

signals:
    void coverUrlReady(const QString& md5, const QString& absoluteUrl);
    void coverUrlFailed(const QString& md5, const QString& reason);

private slots:
    void onSearchReplyFinished();
    void onResolveReplyFinished();
    void onCoverReplyFinished();

private:
    enum class Mode { Idle, Searching, FetchingDetail, ResolvingDownload };

    void cancelActiveReply();
    void reset();
    void fail(const QString& reason);  // search path - emits errorOccurred
    void failResolve(const QString& md5, const QString& reason);
    void failCover(const QString& md5, const QString& reason);

    QList<BookResult> parseSearchHtml(const QByteArray& html) const;
    QStringList       parseResolveHtml(const QByteArray& html) const;
    QString           parseCoverUrl(const QByteArray& html) const;

    Mode    m_mode = Mode::Idle;
    QString m_currentQuery;
    int     m_currentLimit = 30;
    QString m_currentMd5OrId;

    QNetworkReply* m_activeReply = nullptr;
    QHash<QNetworkReply*, QString> m_coverReplies;
};
