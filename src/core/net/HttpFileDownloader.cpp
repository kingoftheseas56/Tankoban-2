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

HttpFileDownloader::~HttpFileDownloader()
{
    // Cleanup if destroyed mid-download (Codex review 2026-06-02): the reply is
    // parented to the long-lived m_nam, so it would leak; the .part file would
    // be orphaned. Disconnect first so the finished slot can't fire into a
    // half-destroyed object, then abort + free.
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_file) {
        m_file->close();
        m_file->remove();   // drop the partial .part; m_file is a child -> auto-deleted
        m_file = nullptr;
    }
}

void HttpFileDownloader::start(const QString& url, const QString& destPath)
{
    // Reject a reentrant start (Codex review): overwriting m_reply/m_file would
    // leak the in-flight reply and orphan the previous .part.
    if (m_reply || m_file) {
        emit failed(QStringLiteral("Download already in progress"));
        return;
    }
    m_writeFailed = false;

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
    // Stall guard (2026-06-02): abort if no data transfers for 30s. Without it a
    // hoster that holds the connection open after the body never fires finished,
    // hanging the caller forever (seen in the Western DDL link walk). This is a
    // per-stall timeout, not a total cap, so a slow-but-progressing large file is
    // unaffected.
    req.setTransferTimeout(30000);

    m_reply = m_nam->get(req);

    // Stream: append each chunk as it arrives — never buffer whole file. Check
    // the write actually landed (disk full => short write); on failure abort so
    // the finished handler takes the failure path and removes the corrupt .part.
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_writeFailed) return;
        const QByteArray chunk = m_reply->readAll();
        if (m_file->write(chunk) != chunk.size()) {
            m_writeFailed = true;
            m_reply->abort();   // -> finished -> failure cleanup
        }
    });

    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 recv, qint64 total) {
                emit progress(recv, total);
            });

    connect(m_reply, &QNetworkReply::finished, this, [this, destPath, partPath]() {
        m_reply->deleteLater();

        // Failure if the network errored OR a chunk write came up short (the
        // abort() in readyRead lands here with OperationCanceledError + the flag).
        if (m_reply->error() != QNetworkReply::NoError || m_writeFailed) {
            const QString err = m_writeFailed
                ? QStringLiteral("Disk write failed (short write)")
                : m_reply->errorString();
            m_reply = nullptr;
            m_file->close();
            m_file->remove();
            m_file->deleteLater();
            m_file = nullptr;
            emit failed(err);
            return;
        }

        // Flush any remaining bytes not yet delivered via readyRead (checked).
        const QByteArray tail = m_reply->readAll();
        if (m_file->write(tail) != tail.size()) {
            m_reply = nullptr;
            m_file->close();
            m_file->remove();
            m_file->deleteLater();
            m_file = nullptr;
            emit failed(QStringLiteral("Disk write failed (short write)"));
            return;
        }
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
