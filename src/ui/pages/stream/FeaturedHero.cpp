#include "FeaturedHero.h"

#include "core/net/NetSeam.h"

#include <QEasingCurve>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonObject>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace tankostream::stream {

namespace {
// Harbor / Electron tokens baked to literals (no runtime Theme::current() —
// mirrors the CenterSearchBar 2026-06-15 approach). Dark canvas + brand gold.
constexpr const char* kAccent      = "#e8b923";   // Theme::kAccent
constexpr const char* kOnAccent    = "#14110a";   // ink on gold
constexpr const char* kText        = "#f3f1ea";
constexpr const char* kMuted       = "#aab1bd";
constexpr const char* kElevated    = "#232833";   // placeholder backdrop bg
constexpr const char* kBorder      = "rgba(255,255,255,0.16)";

// Harbor ease curve cubic-bezier(0.32, 0.72, 0.24, 1) (Theme.h kEasePull).
QEasingCurve easePull()
{
    QEasingCurve c(QEasingCurve::BezierSpline);
    c.addCubicBezierSegment(QPointF(0.32, 0.72), QPointF(0.24, 1.0), QPointF(1.0, 1.0));
    return c;
}
}  // namespace

FeaturedHero::FeaturedHero(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("FeaturedHero"));
    setFixedHeight(kHeroHeight);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    // We custom-paint the backdrop; the QFrame itself is transparent so the
    // painted rounded backdrop is the only visible surface.
    setAttribute(Qt::WA_StyledBackground, false);
    setStyleSheet(QStringLiteral("#FeaturedHero { background: transparent; }"));

    m_nam = tankoban::net::NetSeam::instance()->createManager(
        this, QStringLiteral("stream-featured-hero"));

    // ── content plate ─────────────────────────────────────────────────────
    auto* plate = new QWidget(this);
    plate->setObjectName(QStringLiteral("FeaturedHeroPlate"));
    plate->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    plate->setStyleSheet(QStringLiteral("#FeaturedHeroPlate { background: transparent; }"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(plate);

    auto* col = new QVBoxLayout(plate);
    col->setContentsMargins(kPlatePad, kPlatePad, kPlatePad, kPlatePad);
    col->setSpacing(0);
    col->addStretch(1);

    m_metaLabel = new QLabel(plate);
    m_metaLabel->setObjectName(QStringLiteral("FeaturedHeroMeta"));
    m_metaLabel->setStyleSheet(QString::fromLatin1(
        "#FeaturedHeroMeta { color: %1; font-family: 'Inter'; font-size: 13px; "
        "font-weight: 600; letter-spacing: 1px; background: transparent; }")
        .arg(QString::fromLatin1(kMuted)));
    col->addWidget(m_metaLabel);
    col->addSpacing(12);

    m_titleLabel = new QLabel(plate);
    m_titleLabel->setObjectName(QStringLiteral("FeaturedHeroTitle"));
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setMaximumWidth(kPlateMaxW);
    m_titleLabel->setStyleSheet(QString::fromLatin1(
        "#FeaturedHeroTitle { color: %1; font-family: 'Fraunces'; font-size: 60px; "
        "font-weight: 500; line-height: 1.0; background: transparent; }")
        .arg(QString::fromLatin1(kText)));
    // Drop shadow on the serif title (Harbor drop-shadow-[0_2px_22px...]).
    {
        auto* sh = new QGraphicsDropShadowEffect(m_titleLabel);
        sh->setBlurRadius(28);
        sh->setColor(QColor(0, 0, 0, 160));
        sh->setOffset(0, 3);
        m_titleLabel->setGraphicsEffect(sh);
    }
    col->addWidget(m_titleLabel);
    col->addSpacing(18);

    m_synopsisLabel = new QLabel(plate);
    m_synopsisLabel->setObjectName(QStringLiteral("FeaturedHeroSynopsis"));
    m_synopsisLabel->setWordWrap(true);
    m_synopsisLabel->setMaximumWidth(kPlateMaxW);
    m_synopsisLabel->setStyleSheet(QString::fromLatin1(
        "#FeaturedHeroSynopsis { color: %1; font-family: 'Inter'; font-size: 16px; "
        "line-height: 1.5; background: transparent; }")
        .arg(QString::fromLatin1(kMuted)));
    col->addWidget(m_synopsisLabel);
    col->addSpacing(28);

    auto* btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(12);

    m_playBtn = new QPushButton(plate);
    m_playBtn->setObjectName(QStringLiteral("FeaturedHeroPlay"));
    m_playBtn->setCursor(Qt::PointingHandCursor);
    m_playBtn->setMinimumHeight(48);
    m_playBtn->setText(QString(QChar(0x25B6)) + QStringLiteral("  Play"));  // ▶
    m_playBtn->setStyleSheet(QString::fromLatin1(
        "#FeaturedHeroPlay { background: %1; color: %2; border: none; "
        "border-radius: 24px; padding: 0 28px; font-family: 'Inter'; "
        "font-size: 15px; font-weight: 600; } "
        "#FeaturedHeroPlay:hover { background: #f3ca3f; }")
        .arg(QString::fromLatin1(kAccent), QString::fromLatin1(kOnAccent)));
    connect(m_playBtn, &QPushButton::clicked, this, [this]() {
        if (m_index >= 0 && m_index < m_items.size()) {
            emit itemActivated(m_items[m_index].imdb, m_items[m_index].type);
        }
    });
    // Pause autoplay while hovering the Play button (Harbor behavior).
    m_playBtn->installEventFilter(this);
    btnRow->addWidget(m_playBtn);

    m_watchlistBtn = new QPushButton(plate);
    m_watchlistBtn->setObjectName(QStringLiteral("FeaturedHeroWatchlist"));
    m_watchlistBtn->setCursor(Qt::PointingHandCursor);
    m_watchlistBtn->setMinimumHeight(48);
    m_watchlistBtn->setText(QString(QChar(0x002B)) + QStringLiteral(" Watchlist"));
    m_watchlistBtn->setStyleSheet(QString::fromLatin1(
        "#FeaturedHeroWatchlist { background: rgba(8,8,8,0.55); color: %1; "
        "border: 1px solid %2; border-radius: 24px; padding: 0 24px; "
        "font-family: 'Inter'; font-size: 15px; font-weight: 500; } "
        "#FeaturedHeroWatchlist:hover { background: rgba(8,8,8,0.75); }")
        .arg(QString::fromLatin1(kText), QString::fromLatin1(kBorder)));
    // Watchlist is presentational in v1 (Phase A scope = hero render + Play).
    btnRow->addWidget(m_watchlistBtn);
    btnRow->addStretch(1);

    col->addLayout(btnRow);
    col->addStretch(1);

    // ── prev / next arrows (overlay children, shown on hover) ──────────────
    auto makeArrow = [this](const QString& glyph, const char* name) {
        auto* b = new QPushButton(glyph, this);
        b->setObjectName(QString::fromLatin1(name));
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(kArrowSize, kArrowSize);
        b->setStyleSheet(QString::fromLatin1(
            "QPushButton { background: rgba(8,8,8,0.62); color: %1; border: none; "
            "border-radius: 24px; font-size: 26px; font-weight: 600; } "
            "QPushButton:hover { background: rgba(8,8,8,0.85); }")
            .arg(QString::fromLatin1(kText)));
        b->hide();  // opacity 0 until hover
        return b;
    };
    m_prevBtn = makeArrow(QString(QChar(0x2039)), "FeaturedHeroPrev");  // ‹
    m_nextBtn = makeArrow(QString(QChar(0x203A)), "FeaturedHeroNext");  // ›
    connect(m_prevBtn, &QPushButton::clicked, this, [this]() { prev(); });
    connect(m_nextBtn, &QPushButton::clicked, this, [this]() { next(); });

    // ── stepper dots (bottom-center overlay) ───────────────────────────────
    m_dotsRow = new QWidget(this);
    m_dotsRow->setObjectName(QStringLiteral("FeaturedHeroDots"));
    m_dotsRow->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_dotsRow->setStyleSheet(QStringLiteral("#FeaturedHeroDots { background: transparent; }"));
    m_dotsLayout = new QHBoxLayout(m_dotsRow);
    m_dotsLayout->setContentsMargins(0, 0, 0, 0);
    m_dotsLayout->setSpacing(8);

    // ── timers / animation ─────────────────────────────────────────────────
    m_autoTimer = new QTimer(this);
    m_autoTimer->setInterval(kAutoplayMs);
    connect(m_autoTimer, &QTimer::timeout, this, [this]() { next(); });

    m_fadeAnim = new QPropertyAnimation(this, "fade", this);
    m_fadeAnim->setDuration(kFadeMs);
    m_fadeAnim->setEasingCurve(easePull());
    connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
        m_prevIndex = -1;
        m_backdropFadeIn = false;
        m_fade = 1.0;
        update();
    });
}

