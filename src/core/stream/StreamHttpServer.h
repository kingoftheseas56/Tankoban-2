#pragma once

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QTcpServer>
#include <QThreadPool>

class TorrentEngine;

class StreamHttpServer : public QObject
{
    Q_OBJECT

public:
    explicit StreamHttpServer(TorrentEngine* engine, QObject* parent = nullptr);
    ~StreamHttpServer() override;

    bool start();
    void stop();
    int port() const;

    void registerFile(const QString& infoHash, int fileIndex,
                      const QString& filePath, qint64 fileSize);
    void unregisterFile(const QString& infoHash, int fileIndex);
    void clear();

    struct FileEntry {
        QString infoHash;
        int fileIndex = 0;
        QString path;
        qint64 size = 0;
    };

    FileEntry lookupFile(const QString& infoHash, int fileIndex) const;
    TorrentEngine* engine() const { return m_engine; }

private slots:
    void onNewConnection();

private:
    static QString registryKey(const QString& infoHash, int fileIndex);

    TorrentEngine* m_engine;
    QTcpServer* m_server = nullptr;
    mutable QMutex m_mutex;
    QHash<QString, FileEntry> m_registry;
};
