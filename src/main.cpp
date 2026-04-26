#include <QApplication>
#include <QIcon>
#include <QScreen>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStringList>
#include "core/CoreBridge.h"
#include "core/DebugLogBuffer.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

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
// applyDarkPalette() + noirStylesheet() moved to src/ui/Theme.cpp as part of
// THEME_SYSTEM_FIX Phase 1 (2026-04-25). Theme system is now (mode, preset)-
// driven via Theme::applyThemeFromSettings(app) below — see src/ui/Theme.h.

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

    if (signalExistingInstance())
        return 0;

    // Boot breadcrumbs — routed through DebugLogBuffer (REPO_HYGIENE P1.2,
    // 2026-04-26). Prior pattern wrote to a relative `_boot_debug.txt` in CWD
    // on every launch; that file was a developer-machine debug artifact that
    // shouldn't appear in user runs. The ring buffer is in-memory by default;
    // set TANKOBAN_DEBUG_LOG=1 to flush to <AppDataLocation>/debug.log.
    auto dbg = [](const char* msg) {
        DebugLogBuffer::instance().info("boot", QString::fromLatin1(msg));
    };
    dbg("1-app-created");

    Theme::applyThemeFromSettings(app);
    app.setQuitOnLastWindowClosed(false);
    dbg("2-palette-and-style-set");

    // Data layer
    QString dataDir = CoreBridge::resolveDataDir();
    dbg("3-datadir-resolved");
    CoreBridge bridge(dataDir);
    dbg("4-corebridge-created");

    MainWindow window(&bridge);
    dbg("5-mainwindow-created");

    // Single-instance: claim the local socket so subsequent launches signal us.
    auto *instanceServer = createInstanceServer(&window);
    Q_UNUSED(instanceServer);  // window-parented, dies with window

    // REPO_HYGIENE Phase 3 (2026-04-26) — dev-control bridge (gated dev-only).
    // build_and_run.bat passes --dev-control automatically so the bridge is
    // live for any agent / tankoctl smoke. Production NSIS builds (Phase 6)
    // will not pass the flag and will not advertise the socket.
    const QStringList devArgs = QCoreApplication::arguments();
    const bool devControlFlag = devArgs.contains(QStringLiteral("--dev-control"));
    const bool devControlEnv  = qEnvironmentVariableIntValue("TANKOBAN_DEV_CONTROL") == 1;
    if (devControlFlag || devControlEnv) {
        window.enableDevControl();
        dbg("6a-devcontrol-enabled");
    }

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
