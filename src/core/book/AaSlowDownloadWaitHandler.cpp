#include "AaSlowDownloadWaitHandler.h"

#include <QDebug>
#include <QPointer>

#ifdef HAS_WEBENGINE
#include <QWebEnginePage>
#include <QWebEngineView>
#endif

AaSlowDownloadWaitHandler::AaSlowDownloadWaitHandler(QWebEngineView* view,
                                                     QString predicateJs,
                                                     int pollIntervalMs,
                                                     int timeoutMs,
                                                     QObject* parent)
    : QObject(parent)
    , m_view(view)
    , m_predicateJs(std::move(predicateJs))
    , m_timeoutMs(timeoutMs)
{
    m_pollTimer.setInterval(pollIntervalMs);
    m_pollTimer.setSingleShot(false);
    connect(&m_pollTimer, &QTimer::timeout, this, &AaSlowDownloadWaitHandler::onTick);
}

void AaSlowDownloadWaitHandler::start()
{
    if (m_finished) return;
    m_budget.start();
    m_pollTimer.start();

    // Fire one immediate poll so a predicate that's already true wins without
    // eating the first 500ms interval tax.
    onTick();
}

void AaSlowDownloadWaitHandler::cancel()
{
    if (m_finished) return;
    m_finished = true;
    m_pollTimer.stop();
}

void AaSlowDownloadWaitHandler::onTick()
{
    if (m_finished) return;

    if (m_budget.isValid() && m_budget.elapsed() >= m_timeoutMs) {
        m_finished = true;
        m_pollTimer.stop();
        emit waitFailed(QStringLiteral("timeout after %1ms waiting for: %2")
                        .arg(m_timeoutMs)
                        .arg(predicatePreview()));
        return;
    }

    doPoll();
}

void AaSlowDownloadWaitHandler::doPoll()
{
#ifdef HAS_WEBENGINE
    if (!m_view || !m_view->page()) {
        m_finished = true;
        m_pollTimer.stop();
        emit waitFailed(QStringLiteral("webview invalidated"));
        return;
    }

    // Guard against destruction between dispatch and callback. The handler
    // may outlive its useful life (cancel + delete by caller) while a
    // runJavaScript call is still queued in the render process.
    QPointer<AaSlowDownloadWaitHandler> self(this);
    m_view->page()->runJavaScript(m_predicateJs, [self](const QVariant& result) {
        if (!self) return;
        if (self->m_finished) return;
        self->handlePollResult(result.toBool());
    });
#else
    m_finished = true;
    m_pollTimer.stop();
    emit waitFailed(QStringLiteral("webengine unavailable in this build"));
#endif
}

void AaSlowDownloadWaitHandler::handlePollResult(bool predicateTrue)
{
    if (m_finished) return;
    if (!predicateTrue) return;  // keep polling until budget exhausts

    m_finished = true;
    m_pollTimer.stop();
    emit waitCompleted();
}

QString AaSlowDownloadWaitHandler::predicatePreview() const
{
    constexpr int kCap = 80;
    QString flat = m_predicateJs;
    flat.replace(QChar('\n'), QChar(' '));
    flat = flat.simplified();
    if (flat.size() > kCap) {
        flat = flat.left(kCap) + QStringLiteral("...");
    }
    return flat;
}
