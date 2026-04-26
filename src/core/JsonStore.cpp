#include "JsonStore.h"

#include "DebugLogBuffer.h"

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
    // REPO_HYGIENE Phase 4 P4.1 (2026-04-26) — race fix. Read consults the
    // latest-values map, which always holds the most-recent in-process write
    // for every filename. The prior implementation read from m_pending, which
    // the writer thread erased BEFORE commitToDisk fsync+rename, so a read
    // racing the disk commit could return the previous on-disk value instead
    // of the latest in-memory one.
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_latestValues.find(filename);
        if (it != m_latestValues.end())
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
    // m_latestValues is the read-truth; insert always (overwrites prior).
    m_latestValues.insert(filename, value);
    // m_pending is the disk write-queue; coalescing newer-replaces-older
    // means only the latest queued value per file ever reaches disk.
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
        const bool ok = commitToDisk(filename, value);  // off main thread
        if (!ok) {
            DebugLogBuffer::instance().error(
                "jsonstore",
                QStringLiteral("commitToDisk failed for %1").arg(filename));
        }
        m_mutex.lock();
    }
    // Shutdown drain: flush any writes still queued before the thread exits.
    while (!m_pending.isEmpty()) {
        auto it = m_pending.begin();
        QString filename = it.key();
        QJsonObject value = it.value();
        m_pending.erase(it);
        m_mutex.unlock();
        const bool ok = commitToDisk(filename, value);
        if (!ok) {
            DebugLogBuffer::instance().error(
                "jsonstore",
                QStringLiteral("shutdown-drain commitToDisk failed for %1")
                    .arg(filename));
        }
        m_mutex.lock();
    }
    m_mutex.unlock();
}

bool JsonStore::commitToDisk(const QString& filename, const QJsonObject& value)
{
    QString path = m_dataDir + "/" + filename;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QJsonDocument doc(value);
    if (file.write(doc.toJson(QJsonDocument::Indented)) < 0)
        return false;
    return file.commit();
}
