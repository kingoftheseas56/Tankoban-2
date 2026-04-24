#include <QApplication>
#include <QIcon>
#include <QScreen>
#include <QLocalServer>
#include <QLocalSocket>
#include "core/CoreBridge.h"
#include "ui/MainWindow.h"

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>
#include <avrt.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#endif

// ── Single-instance IPC ─────────────────────────────────────────────────────

static const char *INSTANCE_SERVER_NAME = "TankobanSingleInstance";

/// Try to signal an existing instance to come to front.
/// Returns true if a live instance acknowledged (caller should exit).
static bool signalExistingInstance()
{
    QLocalSocket socket;
    socket.connectToServer(INSTANCE_SERVER_NAME);
    if (!socket.waitForConnected(500))
        return false;

    // Connected — send raise and wait for "ok" ack from the live instance
    socket.write("raise");
    socket.waitForBytesWritten(500);
    if (socket.waitForReadyRead(1000)) {
        QByteArray reply = socket.readAll();
        socket.disconnectFromServer();
        if (reply == "ok")
            return true; // Live instance confirmed — exit
    }
    socket.disconnectFromServer();

    // Connected but no ack — stale pipe from a crashed run.
    // Remove it so we can take over.
    QLocalServer::removeServer(INSTANCE_SERVER_NAME);
    return false;
}

/// Create a local server that listens for "raise" messages from new instances.
static QLocalServer *createInstanceServer(MainWindow *window)
{
    auto *server = new QLocalServer(window);
    if (!server->listen(INSTANCE_SERVER_NAME)) {
        // Stale pipe — remove and retry
        QLocalServer::removeServer(INSTANCE_SERVER_NAME);
        server->listen(INSTANCE_SERVER_NAME);
    }

    QObject::connect(server, &QLocalServer::newConnection, window, [server, window]() {
        while (auto *conn = server->nextPendingConnection()) {
            conn->waitForReadyRead(500);
            // Raise the window and acknowledge
            window->bringToFront();
            conn->write("ok");
            conn->waitForBytesWritten(500);
            conn->deleteLater();
        }
    });

    return server;
}

// ── Theme ───────────────────────────────────────────────────────────────────

static void applyDarkPalette(QApplication &app)
{
    QPalette pal;
    pal.setColor(QPalette::Window,          QColor(0x05, 0x05, 0x05));
    pal.setColor(QPalette::WindowText,      QColor(0xee, 0xee, 0xee));
    pal.setColor(QPalette::Base,            QColor(0x05, 0x05, 0x05));
    pal.setColor(QPalette::AlternateBase,   QColor(0x0a, 0x0a, 0x0a));
    pal.setColor(QPalette::ToolTipBase,     QColor(0x0a, 0x0a, 0x0a));
    pal.setColor(QPalette::ToolTipText,     QColor(0xee, 0xee, 0xee));
    pal.setColor(QPalette::Text,            QColor(0xee, 0xee, 0xee));
    pal.setColor(QPalette::Button,          QColor(0x0a, 0x0a, 0x0a));
    pal.setColor(QPalette::ButtonText,      QColor(0xee, 0xee, 0xee));
    pal.setColor(QPalette::BrightText,      QColor(0xff, 0x44, 0x44));
    pal.setColor(QPalette::Link,            QColor(0xc7, 0xa7, 0x6b));
    pal.setColor(QPalette::Highlight,       QColor(0xc7, 0xa7, 0x6b, 0x38));
    pal.setColor(QPalette::HighlightedText, QColor(0xee, 0xee, 0xee));
    pal.setColor(QPalette::Disabled, QPalette::Text,       QColor(0x55, 0x55, 0x55));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x55, 0x55, 0x55));
    app.setPalette(pal);
}

static const char* noirStylesheet()
{
    return R"qss(
/* ── Base: glass transparency ── */

* {
    font-family: "Segoe UI Variable", "Segoe UI", "Inter", sans-serif;
}

QWidget {
    background: transparent;
    color: #eeeeee;
}

QScrollArea {
    background: transparent;
    border: none;
}

/* ── Brand ── */

QLabel#Brand {
    color: #eeeeee;
    font-size: 14px;
    font-weight: 800;
    letter-spacing: 0.2px;
}

/* ── Topbar: frosted glass strip ── */

QFrame#TopBar {
    min-height: 56px;
    max-height: 56px;
    background: rgba(8,8,8,0.52);
    border-bottom: 1px solid rgba(255,255,255,0.10);
}

QFrame#TopNav {
    background: transparent;
}

/* ── Nav buttons: pill-shaped, glass surface ── */

QPushButton#TopNavButton {
    text-align: left;
    color: rgba(255,255,255,0.78);
    background: rgba(255,255,255,0.06);
    border: 1px solid rgba(255,255,255,0.10);
    border-radius: 14px;
    padding: 4px 14px;
    min-height: 22px;
    font-weight: 800;
    font-size: 11px;
}

QPushButton#TopNavButton:hover {
    background: rgba(255,255,255,0.10);
    border-color: rgba(255,255,255,0.16);
    color: rgba(255,255,255,0.92);
}

