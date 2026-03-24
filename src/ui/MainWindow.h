#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QShortcut>
#include <QButtonGroup>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QCloseEvent>

class GlassBackground;
class CoreBridge;
class RootFoldersOverlay;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(CoreBridge* bridge, QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildTopBar();
    void buildPageStack();
    void bindShortcuts();
    void activatePage(const QString &pageId);
    void showRootFolders();
    void hideRootFolders();

    // System tray
    void setupTrayIcon();
    void hideToTray();
    void restoreFromTray();
    void quitFromTray();

    // Map page id → domain for root folders
    QString domainForPage(const QString& pageId) const;

    CoreBridge *m_bridge = nullptr;

    // Glass background
    GlassBackground *m_glassBg = nullptr;

    // Root folders overlay
    RootFoldersOverlay *m_rootFoldersOverlay = nullptr;

    // System tray
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu           *m_trayMenu = nullptr;
    bool m_quitRequested = false;
    bool m_wasMaximizedBeforeHide = false;

    // Top bar
    QWidget       *m_topBar      = nullptr;
    QLabel        *m_brandLabel  = nullptr;
    QButtonGroup  *m_navGroup    = nullptr;

    // Page stack
    QStackedWidget *m_pageStack = nullptr;

    // Navigation buttons keyed by page id
    struct NavButton {
        QString     pageId;
        QPushButton *button = nullptr;
    };
    QVector<NavButton> m_navButtons;

    QString m_activePageId;
};
