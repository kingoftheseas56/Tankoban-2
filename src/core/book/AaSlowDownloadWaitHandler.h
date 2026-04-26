#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>

class QWebEngineView;

// Polling helper that watches a QWebEngineView for a caller-supplied JS
// predicate to flip true. Used by AnnaArchiveScraper to wait through AA's
// browser-verification interstitial (/books/<id> serves a challenge page
// that the off-the-record webview auto-clears given a few seconds), slow-
// download countdown pages, and "no_cloudflare" warning pages.
//
// Contract:
//   - Caller owns the QWebEngineView; the handler does not take ownership.
//   - Predicate JS must evaluate to a boolean (truthy/falsy) via
//     runJavaScript. Handler treats any truthy return as success.
//   - waitCompleted() fires exactly once on the first truthy poll.
//   - waitFailed(reason) fires exactly once on timeout or view invalidation.
//   - cancel() stops polling silently with no signal.
//   - Destruction mid-poll is safe: pending runJavaScript callbacks guard
//     against dangling-this via QPointer.
class AaSlowDownloadWaitHandler : public QObject
{
    Q_OBJECT
public:
    AaSlowDownloadWaitHandler(QWebEngineView* view,
                              QString predicateJs,
                              int pollIntervalMs = 500,
                              int timeoutMs = 30000,
                              QObject* parent = nullptr);

    void start();
    void cancel();

signals:
    void waitCompleted();
    void waitFailed(const QString& reason);

protected:
    // Pump one poll iteration. Virtual for test-stubbing without a real
    // QWebEngineView. Production implementation calls runJavaScript.
    virtual void doPoll();

    // Called by doPoll's result callback. Protected for test-stubbing so
    // subclasses can fire synthetic results directly.
    void handlePollResult(bool predicateTrue);

private:
    void onTick();
    QString predicatePreview() const;

    QWebEngineView* m_view;
    QString         m_predicateJs;
    QTimer          m_pollTimer;
    QElapsedTimer   m_budget;
    int             m_timeoutMs;
    bool            m_finished = false;
};
