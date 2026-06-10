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
    // THEATRE_STREAMING_RESTORE P2 (2026-06-10) — non-destructive status for
    // playback (buffering / stream-failed) shown BELOW the cards without clearing
    // them, so a pick-first user can choose another source after a failure.
    // (setError clears the cards + hides the scroll; this does neither.)
    void showPlaybackStatus(const QString& message, bool isError);

    // Phase 2 Batch 2.4 — auto-launch toast. StreamPage arms it when a saved
    // source matches the incoming stream list AND the timestamp gate passes;
    // user clicks "Pick different" (or the timer elapses and auto-launch
    // fires). The toast lives above the scroll area; non-blocking to picker.
    void showAutoLaunchToast(const QString& label);
    void hideAutoLaunchToast();

    // v1.3 dev-bridge helper (A4S3, 2026-05-19) — programmatic accessors that
    // the tankoctl dev-control surface uses to drive what a user does when
    // they click "Direct Download" on the Nth source card. Behavioural parity:
    // emits the same directDownloadRequested signal the StreamSourceCard would
    // re-emit on user invocation, so the entire downstream chain (StreamDetailView
    // → StreamPage::onDirectDownloadRequested → TorrentClient::startDownload)
    // fires identically. Returns false (without emitting) when index is out of
    // range. Optional out-params receive a snapshot of the dispatched choice
    // for caller logging; pass nullptr to skip.
    int  sourceCardCount() const { return m_cards.size(); }
    bool triggerDirectDownloadAt(int      index,
                                 QString* outAddonName   = nullptr,
                                 QString* outDisplayName = nullptr,
                                 bool*    outHasMagnet   = nullptr);

    // v1.3 dev-bridge helpers (A4S2, 2026-05-19) — let StreamPage::devGetSources
    // snapshot the populated picker rows + tell "loading" apart from terminal-
    // but-empty states so smokes can wait the async StreamAggregator fan-out
    // out before picking a card. Both are read-only / non-mutating; safe to
    // call any time. isLoading() flips true in setLoading() and back to false
    // in setSources/setEmpty/setError/setPlaceholder.
    QList<StreamPickerChoice> snapshotChoices() const;
    bool isLoading() const { return m_loading; }

signals:
    void sourceActivated(const tankostream::stream::StreamPickerChoice& choice);
    void autoLaunchCancelRequested();

    // STREAM_ADD_TO_TANKORENT (2026-05-06) — re-emitted from each card's
    // addToTankorentRequested signal. StreamDetailView owns the next
    // upstream hop.
    void addToTankorentRequested(const tankostream::stream::StreamPickerChoice& choice);

    // THEATRE_DOWNLOAD_OVERHAUL UI refinement 2026-05-17 - parallel forward
    // of each card's directDownloadRequested. StreamDetailView re-emits to
    // StreamPage which dispatches via TorrentClient::startDownload.
    void directDownloadRequested(const tankostream::stream::StreamPickerChoice& choice);

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

    // v1.3 dev-bridge state flag (A4S2) — true between setLoading() and the
    // next terminal state setter (setSources/setEmpty/setError/setPlaceholder).
    // Read by StreamPage::devGetSources to distinguish in-flight aggregation
    // from a terminal "no sources" / "error" state.
    bool m_loading = false;
};

}
