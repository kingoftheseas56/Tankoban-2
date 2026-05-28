#include "SidebarDrawer.h"

#include <QApplication>
#include <QEvent>
#include <QGraphicsBlurEffect>
#include <QGraphicsOpacityEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpacerItem>
#include <QStyle>
#include <QVBoxLayout>

namespace tankoban_sidebar {

// Bake blur + dim into one QPixmap so the only live graphics effect on
// the BackdropWidget tree is the parent's QGraphicsOpacityEffect (used for
// fade-in). Nesting QGraphicsBlurEffect on a child inside a parent that
// has QGraphicsOpacityEffect produces unreliable compositing in Qt 6 —
// the child effect's offscreen buffer doesn't always survive the parent's
// re-render pass, leaving only the dim layer visible.
static QPixmap composeBlurredDim(const QPixmap& src, qreal blurRadius, int dimAlpha)
{
    if (src.isNull()) return QPixmap();

    QGraphicsScene scene;
    auto* item = scene.addPixmap(src);
    auto* effect = new QGraphicsBlurEffect();
    effect->setBlurRadius(blurRadius);
    effect->setBlurHints(QGraphicsBlurEffect::PerformanceHint);
    item->setGraphicsEffect(effect);

    QPixmap out(src.size());
    out.setDevicePixelRatio(src.devicePixelRatio());
    out.fill(Qt::transparent);
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF target(QPointF(0, 0),
                            QSizeF(src.width()  / src.devicePixelRatio(),
                                   src.height() / src.devicePixelRatio()));
        scene.render(&p, target, target);
        // Composite a uniform dim layer atop the blurred image.
        p.fillRect(target, QColor(0, 0, 0, dimAlpha));
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────
// BackdropWidget
//   Sibling to SidebarDrawer under contentParent. Displays a pre-composed
//   blur+dim pixmap of the page area. The whole widget fades in via a
//   parent-level QGraphicsOpacityEffect (single live effect — no nesting).
//   Click anywhere on the backdrop emits closeRequested.
// ─────────────────────────────────────────────────────────────────────────
class BackdropWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BackdropWidget(QWidget* parent)
        : QWidget(parent)
    {
        setObjectName("SidebarBackdrop");
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setCursor(Qt::PointingHandCursor);

        m_snapshotLabel = new QLabel(this);
        m_snapshotLabel->setObjectName("SidebarBackdropSnapshot");
        m_snapshotLabel->setScaledContents(true);
        m_snapshotLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_snapshotLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    // pm = raw page snapshot. Blur + dim are baked in here, not applied as
    // live effects on this widget tree.
    void setSnapshot(const QPixmap& pm)
    {
        m_baked = composeBlurredDim(pm, /*blurRadius=*/24.0, /*dimAlpha=*/110);
        m_snapshotLabel->setPixmap(m_baked);
        m_snapshotLabel->resize(size());
    }

    void clearSnapshot()
    {
        m_baked = QPixmap();
        m_snapshotLabel->setPixmap(QPixmap());
    }

signals:
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent* ev) override
    {
        if (ev->button() == Qt::LeftButton) {
            emit closeRequested();
            ev->accept();
            return;
        }
        QWidget::mousePressEvent(ev);
    }

    void resizeEvent(QResizeEvent* ev) override
    {
        QWidget::resizeEvent(ev);
        m_snapshotLabel->resize(size());
    }

private:
    QLabel* m_snapshotLabel = nullptr;
    QPixmap m_baked;
};

}  // namespace tankoban_sidebar

using tankoban_sidebar::BackdropWidget;

// ─────────────────────────────────────────────────────────────────────────
// SidebarDrawer
// ─────────────────────────────────────────────────────────────────────────

