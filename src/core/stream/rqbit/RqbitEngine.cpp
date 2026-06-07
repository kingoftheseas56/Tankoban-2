#include "core/stream/rqbit/RqbitEngine.h"
#include "core/stream/rqbit/RqbitProcess.h"

#include <QJsonArray>

namespace tankostream::rqbit {

namespace {
constexpr char kStreamTag[] = "stream";
}

RqbitEngine::RqbitEngine(const QString& downloadDir, QObject* parent)
    : QObject(parent)
    , m_downloadDir(downloadDir)
{
    m_proc   = new RqbitProcess(this);
    m_client = new RqbitClient(this);

    connect(m_proc, &RqbitProcess::ready, this, [this](int port) {
        m_client->setBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(port));
        if (!m_pendingMagnet.isEmpty()) {
            const QString magnet = m_pendingMagnet;
            m_pendingMagnet.clear();
            m_client->addTorrent(QString::fromLatin1(kStreamTag), magnet);
        }
    });
    connect(m_proc, &RqbitProcess::failed, this, [this](const QString& msg) {
        m_pendingMagnet.clear();
        emit streamError(msg);
    });

    connect(m_client, &RqbitClient::torrentAdded, this,
            [this](const QString& /*tag*/, const QString& id, const QJsonArray& files) {
        const int idx = RqbitClient::pickPrimaryVideoFile(files);
        if (idx < 0) {
            emit streamError(tr("No playable video file in this source"));
            return;
        }
        emit streamReady(m_client->streamUrl(id, idx), id, idx);
    });
    connect(m_client, &RqbitClient::requestFailed, this,
            [this](const QString& /*tag*/, const QString& msg) {
        emit streamError(msg);
    });
}

void RqbitEngine::startStream(const QString& magnet)
{
    if (magnet.isEmpty()) {
        emit streamError(tr("Source has no magnet for streaming"));
        return;
    }
    if (!m_proc->isReady()) {
        m_pendingMagnet = magnet;     // replayed on RqbitProcess::ready
        m_proc->start(m_downloadDir);
    } else {
        m_client->addTorrent(QString::fromLatin1(kStreamTag), magnet);
    }
}

void RqbitEngine::stop(const QString& torrentId)
{
    if (!torrentId.isEmpty())
        m_client->deleteTorrent(torrentId);
}

} // namespace tankostream::rqbit
