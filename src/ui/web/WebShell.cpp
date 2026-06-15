#include "WebShell.h"

#include <QVBoxLayout>
#include <QCoreApplication>
#include <QDir>

#ifdef HAS_WEBENGINE
#include "TankobanWebBridge.h"
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebChannel>
#include <QFile>
#include <QByteArray>
#include <QUrl>
#include <QStringList>
#include <QLoggingCategory>

namespace {
// Pipe WebEngine JS console messages to Qt logging so the smoke logs capture
// the shim's "[tankoban-shim] window.api ready" line + any renderer errors.
class LoggingWebEnginePage : public QWebEnginePage {
public:
    using QWebEnginePage::QWebEnginePage;
protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
                                  const QString& message,
                                  int lineNumber,
                                  const QString& sourceID) override {
        const QString src = sourceID.section('/', -1);
        const char* tag = level == ErrorMessageLevel ? "ERROR"
                        : level == WarningMessageLevel ? "WARN" : "LOG";
        qInfo().noquote() << QStringLiteral("[WebShell JS][%1] %2:%3 %4")
            .arg(tag).arg(src).arg(lineNumber).arg(message);
    }
};

// The injected window.api shim. Defines EVERY namespace/method from the Electron
// preload contract (src/preload/index.js) so the renderer never hits an
// undefined window.api.X. Each call funnels through one async request/response
// round-trip over the QWebChannel "tankoban" object (b.request / b.response),
// correlated by an incrementing id → resolver map (BookBridge TTS pattern).
//
// Event-subscription methods (onFullscreenChange / onDownloadUpdate /
// onVideoFrame / onEvent) return a no-op unsubscribe fn in Phase 0 (no events
// are pushed yet); the renderer only needs them to be callable.
const char* kApiShim = R"JS(
(function() {
  try {
    if (typeof qt === 'undefined' || !qt.webChannelTransport) {
      console.error('[tankoban-shim] no qt.webChannelTransport'); return;
    }
    if (typeof QWebChannel === 'undefined') {
      console.error('[tankoban-shim] QWebChannel undefined — qwebchannel.js missing'); return;
    }
    new QWebChannel(qt.webChannelTransport, function(ch) {
      var b = ch.objects.tankoban;
      var _seq = 0, _pending = {};
      b.response.connect(function(id, result) {
        var p = _pending[id];
        if (p) {
          delete _pending[id];
          try { p.resolve(JSON.parse(result)); }
          catch (e) { p.resolve(result); }
        }
      });
      function call(channel) {
        var args = Array.prototype.slice.call(arguments, 1);
        return new Promise(function(resolve) {
          var id = String(++_seq);
          _pending[id] = { resolve: resolve };
          try { b.request(id, channel, JSON.stringify(args)); }
          catch (e) { delete _pending[id]; resolve(null); }
        });
      }
      var noop = function() { return function() {}; };

      window.api = {
        tmdb: function(path, params) { return call('tmdb:get', path, params); },
        cinemeta: function(path) { return call('cinemeta:get', path); },
        anilist: { art: function(t) { return call('anilist:art', t); } },
        itunes: { cover: function(a) { return call('itunes:cover', a); } },
        mangadex: { volumes: function(t) { return call('mangadex:volumes', t); } },
        anilistBrowse: {
          section: function(a) { return call('anilistBrowse:section', a); },
          genre: function(a) { return call('anilistBrowse:genre', a); }
        },
        window: {
          setFullscreen: function(f) { return call('window:setFullscreen', f); },
          toggleFullscreen: function() { return call('window:toggleFullscreen'); },
          isFullscreen: function() { return call('window:isFullscreen'); },
          onFullscreenChange: noop
        },
        holyGrail: {
          probe: function() { return call('hg:probe'); },
          initGpu: function(o) { return call('hg:init', o); },
          loadFile: function(u) { return call('hg:load', u); },
          command: function(a) { return call('hg:command', a); },
          setProp: function(n, v) { return call('hg:setProp', n, v); },
          getProp: function(n) { return call('hg:getProp', n); },
          observe: function(n) { return call('hg:observe', n); },
          resize: function(o) { return call('hg:resize', o); },
          startFrames: function() { return call('hg:startFrames'); },
          stopFrames: function() { return call('hg:stopFrames'); },
          setPresentationActive: function(a) { return call('hg:setPresentation', a); },
          destroy: function() { return call('hg:destroy'); },
          onVideoFrame: noop,
          onEvent: noop
        },
        addons: {
          list: function() { return call('addons:list'); },
          add: function(u) { return call('addons:add', u); },
          remove: function(u) { return call('addons:remove', u); },
          getStreams: function(a) { return call('addons:getStreams', a); },
          setHeaders: function(u, h) { return call('addons:setHeaders', u, h); },
          clearHeaders: function(u) { return call('addons:clearHeaders', u); }
        },
        manga: {
          popular: function(a) { return call('manga:popular', a); },
          latest: function(a) { return call('manga:latest', a); },
          search: function(a) { return call('manga:search', a); },
          series: function(a) { return call('manga:series', a); },
          chapters: function(a) { return call('manga:chapters', a); },
          pages: function(a) { return call('manga:pages', a); },
          downloadChapter: function(a) { return call('manga:download:start', a); },
          cancelDownload: function(id) { return call('manga:download:cancel', id); },
          downloadList: function() { return call('manga:download:list'); },
          isDownloaded: function(a) { return call('manga:download:isDownloaded', a); },
          localPages: function(a) { return call('manga:download:localPages', a); },
          deleteChapter: function(a) { return call('manga:download:deleteChapter', a); },
          deleteSeries: function(id) { return call('manga:download:deleteSeries', id); },
          libraryList: function() { return call('manga:library:list'); },
          libraryAdd: function(s) { return call('manga:library:add', s); },
          libraryRemove: function(id) { return call('manga:library:remove', id); },
          onDownloadUpdate: noop
        },
        dialog: {
          openSubtitle: function() { return call('dialog:openSubtitle'); }
        },
        comics: {
          popular: function(a) { return call('comics:popular', a); },
          latest: function(a) { return call('comics:latest', a); },
          newest: function(a) { return call('comics:newest', a); },
          genre: function(a) { return call('comics:genre', a); },
          search: function(a) { return call('comics:search', a); },
          series: function(a) { return call('comics:series', a); },
          issues: function(a) { return call('comics:issues', a); },
          pages: function(a) { return call('comics:pages', a); },
          downloadChapter: function(a) { return call('comics:download:start', a); },
          cancelDownload: function(id) { return call('comics:download:cancel', id); },
          downloadList: function() { return call('comics:download:list'); },
          isDownloaded: function(a) { return call('comics:download:isDownloaded', a); },
          localPages: function(a) { return call('comics:download:localPages', a); },
          deleteChapter: function(a) { return call('comics:download:deleteChapter', a); },
          deleteSeries: function(id) { return call('comics:download:deleteSeries', id); },
          onDownloadUpdate: noop
        }
      };
      console.log('[tankoban-shim] window.api ready');
    });
  } catch (e) {
    console.error('[tankoban-shim] threw:', e && (e.stack || e.message || String(e)));
  }
})();
)JS";
} // namespace
#endif // HAS_WEBENGINE

