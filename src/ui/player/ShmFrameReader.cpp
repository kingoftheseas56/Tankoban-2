#include "ui/player/ShmFrameReader.h"

#include "core/DebugLogBuffer.h"

#include <QDebug>

static void debugLog(const QString& msg) {
    // REPO_HYGIENE P1.2 (2026-04-26): routed through DebugLogBuffer instead
    // of writing to hardcoded C:/Users/Suprabha/.../_player_debug.txt.
    DebugLogBuffer::instance().info("shm-frame-reader", msg);
}

#ifdef Q_OS_WIN
#include <windows.h>
#endif

bool ShmFrameReader::attach(const QString& shmName, int slotCount, int slotBytes)
{
    detach();

    m_slotCount = slotCount;
    m_slotBytes = slotBytes;

    size_t totalSize = static_cast<size_t>(HEADER_SIZE)
                     + static_cast<size_t>(slotCount) * SLOT_META_SIZE
                     + static_cast<size_t>(slotCount) * slotBytes;

#ifdef Q_OS_WIN
    // Open the named shared memory created by the sidecar
    std::wstring wname = shmName.toStdWString();
    debugLog("[SHM] attach: name=" + shmName + " slots=" + QString::number(slotCount)
             + " slotBytes=" + QString::number(slotBytes) + " totalSize=" + QString::number(totalSize));

    m_hMapFile = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, wname.c_str());
    if (!m_hMapFile) {
        DWORD err = GetLastError();
        debugLog("[SHM] OpenFileMapping FAILED, error=" + QString::number(err));
        return false;
    }

    m_data = static_cast<uint8_t*>(
        MapViewOfFile(m_hMapFile, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, totalSize));
    if (!m_data) {
        DWORD err = GetLastError();
        debugLog("[SHM] MapViewOfFile FAILED, error=" + QString::number(err));
        CloseHandle(m_hMapFile);
        m_hMapFile = nullptr;
        return false;
    }
    debugLog("[SHM] attach SUCCESS, data=" + QString::number(reinterpret_cast<uintptr_t>(m_data), 16));
#else
    m_shm.setNativeKey(shmName);
    if (!m_shm.attach(QSharedMemory::ReadWrite)) {
        qWarning() << "ShmFrameReader: cannot attach" << m_shm.errorString();
        return false;
    }
    m_data = static_cast<uint8_t*>(m_shm.data());
#endif

    m_lastFrameId = 0;
    return true;
}

void ShmFrameReader::detach()
{
#ifdef Q_OS_WIN
    if (m_data) {
        UnmapViewOfFile(m_data);
        m_data = nullptr;
    }
    if (m_hMapFile) {
        CloseHandle(m_hMapFile);
        m_hMapFile = nullptr;
    }
#else
    if (m_shm.isAttached())
        m_shm.detach();
    m_data = nullptr;
#endif
    m_lastFrameId = 0;
}

ShmFrameReader::Frame ShmFrameReader::readLatest()
{
    Frame f;
    if (!m_data) return f;

    // Find newest valid frame across all slots
    uint64_t bestId = 0;
    int bestSlot = -1;

    for (int i = 0; i < m_slotCount; ++i) {
        int metaOff = HEADER_SIZE + i * SLOT_META_SIZE;

        uint32_t valid;
        std::memcpy(&valid, m_data + metaOff + SM_VALID, 4);
        if (valid != 1) continue;

        uint64_t fid;
        std::memcpy(&fid, m_data + metaOff + SM_FRAME_ID, 8);
        if (fid > bestId) {
            bestId = fid;
            bestSlot = i;
        }
    }

    if (bestSlot < 0 || bestId <= m_lastFrameId)
        return f;  // no new frame

    int metaOff = HEADER_SIZE + bestSlot * SLOT_META_SIZE;
    size_t dataOff = static_cast<size_t>(HEADER_SIZE)
                   + static_cast<size_t>(m_slotCount) * SLOT_META_SIZE
                   + static_cast<size_t>(bestSlot) * m_slotBytes;

    std::memcpy(&f.frameId, m_data + metaOff + SM_FRAME_ID, 8);
    std::memcpy(&f.ptsUs,   m_data + metaOff + SM_PTS_US, 8);
    std::memcpy(&f.width,   m_data + metaOff + SM_WIDTH, 4);
    std::memcpy(&f.height,  m_data + metaOff + SM_HEIGHT, 4);
    std::memcpy(&f.stride,  m_data + metaOff + SM_STRIDE, 4);

    // Verify valid flag again (torn write detection)
    uint32_t validAfter;
    std::memcpy(&validAfter, m_data + metaOff + SM_VALID, 4);
    if (validAfter != 1)
        return f;

    f.pixels = m_data + dataOff;
    f.valid  = true;
    m_lastFrameId = f.frameId;

    return f;
}

ShmFrameReader::Frame ShmFrameReader::readBestForClock(int64_t clockUs, int64_t toleranceUs)
{
    Frame f;
    if (!m_data) return f;

    // Find the newest valid frame whose PTS is at or before clock + tolerance.
    // This prevents showing frames "from the future" relative to audio.
    uint64_t bestId = 0;
    int bestSlot = -1;

    for (int i = 0; i < m_slotCount; ++i) {
        int metaOff = HEADER_SIZE + i * SLOT_META_SIZE;

        uint32_t valid;
        std::memcpy(&valid, m_data + metaOff + SM_VALID, 4);
        if (valid != 1) continue;

        uint64_t fid;
        std::memcpy(&fid, m_data + metaOff + SM_FRAME_ID, 8);
        if (fid <= m_lastFrameId) continue;  // already displayed

        int64_t ptsUs;
        std::memcpy(&ptsUs, m_data + metaOff + SM_PTS_US, 8);
        if (ptsUs > clockUs + toleranceUs) continue;  // frame is from the future

        if (fid > bestId) {
            bestId = fid;
            bestSlot = i;
        }
    }

    if (bestSlot < 0)
        return f;  // no suitable frame

    int metaOff = HEADER_SIZE + bestSlot * SLOT_META_SIZE;
    size_t dataOff = static_cast<size_t>(HEADER_SIZE)
                   + static_cast<size_t>(m_slotCount) * SLOT_META_SIZE
                   + static_cast<size_t>(bestSlot) * m_slotBytes;

    std::memcpy(&f.frameId, m_data + metaOff + SM_FRAME_ID, 8);
    std::memcpy(&f.ptsUs,   m_data + metaOff + SM_PTS_US, 8);
    std::memcpy(&f.width,   m_data + metaOff + SM_WIDTH, 4);
    std::memcpy(&f.height,  m_data + metaOff + SM_HEIGHT, 4);
    std::memcpy(&f.stride,  m_data + metaOff + SM_STRIDE, 4);

    // Verify valid flag again (torn write detection)
    uint32_t validAfter;
    std::memcpy(&validAfter, m_data + metaOff + SM_VALID, 4);
    if (validAfter != 1)
        return f;

    f.pixels = m_data + dataOff;
    f.valid  = true;
    m_lastFrameId = f.frameId;

    return f;
}

int64_t ShmFrameReader::readClockUs() const
{
    if (!m_data) return 0;
    int64_t v;
    std::memcpy(&v, m_data + OFF_CLOCK_US, 8);
    return v;
}

void ShmFrameReader::writeConsumerFid(uint64_t fid)
{
    if (!m_data) return;
    std::memcpy(m_data + OFF_CONSUMER_FID, &fid, 8);
}
