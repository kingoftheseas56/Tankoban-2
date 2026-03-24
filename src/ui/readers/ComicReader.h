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
#include <QMap>
#include <QMenu>
#include <QPropertyAnimation>

#include "PageCache.h"

class CoreBridge;
class SmoothScrollArea;

enum class FitMode { FitPage, FitWidth, FitHeight };

struct TwoPagePair {
    int rightIndex = -1;
    int leftIndex = -1;       // -1 if single/spread/cover
    bool isSpread = false;
    bool coverAlone = false;
    bool unpairedSingle = false;
};

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
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

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

    // Double page & pairing
    void toggleDoublePageMode();
    bool resolveSpread(int index) const;
    bool isSpreadIndex(int index) const;
    int  pageAdvanceCount() const;
    QPixmap compositeDoublePages(const QPixmap& left, const QPixmap& right);
    void buildCanonicalPairingUnits();
    void invalidatePairing();
    const TwoPagePair* pairForPage(int pageIndex) const;
    int navigateToUnit(int unitIndex);

    // Coupling
    void toggleCouplingNudge();
    void maybeRunAutoCoupling();
    double edgeContinuityCost(const QPixmap& left, const QPixmap& right);
    double scorePhase(bool shifted);
    QVector<int> autoPhaseSampleIndexes(bool shifted);

    // Spread override
    void showSpreadOverrideMenu(int pageIndex, const QPoint& globalPos);

    // Reading direction
    void toggleReadingDirection();

    // Portrait width
    void showPortraitWidthMenu();
    void setPortraitWidthPct(int pct);

    // Zoom & pan
    void setZoom(int pct);
    void zoomBy(int delta);
    void setPan(double panY);
    void drainPan();
    void resetZoomPan();

    // Click zones
    QString clickZone(const QPoint& pos) const;
    void flashClickZone(const QString& side);

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

    // Canonical pairing
    QVector<TwoPagePair> m_canonicalUnits;
    QMap<int, int> m_unitByPage;

    // Coupling
    QString m_couplingMode  = "auto";
    QString m_couplingPhase = "normal";
    float   m_couplingConfidence = 0.0f;
    bool    m_couplingResolved = false;
    int     m_couplingProbeAttempts = 0;

    // Spread overrides
    QMap<int, bool> m_spreadOverrides;

    // Reading direction
    bool m_rtl = false;

    // Zoom & pan (double-page)
    int    m_zoomPct = 100;
    double m_panX = 0.0, m_panY = 0.0;
    double m_panXMax = 0.0, m_panYMax = 0.0;
    double m_pendingPanPx = 0.0;
    QTimer m_panDrainTimer;

    // Portrait width (single-page)
    int m_portraitWidthPct = 78;
    QPushButton* m_portraitBtn = nullptr;

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
