#include "BookWalkerClient.h"
#include "BookWalkerSeriesPageParser.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>

namespace tankoban::manga::bookwalker {

namespace {
constexpr int kHttpTimeoutMs = 10'000;
constexpr int kThrottleGapMs = 250;
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Tankoban/1.0 (manga-bookwalker)";

QNetworkRequest makeRequest(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    req.setTransferTimeout(kHttpTimeoutMs);
    return req;
}
} // namespace

BookWalkerClient::BookWalkerClient(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam) {}

BookWalkerClient::~BookWalkerClient() = default;

void BookWalkerClient::throttleIfNeeded()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 sinceLast = now - m_lastRequestMs;
    if (m_lastRequestMs != 0 && sinceLast < kThrottleGapMs) {
        QThread::msleep(static_cast<unsigned long>(kThrottleGapMs - sinceLast));
    }
    m_lastRequestMs = QDateTime::currentMSecsSinceEpoch();
}

void BookWalkerClient::searchSeries(const QString& japaneseTitle, int requestId)
{
    if (!m_nam) {
        emit searchFailed(requestId, QStringLiteral("nam-null"));
        return;
    }
    throttleIfNeeded();
    QUrl url(QStringLiteral("https://bookwalker.jp/search/"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("word"), japaneseTitle);
    url.setQuery(q);

    QNetworkReply* reply = m_nam->get(makeRequest(url));
    reply->setProperty("requestId", requestId);
    connect(reply, &QNetworkReply::finished, this, &BookWalkerClient::onSearchReplyFinished);
}

void BookWalkerClient::fetchSeriesCovers(const QString& bookwalkerSeriesId, int requestId)
{
    if (!m_nam) {
        emit coversFailed(requestId, QStringLiteral("nam-null"));
        return;
    }
    if (bookwalkerSeriesId.isEmpty()) {
        emit coversFailed(requestId, QStringLiteral("empty-series-id"));
        return;
    }
    throttleIfNeeded();
    const QUrl url(QStringLiteral("https://bookwalker.jp/series/%1/list/").arg(bookwalkerSeriesId));

    QNetworkReply* reply = m_nam->get(makeRequest(url));
    reply->setProperty("requestId", requestId);
    connect(reply, &QNetworkReply::finished, this, &BookWalkerClient::onCoversReplyFinished);
}

void BookWalkerClient::onSearchReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    const int requestId = reply->property("requestId").toInt();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit searchFailed(requestId, reply->errorString());
        return;
    }
    const QString html = QString::fromUtf8(reply->readAll());
    auto hits = BookWalkerSeriesPageParser::extractSearchHits(html);
    emit searchSucceeded(requestId, hits);
}

void BookWalkerClient::onCoversReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    const int requestId = reply->property("requestId").toInt();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit coversFailed(requestId, reply->errorString());
        return;
    }
    const QString html = QString::fromUtf8(reply->readAll());
    auto urls = BookWalkerSeriesPageParser::extractCoverUrls(html);
    if (urls.isEmpty()) {
        emit coversFailed(requestId, QStringLiteral("parse-failed-zero-data-original"));
        return;
    }
    emit coversSucceeded(requestId, urls);
}

} // namespace tankoban::manga::bookwalker
