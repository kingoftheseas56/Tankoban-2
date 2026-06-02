// src/core/net/HttpFileDownloader.cpp
#include "HttpFileDownloader.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace tankoban::net {

static const QByteArray kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36";

HttpFileDownloader::HttpFileDownloader(QNetworkAccessManager* nam, QObject* parent)
    : QObject(parent)
    , m_nam(nam)
{
}

void HttpFileDownloader::start(const QString& url, const QString& destPath)
{
    const QString partPath = destPath + QLatin1String(".part");

    m_file = new QFile(partPath, this);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString err = m_file->errorString();
        m_file->deleteLater();
        m_file = nullptr;
        emit failed(QStringLiteral("Cannot open .part file: ") + err);
        return;
    }

    const QUrl requestUrl(url);                 // named var: avoids the most-vexing-parse
    QNetworkRequest req(requestUrl);            // (QNetworkRequest req(QUrl(url)) parses as a fn decl)
    req.setRawHeader("User-Agent", kUserAgent);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_nam->get(req);

    // Stream: append each chunk as it arrives — never buffer whole file.
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        m_file->write(m_reply->readAll());
    });

    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 recv, qint64 total) {
                emit progress(recv, total);
            });

    connect(m_reply, &QNetworkReply::finished, this, [this, destPath, partPath]() {
        m_reply->deleteLater();

        if (m_reply->error() != QNetworkReply::NoError) {
            const QString err = m_reply->errorString();
            m_reply = nullptr;
            m_file->close();
            m_file->remove();
            m_file->deleteLater();
            m_file = nullptr;
            emit failed(err);
            return;
        }

        // Flush any remaining bytes not yet delivered via readyRead.
        m_file->write(m_reply->readAll());
        m_file->close();
        m_file->deleteLater();
        m_file = nullptr;
        m_reply = nullptr;

        // Atomic rename: on Windows remove existing dest first, then rename.
        if (QFile::exists(destPath))
            QFile::remove(destPath);

        if (!QFile::rename(partPath, destPath)) {
            QFile::remove(partPath);
            emit failed(QStringLiteral("Rename .part -> dest failed"));
            return;
        }

        emit finished(destPath);
    });
}

void HttpFileDownloader::cancel()
{
    if (!m_reply)
        return;

    // abort() triggers the finished signal, which cleans up m_file + m_reply.
    m_reply->abort();
}

} // namespace tankoban::net
