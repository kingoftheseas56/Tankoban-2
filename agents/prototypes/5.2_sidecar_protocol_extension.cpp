// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 3, Batch 5.2 (Sidecar Protocol Extension)
// Date: 2026-04-14
// References consulted:
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:238
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:243
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:244
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:245
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:246
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:247
//   - C:/Users/Suprabha/Desktop/Tankoban 2/STREAM_PARITY_TODO.md:248
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/SidecarProcess.h:31
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/SidecarProcess.h:33
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/SidecarProcess.h:35
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/SidecarProcess.h:36
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/SidecarProcess.cpp:154
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/SidecarProcess.cpp:169
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/SidecarProcess.cpp:183
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/SidecarProcess.cpp:192
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/SidecarProcess.cpp:293
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/VideoPlayer.cpp:686
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/VideoPlayer.cpp:695
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/VideoPlayer.cpp:709
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/VideoPlayer.cpp:721
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/VideoPlayer.cpp:1581
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/VideoPlayer.cpp:1635
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/VideoPlayer.cpp:1644
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/TrackPopover.h:35
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/TrackPopover.h:37
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/TrackPopover.h:38
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/TrackPopover.cpp:276
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/TrackPopover.cpp:331
//   - C:/Users/Suprabha/Desktop/Tankoban 2/src/ui/player/TrackPopover.cpp:339
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/chat.md:9050
//   - C:/Users/Suprabha/Desktop/Tankoban 2/agents/chat.md:9052
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-video-master/stremio-video-master/src/withHTMLSubtitles/withHTMLSubtitles.js:48
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-video-master/stremio-video-master/src/withHTMLSubtitles/withHTMLSubtitles.js:50
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-video-master/stremio-video-master/src/withHTMLSubtitles/withHTMLSubtitles.js:51
//   - C:/Users/Suprabha/Downloads/Stremio Reference/stremio-video-master/stremio-video-master/src/withHTMLSubtitles/withHTMLSubtitles.js:52
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================
// Prototype by Agent 7 (Codex), 2026-04-14. For Agent 3, Batch 5.2.
// Reference only - domain agent implements their own version.
// Do not compile. Do not include from src/.

#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QScopedPointer>
#include <QSharedPointer>
#include <QString>
#include <QTemporaryFile>
#include <QtGlobal>
#include <QUrl>
#include <QVariantMap>

namespace tankoban::prototype::sidecar52 {

struct SidecarSubtitleTrack {
    int index = -1;      // API surface for 5.2 (index-based, -1 = off)
    QString sidecarId;   // existing set_tracks uses string IDs
    QString lang;
    QString title;
    bool external = false;
};

class SidecarProcess : public QObject {
    Q_OBJECT
public:
    explicit SidecarProcess(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    // Existing shipped commands/events in SidecarProcess.h/.cpp.
    int sendSetTracks(const QString& audioId, const QString& subId);
    int sendSetSubDelay(double delayMs);
    int sendSetSubStyle(int fontSize, int marginV, bool outline);
    int sendLoadExternalSub(const QString& path);

signals:
    void tracksChanged(const QJsonArray& audio,
                       const QJsonArray& subtitle,
                       const QString& activeAudioId,
                       const QString& activeSubId);
    void subDelayChanged(double delayMs);
    void errorOccurred(const QString& message);
};

class SidecarSubtitleProtocolBridge : public QObject {
    Q_OBJECT

public:
    explicit SidecarSubtitleProtocolBridge(SidecarProcess* sidecar,
                                           QObject* parent = nullptr)
        : QObject(parent)
        , m_sidecar(sidecar)
        , m_nam(new QNetworkAccessManager(this))
    {
        Q_ASSERT(m_sidecar);

        connect(m_sidecar, &SidecarProcess::tracksChanged, this,
            [this](const QJsonArray& /*audio*/,
                   const QJsonArray& subtitle,
                   const QString& /*activeAudioId*/,
                   const QString& activeSubId) {
                cacheSubtitleTracks(subtitle, activeSubId);
            });

        connect(m_sidecar, &SidecarProcess::subDelayChanged, this,
            [this](double ms) {
                m_delayMs = static_cast<int>(ms);
                emit subtitleDelayChanged(m_delayMs);
            });
    }

    // -----------------------------------------------------------------
    // 5.2 API surface requested by TODO (index/offset/size semantics)
    // -----------------------------------------------------------------

