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

    m_channel = new QWebChannel(this);
    m_channel->registerObject("bridge", m_bridge);

    m_webView = new QWebEngineView(this);
    m_webView->page()->setWebChannel(m_channel);

    // Allow local file access for loading book files via file:// URLs
    m_webView->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    m_webView->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);

    // Inject the bridge shim BEFORE any page JS runs.
    // This creates window.electronAPI and window.__ebookNav from the QWebChannel bridge.
    QWebEngineScript shimScript;
    shimScript.setName("BookBridgeShim");
    shimScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
    shimScript.setWorldId(QWebEngineScript::MainWorld);
    shimScript.setSourceCode(QStringLiteral(
        "(function() {"
        "  var qt_wc = typeof qt !== 'undefined' && qt.webChannelTransport;"
        "  if (!qt_wc) { console.warn('[shim] No qt.webChannelTransport'); return; }"
        "  new QWebChannel(qt_wc, function(channel) {"
        "    var b = channel.objects.bridge;"
        "    window.electronAPI = {"
        "      files: { read: function(path) { return b.filesRead(path); } },"
        "      booksProgress: {"
        "        getAll: function() { return Promise.resolve({}); },"
        "        get: function(id) { return b.booksProgressGet(id); },"
        "        save: function(id, d) { return b.booksProgressSave(id, d); },"
        "        clear: function() { return Promise.resolve(); },"
        "        clearAll: function() { return Promise.resolve(); }"
        "      },"
        "      booksSettings: {"
        "        get: function(id) { return b.booksSettingsGet(id); },"
        "        save: function(id, d) { return b.booksSettingsSave(id, d); },"
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
        "      booksTtsEdge: {"
        "        probe: function(p) { return b.ttsProbe(); },"
        "        getVoices: function(p) { return b.ttsGetVoices(); },"
        "        synth: function(p) { return b.ttsSynth(p || {}); },"
        "        synthStream: function() { return Promise.resolve({ok:false}); },"
        "        cancelStream: function(id) { return b.ttsCancelStream(id || ''); },"
        "        warmup: function(p) { return b.ttsWarmup(); },"
        "        resetInstance: function() { return b.ttsResetInstance(); }"
        "      },"
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
        "      requestClose: function() { b.requestClose(); }"
        "    };"
        "    console.log('[shim] electronAPI + __ebookNav ready');"
        "  });"
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

void BookReader::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

#ifdef HAS_WEBENGINE
    if (m_webView)
        m_webView->setGeometry(0, 0, width(), height());
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
