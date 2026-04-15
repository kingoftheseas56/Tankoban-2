#include "CloudflareCookieHarvester.h"

#include <QCoreApplication>
#include <QTimer>
#include <QSettings>
#include <QDateTime>
#include <QDebug>

#ifdef HAS_WEBENGINE
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineCookieStore>
#include <QNetworkCookie>
#endif

namespace {

constexpr int kHarvestTimeoutMs  = 30'000;
constexpr int kClearanceTtlDays  = 7;

QString settingsBase(const QString& indexerId)
{
    return QStringLiteral("tankorent/indexers/%1/").arg(indexerId);
}

} // namespace

CloudflareCookieHarvester* CloudflareCookieHarvester::instance()
{
    static CloudflareCookieHarvester* s_instance = nullptr;
    if (!s_instance) {
        // Parent to QCoreApplication so Qt destroys at shutdown without a
        // static destructor fight. Only safe to call after QCoreApplication
        // has been instantiated — Tankorent construction happens well after
        // main(), so that invariant holds by the time any indexer asks.
        s_instance = new CloudflareCookieHarvester(QCoreApplication::instance());
    }
    return s_instance;
}

CloudflareCookieHarvester::CloudflareCookieHarvester(QObject* parent)
    : QObject(parent)
{
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(kHarvestTimeoutMs);
    connect(m_timeout, &QTimer::timeout, this, &CloudflareCookieHarvester::onTimeout);
}

CloudflareCookieHarvester::~CloudflareCookieHarvester()
{
#ifdef HAS_WEBENGINE
    // m_view is parentless (QWidget can't be parented to a non-QWidget QObject);
    // own its lifetime explicitly. m_profile + m_timeout are QObject children
    // and auto-clean via Qt's parent ownership.
    delete m_view;
#endif
}

QString CloudflareCookieHarvester::cachedClearance(const QString& indexerId)
{
    return QSettings().value(settingsBase(indexerId) + QStringLiteral("cf_clearance")).toString();
}

QString CloudflareCookieHarvester::cachedUserAgent(const QString& indexerId)
{
    return QSettings().value(settingsBase(indexerId) + QStringLiteral("cf_user_agent")).toString();
}

QDateTime CloudflareCookieHarvester::clearanceExpires(const QString& indexerId)
{
    return QSettings().value(settingsBase(indexerId) + QStringLiteral("cf_clearance_expires"))
        .toDateTime();
}

void CloudflareCookieHarvester::reset()
{
    m_busy = false;
    m_currentIndexerId.clear();
    m_timeout->stop();
}

void CloudflareCookieHarvester::finishSuccess(const QString& cfClearance)
{
    const QString id = m_currentIndexerId;

#ifdef HAS_WEBENGINE
    const QString ua = m_profile ? m_profile->httpUserAgent() : QString();
#else
    const QString ua;
#endif

    QSettings s;
    const QString base = settingsBase(id);
    s.setValue(base + QStringLiteral("cf_clearance"),         cfClearance);
    s.setValue(base + QStringLiteral("cf_user_agent"),        ua);
    s.setValue(base + QStringLiteral("cf_clearance_expires"),
               QDateTime::currentDateTime().addDays(kClearanceTtlDays));

    reset();
    emit cookieHarvested(id, cfClearance, ua);
}

void CloudflareCookieHarvester::finishFailure(const QString& reason)
{
    const QString id = m_currentIndexerId;
    reset();
    emit harvestFailed(id, reason);
}

void CloudflareCookieHarvester::onTimeout()
{
    finishFailure(QStringLiteral("Cloudflare harvest timed out after 30s"));
}

#ifdef HAS_WEBENGINE

void CloudflareCookieHarvester::harvest(const QUrl& target, const QString& indexerId)
{
    if (m_busy) {
        qDebug() << "[CloudflareCookieHarvester] busy; ignoring concurrent request for" << indexerId;
        return;
    }
    m_busy = true;
    m_currentIndexerId = indexerId;

    // Lazy-construct the view + its own isolated profile. Off-the-record
    // profile keeps cookies ephemeral (won't leak into disk storage for
    // other QWebEngine consumers like the book reader).
    if (!m_view) {
        m_profile = new QWebEngineProfile(this);  // default off-the-record

        auto* store = m_profile->cookieStore();
        connect(store, &QWebEngineCookieStore::cookieAdded, this,
            [this](const QNetworkCookie& cookie) {
                if (!m_busy) return;
                if (cookie.name() != QByteArrayLiteral("cf_clearance")) return;
                const QString value = QString::fromUtf8(cookie.value());
                if (value.isEmpty()) return;
                finishSuccess(value);
            });

        m_view = new QWebEngineView;  // parent-less; managed manually
        m_view->setAttribute(Qt::WA_DontShowOnScreen, true);

        // Attach our profile by swapping in a QWebEnginePage backed by it.
        auto* page = new QWebEnginePage(m_profile, m_view);
        m_view->setPage(page);

        connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
            if (!m_busy) return;
            if (!ok) finishFailure(QStringLiteral("page load failed"));
            // ok==true doesn't finish here — we wait for cf_clearance cookie
            // or the 30s timeout. Some pages load fully before CF sets the
            // cookie, and some CF flows redirect after JS challenge passes.
        });
    }

    m_timeout->start();
    m_view->load(target);
}

#else // HAS_WEBENGINE

void CloudflareCookieHarvester::harvest(const QUrl& /*target*/, const QString& indexerId)
{
    // No-op build: emit failure synchronously. Caller falls back to manual
    // credential entry via the Phase 3 Sources panel.
    m_currentIndexerId = indexerId;
    finishFailure(QStringLiteral("Qt WebEngine not available in this build"));
}

#endif // HAS_WEBENGINE
