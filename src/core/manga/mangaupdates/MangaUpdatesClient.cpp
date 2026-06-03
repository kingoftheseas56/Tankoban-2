#include "MangaUpdatesClient.h"
#include "MangaUpdatesStatusParser.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace tankoban::manga::mangaupdates {

namespace {
constexpr int kThrottleMs = 1000;
constexpr int kSearchPerPage = 10;
constexpr const char* kSearchUrl = "https://api.mangaupdates.com/v1/series/search";
constexpr const char* kSeriesUrlPrefix = "https://api.mangaupdates.com/v1/series/";
constexpr const char* kPropRequestId = "mu_requestId";

QString stringValue(const QJsonValue& v)
{
    if (v.isString()) return v.toString();
    if (v.isDouble()) return QString::number(static_cast<qint64>(v.toDouble()));
    return QString();
}

int intValue(const QJsonValue& v)
{
    if (v.isDouble()) return v.toInt();
    if (v.isString()) return v.toString().toInt();
    return 0;
}

qint64 int64Value(const QJsonValue& v)
{
    if (v.isDouble()) return static_cast<qint64>(v.toDouble());
    if (v.isString()) return v.toString().toLongLong();
    return 0;
}

QStringList extractStringList(const QJsonValue& v)
{
    QStringList out;
    if (!v.isArray()) return out;
    for (const auto& item : v.toArray()) {
        if (item.isString()) {
            out.append(item.toString());
            continue;
        }
        if (!item.isObject()) continue;
        const auto o = item.toObject();
        const QString s = o.value(QStringLiteral("name")).toString(
            o.value(QStringLiteral("title")).toString());
        if (!s.isEmpty()) out.append(s);
    }
    return out;
}

// MangaUpdates /series/{id} endpoint returns associated titles as
// array<object{title:string}>, not array<string> like the search endpoint.
QStringList extractAssociatedTitles(const QJsonValue& v)
{
    QStringList out;
    if (!v.isArray()) return out;
    for (const auto& entry : v.toArray()) {
        const QString t = entry.toObject().value(QStringLiteral("title")).toString().trimmed();
        if (!t.isEmpty()) out.append(t);
    }
    return out;
}

QString imageUrlFromRecord(const QJsonObject& rec)
{
    const auto image = rec.value(QStringLiteral("image"));
    if (image.isString()) return image.toString();
    const auto imageObj = image.toObject();
    const auto url = imageObj.value(QStringLiteral("url"));
    if (url.isString()) return url.toString();
    return url.toObject().value(QStringLiteral("original")).toString();
}

} // namespace

MangaUpdatesClient::MangaUpdatesClient(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam) {}

MangaUpdatesClient::~MangaUpdatesClient() = default;

// Non-blocking 1-req/sec throttle. The old shape slept the calling thread
// (QThread::msleep), freezing the GUI for up to 1s per lookup because this
// client lives on ComicsPage's UI thread. Instead we compute the next free
// slot and defer the actual network call with a single-shot QTimer. Back-to-
// back calls advance m_nextAllowedMs by kThrottleMs each, so a burst drains in
// call order, spaced 1 req/sec, without ever blocking the GUI thread. The timer
// is parented to `this` (auto-cancelled on destruction) and the deferred lambda
// re-checks m_nam defensively in case the NAM disappears before the slot fires.
void MangaUpdatesClient::scheduleThrottled(std::function<void()> dispatch)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 slot = std::max(now, m_nextAllowedMs);
    const qint64 delay = slot - now;
    m_nextAllowedMs = slot + kThrottleMs;
    if (delay <= 0) {
        dispatch();
    } else {
        QTimer::singleShot(static_cast<int>(delay), this, std::move(dispatch));
    }
}

void MangaUpdatesClient::searchByTitle(const QString& query, int requestId)
{
    if (!m_nam) {
        emit searchFailed(requestId, QStringLiteral("no network manager"));
        return;
    }
    scheduleThrottled([this, query, requestId]() {
        if (!m_nam) {
            emit searchFailed(requestId, QStringLiteral("no network manager"));
            return;
        }
        QJsonObject body;
        body.insert(QStringLiteral("search"), query);
        body.insert(QStringLiteral("page"), 1);
        body.insert(QStringLiteral("per_page"), kSearchPerPage);

        QNetworkRequest req{QUrl(QString::fromLatin1(kSearchUrl))};
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Tankoban/1.0 (+tankoyomi)"));

        auto* reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        reply->setProperty(kPropRequestId, requestId);
        connect(reply, &QNetworkReply::finished, this, &MangaUpdatesClient::onSearchReplyFinished);
    });
}

