// src/core/manga/wikidata/WikidataClient.cpp

#include "WikidataClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(lcWikidata, "tankoban.manga.wikidata")

namespace tankoban::manga::wikidata {

namespace {

// SPARQL: get Fandom wiki ID (P4073) + optional Fandom article ID (P6262).
QString buildSparql(const QString& qid)
{
    return QStringLiteral(
        "SELECT ?wikiId ?articleId WHERE { "
        "  wd:%1 wdt:P4073 ?wikiId . "
        "  OPTIONAL { wd:%1 wdt:P6262 ?articleId . } "
        "} LIMIT 1"
    ).arg(qid);
}

} // anonymous

WikidataClient::WikidataClient(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam)
{}

void WikidataClient::resolveQid(int requestId, const QString& qid)
{
    if (!m_nam) {
        emit resolveFailed(requestId, QStringLiteral("network-manager-null"));
        return;
    }
    if (qid.isEmpty()) {
        emit resolveFailed(requestId, QStringLiteral("empty-qid"));
        return;
    }

    QUrl url(QStringLiteral("https://query.wikidata.org/sparql"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("query"),  buildSparql(qid));
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 Tankoban/1.0 (+https://github.com/kingoftheseas56/Tankoban-2)"));
    req.setRawHeader("Accept", "application/sparql-results+json");
    req.setTransferTimeout(15000);

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId, qid]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(lcWikidata).noquote()
                << "SPARQL request failed for" << qid << ":" << reply->errorString();
            emit resolveFailed(requestId, reply->errorString());
            return;
        }

        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qCWarning(lcWikidata).noquote()
                << "invalid SPARQL JSON for" << qid << ":" << err.errorString();
            emit resolveFailed(requestId, QStringLiteral("invalid-json"));
            return;
        }

        const QJsonArray bindings = doc.object()
            .value(QStringLiteral("results")).toObject()
            .value(QStringLiteral("bindings")).toArray();
        if (bindings.isEmpty()) {
            // No P4073 on this item — could be a manga that nobody has
            // mapped to a Fandom wiki yet. Caller falls through to other
            // discovery layers (Fandom search, hardcoded manifest fallback).
            emit resolveFailed(requestId, QStringLiteral("no-p4073"));
            return;
        }

        const QJsonObject row = bindings.first().toObject();
        const QString wikiId = row.value(QStringLiteral("wikiId")).toObject()
            .value(QStringLiteral("value")).toString();
        if (wikiId.isEmpty()) {
            emit resolveFailed(requestId, QStringLiteral("empty-p4073-value"));
            return;
        }

        // Codex §3.1 / audit §2.3 — P6262 drift detection. If the optional
        // articleId's subdomain prefix disagrees with P4073's wikiId, log
        // the conflict but TRUST P4073. P6262 may NEVER switch the
        // subdomain by itself (audit confirmed One Piece's P6262 points
        // to tropedia, which is NOT the One Piece Fandom wiki).
        const QString articleId = row.value(QStringLiteral("articleId")).toObject()
            .value(QStringLiteral("value")).toString();
        if (!articleId.isEmpty()) {
            const QString articleSubdomain = articleId.section(QChar(':'), 0, 0);
            if (!articleSubdomain.isEmpty() && articleSubdomain != wikiId) {
                qCWarning(lcWikidata).noquote()
                    << "fandom-discovery-conflict for" << qid
                    << ": P4073=" << wikiId
                    << ", P6262 subdomain=" << articleSubdomain
                    << "(article=" << articleId << ")"
                    << "— trusting P4073 per spec §3.1";
            }
        }

        tankoban::manga::fandom::FandomReference ref;
        ref.subdomain = wikiId;
        // volumePagePath intentionally left empty; manifest layer or
        // heuristic discovery resolves the page path. pageModel defaults to
        // Monolith and gets refined by the manifest before extraction.
        ref.pageModel = tankoban::manga::fandom::PageModel::Monolith;
        emit resolved(requestId, ref);
    });
}

} // namespace tankoban::manga::wikidata
