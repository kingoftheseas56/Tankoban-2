#include "BookReader.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>

#ifdef HAS_WEBENGINE
#include "BookBridge.h"
#include <QWebEngineSettings>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEnginePage>
#include <QFile>
#include <QLoggingCategory>

// Pipe WebEngine JS console messages to Qt logging so _crash_log.txt captures them.
namespace {
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
        qInfo().noquote() << QStringLiteral("[BookReader JS][%1] %2:%3 %4")
            .arg(tag).arg(src).arg(lineNumber).arg(message);
    }
};
}
#else
#include <QKeyEvent>
#include <QFile>
#include <QTextStream>
#endif

BookReader::BookReader(CoreBridge* core, QWidget* parent)
    : QWidget(parent)
    , m_core(core)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("background: #000000;");

    // Locate the reader HTML relative to the executable
    QString appDir = QCoreApplication::applicationDirPath();
    m_readerHtmlPath = appDir + "/resources/book_reader/ebook_reader.html";

    buildUI();
}

void BookReader::buildUI()
{
#ifdef HAS_WEBENGINE
    // ── WebEngine path ──
    m_bridge = new BookBridge(m_core, this);
    connect(m_bridge, &BookBridge::closeRequested, this, &BookReader::closeRequested);
    connect(m_bridge, &BookBridge::fullscreenRequested, this, &BookReader::fullscreenRequested);
    connect(m_bridge, &BookBridge::readerReady, this, &BookReader::hideLoadingOverlay);

    m_channel = new QWebChannel(this);
    m_channel->registerObject("bridge", m_bridge);

    m_webView = new QWebEngineView(this);
    m_webView->setPage(new LoggingWebEnginePage(m_webView));
    m_webView->page()->setWebChannel(m_channel);

    // Allow local file access for loading book files via file:// URLs
    m_webView->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    m_webView->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);

    // The HTML loads from file://, so qrc:///qtwebchannel/qwebchannel.js is not
    // reachable via <script src>. Inject Qt's built-in qwebchannel.js via the
    // resource system so `new QWebChannel(...)` is defined when the shim runs.
    {
        const QStringList candidates{
            QStringLiteral(":/qtwebchannel/qwebchannel.js"),
            QStringLiteral(":/qt-project.org/qtwebchannel/qwebchannel.js"),
            QStringLiteral(":/qt/qml/QtWebChannel/qwebchannel.js"),
        };
        bool loaded = false;
        for (const QString& path : candidates) {
            QFile qwcFile(path);
            if (!qwcFile.open(QIODevice::ReadOnly)) continue;
            const QByteArray src = qwcFile.readAll();
            qInfo() << "[BookReader] Loaded qwebchannel.js from" << path << "size" << src.size();
            QWebEngineScript qwcScript;
            qwcScript.setName(QStringLiteral("QWebChannelApi"));
            qwcScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
            qwcScript.setWorldId(QWebEngineScript::MainWorld);
            qwcScript.setRunsOnSubFrames(false);
            qwcScript.setSourceCode(QString::fromUtf8(src));
            m_webView->page()->scripts().insert(qwcScript);
            loaded = true;
            break;
        }
        if (!loaded) {
            qWarning() << "[BookReader] qwebchannel.js not found in any Qt resource path — bridge will fail";
        }
    }

    // Inject the bridge shim BEFORE any page JS runs.
    // This creates window.electronAPI and window.__ebookNav from the QWebChannel bridge.
    QWebEngineScript shimScript;
    shimScript.setName("BookBridgeShim");
    shimScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
    shimScript.setWorldId(QWebEngineScript::MainWorld);
    shimScript.setSourceCode(QStringLiteral(
        "(function() {"
        "  try {"
        "  console.log('[shim] start — QWebChannel defined:', typeof QWebChannel, 'qt:', typeof qt);"
        "  var qt_wc = typeof qt !== 'undefined' && qt.webChannelTransport;"
        "  if (!qt_wc) { console.error('[shim] No qt.webChannelTransport'); return; }"
        "  if (typeof QWebChannel === 'undefined') { console.error('[shim] QWebChannel class undefined — qwebchannel.js did not load'); return; }"
        "  new QWebChannel(qt_wc, function(channel) {"
        "    var b = channel.objects.bridge;"
        "    window.electronAPI = {"
        "      files: { read: function(path) { return b.filesRead(path); } },"
        "      booksProgress: {"
        "        getAll: function() { return Promise.resolve({}); },"
        "        keyFor: function(p) { return b.progressKey(p); },"
        "        get: function(id) { return b.booksProgressGet(id); },"
        "        save: function(id, d) { return b.booksProgressSave(id, d); },"
        "        clear: function() { return Promise.resolve(); },"
        "        clearAll: function() { return Promise.resolve(); }"
        "      },"
        "      booksSettings: {"
        "        get: function() { return b.booksSettingsGet(); },"
        "        save: function(d) { return b.booksSettingsSave(d); },"
        "        clear: function() { return Promise.resolve(); }"
        "      },"
        "      booksBookmarks: {"
        "        get: function(id) { return b.booksBookmarksGet(id); },"
        "        save: function(id, d) { return b.booksBookmarksSave(id, d); },"
        "        delete: function(id, bmId) { return b.booksBookmarksDelete(id, bmId || ''); },"
        "        clear: function(id) { return b.booksBookmarksClear(id); }"
        "      },"
        "      booksAnnotations: {"
        "        get: function(id) { return b.booksAnnotationsGet(id); },"
        "        save: function(id, d) { return b.booksAnnotationsSave(id, d); },"
        "        delete: function(id, annId) { return b.booksAnnotationsDelete(id, annId || ''); },"
        "        clear: function(id) { return b.booksAnnotationsClear(id); }"
        "      },"
        "      booksDisplayNames: {"
        "        getAll: function() { return b.booksDisplayNamesGetAll(); },"
        "        save: function(id, name) { return b.booksDisplayNamesSave(id, name); },"
        "        delete: function(id) { return b.booksDisplayNamesDelete(id); },"
        "        clear: function() { return Promise.resolve(); }"
        "      },"
        "      window: {"
        "        isFullscreen: function() { return Promise.resolve(b.windowIsFullscreen()); },"
        "        toggleFullscreen: function() { return b.windowToggleFullscreen(); },"
        "        setFullscreen: function(v) { var on = v === true || v === 'true'; if (b.windowIsFullscreen() !== on) b.windowToggleFullscreen(); return Promise.resolve({ok:true}); },"
        "        minimize: function() { return Promise.resolve({ok:false}); },"
        "        close: function() { b.requestClose(); return Promise.resolve({ok:true}); }"
        "      },"
        "      clipboard: { copyText: function(t) { return Promise.resolve(); } },"
        "      shell: {"
        "        revealPath: function() { return Promise.resolve(); },"
        "        openExternal: function() { return Promise.resolve(); }"
        "      },"
        // EDGE_TTS_FIX Phase 1.3: real bridge to BookBridge → EdgeTtsWorker →
        // EdgeTtsClient (Qt-side direct WSS to Microsoft Edge Read Aloud). IIFE
        // closure holds a per-channel reqId counter + resolver map. Each *Start
        // invocation generates an id, registers the Promise resolver, then
        // dispatches via QWebChannel. Bridge re-emits the matching *Finished
        // signal carrying the same id; the connected handler resolves the
        // Promise with the result payload.
        "      booksTtsEdge: (function() {"
        "        var _r = {};"
        "        var _next = 0;"
        "        function _on(sig) {"
        "          if (sig && typeof sig.connect === 'function') {"
        "            sig.connect(function(reqId, result) {"
        "              var fn = _r[reqId]; delete _r[reqId];"
        "              if (fn) try { fn(result); } catch (e) { console.error('[booksTtsEdge] resolver threw:', e); }"
        "            });"
        "          }"
        "        }"
        "        _on(b.booksTtsEdgeProbeFinished);"
        "        _on(b.booksTtsEdgeVoicesReady);"
        "        _on(b.booksTtsEdgeSynthFinished);"
        "        _on(b.booksTtsEdgeSynthStreamFinished);"
        "        _on(b.booksTtsEdgeWarmupFinished);"
        "        _on(b.booksTtsEdgeResetFinished);"
        "        function _call(starter, args) {"
        "          return new Promise(function(resolve) {"
        "            var id = ++_next; _r[id] = resolve;"
        "            try { starter.apply(b, [id].concat(args || [])); }"
        "            catch (e) { delete _r[id]; resolve({ok:false, reason:'bridge_call_failed'}); }"
        "          });"
        "        }"
        "        return {"
        "          probe: function(opts) {"
        "            opts = opts || {};"
        "            return _call(b.booksTtsEdgeProbeStart, [String(opts.voice || 'en-US-AriaNeural')]);"
        "          },"
        "          getVoices: function() {"
        "            return _call(b.booksTtsEdgeGetVoicesStart, []);"
        "          },"
        "          synth: function(opts) {"
        "            opts = opts || {};"
        "            return _call(b.booksTtsEdgeSynthStart, [String(opts.text || ''), String(opts.voice || ''), Number(opts.rate) || 1.0, Number(opts.pitch) || 1.0]);"
        "          },"
        "          synthStream: function(opts) {"
        "            opts = opts || {};"
        "            return _call(b.booksTtsEdgeSynthStreamStart, [String(opts.text || ''), String(opts.voice || ''), Number(opts.rate) || 1.0, Number(opts.pitch) || 1.0]);"
        "          },"
        "          cancelStream: function(streamId) {"
        "            try { b.booksTtsEdgeCancelStream(Number(streamId) || 0); } catch (e) {}"
        "            return Promise.resolve({ok:true});"
        "          },"
        "          warmup: function() {"
        "            return _call(b.booksTtsEdgeWarmupStart, []);"
        "          },"
        "          resetInstance: function() {"
        "            return _call(b.booksTtsEdgeResetStart, []);"
        "          }"
        "        };"
        "      })(),"
        "      audiobooks: {"
        "        getState: function() { return b.audiobooksGetState(); },"
        "        getProgress: function(id) { return b.audiobooksGetProgress(id); },"
        "        saveProgress: function(id, d) { return b.audiobooksSaveProgress(id, d); },"
        "        getPairing: function(id) { return b.audiobooksGetPairing(id); },"
        "        savePairing: function(id, d) { return b.audiobooksSavePairing(id, d); },"
        "        deletePairing: function(id) { return b.audiobooksDeletePairing(id); }"
        "      }"
        "    };"
        "    window.__ebookNav = {"
        "      requestClose: function() { b.requestClose(); },"
        "      markReaderReady: function() { b.markReaderReady(); }"
        "    };"
        "    console.log('[shim] electronAPI + __ebookNav ready');"
        "  });"
        "  } catch (e) { console.error('[shim] threw:', e && (e.stack || e.message || String(e))); }"
        "})();"
    ));
    m_webView->page()->scripts().insert(shimScript);

    // Detect when the reader page finishes loading
    connect(m_webView, &QWebEngineView::loadFinished, this, [this](bool ok) {
        if (!ok) return;
        m_readerReady = true;
        if (!m_pendingBook.isEmpty()) {
            QString escaped = m_pendingBook;
            escaped.replace("\\", "\\\\").replace("'", "\\'");
            m_webView->page()->runJavaScript(
                QStringLiteral("window.__ebookOpenBook('%1')").arg(escaped));
            m_pendingBook.clear();
        }
    });

    // Batch 1.3: pre-stabilized loading overlay. Sits atop the webview from
    // openBook() until BookBridge::readerReady fires. Solid black + centered
    // label — no spinner (keeps to the gray/black/white UI rule).
    m_loadingOverlay = new QWidget(this);
    m_loadingOverlay->setObjectName("BookReaderLoadingOverlay");
    m_loadingOverlay->setAttribute(Qt::WA_StyledBackground, true);
    m_loadingOverlay->setStyleSheet(
        "QWidget#BookReaderLoadingOverlay { background: #000000; }"
        "QLabel#BookReaderLoadingText { color: rgba(255,255,255,0.55);"
        "  background: transparent; font-size: 13px; letter-spacing: 0.5px; }"
    );
    {
        auto* overlayLayout = new QVBoxLayout(m_loadingOverlay);
        overlayLayout->setContentsMargins(0, 0, 0, 0);
        overlayLayout->addStretch();
        auto* label = new QLabel(QStringLiteral("Loading..."), m_loadingOverlay);
        label->setObjectName("BookReaderLoadingText");
        label->setAlignment(Qt::AlignCenter);
        overlayLayout->addWidget(label);
        overlayLayout->addStretch();
    }
    m_loadingOverlay->hide();

    m_readyWatchdog.setSingleShot(true);
    m_readyWatchdog.setInterval(5000);
    connect(&m_readyWatchdog, &QTimer::timeout, this, [this]() {
        qWarning() << "[BookReader] readerReady not received within 5s — showing anyway";
        hideLoadingOverlay();
    });

    // Batch 2.2: resize debounce — fires 200ms after the last resizeEvent, so
    // a drag that spans N frames produces exactly one relayout, not N. Foliate's
    // ResizeObserver inside the paginator also listens, but in practice the
    // QWebEngine→iframe viewport propagation is lossy enough that pagination
    // can end up with stale column widths. Forcing window.__ebookRelayout()
    // triggers an explicit renderer.render() which reads the current container
    // size and recomputes columns. JS-side gating hides the renderer until the
    // next `stabilized` so the user never sees the intermediate reflow state.
    m_resizeDebounce.setSingleShot(true);
    m_resizeDebounce.setInterval(200);
    connect(&m_resizeDebounce, &QTimer::timeout, this, [this]() {
        if (!m_webView || !m_webView->page()) return;
        m_webView->page()->runJavaScript(
            QStringLiteral("if (typeof window.__ebookRelayout === 'function') window.__ebookRelayout();"));
    });

