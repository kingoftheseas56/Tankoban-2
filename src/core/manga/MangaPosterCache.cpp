#include "MangaPosterCache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace {

QString cacheDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/Tankoban/data/manga_posters");
    QDir().mkpath(dir);
    return dir;
}

QString safeId(QString id)
{
    id.replace(QRegularExpression(R"([<>:"/\\|?*\s])"), QStringLiteral("_"));
    return id;
}

void applyReferer(const QString& sourceId, QNetworkRequest& req)
{
    if (sourceId == QLatin1String("weebcentral"))
        req.setRawHeader("Referer", "https://weebcentral.com/");
    else if (sourceId == QLatin1String("readcomicsonline"))
        req.setRawHeader("Referer", "https://readcomicsonline.ru/");
}

} // namespace

namespace MangaPosterCache {

QString cachePath(const QString& sourceId, const QString& seriesId)
{
    return cacheDir() + QLatin1Char('/') + sourceId + QLatin1Char('_') + safeId(seriesId)
        + QStringLiteral(".jpg");
}

QString existingPath(const QString& sourceId, const QString& seriesId)
{
    const QString path = cachePath(sourceId, seriesId);
    return (QFileInfo::exists(path) && QFileInfo(path).size() > 0) ? path : QString();
}

void download(const MangaResult& preview,
              const QString& imageUrl,
              QNetworkAccessManager* nam,
              QObject* context,
              std::function<void(const QString&)> onReady)
{
    if (!nam || !context || imageUrl.isEmpty()) return;

    const QString path = cachePath(preview.source, preview.id);
    if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) {
        if (onReady) onReady(path);
        return;
    }

    QNetworkRequest req{QUrl(imageUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
    applyReferer(preview.source, req);
    req.setTransferTimeout(10000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    auto* reply = nam->get(req);
    QObject::connect(reply, &QNetworkReply::finished, context,
        [reply, path, cb = std::move(onReady)]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) return;

            const QByteArray data = reply->readAll();
            if (data.isEmpty()) return;

            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) return;
            f.write(data);
            f.close();

            if (cb) cb(path);
        });
}

} // namespace MangaPosterCache