WebShell::WebShell(QWidget* topLevel, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

#ifdef HAS_WEBENGINE
    // ── Bridge + channel ──
    m_bridge  = new TankobanWebBridge(topLevel, this);
    m_channel = new QWebChannel(this);
    m_channel->registerObject(QStringLiteral("tankoban"), m_bridge);

    // ── Web view ──
    m_webView = new QWebEngineView(this);
    m_webView->setPage(new LoggingWebEnginePage(m_webView));
    m_webView->page()->setWebChannel(m_channel);

    // The renderer loads Google Fonts + remote poster images from a file://
    // origin → both LocalContent* attributes must be true (mirrors BookReader's
    // BOOK_DICTIONARY_FIX rationale: local HTML reaching cross-origin resources).
    m_webView->settings()->setAttribute(
        QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    m_webView->settings()->setAttribute(
        QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);

    // Inject Qt's built-in qwebchannel.js (the page is file://, so qrc:/// is
    // not reachable via <script src>). Try the known resource paths in order.
    {
        const QStringList candidates{
            QStringLiteral(":/qtwebchannel/qwebchannel.js"),
            QStringLiteral(":/qt-project.org/qtwebchannel/qwebchannel.js"),
            QStringLiteral(":/qt/qml/QtWebChannel/qwebchannel.js"),
        };
        bool loaded = false;
        for (const QString& path : candidates) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QByteArray src = f.readAll();
            qInfo() << "[WebShell] Loaded qwebchannel.js from" << path
                    << "size" << src.size();
            QWebEngineScript qwc;
            qwc.setName(QStringLiteral("QWebChannelApi"));
            qwc.setInjectionPoint(QWebEngineScript::DocumentCreation);
            qwc.setWorldId(QWebEngineScript::MainWorld);
            qwc.setRunsOnSubFrames(false);
            qwc.setSourceCode(QString::fromUtf8(src));
            m_webView->page()->scripts().insert(qwc);
            loaded = true;
            break;
        }
        if (!loaded)
            qWarning() << "[WebShell] qwebchannel.js not found — bridge will fail";
    }

    // Inject the window.api shim at document creation, main world, so it is
    // defined before any renderer JS runs.
    {
        QWebEngineScript shim;
        shim.setName(QStringLiteral("TankobanApiShim"));
        shim.setInjectionPoint(QWebEngineScript::DocumentCreation);
        shim.setWorldId(QWebEngineScript::MainWorld);
        shim.setRunsOnSubFrames(false);
        shim.setSourceCode(QString::fromUtf8(kApiShim));
        m_webView->page()->scripts().insert(shim);
    }

    layout->addWidget(m_webView);

    // ── Resolve the renderer bundle dir + load index.html ──
    const QString webuiDir = qEnvironmentVariable(
        "TANKOBAN_WEBUI_DIR",
        QCoreApplication::applicationDirPath() + QStringLiteral("/webui"));
    const QString indexPath = QDir(webuiDir).filePath(QStringLiteral("index.html"));
    qInfo() << "[WebShell] Loading renderer from" << indexPath;
    m_webView->setUrl(QUrl::fromLocalFile(indexPath));
#else
    Q_UNUSED(topLevel);
    qWarning() << "[WebShell] Built without HAS_WEBENGINE — web UI unavailable";
#endif
}

WebShell::~WebShell() = default;
