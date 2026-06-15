#pragma once

#include <QWidget>

#ifdef HAS_WEBENGINE
class QWebEngineView;
class QWebChannel;
class TankobanWebBridge;
#endif

// ── WebShell — host for the Electron React UI inside our native window (Phase 0) ──
//
// A plain QWidget wrapping a QWebEngineView that loads Tankoban Electron's
// pre-built renderer bundle (index.html + assets/) and bridges it to native
// C++ via QWebChannel + an injected `window.api` shim. Mirrors BookReader's
// WebEngine setup (qwebchannel.js injection from the Qt resource system, a
// DocumentCreation/MainWorld shim script, the two LocalContent* attributes).
//
// Load path (Phase 0): TANKOBAN_WEBUI_DIR env var, else <appDir>/webui, then
//   "<dir>/index.html" via QUrl::fromLocalFile(). Agent 0 points
//   TANKOBAN_WEBUI_DIR at the Electron out/renderer dir for the smoke.
//
// Gated behind HAS_WEBENGINE (same as BookReader): if WebEngine is not linked,
// WebShell is an empty placeholder widget and MainWindow keeps the native UI.

class WebShell : public QWidget {
    Q_OBJECT
public:
    // `topLevel` is forwarded to the bridge for the window:* fullscreen ops
    // (pass the MainWindow). Ownership of WebShell stays with its QWidget parent.
    explicit WebShell(QWidget* topLevel, QWidget* parent = nullptr);
    ~WebShell() override;

private:
#ifdef HAS_WEBENGINE
    QWebEngineView*    m_webView = nullptr;
    QWebChannel*       m_channel = nullptr;
    TankobanWebBridge* m_bridge  = nullptr;
#endif
};
