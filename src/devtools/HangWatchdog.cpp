#include "devtools/HangWatchdog.h"

#include <chrono>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

HangWatchdog::HangWatchdog(QObject* guiContext)
    : m_guiContext(guiContext)
{
}

HangWatchdog::~HangWatchdog()
{
    stop();
}

void HangWatchdog::start(int thresholdMs, int pollMs, int beatMs)
{
    if (m_run.load(std::memory_order_relaxed))
        return;  // already running

    m_thresholdMs = thresholdMs;
    m_pollMs      = pollMs;
    m_lastBeatMs.store(QDateTime::currentMSecsSinceEpoch(), std::memory_order_relaxed);
    m_hangActive.store(false, std::memory_order_relaxed);

    // Resolve the artifact path up-front (NOT in pollLoop) so the worker never
    // derives a path inside a stall window. Lands beside the exe in out/ (mirrors
    // JsonlEventLog's applicationDirPath() resolver — agents/Monitor look there).
    m_hangPath = QDir(QCoreApplication::applicationDirPath())
                     .absoluteFilePath(QStringLiteral("HANG_DETECTED.json"));
    // A stale artifact from a previous run must not lie to a fresh Monitor.
    QFile::remove(m_hangPath);

    // GUI-side heartbeat: a 16ms QTimer (NOT 0ms — a 0ms QTimer is an idle timer
    // that can be starved under a busy/rendering loop, producing false-positive
    // hangs). The threshold (750ms) is far larger than the beat interval, so
    // normal frame work never trips it.
    m_beatTimer = new QTimer(m_guiContext);
    m_beatTimer->setInterval(beatMs);
    QObject::connect(m_beatTimer, &QTimer::timeout, m_guiContext, [this]() {
        m_lastBeatMs.store(QDateTime::currentMSecsSinceEpoch(), std::memory_order_relaxed);
    });
    m_beatTimer->start();

    m_run.store(true, std::memory_order_relaxed);
    m_thread = std::thread([this]() { pollLoop(); });   // OFF the GUI thread — survives a freeze
}

void HangWatchdog::stop()
{
    if (m_run.exchange(false, std::memory_order_relaxed)) {
        if (m_thread.joinable())
            m_thread.join();
    }
    // Tear down the timer on the GUI thread. By the lifetime contract this object
    // destructs before m_guiContext, so the parented timer is still valid here and
    // we run on its thread — a direct delete is correct (deleteLater would never
    // fire: app.exec() has already returned, so there is no event loop left).
    if (m_beatTimer) {
        m_beatTimer->stop();
        delete m_beatTimer;
        m_beatTimer = nullptr;
    }
}

void HangWatchdog::pollLoop()
{
    // Worker thread. Only reentrant value/IO Qt classes (QFile / QJsonDocument /
    // QDateTime / QDir) are touched here — NEVER m_beatTimer / m_guiContext, which
    // have GUI-thread affinity.
    while (m_run.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(m_pollMs));
        if (!m_run.load(std::memory_order_relaxed))
            break;

        const qint64 now  = QDateTime::currentMSecsSinceEpoch();
        const qint64 last = m_lastBeatMs.load(std::memory_order_relaxed);
        const qint64 age  = now - last;

        if (age > m_thresholdMs && !m_hangActive.load(std::memory_order_relaxed)) {
            // Rising edge (healthy -> stalled): write the artifact exactly once.
            m_hangActive.store(true, std::memory_order_relaxed);
            const QJsonObject o{
                {QStringLiteral("schema"),           QStringLiteral("tankoban.hang.v1")},
                {QStringLiteral("detectedAt"),       QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                {QStringLiteral("stalledMs"),        double(age)},
                {QStringLiteral("thresholdMs"),      double(m_thresholdMs)},
                {QStringLiteral("pid"),              double(QCoreApplication::applicationPid())},
                {QStringLiteral("lastResponsiveAt"), QDateTime::fromMSecsSinceEpoch(last, Qt::UTC).toString(Qt::ISODateWithMs)},
            };
            QFile f(m_hangPath);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
                f.close();
            }
        } else if (age <= m_thresholdMs && m_hangActive.load(std::memory_order_relaxed)) {
            // Recovery edge (stalled -> healthy): remove so a stale file never lies.
            m_hangActive.store(false, std::memory_order_relaxed);
            QFile::remove(m_hangPath);
        }
    }
}
