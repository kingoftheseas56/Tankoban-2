#pragma once

#include <QJsonArray>
#include <QMenu>
#include <QVariant>
#include <functional>

struct VideoContextData {
    bool paused = false;
    bool muted = false;
    double currentSpeed = 1.0;
    QJsonArray audioTracks;      // [{id, lang, title}, ...]
    QJsonArray subtitleTracks;   // [{id, lang, title}, ...]
    int currentAudioId = -1;
    int currentSubId = 0;
    bool subsVisible = true;
    bool deinterlace = false;
    bool normalize = false;
};

class VideoContextMenu {
public:
    enum ActionType {
        TogglePlayPause,
        ToggleMute,
        SetSpeed,           // data = double speed
        SetAspectRatio,     // data = QString "original"/"4:3"/"16:9"/"2.35:1"/"1.85:1"
        ToggleFullscreen,
        SetAudioTrack,      // data = int id
        SetSubtitleTrack,   // data = int id (0 = off)
        LoadExternalSub,
        ToggleDeinterlace,
        ToggleNormalize,
        OpenTracks,
        OpenPlaylist,
        BackToLibrary
    };

    static QMenu* build(const VideoContextData& data, QWidget* parent,
                        std::function<void(ActionType, QVariant)> callback);
};
