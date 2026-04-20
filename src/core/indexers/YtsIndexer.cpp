#include "YtsIndexer.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

YtsIndexer::YtsIndexer(QNetworkAccessManager* nam, QObject* parent)
    : TorrentIndexer(parent), m_nam(nam)
{
    loadPersistedHealth();
}

void YtsIndexer::search(const QString& query, int limit, const QString& categoryId)
{
    QUrl url("https://yts.mx/api/v2/list_movies.json");
    QUrlQuery q;
    q.addQueryItem("query_term", query);
    q.addQueryItem("limit", "50");
    q.addQueryItem("sort_by", "date_added");
    q.addQueryItem("order_by", "desc");
    if (!categoryId.trimmed().isEmpty())
        q.addQueryItem("genre", categoryId.trimmed());
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)");
    req.setRawHeader("Accept", "application/json,*/*");
    req.setTransferTimeout(15000);  // honest-fail unreachable hosts instead of hanging the UI

    startRequestTimer();
    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, limit]() {
        onReplyFinished(reply, limit);
    });
}

void YtsIndexer::onReplyFinished(QNetworkReply* reply, int limit)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        markError(reply);
        emit searchError(reply->errorString());
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        markError(reply);
        emit searchError("JSON parse error: " + err.errorString());
        return;
    }

    markSuccess();

    QJsonArray movies = doc.object().value("data").toObject().value("movies").toArray();
    QList<TorrentResult> results;

    for (const auto& mv : movies) {
        QJsonObject movie = mv.toObject();
        QString title = movie.value("title").toString().trimmed();
        int year = movie.value("year").toInt(0);

        QString display = title;
        if (year > 0)
            display = QStringLiteral("%1 (%2)").arg(title).arg(year);

        const qint64 uploadedSecs = movie.value("date_uploaded_unix").toVariant().toLongLong();
        const QString movieUrl    = movie.value("url").toString();

        // Genre from movie
        QJsonArray genres = movie.value("genres").toArray();
        QString genreStr;
        int gc = 0;
        for (const auto& g : genres) {
            if (gc >= 3) break;
            if (gc > 0) genreStr += ", ";
            genreStr += g.toString();
            ++gc;
        }
        if (genreStr.isEmpty()) genreStr = "Movies";

        QJsonArray torrents = movie.value("torrents").toArray();
        for (const auto& tv : torrents) {
            QJsonObject tor = tv.toObject();
            QString ih = tor.value("hash").toString().trimmed().toLower();
            if (ih.isEmpty()) continue;

            QString quality = tor.value("quality").toString();
            QString type = tor.value("type").toString().toUpper();

            QString rowTitle = QStringLiteral("%1 %2 %3 -YTS")
                                   .arg(display, quality, type).trimmed();

            TorrentResult r;
            r.title      = rowTitle;
            r.magnetUri  = buildMagnet(ih, rowTitle);
            r.sizeBytes  = tor.value("size_bytes").toVariant().toLongLong();
            r.seeders    = tor.value("seeds").toInt(0);
            r.leechers   = tor.value("peers").toInt(0);
            r.sourceName = "YTS";
            r.sourceKey  = "yts";
            r.categoryId = quality.toLower();
            r.category   = genreStr;

            r.infoHash = canonicalizeInfoHash(ih);
            if (r.infoHash.isEmpty())
                qDebug() << "[YtsIndexer] infoHash missing for:" << rowTitle;

            if (uploadedSecs > 0)
                r.publishDate = QDateTime::fromSecsSinceEpoch(uploadedSecs);

            if (!movieUrl.isEmpty())
                r.detailsUrl = movieUrl;

            if (!r.magnetUri.isEmpty())
                results.append(r);

            if (results.size() >= limit)
                break;
        }
        if (results.size() >= limit)
            break;
    }

    emit searchFinished(results);
}
