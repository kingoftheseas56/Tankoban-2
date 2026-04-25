#pragma once

#include <QJsonArray>
#include <QMenu>
#include <QString>
#include <QVariant>
#include <functional>

// VIDEO_CONTEXT_MENU_MINIMALIST 2026-04-25 (Phase 2 of N) — slimmed
// from 13 top-level + 14-item "More ▸" tree down to 8 top-level items
// per Hemanth's minimalist directive (Phase 1 was the bottom HUD).
//
// Final menu shape:
//   Aspect Ratio ▸
//   Crop ▸
//   Fullscreen
//   ─── separator ───
//   Audio ▸          (hidden if no audio tracks)
//   Subtitles ▸
//   Playlist
//   ─── separator ───
//   Keyboard Shortcuts...
//   Back to library
//
// Removed entirely: Pause, Mute, Speed, Take Snapshot, Tracks, Show
// Stats, Always on Top, Picture-in-Picture, Open URL, Recent,
// Deinterlace, Audio normalization, the entire "More ▸" wrapper, and
// the Subtitles submenu's "Open Subtitles menu..." opener. Keyboard
// bindings for the dropped entries are PRESERVED — only the menu
// surface goes (Pause/Space, Mute/M, Speed/zxc, Snapshot/Ctrl+S,
// Stats/I, AOT/Ctrl+T, PiP/Ctrl+P, OpenURL/Ctrl+U all still bound).
//
// Audio + Subtitles submenus simplified to the core "switch track"
// function (and Subtitles also has "Load external subtitle..." for
// addon parity).

struct VideoContextData {
    QJsonArray audioTracks;      // [{id, lang, title}, ...]
    QJsonArray subtitleTracks;   // [{id, lang, title}, ...]
    int  currentAudioId = -1;
    int  currentSubId   = 0;
    bool subsVisible    = true;
    // Current aspect ratio override string ("original"/"4:3"/"16:9"/
    // "2.35:1"/"1.85:1"/"2.39:1"). Drives the check mark on the
    // Aspect Ratio submenu. "original" matches m_forcedAspect == 0.0.
    QString currentAspect = QStringLiteral("original");
    // Current crop target string ("none"/"16:9"/"2.35:1"/"2.39:1"/
    // "1.85:1"/"4:3"). Drives the check mark on the Crop submenu.
    // "none" matches m_cropAspect == 0.0 (no crop). Crop zooms the
    // video to eliminate baked letterbox / pillarbox strips —
    // orthogonal to Aspect Ratio override which changes viewport fit
    // shape.
    QString currentCrop = QStringLiteral("none");
};

class VideoContextMenu {
public:
    enum ActionType {
        SetAspectRatio,     // data = QString "original"/"4:3"/"16:9"/"2.35:1"/"2.39:1"/"1.85:1"
        SetCrop,            // data = QString "none"/"16:9"/"2.35:1"/"2.39:1"/"1.85:1"/"4:3"
        ToggleFullscreen,
        SetAudioTrack,      // data = int id
        SetSubtitleTrack,   // data = int id (-1 = off)
        LoadExternalSub,
        OpenPlaylist,
        OpenKeybindings,
        BackToLibrary,
    };

    static QMenu* build(const VideoContextData& data, QWidget* parent,
                        std::function<void(ActionType, QVariant)> callback);
};
