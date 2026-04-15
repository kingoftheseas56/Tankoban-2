#pragma once

#include <QList>
#include <QString>
#include <QWidget>

#include "StreamSourceChoice.h"

class QFrame;
class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace tankostream::stream {

class StreamSourceCard;

// Scrollable container for StreamSourceCard rows. Sits in the right column
// of StreamDetailView and presents one of four states: placeholder (e.g.
// "Select an episode to see sources"), loading, populated, or error/empty.
//
// The five `set*` slots are state transitions — each clears cards and
// swaps in the appropriate visual. `setSources` also accepts an optional
// `savedChoiceKey` to highlight the card matching the user's last choice.
class StreamSourceList : public QWidget
{
    Q_OBJECT

public:
    explicit StreamSourceList(QWidget* parent = nullptr);

    void setPlaceholder(const QString& message);
    void setLoading();
    void setSources(const QList<StreamPickerChoice>& choices,
                    const QString&                   savedChoiceKey = {});
    void setEmpty();
    void setError(const QString& message);

    // Phase 2 Batch 2.4 — auto-launch toast. StreamPage arms it when a saved
    // source matches the incoming stream list AND the timestamp gate passes;
    // user clicks "Pick different" (or the timer elapses and auto-launch
    // fires). The toast lives above the scroll area; non-blocking to picker.
    void showAutoLaunchToast(const QString& label);
    void hideAutoLaunchToast();

signals:
    void sourceActivated(const tankostream::stream::StreamPickerChoice& choice);
    void autoLaunchCancelRequested();

private:
    void buildUI();
    void clearCards();
    void showStatus(const QString& message, bool emphasizeError = false);

    QScrollArea* m_scroll         = nullptr;
    QWidget*     m_cardsContainer = nullptr;
    QVBoxLayout* m_cardsLayout    = nullptr;
    QLabel*      m_statusLabel    = nullptr;

    // Batch 2.4 — auto-launch toast widgets. Hidden by default.
    QFrame*      m_autoLaunchToast = nullptr;
    QLabel*      m_autoLaunchLabel = nullptr;

    QList<StreamSourceCard*> m_cards;
};

}