FeaturedHero::~FeaturedHero() = default;

// ── public API ─────────────────────────────────────────────────────────────

void FeaturedHero::setItems(const QVector<HeroItem>& items)
{
    m_items = items;
    m_rawBackdrops.assign(items.size(), QPixmap());
    m_composed.assign(items.size(), QPixmap());
    m_fetchStarted.assign(items.size(), false);
    m_index = 0;
    m_prevIndex = -1;
    m_backdropFadeIn = false;
    m_fade = 1.0;
    if (m_fadeAnim->state() == QAbstractAnimation::Running)
        m_fadeAnim->stop();

    rebuildDots();
    rebuildContentForActive();

    // Fetch the active backdrop first, then the rest (so the first slide paints
    // its real image as soon as possible).
    if (!m_items.isEmpty()) {
        fetchBackdrop(0);
        for (int i = 1; i < m_items.size(); ++i)
            fetchBackdrop(i);
    }

    reevaluateAutoplay();
    update();
}

void FeaturedHero::appendItem(const HeroItem& item)
{
    // Append one slide at the end without touching m_index / the autoplay clock /
    // any in-flight transition. The parallel state vectors grow in lock-step so
    // composedFor()/fetchBackdrop() index correctly. The new slide fetches its own
    // backdrop async; the dots row + autoplay are re-evaluated since count grew.
    m_items.append(item);
    m_rawBackdrops.append(QPixmap());
    m_composed.append(QPixmap());
    m_fetchStarted.append(false);

    rebuildDots();          // adds a dot, keeps active highlight via updateDotStyles
    fetchBackdrop(m_items.size() - 1);
    reevaluateAutoplay();   // count may have crossed 1 → enable autorotate
    update();
}

