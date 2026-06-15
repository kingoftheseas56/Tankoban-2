#pragma once

#include <QColor>
#include <QFrame>
#include <QPixmap>
#include <QString>
#include <QVector>

#include "devtools/IDevInspectable.h"

class QLabel;
class QPushButton;
class QHBoxLayout;
class QNetworkAccessManager;
class QPropertyAnimation;
class QTimer;

namespace tankostream::stream {

// HARBOR_REDESIGN Theatre Content Home — Phase A.
//
// FeaturedHero is the auto-rotating banner carousel that sits at the TOP of the
// Theatre home (above the search bar / continue strip). It replicates Harbor's
// home hero (reference order Harbor → Electron). v1 = cross-fade + autorotate,
// NO drag physics (drag is a later add per the plan).
//
// Per slide it composites a backdrop image + a left→right scrim + a bottom→top
// melt into ONE cached QPixmap (m_composed) to avoid per-frame gradient redraws;
// paintEvent then blits the active + (during a transition) the previous composed
// pixmap with an animated cross-fade factor (m_fade, a Q_PROPERTY float driven by
// a QPropertyAnimation on the kEasePull cubic-bezier). Content (meta label, title,
// synopsis, Play pill, optional Watchlist pill) lives in child QLabels/QPushButtons
// laid out in a content plate over the painted backdrop. Backdrops load async via
// QNetworkAccessManager (mirrors the TileCard/CatalogBrowseScreen poster pattern):
// a graceful elevated-bg placeholder shows until each slide's pixmap arrives.
class FeaturedHero : public QFrame, public tankoban::devtools::IDevInspectable {
    Q_OBJECT
    // Cross-fade factor 0..1 from the previous slide (m_prevIndex) to the active
    // slide (m_index). paintEvent reads it; the slide animation drives it.
    Q_PROPERTY(qreal fade READ fade WRITE setFade)

public:
    struct HeroItem {
        QString imdb;        // "tt1234567" or addon meta id
        QString type;        // "movie" | "series"
        QString title;
        QString backdropUrl; // full landscape backdrop URL (MetaItemPreview.background)
        QString synopsis;
        QString metaLine;    // e.g. "2026 · Movie"
    };

    explicit FeaturedHero(QWidget* parent = nullptr);
    ~FeaturedHero() override;

    // Replace the slide set. Resets to slide 0, kicks off async backdrop fetches,
    // and (re)starts autoplay if >1 item. Empty clears the hero (and hides it via
    // the caller checking count()).
    void setItems(const QVector<HeroItem>& items);
    int  count() const { return m_items.size(); }

    // HARBOR_THEATRE_HOME 2026-06-15 — progressive reveal. populateFeaturedHero
    // now gathers slides one-at-a-time (only titles that actually HAVE a high-res
    // landscape backdrop survive the meta-detail filter), so the hero grows as
    // backdropped titles arrive instead of being set in one shot. appendItem adds
    // a single slide to the END of the carousel WITHOUT disturbing the active
    // slide / autoplay clock (unlike setItems, which resets to index 0). Kicks off
    // that slide's async backdrop fetch. Use this for each post-first arrival so
    // the visible carousel stays stable while it fills up to the cap.
    void appendItem(const HeroItem& item);

    // HARBOR_THEATRE_HOME 2026-06-15 — late-binding backdrop. The catalog list
    // carries no landscape backdrop (Cinemeta catalog items only have a portrait
    // poster), so slides are created text-first with an empty backdropUrl and the
    // real high-res landscape arrives later from the meta DETAIL endpoint. This
    // finds the slide whose imdb matches, stores the upgraded backdrop URL, and
    // kicks off the existing async fetch+decode+cross-fade for that one slide.
    // No-op if no slide matches or the URL is empty / unchanged.
    void setBackdropUrl(const QString& imdb, const QString& url);

    // External pause gate (window hidden, mode switched away, etc). Independent of
    // the internal hover-pause; autoplay runs only when neither is set.
    void setPaused(bool paused);

    // OBS-10 / dev-bridge introspection: {activeIndex, count, paused}.
    QJsonObject devSnapshot() const override;

    qreal fade() const { return m_fade; }
    void  setFade(qreal f);

signals:
    // Emitted when the user clicks Play (or the backdrop) for the active slide.
    // StreamPage wires this to its existing open-detail flow.
    void itemActivated(const QString& imdb, const QString& type);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void goToIndex(int index, bool animated);
    void next();
    void prev();
    void rebuildContentForActive();
    void rebuildDots();
    void updateDotStyles();
    void updateArrowGeometry();
    void setArrowsVisible(bool visible);
    void reevaluateAutoplay();

    // Backdrop pipeline.
    void fetchBackdrop(int index);
    void onBackdropDecoded(int index, const QPixmap& raw);
    void invalidateComposite(int index);
    QPixmap composedFor(int index);          // lazy-composite + cache
    QPixmap composeSlide(const QPixmap& backdrop) const;

    QVector<HeroItem> m_items;
    QVector<QPixmap>  m_rawBackdrops;        // decoded source (may be null)
    QVector<QPixmap>  m_composed;            // backdrop+scrim+melt at current size
    QVector<bool>     m_fetchStarted;

    int  m_index     = 0;                    // active slide
    int  m_prevIndex = -1;                   // outgoing slide during a transition
    qreal m_fade     = 1.0;                  // 1.0 == fully on m_index
    // True while a late-arriving backdrop for the ACTIVE slide is fading up over
    // the elevated-bg placeholder (no outgoing slide; distinct from a slide-to-
    // slide transition where m_prevIndex >= 0). paintEvent reads m_fade for the
    // active composite when this is set so the backdrop melts in instead of
    // snapping. Cleared when the fade animation finishes.
    bool m_backdropFadeIn = false;

    bool m_hoverPaused    = false;
    bool m_externalPaused = false;

    QTimer* m_autoTimer = nullptr;
    QPropertyAnimation* m_fadeAnim = nullptr;

    QNetworkAccessManager* m_nam = nullptr;

    // ── content plate widgets ─────────────────────────────────────────────
    QLabel*      m_metaLabel    = nullptr;
    QLabel*      m_titleLabel   = nullptr;
    QLabel*      m_synopsisLabel = nullptr;
    QPushButton* m_playBtn      = nullptr;
    QPushButton* m_watchlistBtn = nullptr;

    // ── navigation chrome ─────────────────────────────────────────────────
    QPushButton* m_prevBtn = nullptr;
    QPushButton* m_nextBtn = nullptr;
    QWidget*     m_dotsRow = nullptr;
    QHBoxLayout* m_dotsLayout = nullptr;
    QVector<QPushButton*> m_dots;

    static constexpr int kHeroHeight   = 560;
    static constexpr int kOuterRadius  = 28;
    static constexpr int kInnerRadius  = 26;
    static constexpr int kPlatePad     = 48;
    static constexpr int kPlateMaxW    = 640;
    static constexpr int kAutoplayMs   = 9000;
    static constexpr int kFadeMs       = 650;
    static constexpr int kArrowSize    = 48;
};

}  // namespace tankostream::stream