QPushButton#TopNavButton:checked {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 rgba(199,167,107,0.22), stop:1 rgba(255,255,255,0.06));
    border-color: rgba(199,167,107,0.40);
    color: #eeeeee;
    font-weight: 800;
}

/* ── Icon buttons ── */

QPushButton#IconButton {
    text-align: center;
    color: rgba(255,255,255,0.78);
    background: rgba(255,255,255,0.06);
    border: 1px solid rgba(255,255,255,0.10);
    border-radius: 12px;
    padding: 0px;
    min-width: 28px;  max-width: 28px;
    min-height: 24px; max-height: 24px;
    font-weight: 700;
    font-size: 11px;
}

QPushButton#IconButton:hover {
    background: rgba(255,255,255,0.10);
    border-color: rgba(255,255,255,0.16);
}

QPushButton#IconButton:disabled {
    color: rgba(238,238,238,0.58);
    border-color: rgba(255,255,255,0.06);
}

/* ── Content area ── */

QFrame#Content {
    background: transparent;
}

/* ── Labels ── */

QLabel {
    color: #eeeeee;
}

/* ── Sidebar: translucent glass ── */

QFrame#LibrarySidebar {
    min-width: 252px;
    max-width: 252px;
    background: rgba(8,8,8,0.46);
    border-right: 1px solid rgba(255,255,255,0.10);
    border-radius: 0px;
}

/* ── Section titles ── */

QLabel#SectionTitle {
    color: #eeeeee;
    font-size: 16px;
    font-weight: 800;
    letter-spacing: 0.2px;
}

/* ── Sidebar action buttons ── */

QPushButton#SidebarAction {
    text-align: left;
    color: rgba(255,255,255,0.86);
    background: rgba(255,255,255,0.06);
    border: 1px solid rgba(255,255,255,0.10);
    border-radius: 10px;
    padding: 6px 10px;
    font-size: 11px;
    font-weight: 600;
}

QPushButton#SidebarAction:hover {
    background: rgba(255,255,255,0.10);
    border-color: rgba(255,255,255,0.16);
}

/* ── Tile cards ── */

QFrame#TileCard {
    background: transparent;
    border: none;
}

QFrame#TileImageWrap {
    border: 1px solid rgba(255,255,255,0.10);
    background: #0a0a0a;
    border-radius: 12px;
}

QLabel#TileTitle {
    font-size: 12px;
    font-weight: 700;
    color: #eeeeee;
}

QLabel#TileSubtitle {
    font-size: 11px;
    color: rgba(238,238,238,0.58);
}

/* ── Tables / Trees / Lists ── */

QTableWidget, QListWidget, QTreeWidget {
    background: rgba(255,255,255,0.05);
    border: 1px solid rgba(255,255,255,18);
    border-radius: 10px;
    selection-background-color: rgba(199,167,107,0.22);
    selection-color: #eeeeee;
    font-size: 11px;
    outline: 0;
}

QHeaderView::section {
    background: rgba(255,255,255,0.06);
    color: #eeeeee;
    border: none;
    border-bottom: 1px solid rgba(255,255,255,0.10);
    padding: 5px 8px;
    font-size: 11px;
    font-weight: 800;
}

/* ── Scrollbars: thin-bubble, brighten on hover ── */
/* Tankoban-Max overhaul.css thin-bubble convention: track has breathing room
   around the handle via handle-margin (not scrollbar-padding — Qt QSS ignores
   padding on QScrollBar). Thumb sits as a pill "floating" inside the track,
   with hover/pressed brightening steps for interaction feedback. */

QScrollBar:vertical {
    background: transparent;
    border: none;
    width: 12px;
    margin: 0;
}
QScrollBar:horizontal {
    background: transparent;
    border: none;
    height: 12px;
    margin: 0;
}

QScrollBar::handle:vertical {
    background: rgba(255,255,255,60);
    border-radius: 4px;
    min-height: 32px;
    margin: 2px 3px;
}
QScrollBar::handle:horizontal {
    background: rgba(255,255,255,60);
    border-radius: 4px;
    min-width: 32px;
    margin: 3px 2px;
}

QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover {
    background: rgba(255,255,255,110);
}

QScrollBar::handle:vertical:pressed, QScrollBar::handle:horizontal:pressed {
    background: rgba(255,255,255,150);
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
    background: none;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0px;
    background: none;
}
QScrollBar::add-page, QScrollBar::sub-page {
    background: none;
}

/* ── Context menus ── */

QMenu {
    background: rgba(8,8,8,0.88);
    border: 1px solid rgba(255,255,255,30);
    border-radius: 10px;
    padding: 4px 0px;
}

QMenu::item {
    color: #eeeeee;
    padding: 6px 16px;
    font-size: 11px;
    background: transparent;
}

QMenu::item:selected {
    background: rgba(199,167,107,0.22);
}

QMenu::item:disabled {
    color: rgba(238,238,238,0.58);
}

QMenu::separator {
    height: 1px;
    background: rgba(255,255,255,18);
    margin: 4px 8px;
}

/* ── Input fields ── */