void FeaturedHero::setBackdropUrl(const QString& imdb, const QString& url)
{
    if (imdb.isEmpty() || url.isEmpty())
        return;

    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].imdb != imdb)
            continue;
        if (m_items[i].backdropUrl == url)
            return;  // already bound to this backdrop; nothing to do

        m_items[i].backdropUrl = url;
        // Re-arm the per-slide fetch state so fetchBackdrop actually runs even if
        // a prior (empty-URL) setItems pass left the slot in a started/decoded
        // state. Clearing the decoded pixmap + composite forces a fresh decode.
        m_fetchStarted[i] = false;
        m_rawBackdrops[i] = QPixmap();
        invalidateComposite(i);
        fetchBackdrop(i);  // async decode → onBackdropDecoded → cross-fade in
        return;
    }
}

void FeaturedHero::setPaused(bool paused)
{
    m_externalPaused = paused;
    reevaluateAutoplay();
}

void FeaturedHero::setFade(qreal f)
{
    m_fade = f;
    update();
}

QJsonObject FeaturedHero::devSnapshot() const
{
    QJsonObject o;
    o[QStringLiteral("activeIndex")] = m_index;
    o[QStringLiteral("count")] = m_items.size();
    o[QStringLiteral("paused")] = (m_externalPaused || m_hoverPaused ||
                                   !m_autoTimer->isActive());
    return o;
}

// ── navigation ───────────────────────────────────────────────────────────────

void FeaturedHero::next()
{
    if (m_items.size() < 2) return;
    goToIndex((m_index + 1) % m_items.size(), true);
}

void FeaturedHero::prev()
{
    if (m_items.size() < 2) return;
    goToIndex((m_index - 1 + m_items.size()) % m_items.size(), true);
}

