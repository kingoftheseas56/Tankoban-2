#pragma once

#include <QWidget>
#include <QStackedWidget>
#include <QPushButton>
#include <QLabel>
#include <QFrame>

class CoreBridge;
class TorrentClient;
class TankorentPage;
class TankoyomiPage;

class SourcesPage : public QWidget
{
    Q_OBJECT

public:
    explicit SourcesPage(CoreBridge* bridge, TorrentClient* client, QWidget* parent = nullptr);

    void activate();
    void deactivate();

private:
    void buildUI();
    void navigateTo(int index);
    void navigateHome();

    CoreBridge*    m_bridge;
    TorrentClient* m_client = nullptr;

    // Back bar
    QFrame*      m_backBar   = nullptr;
    QPushButton* m_backBtn   = nullptr;
    QLabel*      m_backTitle = nullptr;

    // Stack: 0 = launcher, 1 = tankorent, 2 = tankoyomi
    QStackedWidget* m_stack = nullptr;

    // Launcher tiles
    QPushButton* m_tankorentTile = nullptr;
    QPushButton* m_tankoyomiTile = nullptr;

    // Sub-pages
    TankorentPage* m_tankorentPage = nullptr;
    TankoyomiPage* m_tankoyomiPage = nullptr;
};
