#include "ContextMenuHelper.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QProcess>
#include <QMessageBox>
#include <QAction>
#include <QWidgetAction>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QGridLayout>
#include <QSpacerItem>
#include <QSizePolicy>

namespace ContextMenuHelper {

void revealInExplorer(const QString& path)
{
    if (path.isEmpty()) return;
    QProcess::startDetached("explorer",
        {"/select,", QDir::toNativeSeparators(path)});
}

void copyToClipboard(const QString& text)
{
    if (text.isEmpty()) return;
    auto* cb = QApplication::clipboard();
    if (cb) cb->setText(text);
}

bool confirmRemove(QWidget* parent, const QString& title, const QString& message)
{
    return QMessageBox::question(parent, title, message,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

RemoveChoice confirmRemoveWithFile(QWidget* parent,
                                   const QString& title,
                                   const QString& message)
{
    QMessageBox box(parent);
    box.setWindowTitle(title);
    box.setText(message);
    box.setIcon(QMessageBox::Warning);
    box.setStyleSheet(
        "QMessageBox { background: #1e1e1e; }"
        "QLabel { color: rgba(238,238,238,0.86); font-size: 13px; }"
        "QPushButton {"
        "  min-width: 128px; padding: 8px 14px; margin: 0 3px;"
        "  background: rgba(255,255,255,0.08);"
        "  color: rgba(238,238,238,0.86);"
        "  border: 1px solid rgba(255,255,255,0.12);"
        "  border-radius: 6px;"
        "}"
        "QPushButton:hover { background: rgba(255,255,255,0.14); }"
        "QPushButton:default { border-color: rgba(255,255,255,0.30); }"
        "QPushButton[destructive=\"true\"] {"
        "  color: #ff6b6b; border-color: rgba(229,57,53,0.45);"
        "}"
        "QPushButton[destructive=\"true\"]:hover {"
        "  background: rgba(229,57,53,0.16);"
        "}");
    // Shorter labels than the prose originals so three buttons fit without
    // clipping. "Remove from library" keeps the file; "Delete file" (red) also
    // removes it from disk.
    QPushButton* libBtn = box.addButton(
        QObject::tr("Remove from library"), QMessageBox::AcceptRole);
    QPushButton* fileBtn = box.addButton(
        QObject::tr("Delete file"), QMessageBox::DestructiveRole);
    fileBtn->setProperty("destructive", true);
    fileBtn->style()->unpolish(fileBtn);
    fileBtn->style()->polish(fileBtn);
    QPushButton* cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    // QMessageBox sizes itself to the (short) message text, which squeezes the
    // three buttons until their labels clip. Force a comfortable minimum width
    // by spanning a spacer across the dialog's grid so every button renders in
    // full.
    if (auto* grid = qobject_cast<QGridLayout*>(box.layout())) {
        grid->addItem(new QSpacerItem(460, 0, QSizePolicy::Minimum,
                                      QSizePolicy::Expanding),
                      grid->rowCount(), 0, 1, grid->columnCount());
    }
    box.exec();
    if (box.clickedButton() == libBtn)  return RemoveChoice::RemoveFromLibrary;
    if (box.clickedButton() == fileBtn) return RemoveChoice::DeleteFile;
    return RemoveChoice::Cancel;
}

QMenu* createMenu(QWidget* parent)
{
    auto* menu = new QMenu(parent);
    menu->setObjectName("ParityContextMenu");
    menu->setStyleSheet(
        "QMenu#ParityContextMenu {"
        "  background: #1e1e1e;"
        "  border: 1px solid rgba(255,255,255,0.12);"
        "  border-radius: 8px;"
        "  padding: 4px 0;"
        "  color: rgba(238,238,238,0.86);"
        "  font-size: 12px;"
        "}"
        "QMenu#ParityContextMenu::item {"
        "  padding: 6px 20px;"
        "}"
        "QMenu#ParityContextMenu::item:selected {"
        "  background: rgba(255,255,255,0.08);"
        "}"
        "QMenu#ParityContextMenu::separator {"
        "  height: 1px;"
        "  background: rgba(255,255,255,0.08);"
        "  margin: 4px 8px;"
        "}");
    return menu;
}

QAction* addDangerAction(QMenu* menu, const QString& text)
{
    // Use QWidgetAction with a colored QLabel (matching groundwork's approach)
    auto* wa = new QWidgetAction(menu);
    auto* label = new QLabel(text);
    label->setStyleSheet(
        "QLabel { color: #e53935; padding: 6px 20px; font-size: 12px; }"
        "QLabel:hover { background: rgba(255,255,255,0.08); }");
    label->setCursor(Qt::PointingHandCursor);
    wa->setDefaultWidget(label);
    menu->addAction(wa);
    return wa;
}

} // namespace ContextMenuHelper
