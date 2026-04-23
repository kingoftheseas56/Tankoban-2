#include "VideoContextMenu.h"

#include <QActionGroup>
#include <QJsonObject>

static const char* MENU_SS =
    "QMenu {"
    "  background: rgb(18,18,18);"
    "  color: rgb(220,220,220);"
    "  border: 1px solid rgba(255,255,255,30);"
    "  border-radius: 6px;"
    "  padding: 4px 0;"
    "}"
    "QMenu::item {"
    "  padding: 6px 24px 6px 12px;"
    "}"
    "QMenu::item:selected {"
    "  background: rgba(255,255,255,0.08);"
    "}"
    "QMenu::separator {"
    "  height: 1px;"
    "  background: rgba(255,255,255,0.10);"
    "  margin: 4px 8px;"
    "}";

static constexpr double SPEED_PRESETS[] = { 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0 };
static constexpr int SPEED_COUNT = 7;

// VIDEO_PLAYER_UI_POLISH Phase 5 2026-04-23 — audit finding #8
// ("the context menu is overloaded...a dump of available commands rather
// than a carefully curated interaction surface"). Prior layout was a flat
// 18-item list mixing every abstraction level. Re-tiered into:
//
//   [Tier 1 — most-frequent transport + display]
//     Play/Pause, Mute, Speed ▸, Aspect Ratio ▸, Fullscreen, Take Snapshot
//   ---
//   [Tier 2 — moderate-frequency track + navigation]
//     Audio ▸ (conditional), Subtitles ▸, Tracks, Playlist
//   ---
//   [Tier 3 — advanced / admin, nested under "More ▸"]
//     Crop ▸, Always on Top, Picture-in-Picture, Open URL, Recent ▸,
//     Deinterlace, Audio normalization, Keyboard Shortcuts
//   ---
//   [Bottom — session-leaving actions]
//     Show Stats, Back to library
//
// All action handlers + signals are preserved byte-for-byte; this is a
// pure re-tree of where each QAction attaches. No state, no callback
// semantics change.
QMenu* VideoContextMenu::build(const VideoContextData& data, QWidget* parent,
                               std::function<void(ActionType, QVariant)> callback)
{
    auto* menu = new QMenu(parent);
    menu->setStyleSheet(MENU_SS);

    // ===========================================================
    // Tier 1 — most-frequent transport + display
    // ===========================================================

    auto* playPause = menu->addAction(data.paused ? "Play" : "Pause");
    QObject::connect(playPause, &QAction::triggered, parent, [callback]() {
        callback(TogglePlayPause, {});
    });

    auto* muteAction = menu->addAction(data.muted ? "Unmute" : "Mute");
    QObject::connect(muteAction, &QAction::triggered, parent, [callback]() {
        callback(ToggleMute, {});
    });

    // Speed submenu
    auto* speedMenu = menu->addMenu("Speed");
    speedMenu->setStyleSheet(MENU_SS);
    auto* speedGroup = new QActionGroup(speedMenu);
    speedGroup->setExclusive(true);

    for (int i = 0; i < SPEED_COUNT; ++i) {
        double sp = SPEED_PRESETS[i];
        QString label = QString("%1x").arg(sp, 0, 'g', 3);
        auto* act = speedMenu->addAction(label);
        act->setCheckable(true);
        act->setChecked(qAbs(sp - data.currentSpeed) < 1e-6);
        speedGroup->addAction(act);
        QObject::connect(act, &QAction::triggered, parent, [callback, sp]() {
            callback(SetSpeed, sp);
        });
    }

    speedMenu->addSeparator();
    auto* resetSpeed = speedMenu->addAction("Reset to 1.0x");
    QObject::connect(resetSpeed, &QAction::triggered, parent, [callback]() {
        callback(SetSpeed, 1.0);
    });

    // Aspect Ratio submenu
    auto* aspectMenu = menu->addMenu("Aspect Ratio");
    aspectMenu->setStyleSheet(MENU_SS);

    static const struct { const char* label; const char* value; } ASPECTS[] = {
        { "Original", "original" },
        { "4:3",      "4:3"      },
        { "16:9",     "16:9"     },
        { "2.35:1",   "2.35:1"   },
        { "2.39:1",   "2.39:1"   },
        { "1.85:1",   "1.85:1"   },
    };
    auto* aspectGroup = new QActionGroup(aspectMenu);
    aspectGroup->setExclusive(true);
    for (const auto& asp : ASPECTS) {
        auto* act = aspectMenu->addAction(asp.label);
        QString val = asp.value;
        act->setCheckable(true);
        act->setChecked(data.currentAspect == val);
        aspectGroup->addAction(act);
        QObject::connect(act, &QAction::triggered, parent, [callback, val]() {
            callback(SetAspectRatio, val);
        });
    }

    auto* zoomMenu = menu->addMenu("Zoom");
    zoomMenu->setStyleSheet(MENU_SS);
    auto* zoomGroup = new QActionGroup(zoomMenu);
    zoomGroup->setExclusive(true);
    static const struct { const char* label; int pct; } ZOOMS[] = {
        { "100%", Z100 },
        { "105%", Z105 },
        { "110%", Z110 },
        { "115%", Z115 },
        { "120%", Z120 },
    };
    for (const auto& zoom : ZOOMS) {
        auto* act = zoomMenu->addAction(zoom.label);
        act->setCheckable(true);
        act->setChecked(data.currentZoomPct == zoom.pct);
        zoomGroup->addAction(act);
        QObject::connect(act, &QAction::triggered, parent, [callback, zoom]() {
            callback(SetZoom, zoom.pct);
        });
    }

    auto* fsAction = menu->addAction("Fullscreen");
    QObject::connect(fsAction, &QAction::triggered, parent, [callback]() {
        callback(ToggleFullscreen, {});
    });

    auto* snapAction = menu->addAction("Take Snapshot\tCtrl+S");
    QObject::connect(snapAction, &QAction::triggered, parent, [callback]() {
        callback(TakeSnapshot, {});
    });

    menu->addSeparator();

    // ===========================================================
    // Tier 2 — moderate-frequency track + navigation
    // ===========================================================

    // Audio submenu (hidden if no audio tracks available)
    if (!data.audioTracks.isEmpty()) {
        auto* audioMenu = menu->addMenu("Audio");
        audioMenu->setStyleSheet(MENU_SS);
        auto* audioGroup = new QActionGroup(audioMenu);
        audioGroup->setExclusive(true);

        for (int i = 0; i < data.audioTracks.size(); ++i) {
            QJsonObject trk = data.audioTracks[i].toObject();
            int id = trk["id"].toVariant().toInt();
            QString lang = trk["lang"].toString();
            QString title = trk["title"].toString();
            QString label = title.isEmpty() ? lang : QString("%1 (%2)").arg(title, lang);
            if (label.isEmpty())
                label = QString("Track %1").arg(i + 1);

            auto* act = audioMenu->addAction(label);
            act->setCheckable(true);
            act->setChecked(id == data.currentAudioId);
            audioGroup->addAction(act);
            QObject::connect(act, &QAction::triggered, parent, [callback, id]() {
                callback(SetAudioTrack, id);
            });
        }
    }

    // Subtitles submenu
    {
        auto* subMenu = menu->addMenu("Subtitles");
        subMenu->setStyleSheet(MENU_SS);

        auto* openMenuAct = subMenu->addAction("Open Subtitles menu...");
        QObject::connect(openMenuAct, &QAction::triggered, parent, [callback]() {
            callback(OpenSubtitleMenu, {});
        });
        subMenu->addSeparator();

        auto* subGroup = new QActionGroup(subMenu);
        subGroup->setExclusive(true);

        auto* offAct = subMenu->addAction("Off");
        offAct->setCheckable(true);
        offAct->setChecked(!data.subsVisible);
        subGroup->addAction(offAct);
        QObject::connect(offAct, &QAction::triggered, parent, [callback]() {
            callback(SetSubtitleTrack, -1);
        });

        for (int i = 0; i < data.subtitleTracks.size(); ++i) {
            QJsonObject trk = data.subtitleTracks[i].toObject();
            int id = trk["id"].toVariant().toInt();
            QString lang = trk["lang"].toString();
            QString title = trk["title"].toString();
            QString label = title.isEmpty() ? lang : QString("%1 (%2)").arg(title, lang);
            if (label.isEmpty())
                label = QString("Track %1").arg(i + 1);

            auto* act = subMenu->addAction(label);
            act->setCheckable(true);
            act->setChecked(id == data.currentSubId && data.subsVisible);
            subGroup->addAction(act);
            QObject::connect(act, &QAction::triggered, parent, [callback, id]() {
                callback(SetSubtitleTrack, id);
            });
        }

        subMenu->addSeparator();
        auto* loadSub = subMenu->addAction("Load external subtitle...");
        QObject::connect(loadSub, &QAction::triggered, parent, [callback]() {
            callback(LoadExternalSub, {});
        });
    }

    auto* tracksAct = menu->addAction("Tracks");
    QObject::connect(tracksAct, &QAction::triggered, parent, [callback]() {
        callback(OpenTracks, {});
    });

    auto* playlistAct = menu->addAction("Playlist");
    QObject::connect(playlistAct, &QAction::triggered, parent, [callback]() {
        callback(OpenPlaylist, {});
    });

    menu->addSeparator();

    // ===========================================================
    // Tier 3 — advanced / admin (nested under "More ▸")
    // ===========================================================

    auto* moreMenu = menu->addMenu(QStringLiteral("More ▸"));
    moreMenu->setStyleSheet(MENU_SS);

    // Crop submenu
    auto* cropMenu = moreMenu->addMenu("Crop");
    cropMenu->setStyleSheet(MENU_SS);
    static const struct { const char* label; const char* value; } CROPS[] = {
        { "None",   "none"   },
        { "16:9",   "16:9"   },
        { "1.85:1", "1.85:1" },
        { "2.35:1", "2.35:1" },
        { "2.39:1", "2.39:1" },
        { "4:3",    "4:3"    },
    };
    auto* cropGroup = new QActionGroup(cropMenu);
    cropGroup->setExclusive(true);
    for (const auto& cr : CROPS) {
        auto* act = cropMenu->addAction(cr.label);
        QString val = cr.value;
        act->setCheckable(true);
        act->setChecked(data.currentCrop == val);
        cropGroup->addAction(act);
        QObject::connect(act, &QAction::triggered, parent, [callback, val]() {
            callback(SetCrop, val);
        });
    }

    auto* aotAction = moreMenu->addAction("Always on Top\tCtrl+T");
    aotAction->setCheckable(true);
    aotAction->setChecked(data.alwaysOnTop);
    QObject::connect(aotAction, &QAction::triggered, parent, [callback]() {
        callback(ToggleAlwaysOnTop, {});
    });

    auto* pipAction = moreMenu->addAction(data.inPip
        ? "Exit Picture-in-Picture\tCtrl+P"
        : "Picture-in-Picture\tCtrl+P");
    QObject::connect(pipAction, &QAction::triggered, parent, [callback]() {
        callback(TogglePip, {});
    });

    auto* urlAction = moreMenu->addAction("Open URL...\tCtrl+U");
    QObject::connect(urlAction, &QAction::triggered, parent, [callback]() {
        callback(OpenUrl, {});
    });

    // Recent submenu (Tier 3 because power users; most casual use is "just play the file I opened")
    auto* recentMenu = moreMenu->addMenu("Recent");
    recentMenu->setStyleSheet(MENU_SS);
    if (data.recentFiles.isEmpty()) {
        auto* empty = recentMenu->addAction("(No recent files)");
        empty->setEnabled(false);
    } else {
        for (const QString& path : data.recentFiles) {
            QString label;
            bool reachable = true;
            if (path.startsWith("http", Qt::CaseInsensitive)
                || path.startsWith("rtsp", Qt::CaseInsensitive)
                || path.startsWith("rtmp", Qt::CaseInsensitive)) {
                const int slash = path.lastIndexOf('/');
                label = (slash > 0 && slash < path.size() - 1)
                    ? path.mid(slash + 1) : path;
                if (label.isEmpty()) label = path;
            } else {
                label = QFileInfo(path).fileName();
                if (label.isEmpty()) label = path;
                reachable = QFileInfo(path).exists();
            }
            auto* act = recentMenu->addAction(label);
            act->setToolTip(path);
            act->setEnabled(reachable);
            QObject::connect(act, &QAction::triggered, parent, [callback, path]() {
                callback(OpenRecent, path);
            });
        }
        recentMenu->addSeparator();
        auto* clearAct = recentMenu->addAction("Clear Recent");
        QObject::connect(clearAct, &QAction::triggered, parent, [callback]() {
            callback(ClearRecent, {});
        });
    }

    moreMenu->addSeparator();

    auto* deintAct = moreMenu->addAction("Deinterlace");
    deintAct->setCheckable(true);
    deintAct->setChecked(data.deinterlace);
    QObject::connect(deintAct, &QAction::triggered, parent, [callback]() {
        callback(ToggleDeinterlace, {});
    });

    auto* normAct = moreMenu->addAction("Audio normalization");
    normAct->setCheckable(true);
    normAct->setChecked(data.normalize);
    QObject::connect(normAct, &QAction::triggered, parent, [callback]() {
        callback(ToggleNormalize, {});
    });

    moreMenu->addSeparator();

    auto* keysAct = moreMenu->addAction("Keyboard Shortcuts...\t?");
    QObject::connect(keysAct, &QAction::triggered, parent, [callback]() {
        callback(OpenKeybindings, {});
    });

    menu->addSeparator();

    // ===========================================================
    // Bottom — session-leaving actions
    // ===========================================================

    auto* statsAct = menu->addAction("Show Stats\tI");
    statsAct->setCheckable(true);
    statsAct->setChecked(data.showStats);
    QObject::connect(statsAct, &QAction::triggered, parent, [callback]() {
        callback(ToggleStats, {});
    });

    auto* backAct = menu->addAction("Back to library");
    QObject::connect(backAct, &QAction::triggered, parent, [callback]() {
        callback(BackToLibrary, {});
    });

    return menu;
}
