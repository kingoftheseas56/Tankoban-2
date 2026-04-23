#pragma once

#include <QJsonArray>
#include <QMenu>
#include <QStringList>
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
    // VIDEO_PLAYER_FIX Batch 3.1 — drives the context-menu check mark on
    // the Always-on-Top action. VideoPlayer mirrors m_alwaysOnTop into
    // this field before building the menu.
    bool alwaysOnTop = false;
    // VIDEO_PLAYER_FIX Batch 3.3 — drives the PiP entry's check mark + its
    // "Picture-in-Picture" / "Exit Picture-in-Picture" label swap.
    bool inPip       = false;
    // VIDEO_PLAYER_FIX Batch 4.2 — recent files for the Recent submenu.
    // Most-recent-first order; mix of local absolute paths and URL strings.
    // VideoPlayer populates from QSettings at contextMenuEvent time.
    QStringList recentFiles;
    // VIDEO_PLAYER_FIX Batch 7.1 — drives the check mark on the Show
    // Stats entry. VideoPlayer mirrors m_showStats.
    bool showStats   = false;
    // Current aspect ratio override string ("original"/"4:3"/"16:9"/
    // "2.35:1"/"1.85:1"). Drives the check mark on the Aspect Ratio
    // submenu. "original" matches m_forcedAspect == 0.0.
    QString currentAspect = QStringLiteral("original");
    // Current crop target string ("none"/"16:9"/"2.35:1"/"2.39:1"/
    // "1.85:1"/"4:3"). Drives the check mark on the Crop submenu.
    // "none" matches m_cropAspect == 0.0 (no crop). Crop zooms the video
    // to eliminate baked letterbox / pillarbox strips — orthogonal to
    // Aspect Ratio override which changes viewport fit shape.
    QString currentCrop = QStringLiteral("none");
    int currentZoomPct = 100;
};

class VideoContextMenu {
public:
    enum ZoomLevel {
        Z100 = 100,
        Z105 = 105,
        Z110 = 110,
        Z115 = 115,
        Z120 = 120,
    };

    enum ActionType {
        TogglePlayPause,
        ToggleMute,
        SetSpeed,           // data = double speed
        SetAspectRatio,     // data = QString "original"/"4:3"/"16:9"/"2.35:1"/"1.85:1"
        SetCrop,            // data = QString "none"/"16:9"/"2.35:1"/"2.39:1"/"1.85:1"/"4:3"
        SetZoom,            // data = int zoom percent
        ToggleFullscreen,
        ToggleAlwaysOnTop,  // VIDEO_PLAYER_FIX Batch 3.1
        TakeSnapshot,       // VIDEO_PLAYER_FIX Batch 3.2
        TogglePip,          // VIDEO_PLAYER_FIX Batch 3.3
        OpenUrl,            // VIDEO_PLAYER_FIX Batch 4.1
        OpenRecent,         // VIDEO_PLAYER_FIX Batch 4.2 — data = QString path/URL
        ClearRecent,        // VIDEO_PLAYER_FIX Batch 4.2
        OpenKeybindings,    // VIDEO_PLAYER_FIX Batch 6.1
        ToggleStats,        // VIDEO_PLAYER_FIX Batch 7.1
        SetAudioTrack,      // data = int id
        SetSubtitleTrack,   // data = int id (0 = off)
        LoadExternalSub,
        OpenSubtitleMenu,   // Batch 5.3 — opens SubtitleMenu (Tankostream subs)
        ToggleDeinterlace,
        ToggleNormalize,
        OpenTracks,
        OpenPlaylist,
        BackToLibrary
    };

    static QMenu* build(const VideoContextData& data, QWidget* parent,
                        std::function<void(ActionType, QVariant)> callback);
};
