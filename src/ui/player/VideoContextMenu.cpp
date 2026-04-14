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
    for (const auto& asp : ASPECTS) {
        auto* act = aspectMenu->addAction(asp.label);
        QString val = asp.value;
        QObject::connect(act, &QAction::triggered, parent, [callback, val]() {
            callback(SetAspectRatio, val);
        });
    }

    auto* fsAction = menu->addAction("Fullscreen");
    QObject::connect(fsAction, &QAction::triggered, parent, [callback]() {
        callback(ToggleFullscreen, {});
    });

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
        auto* subGroup = new QActionGroup(subMenu);
        subGroup->setExclusive(true);

        auto* offAct = subMenu->addAction("Off");
        offAct->setCheckable(true);
        offAct->setChecked(data.currentSubId == 0 || !data.subsVisible);
        subGroup->addAction(offAct);
        QObject::connect(offAct, &QAction::triggered, parent, [callback]() {
            callback(SetSubtitleTrack, 0);
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

    auto* backAct = menu->addAction("Back to library");
    QObject::connect(backAct, &QAction::triggered, parent, [callback]() {
        callback(BackToLibrary, {});
    });

    return menu;
}