void FeaturedHero::goToIndex(int index, bool animated)
{
    if (index < 0 || index >= m_items.size() || index == m_index)
        return;

    m_prevIndex = m_index;
    m_index = index;
    // A real slide change supersedes any in-progress late-backdrop fade-in: the
    // slide-to-slide transition below owns m_fade from here.
    m_backdropFadeIn = false;
    rebuildContentForActive();
    updateDotStyles();
    // Restart the autoplay clock so a manual step gives a full dwell.
    if (m_autoTimer->isActive())
        m_autoTimer->start();

    if (animated && isVisible()) {
        if (m_fadeAnim->state() == QAbstractAnimation::Running)
            m_fadeAnim->stop();
        m_fade = 0.0;
        m_fadeAnim->setStartValue(0.0);
        m_fadeAnim->setEndValue(1.0);
        m_fadeAnim->start();
    } else {
        m_prevIndex = -1;
        m_fade = 1.0;
        update();
    }
}

void FeaturedHero::rebuildContentForActive()
{
    if (m_index < 0 || m_index >= m_items.size()) {
        m_metaLabel->clear();
        m_titleLabel->clear();
        m_synopsisLabel->clear();
        return;
    }
    const HeroItem& it = m_items[m_index];
    m_metaLabel->setText(it.metaLine.toUpper());
    m_metaLabel->setVisible(!it.metaLine.isEmpty());
    m_titleLabel->setText(it.title);

    // 3-line elide on the synopsis (approximate: clamp by char budget per the
    // plate width, then ElideRight on the 3rd line via word-wrap + max height).
    m_synopsisLabel->setText(it.synopsis);
    QFontMetrics fm(m_synopsisLabel->font());
    m_synopsisLabel->setMaximumHeight(fm.lineSpacing() * 3 + 4);
    m_synopsisLabel->setVisible(!it.synopsis.isEmpty());
}

void FeaturedHero::rebuildDots()
{
    for (QPushButton* d : m_dots) {
        m_dotsLayout->removeWidget(d);
        d->deleteLater();
    }
    m_dots.clear();

    for (int i = 0; i < m_items.size(); ++i) {
        auto* dot = new QPushButton(m_dotsRow);
        dot->setObjectName(QStringLiteral("FeaturedHeroDot"));
        dot->setCursor(Qt::PointingHandCursor);
        dot->setFixedHeight(6);
        connect(dot, &QPushButton::clicked, this, [this, i]() { goToIndex(i, true); });
        m_dotsLayout->addWidget(dot);
        m_dots.append(dot);
    }
    m_dotsRow->setVisible(m_items.size() > 1);
    updateDotStyles();
    updateArrowGeometry();
}

void FeaturedHero::updateDotStyles()
{
    for (int i = 0; i < m_dots.size(); ++i) {
        const bool active = (i == m_index);
        m_dots[i]->setFixedWidth(active ? 30 : 8);
        m_dots[i]->setStyleSheet(QString::fromLatin1(
            "#FeaturedHeroDot { border: none; border-radius: 3px; background: %1; }")
            .arg(active ? QString::fromLatin1(kAccent)
                        : QStringLiteral("rgba(255,255,255,0.38)")));
    }
    if (m_dotsRow)
        m_dotsRow->adjustSize();
    updateArrowGeometry();
}

void FeaturedHero::updateArrowGeometry()
{
    const int midY = (height() - kArrowSize) / 2;
    if (m_prevBtn) m_prevBtn->move(16, midY);
    if (m_nextBtn) m_nextBtn->move(width() - kArrowSize - 16, midY);
    if (m_dotsRow) {
        m_dotsRow->adjustSize();
        const int dw = m_dotsRow->sizeHint().width();
        const int dh = m_dotsRow->sizeHint().height();
        m_dotsRow->setGeometry((width() - dw) / 2, height() - dh - 28, dw, dh);
    }
}

void FeaturedHero::setArrowsVisible(bool visible)
{
    const bool show = visible && m_items.size() > 1;
    if (m_prevBtn) m_prevBtn->setVisible(show);
    if (m_nextBtn) m_nextBtn->setVisible(show);
    if (show) {
        m_prevBtn->raise();
        m_nextBtn->raise();
    }
}

void FeaturedHero::reevaluateAutoplay()
{
    const bool shouldRun = m_items.size() > 1 && isVisible() &&
                           !m_externalPaused && !m_hoverPaused;
    if (shouldRun && !m_autoTimer->isActive())
        m_autoTimer->start();
    else if (!shouldRun && m_autoTimer->isActive())
        m_autoTimer->stop();
}