void MangaUpdatesClient::seriesById(qint64 seriesId, int requestId)
{
    if (!m_nam) {
        emit seriesFailed(requestId, QStringLiteral("no network manager"));
        return;
    }
    scheduleThrottled([this, seriesId, requestId]() {
        if (!m_nam) {
            emit seriesFailed(requestId, QStringLiteral("no network manager"));
            return;
        }
        const QString url = QString::fromLatin1(kSeriesUrlPrefix) + QString::number(seriesId);
        QNetworkRequest req{QUrl(url)};
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Tankoban/1.0 (+tankoyomi)"));

        auto* reply = m_nam->get(req);
        reply->setProperty(kPropRequestId, requestId);
        connect(reply, &QNetworkReply::finished, this, &MangaUpdatesClient::onSeriesReplyFinished);
    });
}

void MangaUpdatesClient::onSearchReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    const int requestId = reply->property(kPropRequestId).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        emit searchFailed(requestId, reply->errorString());
        return;
    }

    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(reply->readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        emit searchFailed(requestId, QStringLiteral("malformed json: %1").arg(err.errorString()));
        return;
    }

    QList<MangaUpdatesSearchHit> hits;
    const auto results = doc.object().value(QStringLiteral("results")).toArray();
    for (const auto& v : results) {
        const auto resultObj = v.toObject();
        auto rec = resultObj.value(QStringLiteral("record")).toObject();
        if (rec.isEmpty()) rec = resultObj;
        if (rec.isEmpty()) continue;

        MangaUpdatesSearchHit hit;
        hit.seriesId = int64Value(rec.value(QStringLiteral("series_id")));
        hit.title = rec.value(QStringLiteral("title")).toString();
        hit.altTitles = extractStringList(rec.value(QStringLiteral("associated_names")));
        hit.authors = extractStringList(rec.value(QStringLiteral("authors")));
        hit.yearStarted = intValue(rec.value(QStringLiteral("year")));
        hit.description = rec.value(QStringLiteral("description")).toString();
        hit.imageUrl = imageUrlFromRecord(rec);
        if (hit.seriesId > 0) hits.append(hit);
    }
    emit searchSucceeded(requestId, hits);
}

void MangaUpdatesClient::onSeriesReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    const int requestId = reply->property(kPropRequestId).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        emit seriesFailed(requestId, reply->errorString());
        return;
    }

    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(reply->readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        emit seriesFailed(requestId, QStringLiteral("malformed json: %1").arg(err.errorString()));
        return;
    }

    const auto rec = doc.object();
    MangaUpdatesSeriesInfo info;
    info.seriesId = int64Value(rec.value(QStringLiteral("series_id")));
    info.title = rec.value(QStringLiteral("title")).toString();
    info.altTitles = extractAssociatedTitles(rec.value(QStringLiteral("associated"))); // NEW
    info.rawStatus = stringValue(rec.value(QStringLiteral("status")));
    info.volumeCount = MangaUpdatesStatusParser::parseLeadingVolumeCount(info.rawStatus);
    info.latestChapter = intValue(rec.value(QStringLiteral("latest_chapter")));
    info.completed = rec.value(QStringLiteral("completed")).toBool();
    info.description = rec.value(QStringLiteral("description")).toString();
    info.imageUrl = imageUrlFromRecord(rec);
    info.lastUpdated = QDateTime::fromString(
        rec.value(QStringLiteral("last_updated")).toObject()
            .value(QStringLiteral("as_rfc3339")).toString(),
        Qt::ISODate);
    info.fetchedAtMs = QDateTime::currentMSecsSinceEpoch();

    if (info.seriesId <= 0) {
        emit seriesFailed(requestId, QStringLiteral("missing series_id in response"));
        return;
    }
    emit seriesSucceeded(requestId, info);
}

} // namespace tankoban::manga::mangaupdates
