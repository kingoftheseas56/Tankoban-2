#include "TankobanWebBridge.h"

#include "core/net/NetSeam.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
#include <QUrl>
#include <QWidget>
#include <QSet>
#include <QLoggingCategory>

namespace {
// Channels that, when stubbed, should resolve to an empty ARRAY rather than an
// empty object — the renderer iterates these (e.g. `.map`/`.forEach`), so "{}"
// would throw. Everything else stubs to "{}".
const QSet<QString>& listChannels() {
    static const QSet<QString> kList = {
        QStringLiteral("addons:list"),
        QStringLiteral("manga:popular"),
        QStringLiteral("manga:latest"),
        QStringLiteral("manga:search"),
        QStringLiteral("manga:chapters"),
        QStringLiteral("manga:pages"),
        QStringLiteral("manga:download:list"),
        QStringLiteral("manga:download:localPages"),
        QStringLiteral("manga:library:list"),
        QStringLiteral("comics:popular"),
        QStringLiteral("comics:latest"),
        QStringLiteral("comics:newest"),
        QStringLiteral("comics:genre"),
        QStringLiteral("comics:search"),
        QStringLiteral("comics:issues"),
        QStringLiteral("comics:pages"),
        QStringLiteral("comics:download:list"),
        QStringLiteral("comics:download:localPages"),
        QStringLiteral("anilistBrowse:section"),
        QStringLiteral("anilistBrowse:genre"),
    };
    return kList;
}
} // namespace

TankobanWebBridge::TankobanWebBridge(QWidget* topLevel, QObject* parent)
    : QObject(parent)
    , m_topLevel(topLevel)
{
}

TankobanWebBridge::~TankobanWebBridge() = default;

QNetworkAccessManager* TankobanWebBridge::nam()
{
    if (!m_nam) {
        // House pattern: instrumented manager via NetSeam, parented to this
        // bridge so it dies with us. sourceTag attributes the request stream.
        m_nam = tankoban::net::NetSeam::instance()->createManager(
            this, QStringLiteral("webui-bridge"));
    }
    return m_nam;
}

void TankobanWebBridge::request(const QString& requestId,
                                const QString& channel,
                                const QString& argsJson)
{
    // Parse the JSON array of positional args (shim always sends an array).
    QJsonArray args;
    {
        const QJsonDocument doc = QJsonDocument::fromJson(argsJson.toUtf8());
        if (doc.isArray())
            args = doc.array();
    }

    // ── REAL: Cinemeta keyless proxy ──
    if (channel == QStringLiteral("cinemeta:get")) {
        const QString path = args.isEmpty() ? QString() : args.at(0).toString();
        handleCinemeta(requestId, path);
        return;
    }

    // ── REAL: window fullscreen ops ──
    if (channel == QStringLiteral("window:setFullscreen")
        || channel == QStringLiteral("window:toggleFullscreen")
        || channel == QStringLiteral("window:isFullscreen")) {
        handleWindow(requestId, channel, argsJson);
        return;
    }

    // ── STUB: everything else — shape-sane empty ──
    const bool wantsArray = listChannels().contains(channel);
    emitStub(requestId, channel,
             wantsArray ? QStringLiteral("[]") : QStringLiteral("{}"));
}

void TankobanWebBridge::handleCinemeta(const QString& requestId, const QString& path)
{
    // Build https://v3-cinemeta.strem.io/<path>. Tolerate a caller-supplied
    // leading slash so both "meta/..." and "/meta/..." resolve correctly.
    QString p = path;
    while (p.startsWith('/'))
        p.remove(0, 1);
    const QUrl url(QStringLiteral("https://v3-cinemeta.strem.io/") + p);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Tankoban/2 (QWebEngine pivot)"));

    QNetworkReply* reply = nam()->get(req);
    // Capture the bridge by QPointer so a destroyed bridge mid-flight is safe.
    QPointer<TankobanWebBridge> self(this);
    connect(reply, &QNetworkReply::finished, this, [self, reply, requestId]() {
        reply->deleteLater();
        if (!self)
            return;
        QString body;
        if (reply->error() == QNetworkReply::NoError) {
            body = QString::fromUtf8(reply->readAll());
        }
        // On any failure, hand back a shape-sane empty object so the renderer
        // does not choke on a network error during Phase 0.
        if (body.isEmpty())
            body = QStringLiteral("{}");
        emit self->response(requestId, body);
    });
}

void TankobanWebBridge::handleWindow(const QString& requestId,
                                     const QString& channel,
                                     const QString& argsJson)
{
    QWidget* w = m_topLevel ? m_topLevel->window() : nullptr;

    bool isFs = w ? w->isFullScreen() : false;

    if (channel == QStringLiteral("window:setFullscreen")) {
        bool flag = false;
        const QJsonDocument doc = QJsonDocument::fromJson(argsJson.toUtf8());
        if (doc.isArray() && !doc.array().isEmpty()) {
            const QJsonValue v = doc.array().at(0);
            flag = v.toBool(v.toString().compare(QStringLiteral("true"),
                                                 Qt::CaseInsensitive) == 0);
        }
        if (w && isFs != flag) {
            if (flag) w->showFullScreen();
            else      w->showNormal();
        }
        isFs = flag;
    } else if (channel == QStringLiteral("window:toggleFullscreen")) {
        if (w) {
            if (isFs) w->showNormal();
            else      w->showFullScreen();
        }
        isFs = !isFs;
    }
    // window:isFullscreen falls through with the queried value.

    emit response(requestId, isFs ? QStringLiteral("true")
                                  : QStringLiteral("false"));
}

void TankobanWebBridge::emitStub(const QString& requestId,
                                 const QString& channel,
                                 const QString& emptyJson)
{
    // Log each distinct stubbed channel exactly once so the smoke shows which
    // surfaces the renderer reaches before they are wired in later phases.
    static QSet<QString> seen;
    if (!seen.contains(channel)) {
        seen.insert(channel);
        qInfo().noquote() << QStringLiteral("[TankobanWebBridge] STUB channel '%1' → %2")
                                 .arg(channel, emptyJson);
    }
    emit response(requestId, emptyJson);
}
