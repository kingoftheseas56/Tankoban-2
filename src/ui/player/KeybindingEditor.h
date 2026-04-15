#pragma once

#include <QDialog>

class KeyBindings;
class QTableWidget;
class QLabel;

// VIDEO_PLAYER_FIX Batch 6.1 — modal keybinding editor. Replaces the
// static ShortcutsOverlay reference card with a live table: click a
// Current cell to capture the next key press, Esc cancels, duplicate
// detection prompts the user to unbind the conflicting action, per-
// row Reset restores an individual default, Reset All wipes all
// customizations. Reads/writes through the shared `KeyBindings`
// instance — changes take effect on any subsequent keyPressEvent.
class KeybindingEditor : public QDialog {
    Q_OBJECT
public:
    explicit KeybindingEditor(KeyBindings* bindings, QWidget* parent = nullptr);

protected:
    // Captures the next key press when m_captureRow >= 0. Escape cancels
    // capture; other keys (including modifier-only) fall through the
    // base implementation so dialog text navigation still works.
    void keyPressEvent(QKeyEvent* ev) override;

private:
    void rebuildTable();
    void beginCapture(int row);
    void applyCaptured(int row, const QKeySequence& seq);
    void resetAllBindings();
    void resetRow(int row);

    KeyBindings*   m_bindings    = nullptr;
    QTableWidget*  m_table       = nullptr;
    QLabel*        m_hint        = nullptr;
    int            m_captureRow  = -1;
};
