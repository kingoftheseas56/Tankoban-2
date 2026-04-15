#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QTimer;
class QWebEngineView;
class QWebEngineProfile;

// Hidden QWebEngineView-backed harvester for Cloudflare challenge cookies
// (cf_clearance). One harvest at a time — concurrent calls are dropped.
// When built without Qt6::WebEngineWidgets (HAS_WEBENGINE undefined) the
// implementation stubs out: every harvest() call emits harvestFailed()
// synchronously with a "webengine not available" reason.
class CloudflareCookieHarvester : public QObject
{
    Q_OBJECT

public:
    static CloudflareCookieHarvester* instance();

    // Kick off a harvest against `target`. The result is delivered via one
    // of the signals below — never both, never zero.
    void harvest(const QUrl& target, const QString& indexerId);

    // Convenience: read persisted cookie + UA + expiry for an indexer id.
    // Returns empty strings / invalid QDateTime when nothing is cached.
    static QString   cachedClearance(const QString& indexerId);
    static QString   cachedUserAgent(const QString& indexerId);
    static QDateTime clearanceExpires(const QString& indexerId);

signals:
    void cookieHarvested(const QString& indexerId,
                         const QString& cfClearance,
                         const QString& userAgent);
    void harvestFailed(const QString& indexerId, const QString& reason);

private:
    explicit CloudflareCookieHarvester(QObject* parent = nullptr);
    ~CloudflareCookieHarvester() override;

    void onTimeout();
    void finishSuccess(const QString& cfClearance);
    void finishFailure(const QString& reason);
    void reset();

    QString             m_currentIndexerId;
    QTimer*             m_timeout = nullptr;

#ifdef HAS_WEBENGINE
    QWebEngineView*     m_view = nullptr;
    QWebEngineProfile*  m_profile = nullptr;
#endif

    bool m_busy = false;
};
