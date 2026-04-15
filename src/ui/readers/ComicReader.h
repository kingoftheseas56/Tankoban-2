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
#include <QSet>
#include <QMenu>
#include <QPropertyAnimation>
#include <QVBoxLayout>
#include <QListWidget>
#include <QMouseEvent>
#include <QFont>

#include "PageCache.h"

class CoreBridge;
class SmoothScrollArea;
class ScrollStripCanvas;
class ThumbnailGenerator;
class QComboBox;
class QFrame;
class QCheckBox;
class QToolButton;
class QScrollArea;
class QGridLayout;

// ── H1: ClickScrim ───────────────────────────────────────────────────────────
class ClickScrim : public QWidget {
    Q_OBJECT
public:
    explicit ClickScrim(QWidget* parent = nullptr) : QWidget(parent) {
        setFocusPolicy(Qt::NoFocus);
    }
signals:
    void clicked();
protected:
    void mousePressEvent(QMouseEvent*) override { emit clicked(); }
};

// ── H2: SideNavArrow ─────────────────────────────────────────────────────────
class SideNavArrow : public QWidget {
    Q_OBJECT
public:
    explicit SideNavArrow(bool isRight, QWidget* parent = nullptr);
    void setHovered(bool h);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    bool m_right = false;
    bool m_hover = false;
};

// ── H4: VerticalThumb ────────────────────────────────────────────────────────
class VerticalThumb : public QWidget {
    Q_OBJECT
public:
    explicit VerticalThumb(QWidget* parent = nullptr);
    void setProgress(double fraction);
    bool isDragging() const { return m_dragging; }
signals:
    void progressRequested(double fraction);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    double m_progress = 0.0;
    bool   m_dragging = false;
    int    m_dragStartY = 0;
    double m_dragStartProgress = 0.0;
};

enum class FitMode { FitPage, FitWidth, FitHeight };
enum class ReaderMode { SinglePage, DoublePage, ScrollStrip };

struct TwoPagePair {
    int rightIndex = -1;
    int leftIndex = -1;
    bool isSpread = false;
    bool coverAlone = false;
    bool unpairedSingle = false;
};

