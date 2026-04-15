#pragma once

#include <QFrame>
#include <functional>

class QLabel;
class QPushButton;
class QTimer;

// C1: Bottom-anchored transient notification.
//
// Floats over its parent widget for a short duration, then removes itself.
// Optional action ("Retry") fires the provided callback and dismisses. At
// most one Toast lives per parent — calling show() while one is already
// visible replaces it.
//
// Reusable across Tankoyomi / Tankorent / Stream — not tied to any of those
// pages. First consumer is scraper-error feedback in TankoyomiPage.
class Toast : public QFrame
{
    Q_OBJECT

public:
    // Info toast — no action button.
    static void show(QWidget* parent, const QString& message,
                     int durationMs = 3500);

    // Action toast — shows an inline action button with the given label.
    // Callback fires when clicked; toast auto-dismisses after click.
    static void show(QWidget* parent, const QString& message,
                     const QString& actionLabel,
                     std::function<void()> onAction,
                     int durationMs = 4500);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    explicit Toast(QWidget* parent, const QString& message,
                   const QString& actionLabel,
                   std::function<void()> onAction,
                   int durationMs);

    void repositionToBottomCenter();

    QLabel*               m_messageLabel = nullptr;
    QPushButton*          m_actionButton = nullptr;
    QTimer*               m_dismissTimer = nullptr;
    std::function<void()> m_onAction;
};
