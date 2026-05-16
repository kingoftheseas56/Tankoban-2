// src/core/manga/anilist/AniListClient.h
#pragma once

#include "AniListTypes.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace tankoban::manga::anilist {

// AniList GraphQL client. Endpoint: https://graphql.anilist.co
// Unauthenticated. Rate limit: 90 requests per minute. We throttle to
// 1 request per second internally to stay well under that ceiling.
//
// Two query shapes:
//   - searchByTitle(query)  -> list of MediaPreview tiles for the search UI
//   - seriesById(anilistId) -> full MediaDetail incl. chapter+volume binding
//
// Both return via signals so the UI thread is not blocked. Failure modes
// surface via the *Failed signals with a human-readable reason; the UI
// either shows a stale-cache fallback or a 'try again' affordance.
class AniListClient : public QObject
{
    Q_OBJECT
public:
    explicit AniListClient(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~AniListClient() override;

    // Fire a search. Result lands on searchSucceeded or searchFailed with
    // a matching `requestId` (caller-generated) so concurrent searches
    // can be disambiguated.
    void searchByTitle(const QString& query, int requestId);

    // Fire a per-series fetch. requestId is the caller's chosen tag for
    // pairing the response with the originating request.
    void seriesById(int anilistId, int requestId);

    // TANKOYOMI_VOLUME_PIVOT Phase 12 -- expose the shared NAM so siblings
    // (ComicsSeriesView's lazy cover-art loader) can reuse the same network
    // stack without ComicsPage having to pass NAM independently. Non-owning;
    // may return nullptr if the client was constructed with a null NAM.
    // Defined out-of-line in the .cpp so QPointer<QNetworkAccessManager>::data()
    // can resolve against the full type (header keeps QNetworkAccessManager
    // forward-declared only).
    QNetworkAccessManager* networkManager() const;

    // PHASE 7+: consider promoting the QString reason to a (FailureCode, QString) pair
    // so the UI can distinguish transient network errors (retry-worthy) from GraphQL
    // schema errors (terminal). Phase 1 ships free-form strings per plan.
signals:
    void searchSucceeded(int requestId, const QList<tankoban::manga::anilist::MediaPreview>& results);
    void searchFailed(int requestId, const QString& reason);

    void seriesSucceeded(int requestId, const tankoban::manga::anilist::MediaDetail& detail);
    void seriesFailed(int requestId, const QString& reason);

private slots:
    void onSearchReplyFinished();
    void onSeriesReplyFinished();

private:
    void throttleIfNeeded();

    QPointer<QNetworkAccessManager> m_nam;
    qint64 m_lastRequestMs = 0;  // simple 1-req/sec throttle

    // requestId is threaded through QNetworkReply::setProperty("anilist_requestId", id) -- see .cpp.
};

} // namespace tankoban::manga::anilist
