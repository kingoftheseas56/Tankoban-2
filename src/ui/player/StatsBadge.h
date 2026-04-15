#pragma once

#include <QWidget>

class QLabel;

// VIDEO_PLAYER_FIX Batch 7.1 — compact performance / source-metadata
// overlay. Single-line "{codec} · {W}x{H} · {fps} fps · {drops} drops"
// rendered top-right of the video surface. Auto-sized; translucent dark
// background, gray/white text — consistent with ToastHud/CenterFlash
// aesthetic, no color accents per `feedback_no_color_no_emoji`.
//
// Data is owned by VideoPlayer (populated from sidecar firstFrame event
// + FrameCanvas::framesSkipped). StatsBadge is a dumb view: caller calls
// `setStats(...)` whenever a field refreshes.
class StatsBadge : public QWidget {
    Q_OBJECT
public:
    explicit StatsBadge(QWidget* parent = nullptr);

    // Pass fps == 0 or drops == (quint64)-1 to render the "unknown" dash
    // for the corresponding field. codec empty hides the whole badge.
    void setStats(const QString& codec, int width, int height,
                  double fps, quint64 drops);

    // Empty the label; caller hides the widget via setVisible(false) too.
    void clear();

private:
    QLabel* m_label = nullptr;
};
