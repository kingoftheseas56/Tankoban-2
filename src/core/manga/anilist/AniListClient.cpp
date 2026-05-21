// src/core/manga/anilist/AniListClient.cpp
#include "AniListClient.h"
#include "AniListParser.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QUrl>

namespace tankoban::manga::anilist {

namespace {

constexpr const char* kEndpoint = "https://graphql.anilist.co";
constexpr qint64 kMinIntervalMs = 1000;  // 1 req/sec internal throttle

constexpr const char* kSearchQuery = R"(
query ($search: String) {
  Page(page: 1, perPage: 25) {
    media(search: $search, type: MANGA) {
      id
      title { romaji english native userPreferred }
      synonyms
      coverImage { medium large extraLarge }
      bannerImage
      format
      status
      startDate { year }
      genres
      description(asHtml: false)
    }
  }
}
)";

constexpr const char* kSeriesQuery = R"(
query ($id: Int) {
  Media(id: $id, type: MANGA) {
    id
    title { romaji english native userPreferred }
    synonyms
    coverImage { medium large extraLarge }
    bannerImage
    format
    status
    startDate { year }
    genres
    description(asHtml: false)
    chapters
    volumes
  }
}
)";
// NOTE: AniList does NOT expose per-chapter volume mapping via the public
// GraphQL schema as of 2026-05. The Media.chapters count + Media.volumes
// count are available, but the per-chapter binding is sparse. Our v1
// strategy: when the series has both totals, AniListVolumeMapper uses
// a chapter-count-per-volume heuristic (chapters / volumes, integer-
// floor). When the per-chapter list comes from WeebCentral (which we
// already scrape) and the AniList totals say how many vols exist, we
// map by ascending chapter number into equal-sized vol buckets. Vol X
// holds the residual unbound chapters past `volumes * (chapters/volumes)`.
// Plan-time decision: this is approximate but tractable; the alternative
// (a different metadata source) is out of scope for v1.

QByteArray makeRequestBody(const char* query, const QJsonObject& variables)
{
    QJsonObject body;
    body["query"] = QString::fromLatin1(query);
    body["variables"] = variables;
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

} // anonymous namespace

AniListClient::AniListClient(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam)
{
    Q_ASSERT(m_nam);
}

AniListClient::~AniListClient() = default;

QNetworkAccessManager* AniListClient::networkManager() const
{
    // PHASE 12 -- defined out-of-line so QPointer<QNetworkAccessManager>::data()
    // can resolve against the full type. Returns nullptr if the client was
    // constructed with a null NAM (Q_ASSERT in ctor catches that in debug,
    // but we still null-check defensively).
    return m_nam.data();
}

// PHASE 7+: when wiring into the search-widget UI thread, replace the
// QThread::msleep here with a single-shot QTimer + pending-call queue so
// we don't freeze the UI when the throttle triggers. Phase 1 keeps the
// simple-blocking shape per plan.
void AniListClient::throttleIfNeeded()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsed = now - m_lastRequestMs;
    if (m_lastRequestMs > 0 && elapsed < kMinIntervalMs) {
        QThread::msleep(static_cast<unsigned long>(kMinIntervalMs - elapsed));
    }
    m_lastRequestMs = QDateTime::currentMSecsSinceEpoch();
}

void AniListClient::searchByTitle(const QString& query, int requestId)
{
    if (!m_nam) {
        emit searchFailed(requestId, QStringLiteral("network manager unavailable"));
        return;
    }
    throttleIfNeeded();

    QJsonObject variables;
    variables["search"] = query;
    const QByteArray body = makeRequestBody(kSearchQuery, variables);

    QNetworkRequest req(QUrl(QString::fromLatin1(kEndpoint)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");

    auto* reply = m_nam->post(req, body);
    reply->setProperty("anilist_requestId", requestId);
    connect(reply, &QNetworkReply::finished,
            this, &AniListClient::onSearchReplyFinished);
}

void AniListClient::seriesById(int anilistId, int requestId)
{
    if (!m_nam) {
        emit seriesFailed(requestId, QStringLiteral("network manager unavailable"));
        return;
    }
    throttleIfNeeded();

    QJsonObject variables;
    variables["id"] = anilistId;
    const QByteArray body = makeRequestBody(kSeriesQuery, variables);

    QNetworkRequest req(QUrl(QString::fromLatin1(kEndpoint)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");

    auto* reply = m_nam->post(req, body);
    reply->setProperty("anilist_requestId", requestId);
    connect(reply, &QNetworkReply::finished,
            this, &AniListClient::onSeriesReplyFinished);
}

void AniListClient::onSearchReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    const int requestId = reply->property("anilist_requestId").toInt();
    if (reply->error() != QNetworkReply::NoError) {
        emit searchFailed(requestId, reply->errorString());
        return;
    }

    const QByteArray data = reply->readAll();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        emit searchFailed(requestId, QStringLiteral("json parse: ") + err.errorString());
        return;
    }
    const QJsonObject root = doc.object();
    if (root.contains("errors")) {
        emit searchFailed(requestId, QStringLiteral("graphql errors in response"));
        return;
    }
    const QJsonArray media = root.value("data").toObject()
                                 .value("Page").toObject()
                                 .value("media").toArray();
    QList<MediaPreview> out;
    for (const auto& v : media) {
        out.append(parseMediaPreviewFromJson(v.toObject()));
    }
    emit searchSucceeded(requestId, out);
}

void AniListClient::onSeriesReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    reply->deleteLater();

    const int requestId = reply->property("anilist_requestId").toInt();
    if (reply->error() != QNetworkReply::NoError) {
        emit seriesFailed(requestId, reply->errorString());
        return;
    }

    const QByteArray data = reply->readAll();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        emit seriesFailed(requestId, QStringLiteral("json parse: ") + err.errorString());
        return;
    }
    const QJsonObject root = doc.object();
    if (root.contains("errors")) {
        emit seriesFailed(requestId, QStringLiteral("graphql errors in response"));
        return;
    }
    const QJsonObject mediaObj = root.value("data").toObject().value("Media").toObject();

    MediaDetail detail;
    detail.preview       = parseMediaPreviewFromJson(mediaObj);
    detail.totalChapters = mediaObj.value("chapters").toInt(0);
    detail.totalVolumes  = mediaObj.value("volumes").toInt(0);
    detail.fetchedAtMs   = QDateTime::currentMSecsSinceEpoch();
    // chapters[] + volumeArt[] populated by callers (AniListVolumeMapper
    // synthesizes from WeebCentral when chapters list isn't on AniList).
    emit seriesSucceeded(requestId, detail);
}

} // namespace tankoban::manga::anilist
