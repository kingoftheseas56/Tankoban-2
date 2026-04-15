#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

class TrackPopover : public QFrame
{
    Q_OBJECT

public:
    explicit TrackPopover(QWidget* parent = nullptr);

    void populate(const QJsonArray& tracks, int currentAudioId,
                  int currentSubId, bool subVisible);
    void setDelay(int ms);
    void setStyle(int fontSize, int margin, bool outline);
    void toggle(QWidget* anchor = nullptr);
    bool isOpen() const;

    int     subFontSize() const;
    int     subMargin() const;
    bool    subOutline() const;
    QString subFontColor() const;
    int     subBgOpacity() const;

signals:
    void audioTrackSelected(int id);
    void subtitleTrackSelected(int id);
    void subDelayAdjusted(int deltaMs);
    void subStyleChanged(int fontSize, int margin, bool outline,
                         const QString& fontColor, int bgOpacity);
    void hoverChanged(bool hovered);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void dismiss();
    void installClickFilter();
    void removeClickFilter();
    void anchorAbove(QWidget* anchor);

    void onAudioClicked(QListWidgetItem* item);
    void onSubClicked(QListWidgetItem* item);
    void onStyleWidgetChanged();
    void emitStyleChanged();

    QListWidget* m_audioList   = nullptr;
    QListWidget* m_subList     = nullptr;
    QPushButton* m_delayMinus  = nullptr;
    QPushButton* m_delayPlus   = nullptr;
    QPushButton* m_delayReset  = nullptr;
    QLabel*      m_delayLabel  = nullptr;
    QSlider*     m_fontSizeSlider = nullptr;
    QSlider*     m_marginSlider   = nullptr;
    QLabel*      m_fontSizeVal = nullptr;
    QLabel*      m_marginVal   = nullptr;
    QCheckBox*   m_outlineCb   = nullptr;
    QComboBox*   m_fontColorCombo = nullptr;
    QSlider*     m_bgOpacitySlider = nullptr;
    QLabel*      m_bgOpacityVal = nullptr;
    QTimer*      m_styleDebounce = nullptr;
    bool         m_clickFilterInstalled = false;
    // Anchor tracked across toggle()/dismiss() — see FilterPopover note.
    QPointer<QWidget> m_anchor;
};
