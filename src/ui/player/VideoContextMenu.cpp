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

// VIDEO_CONTEXT_MENU_MINIMALIST 2026-04-25 (Phase 2) — see VideoContextMenu.h
// header comment for the full final menu shape and rationale.
//
// Layout (8 top-level entries, 2 separators):
//   Aspect Ratio ▸ / Crop ▸ / Fullscreen
//   ───
//   Audio ▸ (conditional) / Subtitles ▸ / Playlist
//   ───
//   Keyboard Shortcuts... / Back to library
QMenu* VideoContextMenu::build(const VideoContextData& data, QWidget* parent,
                               std::function<void(ActionType, QVariant)> callback)
{
    auto* menu = new QMenu(parent);
    menu->setStyleSheet(MENU_SS);

    // ─── Display group ──────────────────────────────────────────

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

    // Crop submenu (promoted to top-level from the prior "More ▸" tree)
    auto* cropMenu = menu->addMenu("Crop");
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

    auto* fsAction = menu->addAction("Fullscreen");
    QObject::connect(fsAction, &QAction::triggered, parent, [callback]() {
        callback(ToggleFullscreen, {});
    });

    menu->addSeparator();

    // ─── Tracks + navigation group ──────────────────────────────

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

    // Subtitles submenu (track switch only + Load external — "Open
    // Subtitles menu..." opener removed; the new Subtitles chip in the
    // bottom HUD is the unified surface for embedded + addon + file).
    {
        auto* subMenu = menu->addMenu("Subtitles");
        subMenu->setStyleSheet(MENU_SS);

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

    auto* playlistAct = menu->addAction("Playlist");
    QObject::connect(playlistAct, &QAction::triggered, parent, [callback]() {
        callback(OpenPlaylist, {});
    });

    menu->addSeparator();

    // ─── Utility + exit ─────────────────────────────────────────

    auto* keysAct = menu->addAction("Keyboard Shortcuts...\t?");
    QObject::connect(keysAct, &QAction::triggered, parent, [callback]() {
        callback(OpenKeybindings, {});
    });

    auto* backAct = menu->addAction("Back to library");
    QObject::connect(backAct, &QAction::triggered, parent, [callback]() {
        callback(BackToLibrary, {});
    });

    return menu;
}
