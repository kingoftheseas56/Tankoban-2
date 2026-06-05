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

void GetComicsResolver::resolve(const QString& seriesTitle, int volumeNumber,
                                int /*year*/, const QString& tierLabel)
{
    Q_UNUSED(tierLabel);
    if (seriesTitle.trimmed().isEmpty()) {
        emit resolveFailed(QStringLiteral("empty series title"));
        return;
    }
    // Tag page first — the clean per-series listing (no /?s= noise).
    fetchTagPage(seriesTitle, volumeNumber, getcomics::tagSlug(seriesTitle), 1);
}

void GetComicsResolver::fetchTagPage(const QString& seriesTitle, int volumeNumber,
                                     const QString& slug, int page)
{
    const QString path = (page <= 1)
        ? QStringLiteral("/tag/") + slug + QStringLiteral("/")
        : QStringLiteral("/tag/") + slug + QStringLiteral("/page/")
              + QString::number(page) + QStringLiteral("/");
    const QUrl tagUrl(GC_BASE + path);
    auto* reply = m_nam->get(makeRequest(tagUrl));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, seriesTitle, volumeNumber, slug, page]() {
        // Tag page missing / network error -> fall straight to /?s= search.
        if (reply->error() != QNetworkReply::NoError) {
            const QStringList queries = buildQueries(seriesTitle, QString());
            trySearchQueries(seriesTitle, volumeNumber, queries, 0, reply->errorString());
            return;
        }
        const QString html = QString::fromUtf8(reply->readAll());
        const auto results = getcomics::parseSearchResults(html);
        const auto match   = getcomics::pickPostForVolume(seriesTitle, volumeNumber, results);
        if (!match.postUrl.isEmpty()) {
            fetchPostForVolume(match, seriesTitle, volumeNumber);
            return;
        }
        // No covering post on this page — try the next, up to the cap. An empty
        // page (no results) means we have run off the end of the tag listing.
        if (!results.isEmpty() && page < kMaxTagPages) {
            fetchTagPage(seriesTitle, volumeNumber, slug, page + 1);
            return;
        }
        // Tag listing exhausted — fall back to the noisy /?s= search.
        const QStringList queries = buildQueries(seriesTitle, QString());
        trySearchQueries(seriesTitle, volumeNumber, queries, 0, QString());
    });
}

void GetComicsResolver::trySearchQueries(const QString& seriesTitle, int volumeNumber,
                                         const QStringList& queries, int idx,
                                         const QString& lastError)
{
    if (idx >= queries.size()) {
        emit resolveFailed(lastError.isEmpty()
                               ? QStringLiteral("no post carries volume ")
                                     + QString::number(volumeNumber)
                               : QStringLiteral("GetComics search failed: ") + lastError);
        return;
    }
    const QUrl searchUrl(GC_BASE + QStringLiteral("/?s=")
                         + QString::fromUtf8(QUrl::toPercentEncoding(queries.at(idx))));
    auto* searchReply = m_nam->get(makeRequest(searchUrl));
    connect(searchReply, &QNetworkReply::finished, searchReply, &QObject::deleteLater);
    connect(searchReply, &QNetworkReply::finished, this,
            [this, searchReply, seriesTitle, volumeNumber, queries, idx, lastError]() {
        if (searchReply->error() != QNetworkReply::NoError) {
            trySearchQueries(seriesTitle, volumeNumber, queries, idx + 1,
                             searchReply->errorString());
            return;
        }
        const QString html = QString::fromUtf8(searchReply->readAll());
        const auto results = getcomics::parseSearchResults(html);
        const auto match   = getcomics::pickPostForVolume(seriesTitle, volumeNumber, results);
        if (match.postUrl.isEmpty()) {
            trySearchQueries(seriesTitle, volumeNumber, queries, idx + 1, lastError);
            return;
        }
        fetchPostForVolume(match, seriesTitle, volumeNumber);
    });
}

void GetComicsResolver::fetchPostForVolume(const getcomics::SearchResult& match,
                                           const QString& seriesTitle, int volumeNumber)
{
    const QUrl postUrl(match.postUrl);
    auto* postReply = m_nam->get(makeRequest(postUrl));
    connect(postReply, &QNetworkReply::finished, postReply, &QObject::deleteLater);
    connect(postReply, &QNetworkReply::finished, this,
            [this, postReply, match, seriesTitle, volumeNumber]() {
        if (postReply->error() != QNetworkReply::NoError) {
            emit resolveFailed(QStringLiteral("GetComics post fetch failed: ")
                               + postReply->errorString());
            return;
        }
        const QString postHtml = QString::fromUtf8(postReply->readAll());

        EditionDownload dl;
        dl.postUrl      = match.postUrl;
        dl.matchedTitle = match.title;
        dl.coverUrl     = getcomics::parsePostCover(postHtml);
        // THIS volume's download (range posts section each volume; standalone
        // posts fall back to pickBest over the whole post).
        dl.best  = getcomics::extractVolumeDownload(postHtml, seriesTitle, volumeNumber);
        dl.links = dl.best.url.isEmpty() ? QList<getcomics::DownloadLink>{}
                                         : QList<getcomics::DownloadLink>{dl.best};

        if (dl.best.url.isEmpty()) {
            emit resolveFailed(QStringLiteral("volume ") + QString::number(volumeNumber)
                               + QStringLiteral(" not found in post"));
            return;
        }
        emit resolved(dl);
    });
}

} // namespace tankoban::manga
