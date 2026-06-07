#include "ui/pages/stream/StreamPlayerController.h"
#include "ui/player/VideoPlayer.h"

#include <QWidget>

StreamPlayerController::StreamPlayerController(QObject* parent)
    : QObject(parent)
{
}

VideoPlayer* StreamPlayerController::findPlayer() const
{
    auto* ownerWidget = qobject_cast<QWidget*>(parent());
    QWidget* top = ownerWidget ? ownerWidget->window() : nullptr;
    return top ? top->findChild<VideoPlayer*>() : nullptr;
}

void StreamPlayerController::playUrl(const QString& httpUrl, const QString& title)
{
    VideoPlayer* player = findPlayer();
    if (!player) {
        // No player to drive — nothing opened, so signal teardown immediately
        // and let the caller drop the just-added rqbit torrent.
        emit closed();
        return;
    }
    // Report teardown when the player next goes idle. Member-function slot +
    // UniqueConnection dedupes across repeated playUrl calls (the connection
    // persists for the player's lifetime; m_active gates spurious idles from
    // unrelated playback, e.g. a downloaded-file close).
    connect(player, &VideoPlayer::playerIdle,
            this, &StreamPlayerController::onPlayerIdle, Qt::UniqueConnection);
    m_active = true;
    player->openFile(httpUrl, {}, 0, 0.0, title);
}

void StreamPlayerController::onPlayerIdle()
{
    if (!m_active)
        return;
    m_active = false;
    emit closed();
}
