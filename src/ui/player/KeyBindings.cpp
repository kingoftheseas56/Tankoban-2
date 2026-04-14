#include "KeyBindings.h"

static const struct { const char* action; const char* label; int key; Qt::KeyboardModifiers mods; } DEFAULTS[] = {
    // Playback
    {"toggle_pause",       "Play / Pause",         Qt::Key_Space,        Qt::NoModifier},
    {"seek_back_10s",      "Seek back 10s",        Qt::Key_Left,         Qt::NoModifier},
    {"seek_fwd_10s",       "Seek forward 10s",     Qt::Key_Right,        Qt::NoModifier},
    {"seek_back_60s",      "Seek back 60s",        Qt::Key_Left,         Qt::ShiftModifier},
    {"seek_fwd_60s",       "Seek forward 60s",     Qt::Key_Right,        Qt::ShiftModifier},
    {"frame_step_fwd",     "Frame step forward",   Qt::Key_Period,       Qt::NoModifier},
    {"frame_step_back",    "Frame step backward",  Qt::Key_Comma,        Qt::NoModifier},

    // Speed (z/x/c handled in VideoPlayer fallback; backslash also resets)
    {"speed_reset",        "Reset speed",          Qt::Key_Backslash,    Qt::NoModifier},

    // Volume
    {"volume_up",          "Volume up",            Qt::Key_Up,           Qt::NoModifier},
    {"volume_down",        "Volume down",          Qt::Key_Down,         Qt::NoModifier},
    {"toggle_mute",        "Toggle mute",          Qt::Key_M,            Qt::NoModifier},

    // Video
    {"toggle_fullscreen",  "Toggle fullscreen",    Qt::Key_F,            Qt::NoModifier},
    {"toggle_fullscreen2", "Toggle fullscreen",    Qt::Key_F11,          Qt::NoModifier},
    {"toggle_deinterlace", "Toggle deinterlace",   Qt::Key_D,            Qt::NoModifier},

    // Audio / Subs
    {"cycle_audio",        "Cycle audio track",    Qt::Key_A,            Qt::NoModifier},
    {"toggle_normalize",   "Toggle normalization", Qt::Key_A,            Qt::ShiftModifier},
    {"cycle_subtitle",     "Cycle subtitle track", Qt::Key_S,            Qt::NoModifier},
    {"toggle_subtitles",   "Toggle subtitles",     Qt::Key_S,            Qt::ShiftModifier},
    {"sub_delay_minus",    "Sub delay -100ms",     Qt::Key_Less,         Qt::NoModifier},
    {"sub_delay_plus",     "Sub delay +100ms",     Qt::Key_Greater,      Qt::NoModifier},
    {"sub_delay_reset",    "Reset sub delay",      Qt::Key_Z,            Qt::ControlModifier | Qt::ShiftModifier},
    {"audio_delay_minus",  "Audio delay -50ms",    Qt::Key_Minus,        Qt::ControlModifier},
    {"audio_delay_plus",   "Audio delay +50ms",    Qt::Key_Equal,        Qt::ControlModifier},
    {"audio_delay_reset",  "Reset audio delay",    Qt::Key_0,            Qt::ControlModifier},

    // Chapters
    {"chapter_next",       "Next chapter",         Qt::Key_PageDown,     Qt::NoModifier},
    {"chapter_prev",       "Previous chapter",     Qt::Key_PageUp,       Qt::NoModifier},

    // Navigation
    {"next_episode",       "Next episode",         Qt::Key_N,            Qt::NoModifier},
    {"prev_episode",       "Previous episode",     Qt::Key_P,            Qt::NoModifier},
    {"toggle_playlist",    "Toggle playlist",      Qt::Key_L,            Qt::NoModifier},
    {"show_shortcuts",     "Show shortcuts",        Qt::Key_Question,     Qt::NoModifier},
    {"back_to_library",    "Back to library",      Qt::Key_Escape,       Qt::NoModifier},
    {"back_fullscreen",    "Exit fullscreen/back",  Qt::Key_Backspace,    Qt::NoModifier},
};

static constexpr int NUM_DEFAULTS = sizeof(DEFAULTS) / sizeof(DEFAULTS[0]);

KeyBindings::KeyBindings()
{
    populateDefaults();
    load();
}

void KeyBindings::populateDefaults()
{
    m_bindings.clear();
    for (int i = 0; i < NUM_DEFAULTS; ++i) {
        int combined = DEFAULTS[i].key | static_cast<int>(DEFAULTS[i].mods);
        m_bindings.insert(DEFAULTS[i].action, QKeySequence(combined));
    }
    rebuildReverseLookup();
}

void KeyBindings::load()
{
    QSettings s;
    s.beginGroup("player/keybindings");
    for (const auto& key : s.childKeys()) {
        QString seq = s.value(key).toString();
        if (!seq.isEmpty())
            m_bindings[key] = QKeySequence::fromString(seq);
    }
    s.endGroup();
    rebuildReverseLookup();
}

void KeyBindings::save() const
{
    QSettings s;
    s.beginGroup("player/keybindings");
    for (auto it = m_bindings.constBegin(); it != m_bindings.constEnd(); ++it)
        s.setValue(it.key(), it.value().toString());
    s.endGroup();
}

void KeyBindings::resetDefaults()
{
    QSettings s;
    s.remove("player/keybindings");
    populateDefaults();
}

QString KeyBindings::actionForKey(int key, Qt::KeyboardModifiers mods) const
{
    return m_keyToAction.value(encodeKey(key, mods));
}

QKeySequence KeyBindings::keyForAction(const QString& action) const
{
    return m_bindings.value(action);
}

void KeyBindings::setBinding(const QString& action, const QKeySequence& key)
{
    m_bindings[action] = key;
    save();
    rebuildReverseLookup();
}

QStringList KeyBindings::allActions() const
{
    QStringList list;
    for (int i = 0; i < NUM_DEFAULTS; ++i)
        list << DEFAULTS[i].action;
    return list;
}

QString KeyBindings::labelForAction(const QString& action)
{
    for (int i = 0; i < NUM_DEFAULTS; ++i) {
        if (action == DEFAULTS[i].action)
            return DEFAULTS[i].label;
    }
    return action;
}

void KeyBindings::rebuildReverseLookup()
{
    m_keyToAction.clear();
    for (auto it = m_bindings.constBegin(); it != m_bindings.constEnd(); ++it) {
        if (!it.value().isEmpty()) {
            int combo = it.value()[0].toCombined();
            m_keyToAction.insert(combo, it.key());
        }
    }
}

int KeyBindings::encodeKey(int key, Qt::KeyboardModifiers mods)
{
    return key | static_cast<int>(mods);
}
