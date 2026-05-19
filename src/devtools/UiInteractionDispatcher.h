#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>

class QWidget;

// REPO_HYGIENE Phase D.5 (2026-05-19) — synthetic UI interaction layer.
//
// Pywinauto-mcp pixel-clicks fail intermittently on Qt custom widgets (UIA
// can't reliably target QLabel-inside-QToolButton, hits Qt Quick + delegate
// trees as opaque rects, etc). Bypass UIA entirely by synthesizing the
// click / keypress / text-input directly against the target QObject — looked
// up by objectName via findChild — using QApplication::postEvent for the
// QKeyEvent / QWheelEvent / QMouseEvent path and QMetaObject::invokeMethod
// for slot invocation (animateClick, setChecked, setText, ...).
//
// Caller (MainWindow::handleDevCommand) is responsible for the
// TANKOBAN_DEV_UI_SIM=1 env gate on write-capable commands. Read-only
// commands (query/list/dry-run) gate only on --dev-control. See
// `isWriteCapable` for the catalogue.
//
// Anti-pattern reminder: this proves the widget RECEIVED the event. It does
// NOT prove the screen looks right. Visual verification still needs
// screenshots or human eyes.
class UiInteractionDispatcher : public QObject
{
    Q_OBJECT
public:
    explicit UiInteractionDispatcher(QWidget* root, QObject* parent = nullptr);

    // Returns true if `cmd` is a ui_* command recognised by this dispatcher
    // and the reply has been populated. `reply` is expected to already carry
    // the dispatcher framing keys ("type":"reply","seq":<int>); on success
    // this method merges its result into it. On error it overwrites "type"
    // to "error" and adds "code" + "message" (MainWindow keeps "seq").
    bool dispatch(const QString& cmd,
                  const QJsonObject& payload,
                  QJsonObject& reply);

    // Catalogue of write-capable ui_* commands. MainWindow uses this to
    // gate on TANKOBAN_DEV_UI_SIM=1 before forwarding. Read-only commands
    // (query/list/dry-run/active-layer/query-focus) return false.
    static bool isWriteCapable(const QString& cmd);

    // Full ui_* command list — emitted by MainWindow's `ping` handler.
    static QStringList commandList();

private:
    QPointer<QWidget> m_root;
};