    // listSubtitleTracks -> {tracks:[{index,lang,title}]}
    // Prebuilt-sidecar fallback: derive from last tracks_changed subtitle array.
    QList<SidecarSubtitleTrack> listSubtitleTracks() const
    {
        return m_tracks;
    }

    // setSubtitleTrack {index}
    // Prebuilt-sidecar fallback: map index -> sidecar string id and call set_tracks.
    void setSubtitleTrack(int index)
    {
        if (index < 0) {
            m_sidecar->sendSetTracks(QString(), QStringLiteral("off"));
            m_activeIndex = -1;
            emit subtitleTrackApplied(-1);
            return;
        }

        for (const SidecarSubtitleTrack& t : m_tracks) {
            if (t.index != index) {
                continue;
            }
            m_sidecar->sendSetTracks(QString(), t.sidecarId);
            m_activeIndex = index;
            emit subtitleTrackApplied(index);
            return;
        }

        emit protocolWarning(QStringLiteral("Unknown subtitle index: %1").arg(index));
    }

    // setSubtitleUrl {url, offset_ms, delay_ms}
    // Prebuilt-sidecar fallback:
    //   1) URL/file -> local temp subtitle path
    //   2) sendLoadExternalSub(path)
    //   3) apply delay and offset through existing style/delay commands
    void setSubtitleUrl(const QUrl& url, int offsetMs, int delayMs)
    {
        m_offsetPx = offsetMs;
        m_delayMs = delayMs;

        if (!url.isValid() || url.scheme().isEmpty()) {
            emit protocolWarning(QStringLiteral("Invalid subtitle URL"));
            return;
        }

        if (url.isLocalFile()) {
            applyLocalSubtitleFile(url.toLocalFile());
            return;
        }

        if (!isSubtitleExtension(url.path())) {
            emit protocolWarning(QStringLiteral("Unsupported subtitle extension"));
            return;
        }

        // Download to temp then hand local path to existing sidecar command.
        QNetworkRequest req(url);
        req.setTransferTimeout(15000);

        QPointer<SidecarSubtitleProtocolBridge> guard(this);
        auto* reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, guard, reply]() {
            const QScopedPointer<QNetworkReply, QScopedPointerDeleteLater> cleanup(reply);
            if (!guard) {
                return;
            }
            if (reply->error() != QNetworkReply::NoError) {
                emit protocolWarning(QStringLiteral("Subtitle download failed: %1")
                    .arg(reply->errorString()));
                return;
            }

            const QByteArray data = reply->readAll();
            if (data.isEmpty()) {
                emit protocolWarning(QStringLiteral("Subtitle download returned empty payload"));
                return;
            }

            auto temp = QSharedPointer<QTemporaryFile>::create(
                QStringLiteral("tankoban_subtitle_XXXXXX.srt"));
            temp->setAutoRemove(false);
            if (!temp->open()) {
                emit protocolWarning(QStringLiteral("Failed to stage subtitle temp file"));
                return;
            }
            temp->write(data);
            temp->flush();
            const QString path = temp->fileName();
            temp->close();

            m_tempSubtitleFiles.push_back(temp); // keep alive for playback duration
            applyLocalSubtitleFile(path);
        });
    }

    // setSubtitleOffset {pixel_offset_y}
    // Fallback: map offset to subtitle margin in existing set_sub_style command.
    void setSubtitleOffset(int pixelOffsetY)
    {
        m_offsetPx = pixelOffsetY;
        pushStyle();
        emit subtitleOffsetChanged(pixelOffsetY);
    }

    // setSubtitleDelay {ms}
    // Directly supported today by sendSetSubDelay.
    void setSubtitleDelay(int ms)
    {
        m_delayMs = ms;
        m_sidecar->sendSetSubDelay(ms);
        emit subtitleDelayChanged(ms);
    }

