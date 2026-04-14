#pragma once

#include <QHash>
#include <QKeySequence>
#include <QString>
#include <QSettings>

// Configurable keybinding system for the video player.
// Reads/writes from QSettings under "player/keybindings/".
// Actions are string identifiers (e.g. "toggle_pause", "seek_back_10s").
class KeyBindings {
public:
    KeyBindings();

    // Look up which action a key event maps to. Returns empty string if unbound.
    QString actionForKey(int key, Qt::KeyboardModifiers mods) const;

    // Get the current key sequence for an action (for display in shortcuts overlay).
    QKeySequence keyForAction(const QString& action) const;

    // Rebind an action to a new key. Saves to QSettings.
    void setBinding(const QString& action, const QKeySequence& key);

    // Reset all bindings to defaults.
    void resetDefaults();

    // Load bindings from QSettings (called in constructor).
    void load();

    // Save all bindings to QSettings.
    void save() const;

    // All known action names (for settings UI).
    QStringList allActions() const;

    // Human-readable label for an action.
    static QString labelForAction(const QString& action);

private:
    void populateDefaults();

    // action -> key sequence
    QHash<QString, QKeySequence> m_bindings;
    // reverse lookup: encoded key -> action
    QHash<int, QString> m_keyToAction;

    void rebuildReverseLookup();
    static int encodeKey(int key, Qt::KeyboardModifiers mods);
};
