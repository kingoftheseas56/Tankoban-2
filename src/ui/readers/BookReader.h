#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

#ifdef HAS_WEBENGINE
#include <QWebEngineView>
#include <QWebChannel>
class BookBridge;
#else
#include <QTextBrowser>
#endif

class CoreBridge;

class BookReader : public QWidget {
    Q_OBJECT
public:
    explicit BookReader(CoreBridge* core, QWidget* parent = nullptr);

    void openBook(const QString& filePath);

signals:
    void closeRequested();
    void fullscreenRequested(bool enter);

protected:
    void resizeEvent(QResizeEvent* event) override;

#ifndef HAS_WEBENGINE
    void keyPressEvent(QKeyEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
#endif

private:
    void buildUI();

#ifndef HAS_WEBENGINE
    void showToolbar();
    void hideToolbar();
    void loadFallback(const QString& filePath);
#endif

    CoreBridge* m_core = nullptr;
    QString m_currentFile;
    QString m_readerHtmlPath;

#ifdef HAS_WEBENGINE
    // Batch 1.3: pre-stabilized loading gate. Opaque overlay covers the webview
    // from openBook() until BookBridge::readerReady() fires (engine_foliate.js
    // subscribes to Foliate's `stabilized` event). 5s watchdog hides the overlay
    // anyway if `stabilized` never fires — the reader becomes visible with a
    // warning rather than staying invisible forever.
    void showLoadingOverlay();
    void hideLoadingOverlay();

    QWebEngineView* m_webView = nullptr;
    BookBridge*     m_bridge  = nullptr;
    QWebChannel*    m_channel = nullptr;
    bool            m_readerReady = false;
    QString         m_pendingBook;
    QWidget*        m_loadingOverlay = nullptr;
    QTimer          m_readyWatchdog;
    // Batch 2.2: resize/fullscreen debounce. resizeEvent sets webview geometry
    // immediately (no reflow lag), then arms a 200ms timer. On timer fire, we
    // ping JS (window.__ebookRelayout) so Foliate's paginator recomputes
    // columns/margins once per resize-burst instead of once per pixel of drag.
    QTimer          m_resizeDebounce;
#else
    QTextBrowser*   m_textBrowser = nullptr;
    QWidget*        m_toolbar     = nullptr;
    QPushButton*    m_backBtn     = nullptr;
    QLabel*         m_titleLabel  = nullptr;
    QTimer          m_hideTimer;
#endif
};