#else
    // ── Fallback path (no WebEngine) ──
    m_textBrowser = new QTextBrowser(this);
    m_textBrowser->setOpenLinks(false);
    m_textBrowser->setStyleSheet(
        "QTextBrowser { background: #101216; color: rgba(238,238,238,0.92);"
        "  border: none; padding: 40px; font-size: 14px; }"
    );

    // Bottom toolbar
    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName("BookReaderToolbar");
    m_toolbar->setFixedHeight(48);
    m_toolbar->setStyleSheet(
        "QWidget#BookReaderToolbar {"
        "  background: rgba(8, 8, 8, 0.82);"
        "  border-top: 1px solid rgba(255, 255, 255, 0.10);"
        "}"
    );

    auto* tbLayout = new QHBoxLayout(m_toolbar);
    tbLayout->setContentsMargins(16, 0, 16, 0);
    tbLayout->setSpacing(12);

    m_backBtn = new QPushButton(QChar(0x2190) + QString(" Back"), m_toolbar);
    m_backBtn->setFixedHeight(28);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setStyleSheet(
        "QPushButton { color: rgba(255,255,255,0.78); background: rgba(255,255,255,0.06);"
        "  border: 1px solid rgba(255,255,255,0.10); border-radius: 8px;"
        "  padding: 4px 10px; font-size: 11px; min-width: 60px; max-width: 80px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.12); }"
    );
    connect(m_backBtn, &QPushButton::clicked, this, &BookReader::closeRequested);
    tbLayout->addWidget(m_backBtn);

    tbLayout->addStretch();

    m_titleLabel = new QLabel("", m_toolbar);
    m_titleLabel->setStyleSheet("color: rgba(255,255,255,0.78); font-size: 12px; background: transparent;");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    tbLayout->addWidget(m_titleLabel);

    tbLayout->addStretch();

    auto* spacer = new QWidget(m_toolbar);
    spacer->setFixedWidth(80);
    spacer->setStyleSheet("background: transparent;");
    tbLayout->addWidget(spacer);

    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(3000);
    connect(&m_hideTimer, &QTimer::timeout, this, &BookReader::hideToolbar);
