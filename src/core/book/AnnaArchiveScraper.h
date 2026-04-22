#pragma once

#include "BookScraper.h"

#include <QString>
#include <QUrl>

class QWebEngineView;
class QWebEngineProfile;
class QTimer;
class AaSlowDownloadWaitHandler;

// Anna's Archive scraper — QWebEngineView-backed.
//
// M1 architecture rationale (see agents/audits/_tankolibrary_m1_snapshots/README.md):
// AA serves a 4.6 KB JS-only interstitial on every URL that runs ad-block
// detection + sets two random-named 32-char cookies + window.location.replace().
// Raw QNetworkRequest never progresses past the interstitial. The scraper
// therefore uses QWebEngineView to execute the interstitial JS, follow the
// redirect, let the real page's DOM settle, then runJavaScript() to extract
// typed BookResult rows.
//
// M2 optimization target (deferred): cookie-harvest-once via QWebEngineView →
// reuse cookies in raw QNetworkRequest for subsequent searches + detail pages.
// For M1, every search spawns one webview load.
//
// Primary domain per reachability test 2026-04-21: annas-archive.li.
// Fallbacks .se and .org are DNS-dead from Hemanth's network.
//
// Lifecycle: per-scraper-instance owned webview, created lazily on first
// search(), persists for the scraper's lifetime. One search at a time — a
// second search() while the first is in flight gets its predecessor cancelled.
class AnnaArchiveScraper : public BookScraper
{
    Q_OBJECT

public:
    explicit AnnaArchiveScraper(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~AnnaArchiveScraper() override;

    QString sourceId()   const override { return QStringLiteral("annas-archive"); }
    QString sourceName() const override { return QStringLiteral("Anna's Archive"); }

    void search(const QString& query, int limit = 30) override;

    // M2.1 — fetchDetail now real; resolveDownload still stubbed for M2.2+.
    // The md5OrId argument is actually the AA route-key (e.g.
    // "36143020-orwell-1984") that BookResult stores in its md5 field; URL
    // construction uses it verbatim under /books/.
    void fetchDetail(const QString& md5OrId) override;
    void resolveDownload(const QString& md5OrId) override;

    // M2.3 — downloadResolved + downloadFailed hoisted to BookScraper base;
    // no concrete-class signal additions needed here.

private:
    void onLoadFinished(bool ok);
    void onExtractTimerFired();
    void extractResults();
    void extractDetail();                 // M2.1 — parses /books/<md5OrId> DOM into BookResult
    void extractSlowDownloadLinks();      // M2.2 — enumerates a[href*="/slow_download/"] on /books/ page
    void ensureView();                    // M2.1 — lazy-init m_view/m_profile shared by search + fetchDetail
    void fail(const QString& reason);
    void reset();

    // State — Mode enum replaces the M1 bool m_busy so search + detail share
    // the same webview serially without stomping each other. UI flow is
    // strictly sequential (search → results → row double-click → detail → back)
    // so reuse wins over spawning a second QWebEngineView.
    enum class Mode { Idle, Searching, FetchingDetail, ResolvingDownload };

    Mode    m_mode = Mode::Idle;
    QString m_currentQuery;
    int     m_currentLimit = 30;
    QString m_currentMd5OrId;   // M2.1 — the AA route-key passed to fetchDetail()

    // M2.2 — retry-once gates for the wait-handler integration. Reset on
    // every new fetchDetail/resolveDownload entry so challenge-clear retries
    // are scoped to the current operation.
    bool m_detailRetryUsed  = false;
    bool m_resolveRetryUsed = false;

    QTimer* m_loadTimeout = nullptr;     // 30s safety — no result arrives
    QTimer* m_settleTimer = nullptr;     // 1.5s delay after loadFinished so JS runs + redirect completes

    // M2.2 — owned on-demand; recreated per wait cycle, deleted via deleteLater
    // when cancelled/completed. Lives off the parent scraper so reset() can
    // tear it down safely.
    AaSlowDownloadWaitHandler* m_waitHandler = nullptr;

#ifdef HAS_WEBENGINE
    QWebEngineView*    m_view    = nullptr;
    QWebEngineProfile* m_profile = nullptr;
#endif
};
