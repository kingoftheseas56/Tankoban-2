#include "PosterFetcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>

void PosterFetcher::download(QNetworkAccessManager* nam,
                             const QUrl& url,
                             const QString& destPath,
                             QObject* ctx,
                             std::function<void(bool success)> cb)
{
    auto fire = [cb](bool ok) {
        if (cb) cb(ok);
    };

    if (!nam || !url.isValid() || destPath.isEmpty()) {
        fire(false);
        return;
    }

    QDir().mkpath(QFileInfo(destPath).absolutePath());

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64)"
                                 " AppleWebKit/537.36"));
    req.setTransferTimeout(10000);
    // Many poster CDNs 302-redirect (e.g. tmdb→images.tmdb, addon→cdn). Qt6's
    // default policy sometimes drops these silently — set explicit same-or-
    // safer-scheme policy so HTTP→HTTPS and HTTPS→HTTPS redirects complete.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = nam->get(req);
    QObject* receiver = ctx ? ctx : nam;
    QObject::connect(reply, &QNetworkReply::finished, receiver,
                     [reply, destPath, fire]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fire(false);
            return;
        }
        const QByteArray bytes = reply->readAll();
        if (bytes.isEmpty()) {
            fire(false);
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
                fire(false);
                return;
            }
        }
        QFile out(destPath);
        if (!out.open(QIODevice::WriteOnly)) {
            fire(false);
            return;
        }
        out.write(bytes);
        out.close();
        fire(true);
    });
}
