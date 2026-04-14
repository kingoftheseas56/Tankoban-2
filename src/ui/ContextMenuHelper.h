#pragma once

#include <QString>
#include <QWidget>
#include <QMenu>

namespace ContextMenuHelper {

// Open Windows Explorer with the file/folder selected
void revealInExplorer(const QString& path);

// Copy text to system clipboard
void copyToClipboard(const QString& text);

// Show a Yes/No confirmation dialog. Returns true if user clicks Yes.
bool confirmRemove(QWidget* parent, const QString& title, const QString& message);

// Create a styled context menu matching the groundwork's dark theme.
// Caller owns the returned QMenu.
QMenu* createMenu(QWidget* parent);

// Add a danger-styled action (red text) to a menu.
QAction* addDangerAction(QMenu* menu, const QString& text);

} // namespace ContextMenuHelper