// ── backdrop pipeline ────────────────────────────────────────────────────────

void FeaturedHero::fetchBackdrop(int index)
{
    if (index < 0 || index >= m_items.size())
        return;
    if (m_fetchStarted[index])
        return;
    const QString url = m_items[index].backdropUrl;
    if (url.isEmpty())
        return;
    m_fetchStarted[index] = true;

    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 Tankoban"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_nam->get(req);
    QPointer<FeaturedHero> guard(this);
    connect(reply, &QNetworkReply::finished, this,
        [guard, reply, index]() {
            reply->deleteLater();
            if (!guard || reply->error() != QNetworkReply::NoError)
                return;
            const QByteArray body = reply->readAll();
            if (body.isEmpty())
                return;
            QImage img;
            if (!img.loadFromData(body))
                return;
            guard->onBackdropDecoded(index, QPixmap::fromImage(img));
        });
}

void FeaturedHero::onBackdropDecoded(int index, const QPixmap& raw)
{
    if (index < 0 || index >= m_rawBackdrops.size() || raw.isNull())
        return;
    m_rawBackdrops[index] = raw;
    invalidateComposite(index);

    // If this backdrop belongs to the ACTIVE slide and we're settled (not mid
    // slide-to-slide transition), melt it up over the elevated-bg placeholder
    // rather than snapping. This is the catalog-text-first → detail-backdrop-late
    // path: the slide painted instantly with text + placeholder, and now the real
    // landscape image arrives and fades in. A slide-to-slide transition
    // (m_prevIndex >= 0) already owns the animation, so leave it alone.
    if (index == m_index && m_prevIndex < 0 && isVisible()) {
        m_backdropFadeIn = true;
        if (m_fadeAnim->state() == QAbstractAnimation::Running)
            m_fadeAnim->stop();
        m_fade = 0.0;
        m_fadeAnim->setStartValue(0.0);
        m_fadeAnim->setEndValue(1.0);
        m_fadeAnim->start();
        return;
    }

    if (index == m_index || index == m_prevIndex)
        update();
}

void FeaturedHero::invalidateComposite(int index)
{
    if (index >= 0 && index < m_composed.size())
        m_composed[index] = QPixmap();
}

QPixmap FeaturedHero::composedFor(int index)
{
    if (index < 0 || index >= m_items.size())
        return {};
    if (!m_composed[index].isNull() &&
        m_composed[index].size() == size() * devicePixelRatioF())
        return m_composed[index];
    if (m_rawBackdrops[index].isNull())
        return {};  // caller paints the placeholder instead
    m_composed[index] = composeSlide(m_rawBackdrops[index]);
    return m_composed[index];
}

QPixmap FeaturedHero::composeSlide(const QPixmap& backdrop) const
{
    const qreal dpr = devicePixelRatioF();
    const QSize px = size() * dpr;
    if (px.isEmpty() || backdrop.isNull())
        return {};

    QPixmap out(px);
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::transparent);

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRectF r(0, 0, width(), height());

    // Rounded clip (outer radius 28).
    QPainterPath clip;
    clip.addRoundedRect(r, kOuterRadius, kOuterRadius);
    p.setClipPath(clip);

    // Backdrop: scaled cover (fill, crop overflow), centered.
    const QPixmap scaled = backdrop.scaled(
        size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QPointF off((width()  - scaled.width()  / scaled.devicePixelRatio()) / 2.0,
                      (height() - scaled.height() / scaled.devicePixelRatio()) / 2.0);
    p.drawPixmap(off, scaled);

    // Canvas tone used by both scrims (near-black #0c0d10).
    const QColor canvas(0x0c, 0x0d, 0x10);

    // Left→right scrim: ~78% → 32% → transparent.
    {
        QLinearGradient g(r.topLeft(), r.topRight());
        QColor c0 = canvas; c0.setAlphaF(0.78);
        QColor c1 = canvas; c1.setAlphaF(0.32);
        QColor c2 = canvas; c2.setAlphaF(0.0);
        g.setColorAt(0.0, c0);
        g.setColorAt(0.5, c1);
        g.setColorAt(1.0, c2);
        p.fillRect(r, g);
    }
    // Bottom→top melt: canvas → transparent.
    {
        QLinearGradient g(r.bottomLeft(), r.topLeft());
        QColor c0 = canvas; c0.setAlphaF(0.92);
        QColor c1 = canvas; c1.setAlphaF(0.30);
        QColor c2 = canvas; c2.setAlphaF(0.0);
        g.setColorAt(0.0, c0);
        g.setColorAt(0.32, c1);
        g.setColorAt(0.6, c2);
        p.fillRect(r, g);
    }
    p.end();
    return out;
}

