#pragma once

#include <QColor>
#include <QRectF>
#include <QWidget>

class QPainter;
class QPropertyAnimation;

class ChapterDownloadIndicator : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int progress READ progress WRITE setProgressImmediate NOTIFY progressChanged)
public:
    enum class State {
        NotDownloaded,
        Queued,
        Downloading,
        Downloaded,
        Errored,
    };
    Q_ENUM(State)

    explicit ChapterDownloadIndicator(QWidget* parent = nullptr);

    State state() const { return m_state; }
    int progress() const { return m_progress; }     // 0-100

public slots:
    void setState(State s);
    void setProgress(int pct);   // clamped to [0, 100]

signals:
    void clicked();
    void stateChanged(State newState);
    void progressChanged(int newProgress);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    QSize sizeHint() const override { return QSize(28, 28); }

private slots:
    void setProgressImmediate(int pct);

private:
    State m_state    = State::NotDownloaded;
    int   m_progress = 0;
    QPropertyAnimation* m_progressAnim = nullptr;

    void paintArrow(QPainter& p, const QRectF& r, const QColor& c) const;
    void paintSpinnerWithArrow(QPainter& p, const QRectF& r, const QColor& c) const;
    void paintProgressArc(QPainter& p, const QRectF& r, const QColor& fg, const QColor& bg, int pct) const;
    void paintCheck(QPainter& p, const QRectF& r, const QColor& fg, const QColor& bg) const;
    void paintError(QPainter& p, const QRectF& r, const QColor& c) const;
};
