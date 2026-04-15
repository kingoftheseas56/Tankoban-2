#include "Toast.h"

#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QTimer>
#include <QEvent>
#include <QResizeEvent>

static constexpr int BOTTOM_OFFSET_PX = 32;

// Replace any existing Toast child so we only ever have one on screen.
static void clearExistingToast(QWidget* parent)
{
    if (!parent) return;
    const auto existing = parent->findChildren<Toast*>(QString(), Qt::FindDirectChildrenOnly);
    for (auto* t : existing) {
        t->hide();
        t->deleteLater();
    }
}

void Toast::show(QWidget* parent, const QString& message, int durationMs)
{
    if (!parent) return;
    clearExistingToast(parent);
    auto* t = new Toast(parent, message, QString(), {}, durationMs);
    t->QFrame::show();
    t->raise();
}

void Toast::show(QWidget* parent, const QString& message,
                 const QString& actionLabel,
                 std::function<void()> onAction,
                 int durationMs)
{
    if (!parent) return;
    clearExistingToast(parent);
    auto* t = new Toast(parent, message, actionLabel, std::move(onAction), durationMs);
    t->QFrame::show();
    t->raise();
}

Toast::Toast(QWidget* parent, const QString& message,
             const QString& actionLabel,
             std::function<void()> onAction,
             int durationMs)
    : QFrame(parent)
    , m_onAction(std::move(onAction))
{
    setObjectName("Toast");
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setFocusPolicy(Qt::NoFocus);

    setStyleSheet(
        "#Toast { background: rgba(20, 20, 20, 0.95); border: 1px solid "
        "rgba(255,255,255,0.10); border-radius: 8px; color: #eee; }"
        "#Toast QLabel { color: #eee; font-size: 12px; padding: 0 4px; }"
        "#Toast QPushButton { background: transparent; color: #60a5fa; "
        "  border: none; font-size: 12px; font-weight: 600; padding: 2px 6px; }"
        "#Toast QPushButton:hover { color: #93c5fd; }"
    );

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 10, 16, 10);
    layout->setSpacing(12);

    m_messageLabel = new QLabel(message, this);
    m_messageLabel->setWordWrap(false);
    layout->addWidget(m_messageLabel);

    if (!actionLabel.isEmpty() && m_onAction) {
        m_actionButton = new QPushButton(actionLabel, this);
        m_actionButton->setCursor(Qt::PointingHandCursor);
        connect(m_actionButton, &QPushButton::clicked, this, [this]() {
            // Fire the action, then dismiss. Copy the callback first so
            // deleteLater() can't chew out-from-under us if the action
            // triggers re-entrant show() calls.
            auto cb = m_onAction;
            hide();
            deleteLater();
            if (cb) cb();
        });
        layout->addWidget(m_actionButton);
    }

    adjustSize();
    repositionToBottomCenter();

    // Watch parent resize so the toast stays bottom-centered.
    if (auto* p = parentWidget()) p->installEventFilter(this);

    m_dismissTimer = new QTimer(this);
    m_dismissTimer->setSingleShot(true);
    m_dismissTimer->setInterval(durationMs);
    connect(m_dismissTimer, &QTimer::timeout, this, [this]() {
        hide();
        deleteLater();
    });
    m_dismissTimer->start();
}

bool Toast::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == parentWidget() && event->type() == QEvent::Resize) {
        repositionToBottomCenter();
    }
    return QFrame::eventFilter(obj, event);
}

void Toast::repositionToBottomCenter()
{
    auto* p = parentWidget();
    if (!p) return;
    const QSize s = sizeHint();
    resize(s);
    const int x = (p->width() - s.width()) / 2;
    const int y = p->height() - s.height() - BOTTOM_OFFSET_PX;
    move(qMax(0, x), qMax(0, y));
}
