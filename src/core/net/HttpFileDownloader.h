// src/core/net/HttpFileDownloader.h
#pragma once

#include <QObject>
#include <QString>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace tankoban::net {

// Streams an HTTP(S) URL to a file on disk, following redirects (incl. the
// getcomics.org/dls/<token> gate). Atomic: writes to <dest>.part then renames
// on success. One download per instance call; emits progress/finished/failed.
class HttpFileDownloader : public QObject {
    Q_OBJECT
public:
    explicit HttpFileDownloader(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~HttpFileDownloader() override;   // aborts + cleans up an in-flight download
    void start(const QString& url, const QString& destPath);
    void cancel();

signals:
    void progress(qint64 received, qint64 total);
    void finished(const QString& path);
    void failed(const QString& reason);

private:
    QNetworkAccessManager* m_nam   = nullptr;
    QNetworkReply*         m_reply = nullptr;  // non-owning while in flight; deleteLater in handler
    QFile*                 m_file  = nullptr;  // owned by this; cleaned up in handler
    bool                   m_writeFailed = false;  // a chunk write came up short (disk full) -> fail
};

} // namespace tankoban::net
