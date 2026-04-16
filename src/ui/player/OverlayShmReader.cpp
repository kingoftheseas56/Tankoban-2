#include "ui/player/OverlayShmReader.h"

#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <atomic>
#include <cstring>

static void debugLog(const QString& msg) {
    QFile f("C:/Users/Suprabha/Desktop/Tankoban 2/_player_debug.txt");
    f.open(QIODevice::Append | QIODevice::Text);
    QTextStream s(&f);
    s << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << " " << msg << "\n";
}

bool OverlayShmReader::attach(const QString& shmName, int width, int height)
{
    detach();

    if (shmName.isEmpty() || width <= 0 || height <= 0) return false;

    m_width  = width;
    m_height = height;

    const size_t totalSize = static_cast<size_t>(HDR_SIZE) +
                              static_cast<size_t>(width) * height * 4;

#ifdef Q_OS_WIN
    std::wstring wname = shmName.toStdWString();
    debugLog(QStringLiteral("[OverlayShmReader] attach: name=") + shmName +
             QStringLiteral(" %1x%2 total=%3").arg(width).arg(height).arg(totalSize));

    m_hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, wname.c_str());
    if (!m_hMap) {
        DWORD err = GetLastError();
        debugLog(QStringLiteral("[OverlayShmReader] OpenFileMapping FAILED err=") +
                 QString::number(err));
        return false;
    }

    m_data = static_cast<uint8_t*>(
        MapViewOfFile(m_hMap, FILE_MAP_READ, 0, 0, totalSize));
    if (!m_data) {
        DWORD err = GetLastError();
        debugLog(QStringLiteral("[OverlayShmReader] MapViewOfFile FAILED err=") +
                 QString::number(err));
        CloseHandle(m_hMap);
        m_hMap = nullptr;
        return false;
    }
    debugLog(QStringLiteral("[OverlayShmReader] attach SUCCESS"));
    return true;
#else
    return false;
#endif
}

void OverlayShmReader::detach()
{
#ifdef Q_OS_WIN
    if (m_data) {
        UnmapViewOfFile(m_data);
        m_data = nullptr;
    }
    if (m_hMap) {
        CloseHandle(m_hMap);
        m_hMap = nullptr;
    }
#endif
    m_width  = 0;
    m_height = 0;
}

OverlayShmReader::Frame OverlayShmReader::read()
{
    Frame f;
    if (!m_data) return f;

    // Counter is the "publish" flag — writer bumps it last (memory_order_release)
    // after payload + valid are settled. Read with acquire so subsequent reads
    // of valid + payload see the writer's completed state.
    auto* counter = reinterpret_cast<std::atomic<uint64_t>*>(m_data + HDR_COUNTER);
    f.counter = counter->load(std::memory_order_acquire);

    uint32_t valid = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::memcpy(&width, m_data + HDR_WIDTH, 4);
    std::memcpy(&height, m_data + HDR_HEIGHT, 4);
    std::memcpy(&valid, m_data + HDR_VALID, 4);
    f.valid  = (valid != 0);
    f.width  = static_cast<int>(width);
    f.height = static_cast<int>(height);
    if (f.width <= 0 || f.height <= 0) {
        f.width = m_width;
        f.height = m_height;
    }
    f.bgra   = m_data + HDR_SIZE;
    return f;
}
