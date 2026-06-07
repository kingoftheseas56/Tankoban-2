#pragma once

// THEATRE_RQBIT_REVIVAL Phase 1 (2026-06-07) — orchestrator over RqbitProcess +
// RqbitClient. The only surface the UI touches: hand it a magnet, get back a
// stream URL (watch-while-download). Modeled on the deleted StreamServerEngine
// (64213b5^). libtorrent (TorrentClient) still owns offline downloads — rqbit
// is additive and streaming-only in Phase 1.

#include <QObject>
#include <QString>
#include "core/stream/rqbit/RqbitClient.h"

namespace tankostream::rqbit {

class RqbitProcess;

class RqbitEngine : public QObject {
    Q_OBJECT
public:
    explicit RqbitEngine(const QString& downloadDir, QObject* parent = nullptr);

    // Lazy-starts the subprocess on first call, adds the magnet, selects the
    // primary video file, and emits streamReady (or streamError). Phase 1
    // streams one title at a time; a second call before ready replaces the
    // pending magnet.
    void startStream(const QString& magnet);

    // Tears down a streamed torrent (Phase 1: delete = remove staging files).
    void stop(const QString& torrentId);

signals:
    void streamReady(const QString& streamUrl, const QString& torrentId, int fileIndex);
    void streamError(const QString& message);

private:
    RqbitProcess* m_proc = nullptr;
    RqbitClient*  m_client = nullptr;
    QString m_downloadDir;
    QString m_pendingMagnet;   // held while the process warms up
};

} // namespace tankostream::rqbit