#endif
}

void BookReader::openBook(const QString& filePath)
{
    m_currentFile = filePath;

#ifdef HAS_WEBENGINE
    // Batch 1.3: cover the webview while Foliate boots + paginates the new
    // book. Overlay stays up until BookBridge::readerReady fires (from the
    // stabilized event in engine_foliate.js) or until the 5s watchdog trips.
    showLoadingOverlay();

    if (!m_readerReady) {
        // Page not loaded yet — queue the book and load the HTML
        m_pendingBook = filePath;
        m_webView->setUrl(QUrl::fromLocalFile(m_readerHtmlPath));
    } else {
        // Page already loaded — just open the new book
        QString escaped = filePath;
        escaped.replace("\\", "\\\\").replace("'", "\\'");
        m_webView->page()->runJavaScript(
            QStringLiteral("window.__ebookOpenBook('%1')").arg(escaped));
    }
#else
    loadFallback(filePath);
#endif
}

#ifdef HAS_WEBENGINE
void BookReader::showLoadingOverlay()
{
    if (!m_loadingOverlay) return;
    m_loadingOverlay->setGeometry(0, 0, width(), height());
    m_loadingOverlay->raise();
    m_loadingOverlay->show();
    m_readyWatchdog.start();
}

void BookReader::hideLoadingOverlay()
{
    m_readyWatchdog.stop();
    if (!m_loadingOverlay) return;
    m_loadingOverlay->hide();
}
#endif