// ── events ───────────────────────────────────────────────────────────────────

void FeaturedHero::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r(0, 0, width(), height());
    QPainterPath clip;
    clip.addRoundedRect(r, kOuterRadius, kOuterRadius);

    // Placeholder underlay (elevated bg) so a not-yet-loaded slide is graceful.
    p.save();
    p.setClipPath(clip);
    p.fillRect(r, QColor(kElevated));
    p.restore();

    // Outgoing slide (during a transition) painted first at (1 - fade).
    if (m_prevIndex >= 0 && m_prevIndex < m_items.size()) {
        const QPixmap prev = composedFor(m_prevIndex);
        if (!prev.isNull()) {
            p.setOpacity(1.0);
            p.drawPixmap(0, 0, prev);
        }
    }

    // Active slide. During a slide-to-slide transition (m_prevIndex >= 0) OR a
    // late-backdrop fade-in over the placeholder (m_backdropFadeIn), draw it at
    // m_fade so it melts up; otherwise fully opaque.
    const QPixmap cur = composedFor(m_index);
    if (!cur.isNull()) {
        p.setOpacity((m_prevIndex >= 0 || m_backdropFadeIn) ? m_fade : 1.0);
        p.drawPixmap(0, 0, cur);
        p.setOpacity(1.0);
    }
}

void FeaturedHero::resizeEvent(QResizeEvent* event)
{
    QFrame::resizeEvent(event);
    // Size changed → every cached composite is stale.
    for (int i = 0; i < m_composed.size(); ++i)
        m_composed[i] = QPixmap();
    updateArrowGeometry();
}

void FeaturedHero::enterEvent(QEnterEvent* event)
{
    QFrame::enterEvent(event);
    m_hoverPaused = true;
    reevaluateAutoplay();
    setArrowsVisible(true);
}

void FeaturedHero::leaveEvent(QEvent* event)
{
    QFrame::leaveEvent(event);
    m_hoverPaused = false;
    reevaluateAutoplay();
    setArrowsVisible(false);
}

void FeaturedHero::mousePressEvent(QMouseEvent* event)
{
    QFrame::mousePressEvent(event);
    if (event->button() != Qt::LeftButton)
        return;
    // Click anywhere on the backdrop (outside the arrows/dots/buttons, which
    // are child widgets that consume their own clicks) opens the active title.
    if (m_index >= 0 && m_index < m_items.size())
        emit itemActivated(m_items[m_index].imdb, m_items[m_index].type);
}

void FeaturedHero::showEvent(QShowEvent* event)
{
    QFrame::showEvent(event);
    reevaluateAutoplay();
}

void FeaturedHero::hideEvent(QHideEvent* event)
{
    QFrame::hideEvent(event);
    reevaluateAutoplay();
}

bool FeaturedHero::eventFilter(QObject* obj, QEvent* event)
{
    // Pause autoplay while hovering the Play button (Harbor behavior: action
    // buttons hold the carousel so a mid-read click doesn't jump slides). The
    // hero itself already pauses on its own enter/leave; this is belt-and-
    // suspenders for the button hotzone.
    if (obj == m_playBtn) {
        if (event->type() == QEvent::Enter) {
            m_hoverPaused = true;
            reevaluateAutoplay();
        } else if (event->type() == QEvent::Leave) {
            // Leaving the button does not necessarily leave the hero; the
            // hero's own leaveEvent clears m_hoverPaused when the cursor
            // actually exits. Keep paused here (still inside the hero).
        }
    }
    return QFrame::eventFilter(obj, event);
}

}  // namespace tankostream::stream
