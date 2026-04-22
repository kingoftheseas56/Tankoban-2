#pragma once

#include <QWidget>
#include <QListWidget>
#include <QCheckBox>
#include <QPointer>

class QToolButton;

class PlaylistDrawer : public QWidget {
    Q_OBJECT

public:
    explicit PlaylistDrawer(QWidget* parent = nullptr);

    void populate(const QStringList& paths, int currentIndex);
    // Optional anchor: when the drawer is opened via a chip click, pass
    // the chip so eventFilter can swallow a subsequent re-click on it
    // (preventing the dismiss-then-reopen race). Keyboard / menu paths
    // pass nullptr; dismiss still works for any outside click.
    void toggle(QWidget* anchor = nullptr);
    bool isOpen() const { return isVisible(); }
    bool isAutoAdvance() const;

    // VIDEO_PLAYER_FIX Batch 5.1 — queue mode state accessors. Mutex is
    // behavior-side, not UI-exclusive: user may leave all four checked,
    // and VideoPlayer::onEndOfFile applies precedence loopFile > repeatOne
    // > repeatAll > shuffle > normal-advance.
    bool shuffle()    const;
    bool repeatAll()  const;
    bool repeatOne()  const;
    bool loopFile()   const;

signals:
    void episodeSelected(int index);
    // VIDEO_PLAYER_FIX Batch 5.1 — VideoPlayer relays to sidecar via
    // sendSetLoopFile. The other three modes are consumed lazily at
    // EOF-dispatch time, no eager signaling needed.
    void loopFileChanged(bool enabled);
    // VIDEO_PLAYER_FIX Batch 5.2 — save/load requests. PlaylistDrawer is
    // UI-only — VideoPlayer holds the m_playlist truth and owns the file
    // dialogs + format parsing. Toolbar just surfaces the intent.
    void saveRequested();
    void loadRequested();

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    // VIDEO_PLAYER_UI_POLISH follow-up 2026-04-23 (hemanth-reported):
    // scrolling inside the drawer was also adjusting volume because
    // wheel events (from QListWidget at scroll limits, or from the
    // toolbar region) bubbled up to VideoPlayer::wheelEvent which
    // treats wheel as volume. Override wheelEvent here to accept all
    // wheel events in the drawer's bounding area so nothing leaks to
    // the parent. The list's own QListWidget::wheelEvent still runs
    // first (child-before-parent delivery) and scrolls when possible.
    void wheelEvent(QWheelEvent* event) override;

private:
    void dismiss();
    void installClickFilter();
    void removeClickFilter();
    void persistQueueMode() const;

    QListWidget* m_list                 = nullptr;
    QCheckBox*   m_autoAdvance          = nullptr;
    bool         m_clickFilterInstalled = false;
    // Anchor tracked across toggle()/dismiss() — see FilterPopover note.
    QPointer<QWidget> m_anchor;

    // VIDEO_PLAYER_FIX Batch 5.1 — queue-mode toolbar buttons. All
    // checkable, all persisted via QSettings("player/queueMode/*").
    QToolButton* m_btnShuffle    = nullptr;
    QToolButton* m_btnRepeatAll  = nullptr;
    QToolButton* m_btnRepeatOne  = nullptr;
    QToolButton* m_btnLoopFile   = nullptr;
};
