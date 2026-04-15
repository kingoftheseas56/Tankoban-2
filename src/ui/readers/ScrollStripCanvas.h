#pragma once

#include <QWidget>
#include <QPixmap>
#include <QVector>
#include <QMap>
#include <QSet>

class ScrollStripCanvas : public QWidget {
    Q_OBJECT
public:
    explicit ScrollStripCanvas(QWidget* parent = nullptr);

    void setPageCount(int count);
    void setPortraitWidthPct(int pct);
    void setViewportWidth(int w);
    void setScalingQuality(Qt::TransformationMode mode) { m_scalingQuality = mode; }
    void setFilterBrightness(int delta);  // P2-1
    void setSidePadding(int px);          // P3-1 — webtoon-style side bars
    void setSplitOnWide(bool enabled);    // P3-3 — stack wide pages as two halves

    // Called when a full-res pixmap is decoded
    void onPageDecoded(int pageIndex, const QPixmap& fullRes, int origW, int origH);

    // A3: Called when fast dimension hint arrives before full decode (stub — implemented in Batch E)
    void updatePageDimensions(int pageIndex, int w, int h);

    // Invalidate all scaled pixmaps (call on resize / portrait-width change)
    void invalidateScaledCache();

    // Evict scaled entries outside the prefetch zone
    void evictScaledOutsideZone(int viewportH, double prefetchMult = 1.2);

    // Query
    int    pageAtCenter(int viewportH) const;
    double pageTopY(int pageIndex) const;
    double totalHeight() const;
    QVector<int> pagesNeedingDecode(int viewportH, double prefetchMult = 1.2) const;
    bool   hasScaled(int pageIndex) const { return m_scaledCache.contains(pageIndex); }

    // Scroll offset (set from scrollbar value)
    void   setScrollOffset(double offset);
    double scrollOffset() const { return m_scrollOffset; }

signals:
    void needDecode(int pageIndex);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct PageSlot {
        double yOffset = 0.0;
        double height  = 0.0;
        int    origW   = 0;
        int    origH   = 0;
        bool   decoded  = false;
        bool   isSpread = false;
        QPixmap rightHalf;             // P3-3 — when split-on-wide is on, stores
                                       // the right half; m_scaledCache holds the left.
    };

    void rebuildYOffsets();
    int  targetPageWidth(int pageIndex) const;
    int  firstVisiblePage(double viewTop) const;

    QVector<PageSlot>  m_slots;
    QMap<int, QPixmap>  m_scaledCache;
    QSet<int>           m_needsSmoothUpgrade; // pages with fast-scaled pixmaps needing smooth re-scale
    int    m_scaledCacheWidth = 0;
    bool   m_yOffsetsDirty = false;

    int    m_pageCount = 0;
    int    m_viewportWidth = 0;
    int    m_portraitWidthPct = 78;
    double m_scrollOffset = 0.0;
    Qt::TransformationMode m_scalingQuality = Qt::SmoothTransformation;
    int    m_filterBrightness = 0;  // P2-1
    int    m_sidePadding      = 0;  // P3-1 — pixels of blank space each side
    bool   m_splitOnWide      = false;  // P3-3 — stack wide pages as two halves

    static constexpr int    SPACING = 0;
    static constexpr double DEFAULT_ASPECT = 1.4;
    static constexpr double SPREAD_RATIO = 1.08;
};
