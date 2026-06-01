// In-Process Player POC — Phase 1 (video) / Phase 2 (audio).
//
// Decodes a local file INSIDE the main app (no sidecar, no cross-process
// hand-off) and publishes BGRA frames into an in-process named shared-memory
// ring that the existing ShmFrameReader/FrameCanvas consume unchanged. The
// named segment is created + read within this one process, so there is no
// process boundary and no D3D11 NT-handle share (the bit broken on UHD 620).
//
// Self-contained decode loop (libavformat/libavcodec/libswscale) — deliberately
// does NOT reuse the sidecar's VideoDecoder, to avoid its subtitle/HDR/d3d11
// dependency tail. Local file only; no subs/HDR/seek/tracks. Entirely gated by
// TANKOBAN_INPROCESS_POC.
#pragma once
#ifdef TANKOBAN_INPROCESS_POC

#include <QObject>
#include <QString>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

class FrameRingWriter;

class InProcessPlayer : public QObject {
    Q_OBJECT
public:
    explicit InProcessPlayer(QObject* parent = nullptr);
    ~InProcessPlayer() override;

    // Probe the file for video dimensions, create the in-process SHM ring, and
    // start the decode thread. Returns true if the ring is ready (so the caller
    // can immediately attach a ShmFrameReader to shmName()). Non-blocking w.r.t.
    // actual decoding (frames stream in on the decode thread).
    bool openFile(const QString& path);
    void stop();

    // Valid after a successful openFile(): identifies the in-process ring so
    // VideoPlayer can point its ShmFrameReader at it.
    QString shmName()   const { return m_shmName; }
    int     slotCount() const { return m_slotCount; }
    int     slotBytes() const { return m_slotBytes; }

private:
    void decodeThreadFunc(std::string path);
    void teardownShm();

    QString m_shmName;
    int     m_slotCount = 4;
    int     m_slotBytes = 0;
    int     m_width  = 0;
    int     m_height = 0;

#ifdef _WIN32
    void*   m_hMapFile = nullptr;   // HANDLE
#endif
    void*   m_mapView  = nullptr;   // mapped base pointer

    std::unique_ptr<FrameRingWriter> m_ringWriter;

    std::thread       m_decodeThread;
    std::atomic<bool> m_stop{false};
};

#endif // TANKOBAN_INPROCESS_POC
