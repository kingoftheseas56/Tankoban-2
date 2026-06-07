#pragma once

// THEATRE_RQBIT_REVIVAL Phase 1 (2026-06-07) — thin controller that opens the
// floating VideoPlayer on a rqbit HTTP stream URL and reports when the player
// closes so the caller (StreamPage) can tear down the rqbit torrent.
//
// This is a purpose-built replacement for the deleted StreamServerEngine-era
// StreamPlayerController (64213b5^), which was ~490 lines of stream-server
// polling / buffered-range / metadata-stall machinery now irrelevant to rqbit
// (RqbitEngine already resolves add -> file-pick -> stream URL). Same role
// (open the player on a URL, signal teardown), a fraction of the code.

#include <QObject>

class VideoPlayer;

class StreamPlayerController : public QObject {
    Q_OBJECT
public:
    explicit StreamPlayerController(QObject* parent = nullptr);

    // Opens the floating VideoPlayer (found under the owning window — the same
    // lookup the trailer / subtitle-route sites use) on the rqbit stream URL.
    // The ffmpeg sidecar plays HTTP URLs with Range seeking (contract §5).
    void playUrl(const QString& httpUrl, const QString& title);

    bool isActive() const { return m_active; }

signals:
    // The VideoPlayer went idle (user closed / stopped / EOF / open failure).
    // StreamPage connects this to RqbitEngine::stop(torrentId).
    void closed();

private slots:
    void onPlayerIdle();

private:
    VideoPlayer* findPlayer() const;

    bool m_active = false;
};
