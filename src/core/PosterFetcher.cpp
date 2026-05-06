#include "PosterFetcher.h"

#include "core/DebugLogBuffer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMetaEnum>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QSslError>
#include <QStringList>
#include <QUrlQuery>

namespace {
QString errName(QNetworkReply::NetworkError e)
{
    static const QMetaEnum me = QMetaEnum::fromType<QNetworkReply::NetworkError>();
    const char* k = me.valueToKey(static_cast<int>(e));
    return k ? QString::fromLatin1(k) : QString::number(static_cast<int>(e));
}
}  // namespace

void PosterFetcher::download(QNetworkAccessManager* nam,
                             const QUrl& url,
                             const QString& destPath,
                             QObject* ctx,
                             std::function<void(bool success)> cb)
{
    const QString urlStr = url.toString();
    auto fire = [cb, urlStr, destPath](bool ok, const QString& result, const QString& detail = QString()) {
        DebugLogBuffer::instance().info("poster-fetcher",
            QStringLiteral("download url='%1' dest='%2' result=%3%4")
                .arg(urlStr, destPath, result,
                     detail.isEmpty() ? QString() : QStringLiteral(" ") + detail));
        if (cb) cb(ok);
    };

    if (!nam || !url.isValid() || destPath.isEmpty()) {
        fire(false, QStringLiteral("fail-args"),
             QStringLiteral("nam=%1 urlValid=%2 destEmpty=%3")
                 .arg(nam ? "ok" : "null", url.isValid() ? "true" : "false",
                      destPath.isEmpty() ? "true" : "false"));
        return;
    }

    QDir().mkpath(QFileInfo(destPath).absolutePath());

    // VIDEOS_FETCH_POSTER_FIX Phase 2B 2026-05-06 — Cinemeta's CDN
    // (images.metahub.space) serves WebP for the `small` variant of some
    // posters (per-image, not negotiable via Accept header — empirically
    // confirmed via curl). Qt's image-format plugin set on this build does
    // not include qwebp.dll, so QImage::loadFromData fails with fail-decode
    // for those candidates. The CDN supports a ?format=jpg query parameter
    // that forces JPEG response on every poster — verified across 4 IMDB IDs
    // including the 3 that previously came back as WebP. Transform applied
    // here at the request-construction boundary; non-metahub URLs untouched.
    QUrl finalUrl = url;
    if (finalUrl.host() == QLatin1String("images.metahub.space")) {
        QUrlQuery q(finalUrl);
        if (!q.hasQueryItem(QStringLiteral("format")))
            q.addQueryItem(QStringLiteral("format"), QStringLiteral("jpg"));
        finalUrl.setQuery(q);
    }
    const QString rewrittenUrlStr = finalUrl.toString();

    QNetworkRequest req(finalUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64)"
                                 " AppleWebKit/537.36"));
    // VIDEOS_FETCH_POSTER_FIX 2026-05-06: bumped from 10s → 30s. Empirical
    // evidence (curl 0.4s, PowerShell HTTPS 21.3s, Qt Schannel timing out at
    // 10s) showed Qt's Windows TLS path is dramatically slower than libcurl's
    // OpenSSL on the same network. 30s gives Schannel + slow CDNs (Cinemeta's
    // images.metahub.space, themoviedb image CDN) the headroom they need
    // without making genuinely-dead URLs hang the picker indefinitely.
    req.setTransferTimeout(30000);
    // Many poster CDNs 302-redirect (e.g. tmdb→images.tmdb, addon→cdn). Qt6's
    // default policy sometimes drops these silently — set explicit same-or-
    // safer-scheme policy so HTTP→HTTPS and HTTPS→HTTPS redirects complete.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = nam->get(req);
    QObject* receiver = ctx ? ctx : nam;
    // SSL-error logging — fires for cert issues, TLS handshake failures, etc.
    // Doesn't ignore the errors (lambda is observe-only); the finished slot's
    // existing error path still aborts the download. Useful for distinguishing
    // "host unreachable" from "TLS misconfiguration" in future regressions.
    QObject::connect(reply, &QNetworkReply::sslErrors, receiver,
                     [urlStr](const QList<QSslError>& errs) {
        QStringList details;
        for (const auto& e : errs)
            details << QStringLiteral("[%1] %2").arg(QString::number(static_cast<int>(e.error())), e.errorString());
        DebugLogBuffer::instance().warning("poster-fetcher",
            QStringLiteral("ssl-errors url='%1' count=%2 details=%3")
                .arg(urlStr, QString::number(errs.size()), details.join(QLatin1String("; "))));
    });
    QObject::connect(reply, &QNetworkReply::finished, receiver,
                     [reply, destPath, fire]() {
        reply->deleteLater();
        const QVariant httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const QVariant ctVar = reply->header(QNetworkRequest::ContentTypeHeader);
        const QString contentType = ctVar.toString();
        if (reply->error() != QNetworkReply::NoError) {
            fire(false, QStringLiteral("fail-net"),
                 QStringLiteral("err=%1 http=%2 errString='%3'")
                     .arg(errName(reply->error()),
                          httpStatus.toString(),
                          reply->errorString()));
            return;
        }
        const QByteArray bytes = reply->readAll();
        if (bytes.isEmpty()) {
            fire(false, QStringLiteral("fail-empty"),
                 QStringLiteral("http=%1 contentType='%2'")
                     .arg(httpStatus.toString(), contentType));
            return;
        }
        // Validate the payload actually decodes as an image. Some addons
        // and CDNs return HTML error pages with HTTP 200 (e.g. Cloudflare
        // challenge, addon 404 wrapper); writing those to disk with a .jpg
        // extension poisons the cache — subsequent QPixmap loads return
        // null forever and the placeholder sticks. Validate before writing.
        {
            QImage probe;
            if (!probe.loadFromData(bytes)) {
                fire(false, QStringLiteral("fail-decode"),
                     QStringLiteral("http=%1 bytes=%2 contentType='%3' first16hex=%4")
                         .arg(httpStatus.toString(),
                              QString::number(bytes.size()),
                              contentType,
                              QString::fromLatin1(bytes.left(16).toHex())));
                return;
            }
        }
        QFile out(destPath);
        if (!out.open(QIODevice::WriteOnly)) {
            fire(false, QStringLiteral("fail-write"),
                 QStringLiteral("bytes=%1 fileError='%2'")
                     .arg(QString::number(bytes.size()), out.errorString()));
            return;
        }
        out.write(bytes);
        out.close();
        fire(true, QStringLiteral("ok"),
             QStringLiteral("http=%1 bytes=%2 contentType='%3'")
                 .arg(httpStatus.toString(),
                      QString::number(bytes.size()),
                      contentType));
    });
}
