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

QMenu* VideoContextMenu::build(const VideoContextData& data, QWidget* parent,
                               std::function<void(ActionType, QVariant)> callback)
{
    auto* menu = new QMenu(parent);
    menu->setStyleSheet(MENU_SS);

    // ---- Playback ----
    auto* playPause = menu->addAction(data.paused ? "Play" : "Pause");
    QObject::connect(playPause, &QAction::triggered, parent, [callback]() {
        callback(TogglePlayPause, {});
    });

    auto* muteAction = menu->addAction(data.muted ? "Unmute" : "Mute");
    QObject::connect(muteAction, &QAction::triggered, parent, [callback]() {
        callback(ToggleMute, {});
    });

    menu->addSeparator();

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

    menu->addSeparator();

    // ---- Video ----
    auto* aspectMenu = menu->addMenu("Aspect Ratio");
    aspectMenu->setStyleSheet(MENU_SS);

    static const struct { const char* label; const char* value; } ASPECTS[] = {
        { "Original", "original" },
        { "4:3",      "4:3"      },
        { "16:9",     "16:9"     },
        { "2.35:1",   "2.35:1"   },
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

    auto* fsAction = menu->addAction("Fullscreen");
    QObject::connect(fsAction, &QAction::triggered, parent, [callback]() {
        callback(ToggleFullscreen, {});
    });

    // VIDEO_PLAYER_FIX Batch 3.1 — Always on Top toggle. Checkable;
    // keyboard shortcut Ctrl+T is surfaced in the label so users discover
    // the binding via the menu (T alone is taken by open_subtitle_menu).
    auto* aotAction = menu->addAction("Always on Top\tCtrl+T");
    aotAction->setCheckable(true);
    aotAction->setChecked(data.alwaysOnTop);
    QObject::connect(aotAction, &QAction::triggered, parent, [callback]() {
        callback(ToggleAlwaysOnTop, {});
    });

    // VIDEO_PLAYER_FIX Batch 3.2 — Take Snapshot (current displayed frame
    // to PNG under Pictures/Tankoban Snapshots/). Ctrl+S since plain S is
    // taken by cycle_subtitle.
    auto* snapAction = menu->addAction("Take Snapshot\tCtrl+S");
    QObject::connect(snapAction, &QAction::triggered, parent, [callback]() {
        callback(TakeSnapshot, {});
    });

    // VIDEO_PLAYER_FIX Batch 3.3 — Picture-in-Picture (mini-mode: frameless
    // + always-on-top 320x180 window). Label swaps to "Exit PiP" while
    // active so the same entry can toggle it back off.
    auto* pipAction = menu->addAction(data.inPip
        ? "Exit Picture-in-Picture\tCtrl+P"
        : "Picture-in-Picture\tCtrl+P");
    QObject::connect(pipAction, &QAction::triggered, parent, [callback]() {
        callback(TogglePip, {});
    });

    // VIDEO_PLAYER_FIX Batch 4.1 — Open URL dialog (HTTP/HTTPS/RTSP/RTMP
    // streams). Same Ctrl+U convention QMPlay2 + IINA + VLC use.
    auto* urlAction = menu->addAction("Open URL...\tCtrl+U");
    QObject::connect(urlAction, &QAction::triggered, parent, [callback]() {
        callback(OpenUrl, {});
    });

    // VIDEO_PLAYER_FIX Batch 4.2 — Recent submenu (last 20 opened paths +
    // URLs, most-recent-first). Local paths greyed out when the file no
    // longer exists; click prunes them. Clear Recent at the bottom.
    auto* recentMenu = menu->addMenu("Recent");
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
                // URL entry — take the tail after the last '/' if present,
                // otherwise show the full URL. Reachability not probed
                // (would require a network roundtrip per menu open).
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

    menu->addSeparator();

    // ---- Audio tracks ----
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

        menu->addSeparator();
    }

    // ---- Subtitles ----
    {
        auto* subMenu = menu->addMenu("Subtitles");
        subMenu->setStyleSheet(MENU_SS);

        // Batch 5.3 (Tankostream) — top entry opens the richer SubtitleMenu
        // which includes addon-fetched external tracks + load-from-file.
        // The per-track list below remains as a shortcut for embedded tracks.
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
        // Distinct sentinel from any real track id. Previously we sent 0
        // for Off, which collided with subtitle streams whose AVStream
        // index happened to be 0 (subtitle-only containers, some remux
        // layouts) — picking those tracks silently did Off.
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

        menu->addSeparator();
    }

    // ---- Filters ----
    auto* deintAct = menu->addAction("Deinterlace");
    deintAct->setCheckable(true);
    deintAct->setChecked(data.deinterlace);
    QObject::connect(deintAct, &QAction::triggered, parent, [callback]() {
        callback(ToggleDeinterlace, {});
    });

    auto* normAct = menu->addAction("Audio normalization");
    normAct->setCheckable(true);
    normAct->setChecked(data.normalize);
    QObject::connect(normAct, &QAction::triggered, parent, [callback]() {
        callback(ToggleNormalize, {});
    });

    menu->addSeparator();

    // ---- Navigation ----
    auto* tracksAct = menu->addAction("Tracks");
    QObject::connect(tracksAct, &QAction::triggered, parent, [callback]() {
        callback(OpenTracks, {});
    });

    auto* playlistAct = menu->addAction("Playlist");
    QObject::connect(playlistAct, &QAction::triggered, parent, [callback]() {
        callback(OpenPlaylist, {});
    });

    // VIDEO_PLAYER_FIX Batch 6.1 — live keybinding editor. Label lists
    // `?` because that's the `show_shortcuts` default — changes if user
    // rebinds, but the label stays correct as a discoverability aid.
    auto* keysAct = menu->addAction("Keyboard Shortcuts...\t?");
    QObject::connect(keysAct, &QAction::triggered, parent, [callback]() {
        callback(OpenKeybindings, {});
    });

    // VIDEO_PLAYER_FIX Batch 7.1 — stats badge toggle (checkable). Label
    // hard-codes `I` for discoverability; rebinding doesn't update here.
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
