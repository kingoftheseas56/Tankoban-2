#include "GoogleBooksClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

QStringList toStringList(const QJsonArray& arr)
{
    QStringList out;
    for (const auto& v : arr) {
        const QString s = v.toString();
        if (!s.isEmpty()) out << s;
    }
    return out;
}

QString yearOnly(const QString& publishedDate)
{
    // Google's publishedDate is "YYYY" | "YYYY-MM" | "YYYY-MM-DD"; truncate.
    if (publishedDate.size() >= 4) return publishedDate.left(4);
    return publishedDate;
}

QNetworkRequest makeGoogleBooksRequest(const QUrl& url)
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

BookCatalogueResult parseItem(const QJsonObject& item)
{
    BookCatalogueResult r;
    const QString volumeId = item.value(QStringLiteral("id")).toString();
    r.catalogueId = QStringLiteral("googlebooks:") + volumeId;

    const auto info = item.value(QStringLiteral("volumeInfo")).toObject();
    r.title = info.value(QStringLiteral("title")).toString();
    r.author = joinStringArray(info.value(QStringLiteral("authors")).toArray(),
                               QStringLiteral(" & "));
    r.publisher = info.value(QStringLiteral("publisher")).toString();
    r.year = yearOnly(info.value(QStringLiteral("publishedDate")).toString());
    r.description = info.value(QStringLiteral("description")).toString();
    r.language = info.value(QStringLiteral("language")).toString();
    r.genres = toStringList(info.value(QStringLiteral("categories")).toArray());

    if (info.contains(QStringLiteral("pageCount"))) {
        const int p = info.value(QStringLiteral("pageCount")).toInt();
        if (p > 0) r.pages = QString::number(p);
    }

    // industryIdentifiers — collect ISBN_13 + ISBN_10 entries, join by comma.
    QStringList isbns;
    const auto ids = info.value(QStringLiteral("industryIdentifiers")).toArray();
    for (const auto& v : ids) {
        if (!v.isObject()) continue;
        auto o = v.toObject();
        const QString type = o.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("ISBN_13") || type == QStringLiteral("ISBN_10")) {
            const QString id = o.value(QStringLiteral("identifier")).toString();
            if (!id.isEmpty()) isbns << id;
        }
    }
    r.isbn = isbns.join(QStringLiteral(","));

    // imageLinks.thumbnail preferred; small fallback. Google returns http://
    // URLs that work over https; rewrite to https for QtWebEngine + Qt6 strict.
    const auto img = info.value(QStringLiteral("imageLinks")).toObject();
    QString cover = img.value(QStringLiteral("thumbnail")).toString();
    if (cover.isEmpty()) cover = img.value(QStringLiteral("smallThumbnail")).toString();
    if (cover.startsWith(QStringLiteral("http://"))) {
        cover.replace(0, 7, QStringLiteral("https://"));
    }
    r.coverUrl = cover;

    return r;
}

} // namespace

QList<BookCatalogueResult>
GoogleBooksClient::parseVolumesResponse(const QByteArray& json)
{
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    QList<BookCatalogueResult> results;
    const auto arr = doc.object().value(QStringLiteral("items")).toArray();
    for (const auto& v : arr) {
        if (!v.isObject()) continue;
        auto item = v.toObject();
        if (!item.contains(QStringLiteral("volumeInfo"))) continue;
        if (item.value(QStringLiteral("id")).toString().isEmpty()) continue;
        results.append(parseItem(item));
    }
    return results;
}

GoogleBooksClient::GoogleBooksClient(QNetworkAccessManager* nam,
                                     const QString& apiKey,
                                     QObject* parent)
    : QObject(parent), m_nam(nam), m_apiKey(apiKey) {}

void GoogleBooksClient::search(const QString& query)
{
    QUrl url(QStringLiteral("https://www.googleapis.com/books/v1/volumes"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    q.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("20"));
    if (!m_apiKey.isEmpty()) {
        q.addQueryItem(QStringLiteral("key"), m_apiKey);
    }
    url.setQuery(q);
    auto* reply = m_nam->get(makeGoogleBooksRequest(url));
    connect(reply, &QNetworkReply::finished, this, &GoogleBooksClient::onSearchReply);
}

void GoogleBooksClient::onSearchReply()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit searchFailed(reply->errorString());
        return;
    }
    emit searchResults(parseVolumesResponse(reply->readAll()));
}
