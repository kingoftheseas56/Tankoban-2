// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 3, Batch 1.1 (PLAYER_LIFECYCLE)
// Date: 2026-04-16
// References consulted:
//   - PLAYER_LIFECYCLE_FIX_TODO.md:74
//   - agents/audits/tankostream_session_lifecycle_2026-04-15.md:112
//   - src/ui/player/SidecarProcess.h:42
//   - src/ui/player/SidecarProcess.cpp:138
//   - src/ui/player/SidecarProcess.cpp:339
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================

// Scope:
//   Add one sessionId gate at the top of SidecarProcess::processLine().
//   The native sidecar already includes sessionId on events and already
//   uses sessionId as an incoming-command guard. This mirrors that rule
//   on the Qt event consumer side without changing signal signatures.
//
// Concrete from current src:
//   - sendOpen() regenerates m_sessionId before every open.
//   - sendCommand() stamps m_sessionId into every command.
//   - processLine() currently dispatches only by event name.
//
// Guess:
//   - The process-global allowlist may need extension if Agent 3 finds
//     another event that is truly process-scoped. Keep the default tight.

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

namespace agent7_player_lifecycle_batch_1_1 {

static bool isProcessGlobalEvent(const QString& name)
{
    static const QSet<QString> kProcessGlobal = {
        QStringLiteral("ready"),
        QStringLiteral("closed"),
        QStringLiteral("shutdown_ack"),
        QStringLiteral("version"),
        QStringLiteral("process_error"),
    };
    return kProcessGlobal.contains(name);
}

// Prototype replacement for the top of SidecarProcess::processLine().
// Keep the existing dispatch body after the session gate.
void SidecarProcess::processLine(const QByteArray& line)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError)
        return;

    const QJsonObject obj = doc.object();
    if (obj.value(QStringLiteral("type")).toString() != QStringLiteral("evt"))
        return;

    const QString name = obj.value(QStringLiteral("name")).toString();

    // Events tied to a decoder/open session must match the current open.
    // Empty sessionId is tolerated for legacy sidecar binaries so Agent 3
    // can ship the Qt-side filter independently of a native rebuild.
    if (!isProcessGlobalEvent(name)) {
        const QString eventSessionId =
            obj.value(QStringLiteral("sessionId")).toString();

        if (!eventSessionId.isEmpty() && eventSessionId != m_sessionId) {
            debugLog(QStringLiteral("[Sidecar] drop stale event: %1 eventSid=%2 currentSid=%3")
                         .arg(name, eventSessionId, m_sessionId));
            return;
        }
    }

    const QJsonObject payload = obj.value(QStringLiteral("payload")).toObject();
    debugLog("[Sidecar] RECV: " + name);

    if (name == QStringLiteral("ready")) {
        emit ready();
    } else if (name == QStringLiteral("first_frame")) {
        emit firstFrame(payload);
    } else if (name == QStringLiteral("time_update")) {
        emit timeUpdate(payload.value(QStringLiteral("positionSec")).toDouble(),
                        payload.value(QStringLiteral("durationSec")).toDouble());
    } else if (name == QStringLiteral("state_changed")) {
        emit stateChanged(payload.value(QStringLiteral("state")).toString());
    } else if (name == QStringLiteral("tracks_changed")) {
        const QJsonArray subtitleArr =
            payload.value(QStringLiteral("subtitle")).toArray();
        const QString activeSubId =
            payload.value(QStringLiteral("active_sub_id")).toString();
        updateSubtitleCache(subtitleArr, activeSubId);
        emit tracksChanged(
            payload.value(QStringLiteral("audio")).toArray(),
            subtitleArr,
            payload.value(QStringLiteral("active_audio_id")).toString(),
            activeSubId);
    } else if (name == QStringLiteral("eof")) {
        emit endOfFile();
    } else if (name == QStringLiteral("error")) {
        const QString code = payload.value(QStringLiteral("code")).toString();
        const QString msg = payload.value(QStringLiteral("message")).toString();
        if (code == QStringLiteral("NOT_IMPLEMENTED")) {
            debugLog("[Sidecar] NOT_IMPLEMENTED (stale sidecar, rebuild required): " + msg);
        } else {
            emit errorOccurred(msg);
        }
    }

    // Agent 3: leave the rest of the existing dispatch exactly as it is:
    // decode_error, subtitle_text, filters_changed, d3d11_texture,
    // media_info, closed, and any newer events.
}

} // namespace agent7_player_lifecycle_batch_1_1
