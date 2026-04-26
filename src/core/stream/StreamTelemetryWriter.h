#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <QString>
#include <QTextStream>
#include <QWaitCondition>

#include <atomic>
#include <thread>

inline bool streamTelemetryEnabled()
{
    static const bool enabled = qgetenv("TANKOBAN_STREAM_TELEMETRY") == "1";
    return enabled;
}

inline QString resolveStreamTelemetryPath()
{
    static const QString path = [] {
        QString dir = QCoreApplication::applicationDirPath();
        if (dir.isEmpty()) dir = QDir::currentPath();
        return dir + QStringLiteral("/stream_telemetry.log");
    }();
    return path;
}

class StreamTelemetryWriter final
{
public:
    static StreamTelemetryWriter& instance()
    {
        static StreamTelemetryWriter writer;
        return writer;
    }

    void enqueue(QString line)
    {
        if (line.isEmpty()) return;
        QMutexLocker lock(&m_mutex);
        m_pending.enqueue(std::move(line));
        m_ready.wakeOne();
    }

    void flush()
    {
        QMutexLocker lock(&m_mutex);
        while (!m_pending.isEmpty() || m_writeInProgress) {
            m_idle.wait(&m_mutex);
        }
    }

    ~StreamTelemetryWriter()
    {
        m_mutex.lock();
        m_shutdown.store(true, std::memory_order_release);
        m_ready.wakeAll();
        m_mutex.unlock();
        if (m_thread.joinable())
            m_thread.join();
    }

private:
    StreamTelemetryWriter()
    {
        m_thread = std::thread([this] { writerLoop(); });
    }

    StreamTelemetryWriter(const StreamTelemetryWriter&) = delete;
    StreamTelemetryWriter& operator=(const StreamTelemetryWriter&) = delete;

    void writerLoop()
    {
        QFile file;
        QTextStream out(&file);
        const QString path = resolveStreamTelemetryPath();

        m_mutex.lock();
        while (true) {
            while (m_pending.isEmpty() && !m_shutdown.load(std::memory_order_acquire)) {
                m_ready.wait(&m_mutex);
            }
            if (m_pending.isEmpty() && m_shutdown.load(std::memory_order_acquire)) {
                break;
            }

            QQueue<QString> batch;
            batch.swap(m_pending);
            m_writeInProgress = true;
            m_mutex.unlock();

            if (!file.isOpen()) {
                file.setFileName(path);
                file.open(QIODevice::Append | QIODevice::Text);
            }

            if (file.isOpen()) {
                while (!batch.isEmpty()) {
                    out << batch.dequeue();
                }
                out.flush();
                file.flush();
            }

            m_mutex.lock();
            m_writeInProgress = false;
            if (m_pending.isEmpty()) {
                m_idle.wakeAll();
            }
        }
        m_writeInProgress = false;
        m_idle.wakeAll();
        m_mutex.unlock();

        if (file.isOpen()) {
            out.flush();
            file.flush();
            file.close();
        }
    }

    QMutex m_mutex;
    QQueue<QString> m_pending;
    QWaitCondition m_ready;
    QWaitCondition m_idle;
    bool m_writeInProgress = false;
    std::atomic<bool> m_shutdown{false};
    std::thread m_thread;
};

inline void appendStreamTelemetryLine(QString line)
{
    if (!streamTelemetryEnabled()) return;
    StreamTelemetryWriter::instance().enqueue(std::move(line));
}

inline void flushStreamTelemetry()
{
    if (!streamTelemetryEnabled()) return;
    StreamTelemetryWriter::instance().flush();
}
