// src/core/manga/GetComicsResolver.cpp
#include "GetComicsResolver.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>

namespace tankoban::manga {

static const QString GC_BASE = QStringLiteral("https://getcomics.org");
static const QString GC_USER_AGENT = QStringLiteral(
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36");

static QNetworkRequest makeRequest(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", GC_USER_AGENT.toUtf8());
    req.setRawHeader("Referer",    GC_BASE.toUtf8());
    req.setRawHeader("Accept",     "text/html,*/*");
    req.setDecompressedSafetyCheckThreshold(-1);
    return req;
}

GetComicsResolver::GetComicsResolver(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent)
    , m_nam(nam)
{
}

void GetComicsResolver::resolve(const QString& editionTitle, int year, const QString& tierLabel)
{
    // Step 1: build search URL — percent-encode the query.
    QUrl searchUrl(GC_BASE + QStringLiteral("/?s=")
                   + QString::fromUtf8(QUrl::toPercentEncoding(editionTitle)));

    auto* searchReply = m_nam->get(makeRequest(searchUrl));
    // lifetime-independent cleanup: reply is deleted even if this object is
    // destroyed mid-flight before the this-context slot fires.
    connect(searchReply, &QNetworkReply::finished, searchReply, &QObject::deleteLater);
    connect(searchReply, &QNetworkReply::finished, this, [this, searchReply, editionTitle, year, tierLabel]() {
        if (searchReply->error() != QNetworkReply::NoError) {
            emit resolveFailed(QStringLiteral("GetComics search failed: ") + searchReply->errorString());
            return;
        }

        const QString html = QString::fromUtf8(searchReply->readAll());

        // Step 2: parse results and fuzzy-match.
        const auto results = getcomics::parseSearchResults(html);
        const auto match   = getcomics::pickBestMatch(editionTitle, year, tierLabel, results);

        if (match.postUrl.isEmpty()) {
            emit resolveFailed(QStringLiteral("no confident match"));
            return;
        }

        // Step 5: fetch the matched post page.
        const QUrl postUrl(match.postUrl);
        auto* postReply = m_nam->get(makeRequest(postUrl));
        // lifetime-independent cleanup for the nested reply.
        connect(postReply, &QNetworkReply::finished, postReply, &QObject::deleteLater);
        connect(postReply, &QNetworkReply::finished, this, [this, postReply, match]() {
            if (postReply->error() != QNetworkReply::NoError) {
                emit resolveFailed(QStringLiteral("GetComics post fetch failed: ") + postReply->errorString());
                return;
            }

            const QString postHtml = QString::fromUtf8(postReply->readAll());

            // Step 6: assemble EditionDownload from the post HTML.
            EditionDownload dl;
            dl.postUrl  = match.postUrl;
            dl.coverUrl = getcomics::parsePostCover(postHtml);
            dl.links    = getcomics::extractDownloads(postHtml);
            dl.best     = getcomics::pickBest(dl.links);

            // Step 7: fail safe — only emit resolved if there is a usable link.
            if (dl.best.url.isEmpty()) {
                emit resolveFailed(QStringLiteral("no usable download in post"));
                return;
            }

            emit resolved(dl);
        });
    });
}

} // namespace tankoban::manga
