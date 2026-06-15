#pragma once

#include <QObject>
#include <QString>
#include <QPointer>

class QNetworkAccessManager;
class QWidget;

// ── TankobanWebBridge — QWebChannel bridge for the QWebEngine UI pivot (Phase 0) ──
//
// Registered on the QWebChannel as "tankoban". The injected window.api shim
// (see WebShell.cpp) wraps every Electron-preload call into a single async
// request/response round-trip over this object:
//
//   JS:  b.request(requestId, channel, argsJson)   // argsJson = JSON array of args
//   C++: emit response(requestId, resultJson)       // resultJson parsed back in JS
//
// Mirrors BookBridge's Edge-TTS promise-correlation pattern (JS holds a
// requestId → resolver map; the bridge re-emits the matching id on completion),
// but generalized to one (request,response) pair routed by `channel` string.
//
// Phase 0 scope:
//   REAL  — "cinemeta:get" (keyless HTTP GET to v3-cinemeta.strem.io) proves
//           real data flows end-to-end through the bridge.
//   REAL  — "window:setFullscreen" / "window:toggleFullscreen" /
//           "window:isFullscreen" operate on the top-level window.
//   STUB  — every other channel (tmdb / anilist / mangadex / itunes / addons /
//           manga / comics / hg / ...) emits a shape-sane empty ("{}" or "[]")
//           so the renderer never hits an undefined call or white-screens.
//
// Later phases replace the stub branches with real native-engine wiring.

class TankobanWebBridge : public QObject {
    Q_OBJECT
public:
    // `topLevel` is the window the window:* fullscreen channels operate on
    // (the MainWindow). May be null in smoke harnesses — those channels then
    // no-op and report fullscreen=false. Ownership stays with the caller.
    explicit TankobanWebBridge(QWidget* topLevel, QObject* parent = nullptr);
    ~TankobanWebBridge() override;

    // Single async entry point. `channel` selects the dispatch branch;
    // `argsJson` is a JSON array string of the call's positional args.
    // Every path eventually emits exactly one response(requestId, ...).
    Q_INVOKABLE void request(const QString& requestId,
                             const QString& channel,
                             const QString& argsJson);

signals:
    // Completion for a prior request(). `resultJson` is JSON.parse()-d by the
    // shim (falling back to the raw string if it is not valid JSON).
    void response(const QString& requestId, const QString& resultJson);

private:
    // REAL — keyless Cinemeta proxy. GET https://v3-cinemeta.strem.io/<path>.
    void handleCinemeta(const QString& requestId, const QString& path);

    // REAL — top-level window fullscreen ops; emits the resulting bool as JSON.
    void handleWindow(const QString& requestId, const QString& channel,
                      const QString& argsJson);

    // STUB — emit a shape-sane empty result and log the channel once.
    void emitStub(const QString& requestId, const QString& channel,
                  const QString& emptyJson);

    QNetworkAccessManager* nam();

    QPointer<QWidget>      m_topLevel;
    QNetworkAccessManager* m_nam = nullptr;  // lazily created via NetSeam; child of this
};
