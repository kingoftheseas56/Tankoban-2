#pragma once

#include <QFrame>
#include <QString>

class QPushButton;
class QPropertyAnimation;
class QLabel;
class QGraphicsOpacityEffect;

namespace tankoban_sidebar {
class BackdropWidget;
}

class SidebarDrawer : public QFrame
{
    Q_OBJECT
public:
    explicit SidebarDrawer(QWidget* contentParent);
    ~SidebarDrawer() override;

    void open();
    void close();
    void toggle();
    bool isOpen() const { return m_open; }

    // pageId in {"tankorent","tankolibrary","streamDownloads"}; empty string clears.
    void setActiveSource(const QString& pageId);

signals:
    void sourceClicked(const QString& pageId);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    void layoutToParent();   // size + position drawer + backdrop to parent geometry
    void captureBackdropSnapshot();
    void releaseBackdropSnapshot();
    void onSidebarItemClicked(const QString& pageId);
    void styleItem(QPushButton* btn, const QString& pageId);

    QWidget* m_contentParent = nullptr;
    tankoban_sidebar::BackdropWidget* m_backdrop = nullptr;
    QPropertyAnimation* m_slideAnim = nullptr;
    QPropertyAnimation* m_backdropFade = nullptr;
    QGraphicsOpacityEffect* m_backdropOpacity = nullptr;

    QPushButton* m_btnTankorent = nullptr;
    QPushButton* m_btnTankoLibrary = nullptr;
    QPushButton* m_btnStreamDownloads = nullptr;

    QString m_activePageId;
    bool m_open = false;
    bool m_animating = false;

    static constexpr int kDrawerWidth   = 280;
    static constexpr int kTopBarHeight  = 56;
    static constexpr int kSlideMs       = 220;
    static constexpr int kFadeMs        = 220;  // matches slide so user perceives one motion
    static constexpr int kItemHeight    = 40;
};