class ScrubBar : public QWidget {
    Q_OBJECT
public:
    explicit ScrubBar(QWidget* parent = nullptr);
    void setProgress(double value);
    void setTotalPages(int total);

signals:
    void scrubRequested(int pageIndex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    double ratioForX(double x) const;
    int pageForRatio(double ratio) const;
    void updateBubble(double x);

    double  m_progress = 0.0;
    bool    m_dragging = false;
    bool    m_hover = false;
    int     m_totalPages = 0;
    QLabel* m_bubble = nullptr;
public:
    bool isDragging() const { return m_dragging; }
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
    void fullscreenRequested(bool enter);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUI();
    void handleCursorActivity(const QPoint& posInReader);
    void showPage(int index);
    void nextPage();
    void prevPage();
    void updatePageLabel();
    void displayCurrentPage();
    void requestDecode(int pageIndex);
    void prefetchNeighbors();
    void showToolbar();
    void hideToolbar();
    void toggleToolbar();

    // End-of-volume overlay
    void showEndOverlay();
    void hideEndOverlay();

    // Volume navigator
    void showVolumeNavigator();
    void hideVolumeNavigator();

    // P5-1: Settings panel
    void showSettingsPanel();
    void hideSettingsPanel();

    // P6-2: Thumbnail grid panel
    void showThumbsPanel();
    void hideThumbsPanel();
    void onThumbnailReady(int pageIndex, const QImage& thumb);

    // Async decode callback
    void onPageDecoded(int pageIndex, const QPixmap& pixmap, int w, int h, int volumeId);

    // Progress
    void saveCurrentProgress();
    int  restoreSavedPage();
    QString itemIdForPath(const QString& path) const;

    // Fit modes
    void cycleFitMode();
    void showToast(const QString& text);

    // Mode cycling
    void cycleReaderMode();

    // Double page & pairing
    bool resolveSpread(int index) const;
    bool isSpreadIndex(int index) const;
    int  pageAdvanceCount() const;
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

    // Zoom
    void setZoom(int pct);
    void zoomBy(int delta);
    void resetZoomPan();
    void applyPan();          // I1: apply m_panX to horizontal scrollbar

    // Click zones
    QString clickZone(const QPoint& pos) const;
    void flashClickZone(const QString& side, bool blocked = false);

    // Scroll strip mode
    void buildScrollStrip();
    void clearScrollStrip();
    void reflowScrollStrip();
    int  computePageInView() const;
    void refreshVisibleStripPages();
    void onStripScrollChanged();

    // Go-to-page
    void showGoToDialog();
    void hideGoToDialog();

    // Volume navigation
    void prevVolume();
    void nextVolume();
    void openVolumeByIndex(int volumeIndex);

    // D1: Keys overlay + overlay gate
    bool isAnyOverlayOpen() const;
    void toggleKeysOverlay();

    // F2: Overlay single-open discipline
    void closeAllOverlays();

    // D2-D5: Session keys
    void toggleBookmark();
    void instantReplay();
    void clearResume();
    void saveCheckpoint();

    // D11: Series settings
    QString seriesSettingsKey() const;
    void saveSeriesSettings();
    void applySeriesSettings();

    // J2: Memory saver
    void toggleMemorySaver();

    // C3: Reset series settings
    void resetSeriesSettings();

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
    ReaderMode  m_readerMode = ReaderMode::SinglePage;
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

    // Bookmarks (B key — toggled in Batch D, persisted in Batch C)
    QSet<int> m_bookmarks;

    // Reading direction
    bool m_rtl = false;

    // Zoom + pan (double-page)
    int  m_zoomPct = 100;
    int  m_panX = 0;
    int  m_panY = 0;               // E1: Y-axis pan
    bool m_panDragging = false;
    int  m_panDragStartX = 0;
    int  m_panDragStartPanX = 0;

    // E2: Navigation coalescing
    bool m_navBusy = false;
    int  m_navTarget = -1;

    // Display cache (avoid redundant scaling)
    QPixmap m_displayCache;
    int     m_displayCacheW = 0;
    int     m_displayCacheH = 0;
    int     m_displayCacheZoom = 0;
    int     m_displayCachePage = -1;
    bool    m_displayCacheHasPair = false;
    int     m_displayCacheBrightness = 0;  // P2-1

    // In-flight decode tracking
    QSet<int> m_inflightDecodes;
    int       m_currentVolumeId = 0;

    // Portrait width
    int m_portraitWidthPct = 78;
    QPushButton* m_portraitBtn = nullptr;
    QPushButton* m_modeBtn = nullptr;
    QPushButton* m_settingsBtn = nullptr;  // P5-4

    // J2: memory saver
    bool          m_memorySaver    = false;

    // H3: gutter shadow strength
    double        m_gutterShadow   = 0.35;

    // P2-1: image filters — brightness delta -100..+100, 0 = off
    int           m_filterBrightness = 0;

    // P3-1: ScrollStrip side padding in px (each side), 0 = off
    int           m_stripSidePadding = 0;

    // P3-2: Auto-crop uniform borders on decoded pages
    bool          m_cropBorders = false;

    // P3-3: ScrollStrip — split wide pages into two stacked half-width portraits
    bool          m_splitOnWide = false;

    // C4: image scaling quality
    Qt::TransformationMode m_scalingQuality = Qt::SmoothTransformation;

    // H1: goto scrim
    QWidget*      m_gotoScrim      = nullptr;

    // H2: side nav arrows
    SideNavArrow* m_leftArrow      = nullptr;
    SideNavArrow* m_rightArrow     = nullptr;

    // H4: vertical scroll thumb (strip mode)
    VerticalThumb* m_verticalThumb = nullptr;

    // Scroll strip mode
    ScrollStripCanvas* m_stripCanvas = nullptr;
    QTimer       m_stripRefreshTimer;

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
    ScrubBar*    m_scrubBar   = nullptr;
    QTimer       m_cursorTimer;
    QTimer       m_hudAutoHideTimer;     // auto-hide HUD after 3s inactivity
    QTimer       m_clickTimer;           // center-zone single/double click debounce
    bool         m_hudPinned = false;            // true in SinglePage/DoublePage — HUD never auto-hides
    bool         m_hudExplicitlyHidden = false; // user pressed H or clicked center to hide
    bool         m_edgeCooldown = false; // 600ms cooldown for edge proximity

    // Go-to-page
    QWidget*     m_gotoOverlay = nullptr;
    QLineEdit*   m_gotoInput   = nullptr;

    // End-of-volume overlay
    QWidget*     m_endOverlay = nullptr;
    QLabel*      m_endSubtitle = nullptr;
    QPushButton* m_endNextBtn = nullptr;

    // Volume navigator
    QWidget*     m_volOverlay     = nullptr;
    QWidget*     m_volCard        = nullptr;
    QLineEdit*   m_volSearch      = nullptr;
    QListWidget* m_volList        = nullptr;
    QLabel*      m_volTitle       = nullptr;
    QLabel*      m_volEmptyLabel  = nullptr;
    QPushButton* m_volBtn         = nullptr;

    QWidget* buildVolumeRow(const QString& title,
                            const QString& meta,
                            const QString& pillText,
                            bool isCurrent);

    // Keys overlay
    QWidget*     m_keysOverlay = nullptr;

    // P5-1: Settings panel
    QWidget*     m_settingsOverlay = nullptr;
    QFrame*      m_settingsCard    = nullptr;
    QScrollArea* m_settingsScroll  = nullptr;
    QComboBox*   m_settingsModeCombo     = nullptr;
    QComboBox*   m_settingsPortraitCombo = nullptr;
    QComboBox*   m_settingsFitCombo      = nullptr;
    // P5-2: Image section
    QComboBox*   m_settingsBrightnessCombo = nullptr;
    QCheckBox*   m_settingsCropCheckbox    = nullptr;
    QComboBox*   m_settingsQualityCombo    = nullptr;
    QCheckBox*   m_settingsMemoryCheckbox  = nullptr;
    // P5-3: Mode-specific sections
    QWidget*     m_settingsDoublePageSection = nullptr;
    QWidget*     m_settingsScrollStripSection = nullptr;
    QCheckBox*   m_settingsRtlCheckbox       = nullptr;
    QComboBox*   m_settingsGutterCombo       = nullptr;
    QComboBox*   m_settingsSidePaddingCombo  = nullptr;
    QCheckBox*   m_settingsSplitCheckbox     = nullptr;

    // P6-2: Thumbnail grid panel
    ThumbnailGenerator* m_thumbnailGen = nullptr;
    QWidget*     m_thumbsOverlay = nullptr;
    QFrame*      m_thumbsCard    = nullptr;
    QLabel*      m_thumbsTitle   = nullptr;
    QScrollArea* m_thumbsScroll  = nullptr;
    QWidget*     m_thumbsContent = nullptr;
    QGridLayout* m_thumbsGrid    = nullptr;
    QVector<QToolButton*> m_thumbCells;
    QToolButton* m_currentThumbCell = nullptr;
    QString      m_thumbsBuiltForCbz;  // detect series swap to trigger grid rebuild

    // Toast
    QLabel*      m_toastLabel = nullptr;
    QTimer       m_toastTimer;
};
