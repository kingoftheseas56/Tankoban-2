#include "OpenLibraryClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {

QString joinStringArray(const QJsonArray& arr, const QString& sep)
{
    QStringList parts;
    for (const auto& v : arr) {
        const QString s = v.toString();
        if (!s.isEmpty()) parts << s;
    }
    return parts.join(sep);
}

QString firstString(const QJsonValue& v)
{
    if (v.isArray()) {
        auto a = v.toArray();
        if (a.isEmpty()) return {};
        return a.first().toString();
    }
    if (v.isString()) return v.toString();
    return {};
}

QStringList toStringList(const QJsonArray& arr)
{
    QStringList out;
    for (const auto& v : arr) {
        const QString s = v.toString();
        if (!s.isEmpty()) out << s;
    }
    return out;
}

QNetworkRequest makeOpenLibraryRequest(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent",
                     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Tankoban/1.0");
    req.setRawHeader("Accept", "application/json,text/plain,*/*");
    req.setTransferTimeout(10000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}

BookCatalogueResult parseDoc(const QJsonObject& doc)
{
    BookCatalogueResult r;
    const QString workKey = doc.value(QStringLiteral("key")).toString();
    r.catalogueId = QStringLiteral("openlib:") + workKey;
    r.workId = workKey;

    r.title = doc.value(QStringLiteral("title")).toString();

    const auto authorArr = doc.value(QStringLiteral("author_name")).toArray();
    r.author = joinStringArray(authorArr, QStringLiteral(" & "));

    if (doc.contains(QStringLiteral("first_publish_year"))) {
        r.year = QString::number(doc.value(QStringLiteral("first_publish_year")).toInt());
    }
    r.publisher = firstString(doc.value(QStringLiteral("publisher")));
    r.language  = firstString(doc.value(QStringLiteral("language")));

    const auto isbnArr = doc.value(QStringLiteral("isbn")).toArray();
    r.isbn = joinStringArray(isbnArr, QStringLiteral(","));

    r.genres = toStringList(doc.value(QStringLiteral("subject")).toArray());

    if (doc.contains(QStringLiteral("cover_i"))) {
        const int coverId = doc.value(QStringLiteral("cover_i")).toInt();
        if (coverId > 0) {
            r.coverUrl = QStringLiteral("https://covers.openlibrary.org/b/id/%1-L.jpg")
                             .arg(coverId);
        }
    }

    if (doc.contains(QStringLiteral("number_of_pages_median"))) {
        const int p = doc.value(QStringLiteral("number_of_pages_median")).toInt();
        if (p > 0) r.pages = QString::number(p);
    }

    // Series field is patchy in Open Library; v1 uses author + title-suffix
    // heuristic in Phase 3 (BookCatalogueAggregator) instead of trusting this.
    // Surface here for diagnostic value only.
    const auto seriesArr = doc.value(QStringLiteral("series")).toArray();
    if (!seriesArr.isEmpty()) {
        r.seriesName = firstString(doc.value(QStringLiteral("series")));
    }
    r.isSeries = false; // Aggregator decides; do not infer here.

    return r;
}

} // namespace

// ── Pure parsers ────────────────────────────────────────────────────────────

QList<BookCatalogueResult> OpenLibraryClient::parseSearchResponse(const QByteArray& json)
{
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    QList<BookCatalogueResult> results;
    const auto arr = doc.object().value(QStringLiteral("docs")).toArray();
    for (const auto& v : arr) {
        if (!v.isObject()) continue;
        results.append(parseDoc(v.toObject()));
    }
    return results;
}

