#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QStringList>
#include <QPixmap>
#include <QTimer>
#include <QThreadPool>
#include <QVector>

#include "PageCache.h"

class CoreBridge;
class SmoothScrollArea;

enum class FitMode { FitPage, FitWidth, FitHeight };

class ComicReader : public QWidget {
    Q_OBJECT
public:
    explicit ComicReader(CoreBridge* bridge, QWidget* parent = nullptr);

    void openBook(const QString& cbzPath,
                  const QStringList& seriesCbzList = {},
                  const QString& seriesName = {});

signals:
    void closeRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void buildUI();
    void showPage(int index);
    void nextPage();
    void prevPage();
    void updatePageLabel();
    void displayCurrentPage();
    void requestDecode(int pageIndex);
    void prefetchNeighbors();
    void showToolbar();
    void hideToolbar();

    // Async decode callback
    void onPageDecoded(int pageIndex, const QPixmap& pixmap, int w, int h);

    // Progress
    void saveCurrentProgress();
    int  restoreSavedPage();
    QString itemIdForPath(const QString& path) const;

    // Fit modes
    void cycleFitMode();
    void showToast(const QString& text);

    // Double page
    void toggleDoublePageMode();
    bool isSpreadIndex(int index) const;
    int  pageAdvanceCount() const;
    QPixmap compositeDoublePages(const QPixmap& left, const QPixmap& right);

    // Go-to-page
    void showGoToDialog();
    void hideGoToDialog();

    // Volume navigation
    void prevVolume();
    void nextVolume();
    void openVolumeByIndex(int volumeIndex);

    // Core
    CoreBridge* m_bridge = nullptr;

    // State
    QString     m_cbzPath;
    QStringList m_pageNames;
    QVector<PageMeta> m_pageMeta;
    int         m_currentPage = 0;
    QPixmap     m_currentPixmap;
    QPixmap     m_secondPixmap;

    // Series context
    QStringList m_seriesCbzList;
    QString     m_seriesName;

    // Modes
    bool        m_doublePageMode = false;
    FitMode     m_fitMode = FitMode::FitPage;

    // Infrastructure
    PageCache    m_cache;
    QThreadPool  m_decodePool;

    // UI
    SmoothScrollArea* m_scrollArea = nullptr;
    QLabel*      m_imageLabel = nullptr;
    QWidget*     m_toolbar    = nullptr;
    QPushButton* m_backBtn    = nullptr;
    QPushButton* m_prevBtn    = nullptr;
    QPushButton* m_nextBtn    = nullptr;
    QPushButton* m_prevVolBtn = nullptr;
    QPushButton* m_nextVolBtn = nullptr;
    QLabel*      m_pageLabel  = nullptr;
    QTimer       m_hideTimer;

    // Go-to-page
    QWidget*     m_gotoOverlay = nullptr;
    QLineEdit*   m_gotoInput   = nullptr;

    // Toast
    QLabel*      m_toastLabel = nullptr;
    QTimer       m_toastTimer;
};