SidebarDrawer::SidebarDrawer(QWidget* contentParent)
    : QFrame(contentParent)
    , m_contentParent(contentParent)
{
    setObjectName("SidebarDrawer");
    setFocusPolicy(Qt::StrongFocus);

    // Backdrop is a sibling, NOT a child of the drawer — Qt-z-order needs
    // them as peers under contentParent, with drawer raised above backdrop.
    m_backdrop = new BackdropWidget(contentParent);
    m_backdrop->setObjectName("SidebarBackdrop");
    m_backdrop->hide();

    m_backdropOpacity = new QGraphicsOpacityEffect(m_backdrop);
    m_backdropOpacity->setOpacity(0.0);
    m_backdrop->setGraphicsEffect(m_backdropOpacity);

    QObject::connect(m_backdrop, &BackdropWidget::closeRequested,
                     this, [this]() { close(); });

    buildUi();

    m_slideAnim = new QPropertyAnimation(this, "pos", this);
    m_slideAnim->setDuration(kSlideMs);
    m_slideAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_backdropFade = new QPropertyAnimation(m_backdropOpacity, "opacity", this);
    m_backdropFade->setDuration(kFadeMs);
    m_backdropFade->setEasingCurve(QEasingCurve::OutCubic);

    QObject::connect(m_slideAnim, &QPropertyAnimation::finished, this, [this]() {
        m_animating = false;
        if (!m_open) {
            hide();
            m_backdrop->hide();
            releaseBackdropSnapshot();
        }
    });

    // Always-on filter for parent resize; per-open filter for global Esc.
    if (m_contentParent)
        m_contentParent->installEventFilter(this);

    layoutToParent();
    move(-kDrawerWidth, kTopBarHeight);
    hide();
}

SidebarDrawer::~SidebarDrawer()
{
    if (m_contentParent)
        m_contentParent->removeEventFilter(this);
    qApp->removeEventFilter(this);
}

void SidebarDrawer::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(6);

    auto* header = new QLabel(tr("SOURCES"), this);
    header->setObjectName("SidebarSectionHeader");
    layout->addWidget(header);

    auto makeItem = [this](const QString& label, const QString& iconPath, const QString& pageId) {
        auto* btn = new QPushButton(this);
        btn->setObjectName("SidebarItem");
        btn->setText(label);
        btn->setIcon(QIcon(iconPath));
        btn->setIconSize(QSize(16, 16));
        btn->setFixedHeight(kItemHeight);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::TabFocus);
        btn->setProperty("active", false);
        QObject::connect(btn, &QPushButton::clicked, this, [this, pageId]() {
            onSidebarItemClicked(pageId);
        });
        return btn;
    };

    m_btnTankorent        = makeItem(tr("Tankorent"),    QStringLiteral(":/icons/magnet.svg"),   QStringLiteral("tankorent"));
    m_btnStreamDownloads  = makeItem(tr("Downloads"),    QStringLiteral(":/icons/download.svg"), QStringLiteral("streamDownloads"));
    m_btnStreamDownloads->setVisible(false);  // Hidden by default; shown only in Theatre/stream mode
    m_btnComicsDownloads  = makeItem(tr("Downloads"),    QStringLiteral(":/icons/download.svg"), QStringLiteral("comicsDownloads"));
    m_btnComicsDownloads->setVisible(false);  // Hidden by default; shown only in Comics mode
    m_btnBooksDownloads   = makeItem(tr("Downloads"),    QStringLiteral(":/icons/download.svg"), QStringLiteral("booksDownloads"));
    m_btnBooksDownloads->setVisible(false);  // Hidden by default; shown only in Books mode

    layout->addWidget(m_btnTankorent);
    layout->addWidget(m_btnStreamDownloads);
    layout->addWidget(m_btnComicsDownloads);
    layout->addWidget(m_btnBooksDownloads);

    layout->addStretch(1);
}

void SidebarDrawer::layoutToParent()
{
    if (!m_contentParent)
        return;
    const int pw = m_contentParent->width();
    const int ph = m_contentParent->height();
    const int areaH = std::max(0, ph - kTopBarHeight);

    // Backdrop covers the page area below the topbar.
    m_backdrop->setGeometry(0, kTopBarHeight, pw, areaH);

    // Drawer height matches; x-position depends on open/closed state.
    const int targetX = m_open ? 0 : -kDrawerWidth;
    setGeometry(targetX, kTopBarHeight, kDrawerWidth, areaH);
}