QList<BookCatalogueResult>
OpenLibraryClient::parseAuthorWorksResponse(const QByteArray& json,
                                            const QString& authorName)
{
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    QList<BookCatalogueResult> results;
    const auto arr = doc.object().value(QStringLiteral("entries")).toArray();
    for (const auto& v : arr) {
        if (!v.isObject()) continue;
        auto obj = v.toObject();
        BookCatalogueResult r;
        const QString workKey = obj.value(QStringLiteral("key")).toString();
        r.catalogueId = QStringLiteral("openlib:") + workKey;
        r.workId = workKey;
        r.title = obj.value(QStringLiteral("title")).toString();
        r.author = authorName; // /authors/<id>/works.json doesn't include author_name per row
        // covers can be array of ints in author/works response
        const auto covers = obj.value(QStringLiteral("covers")).toArray();
        if (!covers.isEmpty()) {
            const int coverId = covers.first().toInt();
            if (coverId > 0) {
                r.coverUrl = QStringLiteral("https://covers.openlibrary.org/b/id/%1-L.jpg")
                                 .arg(coverId);
            }
        }
        if (r.title.isEmpty()) continue;
        results.append(r);
    }
    return results;
}

QString OpenLibraryClient::parseWorkDescription(const QByteArray& json)
{
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    const QJsonValue v = doc.object().value(QStringLiteral("description"));
    // Open Library work descriptions are sometimes a string, sometimes
    // an object {"type":"/type/text","value":"..."}.
    if (v.isString()) return v.toString();
    if (v.isObject()) return v.toObject().value(QStringLiteral("value")).toString();
    return {};
}

// ── Network ─────────────────────────────────────────────────────────────────

OpenLibraryClient::OpenLibraryClient(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam) {}

void OpenLibraryClient::search(const QString& query)
{
    QUrl url(QStringLiteral("https://openlibrary.org/search.json"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    q.addQueryItem(QStringLiteral("fields"),
                   QStringLiteral("key,title,author_name,author_key,first_publish_year,"
                                  "isbn,subject,cover_i,publisher,language,"
                                  "number_of_pages_median,series"));
    url.setQuery(q);
    auto* reply = m_nam->get(makeOpenLibraryRequest(url));
    connect(reply, &QNetworkReply::finished, this, &OpenLibraryClient::onSearchReply);
}

void OpenLibraryClient::fetchAuthorWorks(const QString& authorKey,
                                         const QString& authorName)
{
    QUrl url(QStringLiteral("https://openlibrary.org/authors/%1/works.json").arg(authorKey));
    auto* reply = m_nam->get(makeOpenLibraryRequest(url));
    reply->setProperty("authorKey", authorKey);
    reply->setProperty("authorName", authorName);
    connect(reply, &QNetworkReply::finished, this, &OpenLibraryClient::onAuthorWorksReply);
}

void OpenLibraryClient::fetchWorkDetail(const QString& workKey)
{
    // workKey is "/works/OL27448W"; URL is "https://openlibrary.org/works/OL27448W.json".
    const QString suffix = workKey.startsWith(QLatin1Char('/'))
                               ? workKey.mid(1) : workKey;
    QUrl url(QStringLiteral("https://openlibrary.org/%1.json").arg(suffix));
    auto* reply = m_nam->get(makeOpenLibraryRequest(url));
    reply->setProperty("workKey", workKey);
    connect(reply, &QNetworkReply::finished, this, &OpenLibraryClient::onWorkDetailReply);
}

void OpenLibraryClient::onSearchReply()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit searchFailed(reply->errorString());
        return;
    }
    emit searchResults(parseSearchResponse(reply->readAll()));
}

void OpenLibraryClient::onAuthorWorksReply()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    const QString authorKey = reply->property("authorKey").toString();
    const QString authorName = reply->property("authorName").toString();
    if (reply->error() != QNetworkReply::NoError) {
        emit authorWorksFailed(authorKey, reply->errorString());
        return;
    }
    emit authorWorksResults(authorKey,
                            parseAuthorWorksResponse(reply->readAll(), authorName));
}

void OpenLibraryClient::onWorkDetailReply()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    const QString workKey = reply->property("workKey").toString();
    if (reply->error() != QNetworkReply::NoError) {
        emit workDetailFailed(workKey, reply->errorString());
        return;
    }
    emit workDetailReady(workKey, parseWorkDescription(reply->readAll()));
}
