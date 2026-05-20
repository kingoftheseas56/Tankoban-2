// src/core/manga/wikipedia/WikipediaResolver.cpp

#include "WikipediaResolver.h"

#include "WikipediaParser.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(lcWikipedia, "tankoban.manga.wikipedia")

namespace tankoban::manga::wikipedia {

namespace {

using tankoban::manga::fandom::FandomVolume;

QString urlPageEncode(const QString& displayTitle)
{
    QString s = displayTitle;
    s.replace(QChar(' '), QChar('_'));
    return s;
}

} // anonymous

WikipediaResolver::WikipediaResolver(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam)
{}

QList<FandomVolume> WikipediaResolver::parseVolumeTable(const QString& rawHtml)
{
    // Delegate to the pure-logic translation unit (zero Qt::Network dep) so
    // both the main app's resolver path and the test target's parser-only
    // path can use the same code.
    return tankoban::manga::wikipedia::parseVolumeTable(rawHtml);
}

void WikipediaResolver::resolveForTitle(const QString& seriesId,
                                        const QString& englishTitle)
{
    if (!m_nam) {
        emit unresolved(seriesId, QStringLiteral("network-manager-null"));
        return;
    }
    if (englishTitle.isEmpty()) {
        emit unresolved(seriesId, QStringLiteral("empty-english-title"));
        return;
    }

    // Tier-2 try order: "List of X manga volumes" first.
    const QString primary  = QStringLiteral("List of %1 manga volumes").arg(englishTitle);
    const QString fallback = QStringLiteral("List of %1 chapters").arg(englishTitle);

    // We dispatch the primary fetch with sourcePath="volumes". The
    // pageFetchFailed handler decides whether to retry with the chapters
    // URL based on the tag carried along with the request.
    fetchPath(/*requestId*/ 1, seriesId, englishTitle, primary, QStringLiteral("volumes"));
    Q_UNUSED(fallback);  // wired inside fetchPath's failure path below
}

void WikipediaResolver::fetchPath(int /*requestId*/,
                                  const QString& seriesId,
                                  const QString& englishTitle,
                                  const QString& pageTitle,
                                  const QString& sourcePathTag)
{
    QUrl url(QStringLiteral("https://en.wikipedia.org/w/api.php"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("action"), QStringLiteral("parse"));
    q.addQueryItem(QStringLiteral("page"),   urlPageEncode(pageTitle));
    q.addQueryItem(QStringLiteral("prop"),   QStringLiteral("text"));
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 Tankoban/1.0 (+https://github.com/kingoftheseas56/Tankoban-2)"));
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(15000);

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, seriesId, englishTitle, sourcePathTag]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            // 404 on the "volumes" tier → retry with "chapters" tier.
            const bool isVolumeTier = (sourcePathTag == QStringLiteral("volumes"));
            const bool likely404    = reply->error() == QNetworkReply::ContentNotFoundError
                                        || reply->error() == QNetworkReply::ProtocolUnknownError;
            if (isVolumeTier && likely404) {
                const QString chaptersTitle =
                    QStringLiteral("List of %1 chapters").arg(englishTitle);
                fetchPath(/*requestId*/ 2, seriesId, englishTitle,
                          chaptersTitle, QStringLiteral("chapters"));
                return;
            }
            emit unresolved(seriesId,
                            QStringLiteral("wikipedia-http-failed:") + reply->errorString());
            return;
        }

        // Parse the JSON, extract parse.text["*"], run parseVolumeTable.
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            emit unresolved(seriesId, QStringLiteral("invalid-json"));
            return;
        }
        const QString rawHtml = doc.object()
            .value(QStringLiteral("parse")).toObject()
            .value(QStringLiteral("text")).toObject()
            .value(QStringLiteral("*")).toString();

        // MediaWiki returns a JSON error envelope (no parse object) when a
        // page genuinely doesn't exist — treat that as a tier-2 fallback
        // trigger on the volumes path.
        if (rawHtml.isEmpty()) {
            const bool isVolumeTier = (sourcePathTag == QStringLiteral("volumes"));
            if (isVolumeTier) {
                const QString chaptersTitle =
                    QStringLiteral("List of %1 chapters").arg(englishTitle);
                fetchPath(/*requestId*/ 3, seriesId, englishTitle,
                          chaptersTitle, QStringLiteral("chapters"));
                return;
            }
            emit unresolved(seriesId, QStringLiteral("empty-page-on-fallback"));
            return;
        }

        WikipediaCatalog catalog;
        catalog.seriesId     = seriesId;
        catalog.englishTitle = englishTitle;
        catalog.sourcePath   = sourcePathTag;
        catalog.volumes      = parseVolumeTable(rawHtml);

        if (catalog.volumes.isEmpty()) {
            emit unresolved(seriesId, QStringLiteral("parser-returned-zero-volumes"));
            return;
        }

        emit resolved(catalog);
    });
}

} // namespace tankoban::manga::wikipedia