void BookReader::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

#ifdef HAS_WEBENGINE
    if (m_webView)
        m_webView->setGeometry(0, 0, width(), height());
    if (m_loadingOverlay && m_loadingOverlay->isVisible())
        m_loadingOverlay->setGeometry(0, 0, width(), height());
    // Batch 2.2: kick the debounce. Only fires 200ms after the last resize —
    // drag-resize, fullscreen toggle, and splitter adjustments all funnel
    // through this one code path, and all get exactly one relayout each.
    if (m_readerReady)
        m_resizeDebounce.start();
#else
    if (m_toolbar)
        m_toolbar->setGeometry(0, height() - m_toolbar->height(),
                               width(), m_toolbar->height());
    if (m_textBrowser)
        m_textBrowser->setGeometry(0, 0, width(),
                                    height() - (m_toolbar && m_toolbar->isVisible() ? m_toolbar->height() : 0));
#endif
}

#ifndef HAS_WEBENGINE

void BookReader::keyPressEvent(QKeyEvent* event)
{
    showToolbar();
    if (event->key() == Qt::Key_Escape)
        emit closeRequested();
    else
        QWidget::keyPressEvent(event);
}

void BookReader::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);
    showToolbar();
}

void BookReader::showToolbar()
{
    m_toolbar->show();
    m_toolbar->raise();
    m_hideTimer.start();
}

void BookReader::hideToolbar()
{
    m_toolbar->hide();
}

void BookReader::loadFallback(const QString& filePath)
{
    QString ext = QFileInfo(filePath).suffix().toLower();

    if (ext == "txt") {
        QFile f(filePath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_textBrowser->setPlainText(QTextStream(&f).readAll());
        } else {
            m_textBrowser->setPlainText("Could not open file.");
        }
    } else if (ext == "epub") {
        m_textBrowser->setHtml(
            "<div style='text-align:center; padding:80px; color:rgba(238,238,238,0.58);'>"
            "<p style='font-size:16px;'>EPUB rendering requires Qt WebEngine.</p>"
            "<p style='font-size:13px; margin-top:12px;'>Rebuild with WebEngine to enable the full reader.</p>"
            "</div>"
        );
    } else {
        m_textBrowser->setHtml(
            "<div style='text-align:center; padding:80px; color:rgba(238,238,238,0.58);'>"
            "<p style='font-size:16px;'>This format requires Qt WebEngine.</p>"
            "<p style='font-size:13px; margin-top:12px;'>Supported without WebEngine: .txt</p>"
            "</div>"
        );
    }

    m_titleLabel->setText(QFileInfo(filePath).completeBaseName());
    showToolbar();
}

#endif
