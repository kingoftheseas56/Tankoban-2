#pragma once

#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMutex>
#include <QWaitCondition>
#include <QString>
#include <QDir>
#include <QHash>
#include <atomic>
#include <thread>

// JsonStore — atomic JSON persistence with in-process coalescing async writer.
//
// STREAM_STUTTER_JSONSTORE_FIX 2026-04-21: writes are enqueued to a mutex-
// protected QHash keyed by filename; a dedicated background thread drains the
// queue and runs QSaveFile::commit() (fsync + atomic rename) off the main
// thread. On Windows with Defender each tiny-JSON rename triggers a 50-300ms
// scan that previously blocked the Qt event loop once per second per
// saveProgress call — visible as a structural 280ms paint-p99 spike at 1 Hz.
// Coalescing guarantees: newer write() for the same file replaces the older
// queued value before disk commit. read() checks the pending queue first so
// in-process read-after-write is consistent even when commit is still queued.
// Destructor sets shutdown + drains remaining pending before joining; lost
// writes only possible on process crash, same risk surface as the prior
// synchronous QSaveFile path (crash between open/commit also loses data).
class JsonStore {
public:
    explicit JsonStore(const QString& dataDir);
    ~JsonStore();

    QJsonObject read(const QString& filename, const QJsonObject& fallback = {}) const;
    void write(const QString& filename, const QJsonObject& value);

    QString dataDir() const { return m_dataDir; }

private:
    void writerLoop();
    void commitToDisk(const QString& filename, const QJsonObject& value);

    QString m_dataDir;
    mutable QMutex m_mutex;
    QHash<QString, QJsonObject> m_pending;  // coalescing queue keyed by filename
    QWaitCondition m_cond;
    std::atomic<bool> m_shutdown{false};
    std::thread m_writer;
};