void SidebarDrawer::captureBackdropSnapshot()
{
    if (!m_contentParent)
        return;
    const int pw = m_contentParent->width();
    const int ph = m_contentParent->height();
    const int areaH = std::max(0, ph - kTopBarHeight);
    if (pw <= 0 || areaH <= 0)
        return;

    const QRect grabRect(0, kTopBarHeight, pw, areaH);
    QPixmap snap = m_contentParent->grab(grabRect);
    snap.setDevicePixelRatio(m_contentParent->devicePixelRatioF());
    m_backdrop->setSnapshot(snap);
}

void SidebarDrawer::releaseBackdropSnapshot()
{
    if (m_backdrop)
        m_backdrop->clearSnapshot();
}

void SidebarDrawer::open()
{
    if (m_open && !m_animating)
        return;

    m_slideAnim->stop();
    m_backdropFade->stop();

    captureBackdropSnapshot();
    layoutToParent();

    m_backdrop->show();
    show();
    m_backdrop->raise();
    raise();
    setFocus(Qt::OtherFocusReason);

    m_open = true;
    m_animating = true;

    m_slideAnim->setStartValue(QPoint(-kDrawerWidth, kTopBarHeight));
    m_slideAnim->setEndValue(QPoint(0, kTopBarHeight));
    m_slideAnim->start();

    m_backdropFade->setStartValue(m_backdropOpacity->opacity());
    m_backdropFade->setEndValue(1.0);
    m_backdropFade->start();

    qApp->installEventFilter(this);
}

void SidebarDrawer::close()
{
    if (!m_open && !m_animating)
        return;

    m_slideAnim->stop();
    m_backdropFade->stop();

    m_open = false;
    m_animating = true;

    m_slideAnim->setStartValue(pos());
    m_slideAnim->setEndValue(QPoint(-kDrawerWidth, kTopBarHeight));
    m_slideAnim->start();

    m_backdropFade->setStartValue(m_backdropOpacity->opacity());
    m_backdropFade->setEndValue(0.0);
    m_backdropFade->start();

    qApp->removeEventFilter(this);
}

void SidebarDrawer::toggle()
{
    if (m_open) close(); else open();
}

void SidebarDrawer::setActiveSource(const QString& pageId)
{
    m_activePageId = pageId;
    styleItem(m_btnTankorent,        QStringLiteral("tankorent"));
    styleItem(m_btnStreamDownloads,  QStringLiteral("streamDownloads"));
    styleItem(m_btnComicsDownloads,  QStringLiteral("comicsDownloads"));
    styleItem(m_btnBooksDownloads,   QStringLiteral("booksDownloads"));
}

void SidebarDrawer::setStreamDownloadsVisible(bool visible)
{
    if (m_btnStreamDownloads)
        m_btnStreamDownloads->setVisible(visible);
}

void SidebarDrawer::setComicsDownloadsVisible(bool visible)
{
    if (m_btnComicsDownloads)
        m_btnComicsDownloads->setVisible(visible);
}

void SidebarDrawer::setBooksDownloadsVisible(bool visible)
{
    if (m_btnBooksDownloads)
        m_btnBooksDownloads->setVisible(visible);
}

void SidebarDrawer::styleItem(QPushButton* btn, const QString& pageId)
{
    if (!btn) return;
    const bool active = (pageId == m_activePageId);
    if (btn->property("active").toBool() == active)
        return;
    btn->setProperty("active", active);
    btn->style()->unpolish(btn);
    btn->style()->polish(btn);
    btn->update();
}

void SidebarDrawer::onSidebarItemClicked(const QString& pageId)
{
    emit sourceClicked(pageId);
    close();
}

bool SidebarDrawer::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_contentParent && event->type() == QEvent::Resize) {
        layoutToParent();
        return false;  // pass-through
    }
    if (m_open && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            close();
            return true;  // consume
        }
    }
    return QFrame::eventFilter(watched, event);
}

#include "SidebarDrawer.moc"
