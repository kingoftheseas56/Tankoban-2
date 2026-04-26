#pragma once

#include <QFrame>

#include "../Theme.h"

class QPushButton;

// Topbar widget — single icon button (sun/moon) that cycles through the 6
// theme Modes on click. Sits in the topbar cluster alongside the existing
// IconButton-class scan/add buttons.
//
// Phase 2 of THEME_SYSTEM_FIX_TODO. Spec correction 2026-04-25 dropped the
// previous Preset axis; this is now a single-axis (Mode) picker.

class ThemePicker : public QFrame {
    Q_OBJECT
public:
    explicit ThemePicker(QWidget* parent = nullptr);

private slots:
    void onModeButtonClicked();

private:
    void refreshModeButtonIcon();

    QPushButton* m_modeBtn = nullptr;
};
