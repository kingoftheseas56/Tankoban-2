// src/core/manga/fandom/FandomClient.cpp

#include "FandomClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(lcFandomClient, "tankoban.manga.fandom.client")

namespace tankoban::manga::fandom {

FandomClient::FandomClient(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent), m_nam(nam)
{}

void FandomClient::fetchPage(int requestId, const QString& subdomain, const QString& pagePath)
{
    if (!m_nam) {
        emit pageFetchFailed(requestId, QStringLiteral("network-manager-null"));
        return;
    }
    if (subdomain.isEmpty() || pagePath.isEmpty()) {
        emit pageFetchFailed(requestId, QStringLiteral("empty-target"));
        return;
    }

    // action=parse takes the bare page title; strip a leading "/wiki/" if present.
    QString pageTitle = pagePath;
    if (pageTitle.startsWith(QStringLiteral("/wiki/")))
        pageTitle = pageTitle.mid(QStringLiteral("/wiki/").size());

    QUrl url(QStringLiteral("https://%1.fandom.com/api.php").arg(subdomain));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("action"),    QStringLiteral("parse"));
    q.addQueryItem(QStringLiteral("page"),      pageTitle);
    q.addQueryItem(QStringLiteral("prop"),      QStringLiteral("text|sections"));
    q.addQueryItem(QStringLiteral("format"),    QStringLiteral("json"));
    q.addQueryItem(QStringLiteral("redirects"), QStringLiteral("1"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 Tankoban/1.0 (+https://github.com/kingoftheseas56/Tankoban-2)"));
    req.setTransferTimeout(15000);  // 15s hard timeout; soft SLO lives in FallbackChainResolver

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId, pageTitle]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(lcFandomClient).noquote()
                << "fetch failed for" << pageTitle << ":" << reply->errorString();
            emit pageFetchFailed(requestId, reply->errorString());
            return;
        }

        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qCWarning(lcFandomClient).noquote()
                << "invalid JSON for" << pageTitle << ":" << err.errorString();
            emit pageFetchFailed(requestId, QStringLiteral("invalid-json"));
            return;
        }

        const QJsonObject root = doc.object();
        // MediaWiki errors come back as { "error": { "code": ..., "info": ... } }
        if (root.contains(QStringLiteral("error"))) {
            const QString info = root.value(QStringLiteral("error")).toObject()
                .value(QStringLiteral("info")).toString();
            qCWarning(lcFandomClient).noquote()
                << "MediaWiki error for" << pageTitle << ":" << info;
            emit pageFetchFailed(requestId,
                QStringLiteral("mediawiki-error: ") + info);
            return;
        }

        const QJsonObject parse = root.value(QStringLiteral("parse")).toObject();
        if (parse.isEmpty()) {
            emit pageFetchFailed(requestId, QStringLiteral("no-parse-block"));
            return;
        }

        ParsedPage page;
        page.rawHtml = parse.value(QStringLiteral("text")).toObject()
            .value(QStringLiteral("*")).toString();
        page.sections           = parse.value(QStringLiteral("sections")).toArray();
        page.pageTitle          = parse.value(QStringLiteral("title")).toString();
        page.canonicalPageTitle = page.pageTitle;  // post-redirect title from action=parse&redirects=1

        // Surface the redirect trail when present. MediaWiki returns
        // `redirects` as an array of {"from":..., "to":...} entries when
        // multiple redirects were followed.
        const QJsonArray redirects = parse.value(QStringLiteral("redirects")).toArray();
        for (const auto& r : redirects) {
            const QJsonObject hop = r.toObject();
            const QString from = hop.value(QStringLiteral("from")).toString();
            if (!from.isEmpty())
                page.redirectTrail.append(from);
        }

        if (!page.redirectTrail.isEmpty()) {
            qCInfo(lcFandomClient).noquote()
                << "page" << pageTitle
                << "redirected via" << page.redirectTrail.size() << "hop(s) to"
                << page.canonicalPageTitle;
        }

        emit pageFetched(requestId, page);
    });
}

} // namespace tankoban::manga::fandom
