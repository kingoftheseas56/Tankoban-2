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

// Ordered search queries for a series: the series name boosted by each
// collected-edition keyword, plain series name last. GetComics buries the
// collected edition of a common-word series ("Invincible" -> Iron Man noise)
// unless a keyword disambiguates; distinctive names ("Descender", "Watchmen")
// surface their collected edition on the bare query, so it is the catch-all.
static QStringList buildQueries(const QString& series, const QString& tierLabel)
{
    QStringList q;
    auto add = [&q](const QString& s) {
        const QString t = s.trimmed();
        if (!t.isEmpty() && !q.contains(t, Qt::CaseInsensitive)) q << t;
    };
    const QString s = series.trimmed();
    if (s.isEmpty()) return q;
    if (!tierLabel.trimmed().isEmpty())
        add(s + QLatin1Char(' ') + tierLabel.trimmed());   // tier hint first
    add(s + QStringLiteral(" Compendium"));
    add(s + QStringLiteral(" Omnibus"));
    add(s + QStringLiteral(" Complete Collection"));
    add(s + QStringLiteral(" Collection"));
    add(s + QStringLiteral(" Deluxe Edition"));
    add(s);                                                 // plain catch-all last
    return q;
}

void GetComicsResolver::resolve(const QString& seriesTitle, int year, const QString& tierLabel)
{
    const QStringList queries = buildQueries(seriesTitle, tierLabel);
    if (queries.isEmpty()) {
        emit resolveFailed(QStringLiteral("empty series title"));
        return;
    }
    tryQuery(seriesTitle, year, queries, 0, QString());
}

void GetComicsResolver::tryQuery(const QString& seriesTitle, int year,
                                 const QStringList& queries, int idx,
                                 const QString& lastError)
{
    if (idx >= queries.size()) {
        emit resolveFailed(lastError.isEmpty()
                               ? QStringLiteral("no confident match")
                               : QStringLiteral("GetComics search failed: ") + lastError);
        return;
    }

    const QUrl searchUrl(GC_BASE + QStringLiteral("/?s=")
                         + QString::fromUtf8(QUrl::toPercentEncoding(queries.at(idx))));
    auto* searchReply = m_nam->get(makeRequest(searchUrl));
    // lifetime-independent cleanup: reply is deleted even if this object is
    // destroyed mid-flight before the this-context slot fires.
    connect(searchReply, &QNetworkReply::finished, searchReply, &QObject::deleteLater);
    connect(searchReply, &QNetworkReply::finished, this,
            [this, searchReply, seriesTitle, year, queries, idx, lastError]() {
        if (searchReply->error() != QNetworkReply::NoError) {
            // Network error on THIS query — try the next rather than aborting the
            // whole resolve. Carry the latest error in case every query fails.
            tryQuery(seriesTitle, year, queries, idx + 1, searchReply->errorString());
            return;
        }

        const QString html = QString::fromUtf8(searchReply->readAll());
        const auto results = getcomics::parseSearchResults(html);
        const auto match   = getcomics::pickBestCollectedEdition(seriesTitle, year, results);

        if (match.postUrl.isEmpty()) {
            // No strict collected-edition match on this query — try the next.
            // Preserve any earlier network error so an all-fail run reports it.
            tryQuery(seriesTitle, year, queries, idx + 1, lastError);
            return;
        }
        fetchPost(match);
    });
}

void GetComicsResolver::fetchPost(const getcomics::SearchResult& match)
{
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

        // Assemble EditionDownload from the post HTML.
        EditionDownload dl;
        dl.postUrl      = match.postUrl;
        dl.matchedTitle = match.title;
        dl.coverUrl     = getcomics::parsePostCover(postHtml);
        dl.links    = getcomics::extractDownloads(postHtml);
        dl.best     = getcomics::pickBest(dl.links);

        // Fail safe — only emit resolved if there is a usable link.
        if (dl.best.url.isEmpty()) {
            emit resolveFailed(QStringLiteral("no usable download in post"));
            return;
        }

        emit resolved(dl);
    });
}

} // namespace tankoban::manga
