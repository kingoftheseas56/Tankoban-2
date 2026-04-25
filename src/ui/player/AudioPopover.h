#pragma once

#include <QFrame>
#include <QJsonArray>
#include <QListWidget>
#include <QPointer>

// Audio-track-only popover. Replaces TrackPopover's audio section
// after the 2026-04-25 minimalist HUD redesign. Single QListWidget
// with one selectable row per track. Selection emits the track id;
// VideoPlayer routes to sidecar + saves the language preference.
class AudioPopover : public QFrame
{
    Q_OBJECT

public:
    explicit AudioPopover(QWidget* parent = nullptr);

    void populate(const QJsonArray& tracks, int currentAudioId);
    void toggle(QWidget* anchor = nullptr);
    bool isOpen() const { return isVisible(); }

signals:
    void audioTrackSelected(int id);
    void hoverChanged(bool hovered);
    // VIDEO_HUD_MINIMALIST polish 2026-04-25 — see SubtitlePopover.h
    // for rationale; fired from dismiss() so the anchor chip's
    // :checked state mirrors popover visibility in lockstep.
    void dismissed();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void dismiss();
    void installClickFilter();
    void removeClickFilter();
    void anchorAbove(QWidget* anchor);
    void onItemClicked(QListWidgetItem* item);

    QListWidget* m_list = nullptr;
    bool m_clickFilterInstalled = false;
    QPointer<QWidget> m_anchor;
};