    // setSubtitleSize {scale}
    // Fallback: map scale -> font size and call existing set_sub_style.
    void setSubtitleSize(double scale)
    {
        m_sizeScale = qBound(0.5, scale, 3.0);
        pushStyle();
        emit subtitleSizeChanged(m_sizeScale);
    }

signals:
    void subtitleTracksListed(const QList<SidecarSubtitleTrack>& tracks, int activeIndex);
    void subtitleTrackApplied(int index);
    void subtitleDelayChanged(int ms);
    void subtitleOffsetChanged(int pixelOffsetY);
    void subtitleSizeChanged(double scale);
    void protocolWarning(const QString& message);

private:
    static bool isSubtitleExtension(const QString& path)
    {
        const QString ext = QFileInfo(path).suffix().toLower();
        return ext == QStringLiteral("srt")
            || ext == QStringLiteral("vtt")
            || ext == QStringLiteral("ass")
            || ext == QStringLiteral("ssa")
            || ext == QStringLiteral("sub");
    }

    static QString bestTrackTitle(const QJsonObject& track)
    {
        const QString title = track.value(QStringLiteral("title")).toString().trimmed();
        if (!title.isEmpty()) {
            return title;
        }

        const QString lang = track.value(QStringLiteral("lang")).toString().trimmed();
        if (!lang.isEmpty()) {
            return lang.toUpper();
        }

        return QStringLiteral("Subtitle");
    }

    void cacheSubtitleTracks(const QJsonArray& subtitle, const QString& activeSubId)
    {
        m_tracks.clear();

        int nextIndex = 0;
        for (const QJsonValue& v : subtitle) {
            const QJsonObject t = v.toObject();
            const QString id = t.value(QStringLiteral("id")).toString().trimmed();
            if (id.isEmpty()) {
                continue;
            }

            SidecarSubtitleTrack row;
            row.index = nextIndex++;
            row.sidecarId = id;
            row.lang = t.value(QStringLiteral("lang")).toString().trimmed();
            row.title = bestTrackTitle(t);
            row.external = id.startsWith(QStringLiteral("ext:"), Qt::CaseInsensitive);

            if (id == activeSubId) {
                m_activeIndex = row.index;
            }

            m_tracks.push_back(row);
        }

        emit subtitleTracksListed(m_tracks, m_activeIndex);
    }

    void applyLocalSubtitleFile(const QString& path)
    {
        m_sidecar->sendLoadExternalSub(path);
        m_sidecar->sendSetSubDelay(m_delayMs);
        pushStyle();
    }

    void pushStyle()
    {
        // Map desired 5.2 controls onto current set_sub_style(font,margin,outline).
        const int baseFont = 24;
        const int fontSize = qBound(14, static_cast<int>(baseFont * m_sizeScale), 72);

        // Existing sidecar style API expects a bottom margin value.
        // Positive offset pushes subtitles upward in current overlay layout.
        const int baseMargin = 40;
        const int margin = qBound(0, baseMargin + m_offsetPx, 200);

        const bool outline = true;
        m_sidecar->sendSetSubStyle(fontSize, margin, outline);
    }

    QPointer<SidecarProcess> m_sidecar;
    QNetworkAccessManager* m_nam = nullptr;

    QList<SidecarSubtitleTrack> m_tracks;
    int m_activeIndex = -1;

    int m_delayMs = 0;
    int m_offsetPx = 0;
    double m_sizeScale = 1.0;

    // Keeps downloaded subtitle temp files alive while playback is active.
    QList<QSharedPointer<QTemporaryFile>> m_tempSubtitleFiles;
};

} // namespace tankoban::prototype::sidecar52

// -----------------------------------------------------------------
// Integration notes for Agent 3 (Batch 5.2)
// -----------------------------------------------------------------
//
// 1) If sidecar binary source becomes editable in-repo:
//    - Add true JSON commands in the executable command handler:
//      listSubtitleTracks, setSubtitleTrack, setSubtitleUrl,
//      setSubtitleOffset, setSubtitleDelay, setSubtitleSize.
//    - Keep current snake_case commands as compatibility aliases.
//
// 2) If sidecar remains prebuilt (current repo state):
//    - Implement the bridge above in Qt-side code only.
//    - listSubtitleTracks derives from tracks_changed subtitle payload.
//    - setSubtitleTrack maps index -> sendSetTracks("", id) or off.
//    - setSubtitleUrl downloads remote URL to temp file then sendLoadExternalSub(path).
//    - setSubtitleDelay maps directly to sendSetSubDelay.
//    - setSubtitleOffset/setSubtitleSize map to sendSetSubStyle until a native
//      sidecar offset/size API is available.
//
// 3) Batch 5.3 can consume this bridge plus 5.1 external subtitle results to
//    present a unified subtitle menu (embedded + addon + local file + controls).
