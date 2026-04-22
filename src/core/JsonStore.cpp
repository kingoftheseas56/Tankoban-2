#include "JsonStore.h"

#include <QFile>
#include <QSaveFile>
#include <QDir>

JsonStore::JsonStore(const QString& dataDir)
    : m_dataDir(dataDir)
{
    QDir().mkpath(m_dataDir);
    m_writer = std::thread([this]{ writerLoop(); });
}

JsonStore::~JsonStore()
{
    m_mutex.lock();
    m_shutdown.store(true, std::memory_order_release);
    m_cond.wakeAll();
    m_mutex.unlock();
    if (m_writer.joinable())
        m_writer.join();
}

QJsonObject JsonStore::read(const QString& filename, const QJsonObject& fallback) const
{
    // STREAM_STUTTER_JSONSTORE_FIX: check pending-writes queue first so an
    // in-process write() followed immediately by read() returns the latest
    // value even when the disk commit is still queued on the writer thread.
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_pending.find(filename);
        if (it != m_pending.end())
            return it.value();
    }

    QString path = m_dataDir + "/" + filename;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fallback;

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return fallback;

    return doc.object();
}

void JsonStore::write(const QString& filename, const QJsonObject& value)
{
    QMutexLocker lock(&m_mutex);
    // QHash::insert replaces existing value for the same key — coalescing
    // guarantees only the LATEST value per file ever reaches disk.
    m_pending.insert(filename, value);
    m_cond.wakeOne();
}

void JsonStore::writerLoop()
{
    m_mutex.lock();
    while (!m_shutdown.load(std::memory_order_acquire)) {
        if (m_pending.isEmpty()) {
            m_cond.wait(&m_mutex);
            continue;
        }
        auto it = m_pending.begin();
        QString filename = it.key();
        QJsonObject value = it.value();
        m_pending.erase(it);
        m_mutex.unlock();
        commitToDisk(filename, value);  // fsync + atomic rename off main thread
        m_mutex.lock();
    }
    // Shutdown drain: flush any writes still queued before the thread exits.
    while (!m_pending.isEmpty()) {
        auto it = m_pending.begin();
        QString filename = it.key();
        QJsonObject value = it.value();
        m_pending.erase(it);
        m_mutex.unlock();
        commitToDisk(filename, value);
        m_mutex.lock();
    }
    m_mutex.unlock();
}

void JsonStore::commitToDisk(const QString& filename, const QJsonObject& value)
{
    QString path = m_dataDir + "/" + filename;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    QJsonDocument doc(value);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.commit();
}