QLineEdit {
    background: rgba(255,255,255,0.05);
    color: #eeeeee;
    border: 1px solid rgba(255,255,255,18);
    border-radius: 10px;
    padding: 5px 10px;
    font-size: 11px;
    selection-background-color: rgba(199,167,107,0.22);
    selection-color: #eeeeee;
}

QLineEdit:focus {
    border-color: rgba(199,167,107,0.45);
}

QComboBox {
    background: rgba(255,255,255,0.05);
    color: #eeeeee;
    border: 1px solid rgba(255,255,255,18);
    border-radius: 10px;
    padding: 5px 10px;
    font-size: 11px;
}

QComboBox::drop-down {
    border: none;
    width: 18px;
}

QComboBox QAbstractItemView {
    background: rgba(8,8,8,0.88);
    border: 1px solid rgba(255,255,255,30);
    border-radius: 10px;
    selection-background-color: rgba(199,167,107,0.22);
    selection-color: #eeeeee;
    outline: 0;
}

/* ── Toast ── */

QLabel#ShellToast {
    background: rgba(8,8,8,0.82);
    color: #e0e0e0;
    border-radius: 6px;
    padding: 6px 18px;
    font-size: 13px;
}

/* ── Root folders overlay ── */

QWidget#root_folders_overlay {
    background: rgba(0,0,0,180);
}

QFrame#RootFoldersCard {
    background: rgba(8,8,8,0.92);
    border: 1px solid rgba(255,255,255,0.12);
    border-radius: 14px;
}

QLabel#RootFoldersTitle {
    font-size: 14px;
    font-weight: 800;
    color: #eeeeee;
}

QPushButton#RootFoldersCloseBtn {
    background: rgba(255,255,255,0.06);
    border: 1px solid rgba(255,255,255,0.10);
    border-radius: 13px;
    color: rgba(255,255,255,0.78);
    font-size: 14px;
    font-weight: bold;
}

QPushButton#RootFoldersCloseBtn:hover {
    background: rgba(255,255,255,0.12);
}

QPushButton#RootFoldersAddBtn {
    background: rgba(255,255,255,0.06);
    border: 1px solid rgba(255,255,255,0.10);
    border-radius: 10px;
    color: rgba(255,255,255,0.78);
    font-size: 11px;
    font-weight: 600;
}

QPushButton#RootFoldersAddBtn:hover {
    background: rgba(255,255,255,0.10);
    border-color: rgba(255,255,255,0.16);
}

/* ── Tooltips ── */

QToolTip {
    background: rgba(8,8,8,0.92);
    color: #eeeeee;
    border: 1px solid rgba(255,255,255,0.12);
    border-radius: 6px;
    padding: 4px 8px;
    font-size: 11px;
}
)qss";
}

#ifdef Q_OS_WIN
static void applyWindowsDarkTitleBar(QWidget *window)
{
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    BOOL useDark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));

    // Dark background brush to prevent white flash on resize
    HBRUSH darkBrush = CreateSolidBrush(RGB(0x05, 0x05, 0x05));
    SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(darkBrush));
}
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    SetProcessDPIAware();
#endif

    QApplication app(argc, argv);
    app.setApplicationName("Tankoban");
    app.setOrganizationName("Tankoban");
    app.setApplicationVersion("0.1.0");
    app.setWindowIcon(QIcon(":/icons/tankoban_app_icon.png"));

    // Debug breadcrumbs — writing to file since /subsystem:windows has no console
    auto dbg = [](const char* msg) {
        FILE* f = fopen("_boot_debug.txt", "a");
        if (f) { fprintf(f, "%s\n", msg); fflush(f); fclose(f); }
    };
    dbg("1-app-created");

    applyDarkPalette(app);
    app.setStyleSheet(noirStylesheet());
    app.setQuitOnLastWindowClosed(false);
    dbg("2-palette-and-style-set");

    // Data layer
    QString dataDir = CoreBridge::resolveDataDir();
    dbg("3-datadir-resolved");
    CoreBridge bridge(dataDir);
    dbg("4-corebridge-created");

    MainWindow window(&bridge);
    dbg("5-mainwindow-created");

#ifdef Q_OS_WIN
    applyWindowsDarkTitleBar(&window);
#endif
    dbg("6-darkbar-applied");

    window.showMaximized();
    window.raise();
    window.activateWindow();
    dbg("7-window-shown");

#ifdef Q_OS_WIN
    // Force foreground on Windows — showMaximized alone isn't enough
    SetForegroundWindow(reinterpret_cast<HWND>(window.winId()));

    // MMCSS: tell Windows scheduler the GUI/render thread does timing-critical
    // media work. The Qt GUI thread is also where QRhiWidget renders to vsync,
    // so this directly reduces frame-presentation jitter under CPU load.
    DWORD mmcss_idx = 0;
    HANDLE mmcss_handle = AvSetMmThreadCharacteristicsW(L"Playback", &mmcss_idx);
#endif

    int ret = app.exec();

#ifdef Q_OS_WIN
    if (mmcss_handle) AvRevertMmThreadCharacteristics(mmcss_handle);
#endif
    return ret;
}
