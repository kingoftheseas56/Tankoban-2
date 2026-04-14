#include "ComicReader.h"
#include "ScrollStripCanvas.h"
#include "SmoothScrollArea.h"
#include "DecodeTask.h"
#include "core/ArchiveReader.h"
#include "core/CoreBridge.h"
#include "ui/ContextMenuHelper.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QPainter>
#include <QLinearGradient>
#include <QFileInfo>
#include <QScrollBar>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QSettings>
#include <QRegularExpression>
#include <QGraphicsOpacityEffect>
#include <QColor>
#include <cmath>

static constexpr double SPREAD_RATIO = 1.08;
static constexpr int TWO_PAGE_GUTTER_PX = 8;  // B3: physical gap between paired pages
#define m_isDoublePage (m_readerMode == ReaderMode::DoublePage)
#define m_isScrollStrip (m_readerMode == ReaderMode::ScrollStrip)
static constexpr double COUPLING_MIN_CONFIDENCE = 0.12;
static constexpr int COUPLING_PROBE_MAX_PAGES = 8;
static constexpr int COUPLING_MAX_SAMPLES = 4;
static constexpr int COUPLING_MAX_PROBE_ATTEMPTS = 6;
static const int PORTRAIT_PRESETS[] = {50, 60, 70, 74, 78, 90, 100};

// ── SideNavArrow ─────────────────────────────────────────────────────────────

SideNavArrow::SideNavArrow(bool isRight, QWidget* parent)
    : QWidget(parent), m_right(isRight)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setStyleSheet("background: transparent;");
}

void SideNavArrow::setHovered(bool h)
{
    if (m_hover == h) return;
    m_hover = h;
    update();
}

void SideNavArrow::paintEvent(QPaintEvent*)
{
    if (!m_hover) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont font("Segoe UI", 22);
    font.setWeight(QFont::Black);
    p.setFont(font);
    QString ch = m_right ? QString(QChar(0x203A)) : QString(QChar(0x2039));
    // Drop shadow
    p.setPen(QColor(0, 0, 0, 90));
    p.drawText(rect().adjusted(2, 2, 2, 2), Qt::AlignCenter, ch);
    // Foreground
    p.setPen(QColor(255, 255, 255, 180));
    p.drawText(rect(), Qt::AlignCenter, ch);
}

// ── VerticalThumb ─────────────────────────────────────────────────────────────

VerticalThumb::VerticalThumb(QWidget* parent) : QWidget(parent)
{
    setCursor(Qt::SizeVerCursor);
}

void VerticalThumb::setProgress(double f)
{
    m_progress = qBound(0.0, f, 1.0);
    update();
}

void VerticalThumb::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    // Track line
    p.fillRect((width() - 3) / 2, 0, 3, height(), QColor(255, 255, 255, 30));
    // Thumb
    constexpr int TH = 54, TW = 7;
    int availH = qMax(1, height() - TH);
    int thumbY  = static_cast<int>(m_progress * availH);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 120));
    p.drawRoundedRect((width() - TW) / 2, thumbY, TW, TH, 3, 3);
}

void VerticalThumb::mousePressEvent(QMouseEvent* e)
{
    m_dragging = true;
    m_dragStartY = e->pos().y();
    m_dragStartProgress = m_progress;
    e->accept();
}

void VerticalThumb::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging) { e->ignore(); return; }
    double frac = m_dragStartProgress + double(e->pos().y() - m_dragStartY) / qMax(1, height() - 54);
    m_progress = qBound(0.0, frac, 1.0);
    update();
    emit progressRequested(m_progress);
    e->accept();
}

void VerticalThumb::mouseReleaseEvent(QMouseEvent* e)
{
    m_dragging = false;
    e->accept();
}

// ── ScrubBar ─────────────────────────────────────────────────────────────────

ScrubBar::ScrubBar(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(18);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);

    m_bubble = new QLabel(this);
    m_bubble->setStyleSheet(
        "QLabel { background: rgba(0,0,0,220); color: white;"
        "  border: 1px solid rgba(255,255,255,40);"
        "  border-radius: 4px; padding: 2px 6px;"
        "  font-size: 11px; font-weight: 700; }"
    );
    m_bubble->setAlignment(Qt::AlignCenter);
    m_bubble->hide();
}

void ScrubBar::setProgress(double value)
{
    double clamped = qBound(0.0, value, 1.0);
    if (std::abs(clamped - m_progress) < 0.0001) return;
    m_progress = clamped;
    update();
}

void ScrubBar::setTotalPages(int total)
{
    m_totalPages = total;
}

void ScrubBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QRectF full(rect());
    double trackH = 4.0;
    QRectF track(0.0, (full.height() - trackH) / 2.0, full.width(), trackH);
    double radius = trackH / 2.0;

    p.setPen(Qt::NoPen);

    // Track background
    p.setBrush(Qt::white);
    p.setOpacity(0.26);
    p.drawRoundedRect(track, radius, radius);

    // Fill
    double fillW = track.width() * m_progress;
    if (fillW > 0.0) {
        QRectF fill(track.left(), track.top(), fillW, track.height());
        p.setOpacity(0.9);
        p.drawRoundedRect(fill, radius, radius);
    }

    // Thumb
    double thumbR = (m_hover || m_dragging) ? 5.0 : 4.0;
    double thumbX = track.left() + fillW;
    thumbX = qBound(track.left() + thumbR, thumbX, track.right() - thumbR);
    p.setOpacity((m_hover || m_dragging) ? 0.98 : 0.82);
    p.drawEllipse(QPointF(thumbX, full.center().y()), thumbR, thumbR);
}

double ScrubBar::ratioForX(double x) const
{
    return qBound(0.0, x / qMax(1.0, static_cast<double>(width())), 1.0);
}

int ScrubBar::pageForRatio(double ratio) const
{
    if (m_totalPages <= 1) return 0;
    return qBound(0, static_cast<int>(std::round(ratio * (m_totalPages - 1))), m_totalPages - 1);
}

void ScrubBar::updateBubble(double x)
{
    if (m_totalPages <= 0) { m_bubble->hide(); return; }
    double ratio = ratioForX(x);
    int page = pageForRatio(ratio) + 1; // 1-indexed
    m_bubble->setText(QString::number(page));
    m_bubble->adjustSize();
    int bw = m_bubble->width();
    int bx = qBound(0, static_cast<int>(x - bw / 2.0), width() - bw);
    int by = -m_bubble->height() - 4;
    m_bubble->move(bx, by);
    m_bubble->show();
    m_bubble->raise();
}

void ScrubBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_totalPages > 1) {
        m_dragging = true;
        double x = event->position().x();
        int page = pageForRatio(ratioForX(x));
        emit scrubRequested(page);
        updateBubble(x);
        update();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ScrubBar::mouseMoveEvent(QMouseEvent* event)
{
    double x = event->position().x();
    if (m_dragging) {
        int page = pageForRatio(ratioForX(x));
        emit scrubRequested(page);
        updateBubble(x);
        update();
        event->accept();
        return;
    }
    updateBubble(x);
    QWidget::mouseMoveEvent(event);
}

void ScrubBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false;
        double x = event->position().x();
        emit scrubRequested(pageForRatio(ratioForX(x)));
        if (!m_hover) m_bubble->hide();
        update();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ScrubBar::enterEvent(QEnterEvent*)
{
    m_hover = true;
    update();
}

void ScrubBar::leaveEvent(QEvent*)
{
    if (!m_dragging) {
        m_hover = false;
        m_bubble->hide();
        update();
    }
}

// ── ComicReader ──────────────────────────────────────────────────────────────

ComicReader::ComicReader(CoreBridge* bridge, QWidget* parent)
    : QWidget(parent)
    , m_bridge(bridge)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("background: #000000;");

    m_decodePool.setMaxThreadCount(2);

    buildUI();

    // J2: restore global memory saver setting
    {
        QSettings s("Tankoban", "Tankoban");
        m_memorySaver = s.value("memorySaver", false).toBool();
        if (m_memorySaver) m_cache.setBudget(256LL * 1024 * 1024);
    }

    m_cursorTimer.setSingleShot(true);
    m_cursorTimer.setInterval(3000);
    connect(&m_cursorTimer, &QTimer::timeout, this, [this]() {
        // Don't hide cursor when hovering over toolbar or its children
        if (m_toolbar->isVisible() && m_toolbar->underMouse())
            return;
        setCursor(Qt::BlankCursor);
    });

    m_hudAutoHideTimer.setSingleShot(true);
    m_hudAutoHideTimer.setInterval(3000);
    connect(&m_hudAutoHideTimer, &QTimer::timeout, this, [this]() {
        if (m_hudPinned) return;                                           // A1: pinned modes never auto-hide
        if (!m_toolbar->isVisible()) return;
        if (m_scrubBar && m_scrubBar->isDragging()) { m_hudAutoHideTimer.start(); return; } // A5: scrub freeze
        if (m_toolbar->underMouse()) { m_hudAutoHideTimer.start(); return; }
        if (isAnyOverlayOpen()) { m_hudAutoHideTimer.start(); return; }
        hideToolbar();
    });

    m_toastTimer.setSingleShot(true);
    m_toastTimer.setInterval(1200);

    m_clickTimer.setSingleShot(true);
    m_clickTimer.setInterval(220); // debounce: center zone single vs double click
    connect(&m_clickTimer, &QTimer::timeout, this, [this]() {
        // Single click center confirmed (no double-click followed) → toggle HUD
        toggleToolbar();
    });

    m_stripRefreshTimer.setSingleShot(true);
    m_stripRefreshTimer.setInterval(16);
    m_stripRefreshTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_stripRefreshTimer, &QTimer::timeout, this, [this]() {
        refreshVisibleStripPages();
        // E5: sync page counter after reflow (dimension hints can shift layout without a scroll event)
        if (m_stripCanvas) {
            int page = computePageInView();
            if (page != m_currentPage) {
                m_currentPage = page;
                updatePageLabel();
            }
        }
        // H4: keep vertical thumb in sync with scroll position
        if (m_verticalThumb) {
            auto* vbar = m_scrollArea->verticalScrollBar();
            if (vbar && vbar->maximum() > 0)
                m_verticalThumb->setProgress(double(vbar->value()) / vbar->maximum());
        }
        saveCurrentProgress();
    });
}

void ComicReader::buildUI()
{
    m_scrollArea = new SmoothScrollArea(this);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setStyleSheet("background: transparent; border: none;");
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setAlignment(Qt::AlignCenter);

    m_imageLabel = new QLabel();
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet("background: transparent;");
    m_scrollArea->setWidget(m_imageLabel);

    // Toolbar
    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName("ComicReaderToolbar");
    m_toolbar->setFixedHeight(66);
    m_toolbar->setStyleSheet(
        "QWidget#ComicReaderToolbar {"
        "  background: rgba(8, 8, 8, 0.82);"
        "  border-top: 1px solid rgba(255, 255, 255, 0.10);"
        "}"
    );

    auto* tbVBox = new QVBoxLayout(m_toolbar);
    tbVBox->setContentsMargins(16, 6, 16, 0);
    tbVBox->setSpacing(4);

    // Scrub bar (top of toolbar)
    m_scrubBar = new ScrubBar(m_toolbar);
    tbVBox->addWidget(m_scrubBar);

    // Button row (bottom of toolbar)
    auto* btnRow = new QWidget(m_toolbar);
    auto* tbLayout = new QHBoxLayout(btnRow);
    tbLayout->setContentsMargins(0, 0, 0, 0);
    tbLayout->setSpacing(8);
    tbVBox->addWidget(btnRow);

    auto makeBtn = [this](const QString& text, int w = 32) {
        auto* btn = new QPushButton(text, m_toolbar);
        btn->setFixedSize(w, 28);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton { color: rgba(255,255,255,0.78); background: rgba(255,255,255,0.06);"
            "  border: 1px solid rgba(255,255,255,0.10); border-radius: 8px;"
            "  padding: 0 8px; font-size: 11px; }"
            "QPushButton:hover { background: rgba(255,255,255,0.12); }"
        );
        return btn;
    };

    m_backBtn = makeBtn(QChar(0x2190) + QString(" Back"), 70);
    connect(m_backBtn, &QPushButton::clicked, this, [this]() {
        saveCurrentProgress();
        emit closeRequested();
    });
    tbLayout->addWidget(m_backBtn);

    m_prevVolBtn = makeBtn(QString(QChar(0x00AB)), 32);
    m_prevVolBtn->setToolTip("Previous volume");
    m_prevVolBtn->hide();
    connect(m_prevVolBtn, &QPushButton::clicked, this, &ComicReader::prevVolume);
    tbLayout->addWidget(m_prevVolBtn);

    tbLayout->addStretch();

    m_prevBtn = makeBtn(QString(QChar(0x25C1)), 32);
    connect(m_prevBtn, &QPushButton::clicked, this, &ComicReader::prevPage);
    tbLayout->addWidget(m_prevBtn);

    m_pageLabel = new QLabel("Page 1 / 1", m_toolbar);
    m_pageLabel->setStyleSheet("color: rgba(255,255,255,0.78); font-size: 12px; background: transparent;");
    m_pageLabel->setAlignment(Qt::AlignCenter);
    m_pageLabel->setMinimumWidth(120);
    tbLayout->addWidget(m_pageLabel);

    m_nextBtn = makeBtn(QString(QChar(0x25B7)), 32);
    connect(m_nextBtn, &QPushButton::clicked, this, &ComicReader::nextPage);
    tbLayout->addWidget(m_nextBtn);

    tbLayout->addStretch();

    // Mode button
    m_modeBtn = makeBtn("Single", 64);
    m_modeBtn->setToolTip("Reading mode (M)");
    connect(m_modeBtn, &QPushButton::clicked, this, &ComicReader::cycleReaderMode);
    tbLayout->addWidget(m_modeBtn);

    // Portrait width button
    m_portraitBtn = makeBtn(QString::number(m_portraitWidthPct) + "%", 48);
    m_portraitBtn->setToolTip("Page width");
    connect(m_portraitBtn, &QPushButton::clicked, this, &ComicReader::showPortraitWidthMenu);
    tbLayout->addWidget(m_portraitBtn);

    m_nextVolBtn = makeBtn(QString(QChar(0x00BB)), 32);
    m_nextVolBtn->setToolTip("Next volume");
    m_nextVolBtn->hide();
    connect(m_nextVolBtn, &QPushButton::clicked, this, &ComicReader::nextVolume);
    tbLayout->addWidget(m_nextVolBtn);

    // Connect scrub bar
    connect(m_scrubBar, &ScrubBar::scrubRequested, this, [this](int page) {
        showPage(page);
    });

    // ── End-of-volume overlay ──
    m_endOverlay = new QWidget(this);
    m_endOverlay->setStyleSheet("background: rgba(0, 0, 0, 0.75);");
    m_endOverlay->hide();

    auto* endCard = new QWidget(m_endOverlay);
    endCard->setFixedWidth(300);
    endCard->setStyleSheet(
        "background: rgba(18, 18, 18, 0.95);"
        "border: 1px solid rgba(255, 255, 255, 0.10);"
        "border-radius: 12px;"
    );

    auto* endLayout = new QVBoxLayout(endCard);
    endLayout->setContentsMargins(24, 20, 24, 20);
    endLayout->setSpacing(10);

    auto* endTitle = new QLabel("End of Volume", endCard);
    endTitle->setStyleSheet("color: rgba(255,255,255,0.90); font-size: 16px; font-weight: bold; background: transparent; border: none;");
    endTitle->setAlignment(Qt::AlignCenter);
    endLayout->addWidget(endTitle);

    m_endSubtitle = new QLabel("", endCard);
    m_endSubtitle->setStyleSheet("color: rgba(255,255,255,0.50); font-size: 11px; background: transparent; border: none;");
    m_endSubtitle->setAlignment(Qt::AlignCenter);
    m_endSubtitle->setWordWrap(true);
    endLayout->addWidget(m_endSubtitle);

    endLayout->addSpacing(6);

    auto endBtn = [endCard](const QString& text) {
        auto* btn = new QPushButton(text, endCard);
        btn->setFixedHeight(34);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton { color: rgba(255,255,255,0.82); background: rgba(255,255,255,0.06);"
            "  border: 1px solid rgba(255,255,255,0.12); border-radius: 8px;"
            "  font-size: 12px; padding: 0 16px; }"
            "QPushButton:hover { background: rgba(255,255,255,0.14); }"
        );
        return btn;
    };

    m_endNextBtn = endBtn("Next Volume  " + QString(QChar(0x2192)));
    connect(m_endNextBtn, &QPushButton::clicked, this, [this]() {
        hideEndOverlay(); nextVolume();
    });
    endLayout->addWidget(m_endNextBtn);

    auto* replayBtn = endBtn("Replay from Start");
    connect(replayBtn, &QPushButton::clicked, this, [this]() {
        hideEndOverlay(); showPage(0);
    });
    endLayout->addWidget(replayBtn);

    auto* backBtn = endBtn(QString(QChar(0x2190)) + "  Back to Library");
    connect(backBtn, &QPushButton::clicked, this, [this]() {
        saveCurrentProgress(); emit closeRequested();
    });
    endLayout->addWidget(backBtn);

    endLayout->addSpacing(4);

    auto* hint = new QLabel("Space: next  \xC2\xB7  Esc: library", endCard);
    hint->setStyleSheet("color: rgba(255,255,255,0.30); font-size: 10px; background: transparent; border: none;");
    hint->setAlignment(Qt::AlignCenter);
    endLayout->addWidget(hint);

    // Center the card in the overlay
    auto* overlayLayout = new QVBoxLayout(m_endOverlay);
    overlayLayout->setAlignment(Qt::AlignCenter);
    overlayLayout->addWidget(endCard, 0, Qt::AlignCenter);

    // ── Volume navigator overlay ── H1: ClickScrim dismisses on background click
    auto* volScrim = new ClickScrim(this);
    volScrim->setStyleSheet("background: rgba(0, 0, 0, 0.75);");
    connect(volScrim, &ClickScrim::clicked, this, &ComicReader::hideVolumeNavigator);
    m_volOverlay = volScrim;
    m_volOverlay->hide();

    auto* volCard = new QWidget(m_volOverlay);
    volCard->setFixedWidth(360);
    volCard->setMaximumHeight(static_cast<int>(height() * 0.6));
    volCard->setStyleSheet(
        "background: rgba(18, 18, 18, 0.95);"
        "border: 1px solid rgba(255, 255, 255, 0.10);"
        "border-radius: 12px;"
    );

    auto* volLayout = new QVBoxLayout(volCard);
    volLayout->setContentsMargins(20, 16, 20, 16);
    volLayout->setSpacing(8);

    m_volTitle = new QLabel("Volumes", volCard);
    m_volTitle->setStyleSheet("color: rgba(255,255,255,0.90); font-size: 14px; font-weight: bold; background: transparent; border: none;");
    m_volTitle->setAlignment(Qt::AlignCenter);
    volLayout->addWidget(m_volTitle);

    m_volSearch = new QLineEdit(volCard);
    m_volSearch->setPlaceholderText("Search volumes...");
    m_volSearch->setStyleSheet(
        "QLineEdit { color: white; background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.15); border-radius: 6px;"
        "  padding: 6px 10px; font-size: 12px; }"
        "QLineEdit:focus { border-color: rgba(255,255,255,0.30); }"
    );
    volLayout->addWidget(m_volSearch);

    m_volList = new QListWidget(volCard);
    m_volList->setStyleSheet(
        "QListWidget { background: transparent; border: none; outline: none; }"
        "QListWidget::item { color: rgba(255,255,255,0.72); padding: 8px 10px;"
        "  border-radius: 6px; font-size: 12px; }"
        "QListWidget::item:selected { background: rgba(255,255,255,0.12);"
        "  color: rgba(255,255,255,0.95); }"
        "QListWidget::item:hover { background: rgba(255,255,255,0.08); }"
    );
    m_volList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_volList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    volLayout->addWidget(m_volList, 1);

    // Search filtering — D9: jump to first match on type, restore to current volume on clear
    connect(m_volSearch, &QLineEdit::textChanged, this, [this](const QString& text) {
        int currentIdx = m_seriesCbzList.indexOf(m_cbzPath);
        QListWidgetItem* firstMatch = nullptr;
        // D4: Check if query is all digits for numeric search
        bool numericQuery = !text.isEmpty() && QRegularExpression("^\\d+$").match(text).hasMatch();
        for (int i = 0; i < m_volList->count(); ++i) {
            auto* item = m_volList->item(i);
            bool match = false;
            if (text.isEmpty()) {
                match = true;
            } else if (numericQuery) {
                // D4: Numeric search — extract digit sequences from basename, match if any contains the query
                QString baseName = item->data(Qt::UserRole + 1).toString();
                QRegularExpression numRe("\\d+");
                auto it = numRe.globalMatch(baseName);
                while (it.hasNext()) {
                    if (it.next().captured() == text) { match = true; break; }
                }
                // Also try substring match on display text as fallback
                if (!match) match = item->text().contains(text, Qt::CaseInsensitive);
            } else {
                match = item->text().contains(text, Qt::CaseInsensitive);
            }
            item->setHidden(!match);
            if (match && !firstMatch) firstMatch = item;
        }
        if (text.isEmpty()) {
            for (int i = 0; i < m_volList->count(); ++i) {
                auto* item = m_volList->item(i);
                if (item->data(Qt::UserRole).toInt() == currentIdx) {
                    m_volList->setCurrentItem(item);
                    m_volList->scrollToItem(item);
                    break;
                }
            }
        } else if (firstMatch) {
            m_volList->setCurrentItem(firstMatch);
            m_volList->scrollToItem(firstMatch);
        }
    });

    // Double-click to open
    connect(m_volList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        int idx = item->data(Qt::UserRole).toInt();
        hideVolumeNavigator();
        openVolumeByIndex(idx);
    });

    // Enter key in list
    connect(m_volList, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        int idx = item->data(Qt::UserRole).toInt();
        hideVolumeNavigator();
        openVolumeByIndex(idx);
    });

    auto* volOverlayLayout = new QVBoxLayout(m_volOverlay);
    volOverlayLayout->setAlignment(Qt::AlignCenter);
    volOverlayLayout->addWidget(volCard, 0, Qt::AlignCenter);

    // ── H2: Side nav arrows ───────────────────────────────────────────────────
    m_leftArrow  = new SideNavArrow(false, this);
    m_rightArrow = new SideNavArrow(true,  this);
    m_leftArrow->hide();
    m_rightArrow->hide();
}

// ── Open Book ───────────────────────────────────────────────────────────────

void ComicReader::openBook(const QString& cbzPath,
                            const QStringList& seriesCbzList,
                            const QString& seriesName)
{
    m_cbzPath = cbzPath;
    m_seriesCbzList = seriesCbzList;
    m_seriesName = seriesName;
    // J1: brief loading indicator before synchronous pageList call
    m_imageLabel->setText("Loading...");
    m_imageLabel->repaint();
    m_pageNames = ArchiveReader::pageList(cbzPath);
    m_currentPage = 0;
    m_currentPixmap = QPixmap();
    m_secondPixmap = QPixmap();
    // A1: Invalidate any in-flight decodes from the previous volume
    m_inflightDecodes.clear();
    ++m_currentVolumeId;
    m_navBusy = false;
    m_navTarget = -1;
    m_cache.clear();
    m_spreadOverrides.clear();
    m_couplingMode = "auto";
    m_couplingPhase = "normal";
    m_couplingConfidence = 0.0f;
    m_couplingResolved = false;
    m_couplingProbeAttempts = 0;
    resetZoomPan();

    m_pageMeta.clear();
    m_pageMeta.resize(m_pageNames.size());
    for (int i = 0; i < m_pageNames.size(); ++i) {
        m_pageMeta[i].index = i;
        m_pageMeta[i].filename = m_pageNames[i];
    }
    invalidatePairing();

    if (m_pageNames.isEmpty()) {
        // J1: distinguish missing file from empty archive
        m_imageLabel->setText(QFileInfo(cbzPath).exists()
            ? "No image pages found"
            : "File not found");
        updatePageLabel();
        return;
    }

    int volIdx = m_seriesCbzList.indexOf(m_cbzPath);
    m_prevVolBtn->setVisible(!m_seriesCbzList.isEmpty() && volIdx > 0);
    m_nextVolBtn->setVisible(!m_seriesCbzList.isEmpty() && volIdx < m_seriesCbzList.size() - 1);

    applySeriesSettings();  // D11: restore portrait width, mode, coupling phase — must precede button visibility
    m_hudPinned = (m_readerMode != ReaderMode::ScrollStrip); // A2: SinglePage/DoublePage = pinned HUD

    // Update toolbar visibility for mode (after applySeriesSettings so restored mode is reflected)
    m_portraitBtn->setVisible(!m_isDoublePage);

    int startPage = restoreSavedPage();
    QJsonObject savedProgress = m_bridge ? m_bridge->progress("comics", itemIdForPath(cbzPath)) : QJsonObject();
    bool hadProgress = savedProgress.contains("updatedAt");
    double savedScrollFrac = savedProgress["scrollFraction"].toDouble(0.0);
    showPage(startPage);

    // Batch K: restore scroll fraction in scroll-strip mode
    if (m_readerMode == ReaderMode::ScrollStrip && savedScrollFrac > 0.0) {
        QTimer::singleShot(300, this, [this, savedScrollFrac]() {
            if (m_readerMode != ReaderMode::ScrollStrip || !m_scrollArea) return;
            auto* vbar = m_scrollArea->verticalScrollBar();
            if (vbar && vbar->maximum() > 0) {
                int target = static_cast<int>(std::round(savedScrollFrac * vbar->maximum()));
                vbar->setValue(target);
                m_scrollArea->syncExternalScroll(target);
            }
        });
    }

    // D7: Resumed/Ready toast
    QTimer::singleShot(250, this, [this, hadProgress]() { showToast(hadProgress ? "Resumed" : "Ready"); });
    showToolbar();
}

// ── Canonical Pairing ───────────────────────────────────────────────────────

bool ComicReader::resolveSpread(int index) const
{
    if (index <= 0 || index >= m_pageMeta.size()) return false;
    auto it = m_spreadOverrides.find(index);
    if (it != m_spreadOverrides.end()) return it.value();
    return m_pageMeta[index].isSpread;
}

bool ComicReader::isSpreadIndex(int index) const { return resolveSpread(index); }

void ComicReader::invalidatePairing()
{
    m_canonicalUnits.clear();
    m_unitByPage.clear();
}

void ComicReader::buildCanonicalPairingUnits()
{
    m_canonicalUnits.clear();
    m_unitByPage.clear();

    int total = m_pageNames.size();
    if (total == 0) return;

    int nudge = (m_couplingPhase == "shifted") ? 1 : 0;
    int extraSlots = 0;
    int idx = 0;

    while (idx < total) {
        TwoPagePair unit;

        if (idx == 0) {
            // Cover always alone
            unit.rightIndex = 0;
            unit.coverAlone = true;
            m_unitByPage[0] = m_canonicalUnits.size();
            m_canonicalUnits.append(unit);
            idx++;
            continue;
        }

        bool isSpr = resolveSpread(idx);
        if (isSpr) {
            unit.rightIndex = idx;
            unit.isSpread = true;
            m_unitByPage[idx] = m_canonicalUnits.size();
            m_canonicalUnits.append(unit);
            extraSlots++;
            idx++;
            continue;
        }

        int parity = (idx + extraSlots + nudge) % 2;
        if (parity == 1 && idx + 1 < total && !resolveSpread(idx + 1)) {
            // Pair two pages
            unit.rightIndex = idx;
            unit.leftIndex = idx + 1;
            m_unitByPage[idx] = m_canonicalUnits.size();
            m_unitByPage[idx + 1] = m_canonicalUnits.size();
            m_canonicalUnits.append(unit);
            idx += 2;
        } else {
            // Single page
            unit.rightIndex = idx;
            unit.unpairedSingle = true;
            m_unitByPage[idx] = m_canonicalUnits.size();
            m_canonicalUnits.append(unit);
            idx++;
        }
    }
}

const TwoPagePair* ComicReader::pairForPage(int pageIndex) const
{
    auto it = m_unitByPage.find(pageIndex);
    if (it == m_unitByPage.end() || it.value() >= m_canonicalUnits.size())
        return nullptr;
    return &m_canonicalUnits[it.value()];
}

int ComicReader::navigateToUnit(int unitIndex)
{
    if (unitIndex < 0 || unitIndex >= m_canonicalUnits.size())
        return m_currentPage;
    return m_canonicalUnits[unitIndex].rightIndex;
}

// ── Show Page ───────────────────────────────────────────────────────────────

void ComicReader::requestDecode(int pageIndex)
{
    if (pageIndex < 0 || pageIndex >= m_pageNames.size()) return;
    if (m_cache.contains(pageIndex)) return;
    if (m_inflightDecodes.contains(pageIndex)) return;

    m_inflightDecodes.insert(pageIndex);
    auto* task = new DecodeTask(pageIndex, m_cbzPath, m_pageNames[pageIndex], m_currentVolumeId);
    // A1: lambda connection matches 5-arg decoded signal (includes volumeId)
    connect(&task->notifier, &DecodeTaskSignals::decoded, this,
            [this](int idx, const QPixmap& px, int w, int h, int vid) {
                onPageDecoded(idx, px, w, h, vid);
            }, Qt::QueuedConnection);
    connect(&task->notifier, &DecodeTaskSignals::failed,
            this, [this](int idx) {
                m_inflightDecodes.remove(idx);
                // Clear nav lock if we were waiting for this page
                if (idx == m_currentPage && m_navBusy) {
                    m_navBusy = false;
                    if (m_navTarget != -1) {
                        int target = m_navTarget;
                        m_navTarget = -1;
                        showPage(target);
                    }
                }
            }, Qt::QueuedConnection);
    // A3: wire dimensionsReady to ScrollStripCanvas when in strip mode
    if (m_stripCanvas) {
        connect(&task->notifier, &DecodeTaskSignals::dimensionsReady,
                m_stripCanvas, &ScrollStripCanvas::updatePageDimensions,
                Qt::QueuedConnection);
    }
    m_decodePool.start(task);
}

void ComicReader::onPageDecoded(int pageIndex, const QPixmap& pixmap, int w, int h, int volumeId)
{
    // A1: Discard results from a previous volume (rapid open-another-volume case)
    if (volumeId != m_currentVolumeId) return;
    m_inflightDecodes.remove(pageIndex);
    m_cache.insert(pageIndex, pixmap);

    bool spreadChanged = false;
    if (pageIndex >= 0 && pageIndex < m_pageMeta.size()) {
        bool wasSpread = m_pageMeta[pageIndex].isSpread;
        m_pageMeta[pageIndex].width = w;
        m_pageMeta[pageIndex].height = h;
        m_pageMeta[pageIndex].isSpread = (h > 0 && static_cast<double>(w) / h > SPREAD_RATIO);
        m_pageMeta[pageIndex].decoded = true;
        spreadChanged = (m_pageMeta[pageIndex].isSpread != wasSpread);
    }

    // Try auto-coupling after pages decode
    if (!m_couplingResolved && m_couplingMode == "auto")
        maybeRunAutoCoupling();

    // Rebuild pairing when a newly decoded page changes spread status
    if (spreadChanged && m_isDoublePage && !m_canonicalUnits.isEmpty()) {
        invalidatePairing();
        buildCanonicalPairingUnits();
        // Reload second pixmap for the current pair
        m_secondPixmap = QPixmap();
        m_displayCachePage = -1;
        auto* newPair = pairForPage(m_currentPage);
        if (newPair && newPair->leftIndex >= 0 && m_cache.contains(newPair->leftIndex))
            m_secondPixmap = m_cache.get(newPair->leftIndex);
        if (!m_currentPixmap.isNull())
            displayCurrentPage();
    }

    // Scroll strip: feed to canvas
    if (m_isScrollStrip && m_stripCanvas) {
        m_stripCanvas->onPageDecoded(pageIndex, pixmap, w, h);
        return;
    }

    if (pageIndex == m_currentPage) {
        m_currentPixmap = pixmap;
        m_displayCachePage = -1; // invalidate cache — new pixmap data
        displayCurrentPage();

        // E2: Navigation coalescing drain
        if (m_navTarget != -1) {
            int target = m_navTarget;
            m_navBusy = false;
            showPage(target);
            return;
        }
        m_navBusy = false;
    }
    if (m_isDoublePage && m_secondPixmap.isNull() && pageIndex != m_currentPage) {
        auto* pair = pairForPage(m_currentPage);
        if (pair && pair->leftIndex == pageIndex) {
            m_secondPixmap = pixmap;
            m_displayCachePage = -1; // invalidate cache — pair arrived
            displayCurrentPage();
        }
    }
}

int ComicReader::pageAdvanceCount() const
{
    if (!m_isDoublePage) return 1;
    auto* pair = pairForPage(m_currentPage);
    if (!pair) return 1;
    if (pair->leftIndex >= 0) return 2; // paired
    return 1; // single/spread/cover
}


void ComicReader::showPage(int index)
{
    if (m_pageNames.isEmpty()) return;
    index = qBound(0, index, m_pageNames.size() - 1);

    // E2: Navigation coalescing — queue async targets, but let cached pages through
    if (m_navBusy) {
        if (!m_cache.contains(index)) {
            m_navTarget = index;
            return;
        }
        // Cache hit while busy — show immediately, abandon pending decode wait
    }
    m_navBusy = true;
    m_navTarget = -1;

    // Double-page: ensure pairing and snap to rightIndex of the unit
    // (prevents duplicate rendering when called with a leftIndex,
    //  e.g. from restored progress or auto-coupling)
    if (m_isDoublePage) {
        if (m_canonicalUnits.isEmpty())
            buildCanonicalPairingUnits();
        auto it = m_unitByPage.find(index);
        if (it != m_unitByPage.end() && it.value() < m_canonicalUnits.size())
            index = m_canonicalUnits[it.value()].rightIndex;
    }

    m_currentPage = index;

    // Scroll strip mode: scroll to the page position
    if (m_isScrollStrip && m_stripCanvas) {
        double targetY = m_stripCanvas->pageTopY(index);
        if (auto* vbar = m_scrollArea->verticalScrollBar())
            vbar->setValue(static_cast<int>(targetY));
        updatePageLabel();
        refreshVisibleStripPages();
        saveCurrentProgress();
        m_navBusy = false;
        return;
    }

    m_secondPixmap = QPixmap();
    m_displayCachePage = -1; // invalidate display cache for new page

    // Load current page — async on cache miss (never block the UI)
    m_cache.pin(index);
    if (m_cache.contains(index)) {
        m_currentPixmap = m_cache.get(index);
    } else {
        m_currentPixmap = QPixmap(); // show nothing until decoded
        requestDecode(index);
    }

    // Load second page for double-page pairs — async on cache miss
    if (m_isDoublePage) {
        auto* pair = pairForPage(index);
        if (pair && pair->leftIndex >= 0) {
            int secondIdx = pair->leftIndex;
            m_cache.pin(secondIdx);
            if (m_cache.contains(secondIdx)) {
                m_secondPixmap = m_cache.get(secondIdx);
            } else {
                requestDecode(secondIdx);
            }
        }
    }

    if (!m_currentPixmap.isNull())
        displayCurrentPage();

    // Reset scroll to top on page turn; reset pan
    if (auto* vbar = m_scrollArea->verticalScrollBar())
        vbar->setValue(0);
    m_panX = 0;
    m_panY = 0;
    if (auto* hbar = m_scrollArea->horizontalScrollBar())
        hbar->setValue(0);

    updatePageLabel();
    prefetchNeighbors();
    saveCurrentProgress();

    // E2: If page was displayed synchronously (cache hit), clear nav busy and drain
    if (!m_currentPixmap.isNull()) {
        if (m_navTarget != -1) {
            int target = m_navTarget;
            m_navBusy = false;
            showPage(target);
            return;
        }
        m_navBusy = false;
    }
}

static void drawGutterShadow(QPainter& p, double gutterX, double y, double h, double strength)
{
    if (strength <= 0.0 || h <= 0.0) return;
    double bleed = 4.0;
    double x0 = gutterX - bleed;
    double totalW = bleed * 2.0;

    QLinearGradient grad(x0, 0.0, x0 + totalW, 0.0);
    double aEdge = 0.10 * strength;
    double aMid  = 0.28 * strength;
    grad.setColorAt(0.00, QColor(0, 0, 0, static_cast<int>(aEdge * 255)));
    grad.setColorAt(0.45, QColor(0, 0, 0, static_cast<int>(aMid  * 255)));
    grad.setColorAt(0.55, QColor(0, 0, 0, static_cast<int>(aMid  * 255)));
    grad.setColorAt(1.00, QColor(0, 0, 0, static_cast<int>(aEdge * 255)));

    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawRect(QRectF(x0, y, totalW, h));
    p.restore();
}

void ComicReader::displayCurrentPage()
{
    if (m_currentPixmap.isNull()) return;

    int availW = m_scrollArea->viewport()->width();
    int availH = m_scrollArea->viewport()->height();
    if (availW <= 0 || availH <= 0) return;

    // Check display cache — skip rendering if nothing changed
    bool hasPair = m_isDoublePage && !m_secondPixmap.isNull();
    if (!m_displayCache.isNull()
        && m_displayCacheW == availW
        && m_displayCacheH == availH
        && m_displayCacheZoom == m_zoomPct
        && m_displayCachePage == m_currentPage
        && m_displayCacheHasPair == hasPair) {
        if (m_isDoublePage && m_zoomPct > 100) applyPan(); // pan only active when zoomed
        return; // already rendered this exact state
    }

    // Single-page mode: no-upscale metric (B1)
    if (!m_isDoublePage) {
        QPixmap scaled;
        switch (m_fitMode) {
        case FitMode::FitPage: {
            double dpr = devicePixelRatioF();
            int capW = qRound(m_currentPixmap.width() * dpr); // native-resolution ceiling
            bool isWide = (m_currentPixmap.width() > m_currentPixmap.height() * SPREAD_RATIO);
            int maxW = isWide ? availW : qRound(availW * m_portraitWidthPct / 100.0);
            int drawW = qMin(maxW, capW);
            double scale = drawW / static_cast<double>(qMax(1, m_currentPixmap.width()));
            int drawH = qRound(m_currentPixmap.height() * scale);
            // Also cap to viewport height
            if (drawH > availH) {
                scale = availH / static_cast<double>(qMax(1, m_currentPixmap.height()));
                drawW = qRound(m_currentPixmap.width() * scale);
                drawH = availH;
            }
            scaled = m_currentPixmap.scaled(drawW, drawH, Qt::KeepAspectRatio, m_scalingQuality);
            break;
        }
        case FitMode::FitWidth: {
            double frac = m_portraitWidthPct / 100.0;
            scaled = m_currentPixmap.scaledToWidth(static_cast<int>(availW * frac), m_scalingQuality);
            break;
        }
        case FitMode::FitHeight:
            scaled = m_currentPixmap.scaledToHeight(availH, m_scalingQuality);
            break;
        }
        m_imageLabel->setPixmap(scaled);
        m_imageLabel->resize(scaled.size());
        return;
    }

    // ── Double-page mode: fit-width, each page fills its half ────────────
    double zoom = m_zoomPct / 100.0;
    double totalW = availW * zoom;

    auto* pair = pairForPage(m_currentPage);
    bool isSpread = pair && pair->isSpread;
    hasPair = pair && pair->leftIndex >= 0 && !m_secondPixmap.isNull();
    bool coverOrSingle = !hasPair && !isSpread;

    if (isSpread) {
        // Spread: single wide image fills full viewport width
        int drawW = static_cast<int>(totalW);
        drawW = qMin(drawW, m_currentPixmap.width()); // no upscale
        double scale = static_cast<double>(drawW) / qMax(1, m_currentPixmap.width());
        int drawH = static_cast<int>(m_currentPixmap.height() * scale);

        int canvasW = qMax(availW, drawW);
        double dpr = devicePixelRatioF();
        QPixmap canvas(static_cast<int>(canvasW * dpr + 0.5), static_cast<int>(drawH * dpr + 0.5));
        canvas.setDevicePixelRatio(dpr);
        canvas.fill(Qt::black);
        QPainter p(&canvas);
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_scalingQuality == Qt::SmoothTransformation);
        int dx = (canvasW - drawW) / 2;  // logical coords — painter scales to physical
        p.drawPixmap(dx, 0, drawW, drawH, m_currentPixmap);
        m_imageLabel->setPixmap(canvas);
        m_imageLabel->resize(canvasW, drawH);

    } else if (coverOrSingle) {
        // B3+B4: Cover alone or unpaired single — flush to spine line with gutter gap
        int totalWi = static_cast<int>(totalW);
        int leftW = (totalWi - TWO_PAGE_GUTTER_PX) / 2;
        int coverDrawW = qMin(leftW, m_currentPixmap.width()); // no upscale
        double scale = static_cast<double>(coverDrawW) / qMax(1, m_currentPixmap.width());
        int drawH = static_cast<int>(m_currentPixmap.height() * scale);

        int canvasW2 = qMax(availW, totalWi);
        double dpr2 = devicePixelRatioF();
        QPixmap canvas(static_cast<int>(canvasW2 * dpr2 + 0.5), static_cast<int>(drawH * dpr2 + 0.5));
        canvas.setDevicePixelRatio(dpr2);
        canvas.fill(Qt::black);
        QPainter p(&canvas);
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_scalingQuality == Qt::SmoothTransformation);
        // B4: flush cover to spine line using gutter-aware leftW
        int dx = m_rtl ? (leftW + TWO_PAGE_GUTTER_PX) : (leftW - coverDrawW);
        p.drawPixmap(dx, 0, coverDrawW, drawH, m_currentPixmap);
        m_imageLabel->setPixmap(canvas);
        m_imageLabel->resize(canvasW2, drawH);

    } else {
        // B2+B3: Normal pair with unified scale and gutter gap
        int totalWi = static_cast<int>(totalW);
        int leftW  = (totalWi - TWO_PAGE_GUTTER_PX) / 2;
        int rightW = totalWi - TWO_PAGE_GUTTER_PX - leftW;

        // Compute individual scale factors capped to each half
        double scaleR = static_cast<double>(qMin(rightW, m_currentPixmap.width())) / qMax(1, m_currentPixmap.width());
        double scaleL = static_cast<double>(qMin(leftW,  m_secondPixmap.width()))  / qMax(1, m_secondPixmap.width());

        // B2: Unified pair scale — both pages at identical heights
        double baseScale = qMin(scaleR, scaleL);
        int rw = qRound(m_currentPixmap.width()  * baseScale);
        int rh = qRound(m_currentPixmap.height()  * baseScale);
        int lw = qRound(m_secondPixmap.width()  * baseScale);
        int lh = qRound(m_secondPixmap.height() * baseScale);

        int contentH = qMax(rh, lh);
        int canvasW = qMax(availW, totalWi);
        double dpr3 = devicePixelRatioF();
        QPixmap canvas(static_cast<int>(canvasW * dpr3 + 0.5), static_cast<int>(contentH * dpr3 + 0.5));
        canvas.setDevicePixelRatio(dpr3);
        canvas.fill(Qt::black);
        QPainter p(&canvas);
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_scalingQuality == Qt::SmoothTransformation);

        // Position pages: right page in right half, left page in left half
        // RTL swaps which pixmap goes where
        const QPixmap& pageL = m_rtl ? m_currentPixmap : m_secondPixmap;
        const QPixmap& pageR = m_rtl ? m_secondPixmap : m_currentPixmap;
        int plw = m_rtl ? rw : lw;
        int plh = m_rtl ? rh : lh;
        int prw = m_rtl ? lw : rw;
        int prh = m_rtl ? lh : rh;

        // Left page: right-aligned within left half, centered vertically
        int dxL = leftW - plw;
        int dyL = (contentH - plh) / 2;
        p.drawPixmap(dxL, dyL, plw, plh, pageL);

        // Right page: left-aligned within right half (after gutter), centered vertically
        int dxR = leftW + TWO_PAGE_GUTTER_PX;
        int dyR = (contentH - prh) / 2;
        p.drawPixmap(dxR, dyR, prw, prh, pageR);

        // Gutter shadow at the center line (spans the gutter gap)
        drawGutterShadow(p, leftW + TWO_PAGE_GUTTER_PX / 2, 0, contentH, m_gutterShadow);

        m_imageLabel->setPixmap(canvas);
        m_imageLabel->resize(canvasW, contentH);
    }

    // Update display cache
    m_displayCacheW = availW;
    m_displayCacheH = availH;
    m_displayCacheZoom = m_zoomPct;
    m_displayCachePage = m_currentPage;
    m_displayCacheHasPair = hasPair;
    m_displayCache = m_imageLabel->pixmap();
    if (m_isDoublePage && m_zoomPct > 100) applyPan();
}

void ComicReader::prefetchNeighbors()
{
    if (m_isDoublePage && !m_canonicalUnits.isEmpty()) {
        // Pair-aware prefetch: decode both pages of next and prev pairs
        auto it = m_unitByPage.find(m_currentPage);
        if (it == m_unitByPage.end()) return;
        int unitIdx = it.value();

        // Prefetch next 2 pairs
        for (int u = unitIdx + 1; u <= unitIdx + 2 && u < m_canonicalUnits.size(); ++u) {
            const auto& pair = m_canonicalUnits[u];
            requestDecode(pair.rightIndex);
            if (pair.leftIndex >= 0) requestDecode(pair.leftIndex);
        }
        // Prefetch previous pair
        if (unitIdx > 0) {
            const auto& pair = m_canonicalUnits[unitIdx - 1];
            requestDecode(pair.rightIndex);
            if (pair.leftIndex >= 0) requestDecode(pair.leftIndex);
        }
    } else {
        // Single-page: prefetch forward + backward
        for (int i = 1; i <= 4; ++i) {
            int idx = m_currentPage + i;
            if (idx < m_pageNames.size()) requestDecode(idx);
        }
        if (m_currentPage > 0) requestDecode(m_currentPage - 1);
    }
}

void ComicReader::nextPage()
{
    if (!m_isDoublePage) {
        if (m_currentPage + 1 < m_pageNames.size())
            showPage(m_currentPage + 1);
        else
            showEndOverlay();
        return;
    }
    auto it = m_unitByPage.find(m_currentPage);
    if (it != m_unitByPage.end() && it.value() + 1 < m_canonicalUnits.size())
        showPage(navigateToUnit(it.value() + 1));
    else
        showEndOverlay();
}

void ComicReader::prevPage()
{
    if (!m_isDoublePage) {
        if (m_currentPage > 0) showPage(m_currentPage - 1);
        return;
    }
    auto it = m_unitByPage.find(m_currentPage);
    if (it != m_unitByPage.end() && it.value() > 0)
        showPage(navigateToUnit(it.value() - 1));
}

void ComicReader::updatePageLabel()
{
    int total = m_pageNames.size();
    if (total == 0) { m_pageLabel->setText("No pages"); return; }

    // Update scrub bar
    m_scrubBar->setTotalPages(total);
    m_scrubBar->setProgress(total <= 1 ? 0.0 : static_cast<double>(m_currentPage) / (total - 1));

    if (m_isDoublePage && !m_secondPixmap.isNull()) {
        auto* pair = pairForPage(m_currentPage);
        if (pair && pair->leftIndex >= 0) {
            m_pageLabel->setText(QString("Pages %1-%2 / %3")
                .arg(pair->rightIndex + 1).arg(pair->leftIndex + 1).arg(total));
            return;
        }
    }
    m_pageLabel->setText(QString("Page %1 / %2").arg(m_currentPage + 1).arg(total));
}

// ── Progress ────────────────────────────────────────────────────────────────

QString ComicReader::itemIdForPath(const QString& path) const
{
    return QString(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
}

void ComicReader::saveCurrentProgress()
{
    if (!m_bridge || m_pageNames.isEmpty()) return;

    QJsonObject prev = m_bridge->progress("comics", itemIdForPath(m_cbzPath));
    int maxSeen = qMax(prev["maxPageIndexSeen"].toInt(0), m_currentPage);

    QJsonArray spreadsArr, normalsArr;
    for (int i = 0; i < m_pageMeta.size(); ++i) {
        if (m_pageMeta[i].decoded) {
            if (m_pageMeta[i].isSpread) spreadsArr.append(i);
            else normalsArr.append(i);
        }
    }

    QJsonArray bookmarksArr;
    for (int b : m_bookmarks) bookmarksArr.append(b);

    QJsonObject data;
    data["page"]               = m_currentPage;
    data["pageCount"]          = m_pageNames.size();
    data["path"]               = m_cbzPath;           // CONTRACTS.md — non-negotiable
    data["maxPageIndexSeen"]   = maxSeen;
    data["knownSpreadIndices"] = spreadsArr;
    data["knownNormalIndices"] = normalsArr;
    data["updatedAt"]          = QDateTime::currentMSecsSinceEpoch();
    data["couplingMode"]       = m_couplingMode;
    data["couplingPhase"]      = m_couplingPhase;
    data["bookmarks"]          = bookmarksArr;

    // Scroll fraction for scroll-strip resume
    if (m_readerMode == ReaderMode::ScrollStrip && m_scrollArea) {
        auto* vbar = m_scrollArea->verticalScrollBar();
        if (vbar && vbar->maximum() > 0)
            data["scrollFraction"] = double(vbar->value()) / double(vbar->maximum());
        else
            data["scrollFraction"] = 0.0;
    } else {
        data["scrollFraction"] = 0.0;
    }

    // Preserve finished flag — don't overwrite a completed volume
    if (prev.contains("finished"))   data["finished"]   = prev["finished"];
    if (prev.contains("finishedAt")) data["finishedAt"] = prev["finishedAt"];

    int total = m_pageNames.size();
    if (total > 0 && maxSeen >= total - 1 && !data["finished"].toBool(false)) {
        data["finished"]   = true;
        data["finishedAt"] = QDateTime::currentMSecsSinceEpoch();
    }

    m_bridge->saveProgress("comics", itemIdForPath(m_cbzPath), data);
    saveSeriesSettings();  // D11: persist series-level settings alongside progress
}

int ComicReader::restoreSavedPage()
{
    if (!m_bridge) return 0;
    QJsonObject data = m_bridge->progress("comics", itemIdForPath(m_cbzPath));
    if (data.isEmpty()) return 0;

    // Restore spread knowledge BEFORE pairing build — pairing needs isSpread data
    for (const auto& v : data["knownSpreadIndices"].toArray()) {
        int i = v.toInt();
        if (i >= 0 && i < m_pageMeta.size()) m_pageMeta[i].isSpread = true;
    }

    // Restore coupling — prevents re-detection on re-open
    QString savedMode  = data["couplingMode"].toString();
    QString savedPhase = data["couplingPhase"].toString();
    if (!savedMode.isEmpty() && savedMode != "auto") {
        m_couplingMode       = savedMode;
        m_couplingPhase      = savedPhase.isEmpty() ? "normal" : savedPhase;
        m_couplingConfidence = 1.0f;
        m_couplingResolved   = true;
        if (m_isDoublePage) { invalidatePairing(); buildCanonicalPairingUnits(); }
    }

    // Restore bookmarks
    m_bookmarks.clear();
    for (const auto& v : data["bookmarks"].toArray()) m_bookmarks.insert(v.toInt());

    int page = data["page"].toInt(0);
    if (page >= m_pageNames.size()) page = 0;
    return page;
}

// ── Fit Modes ───────────────────────────────────────────────────────────────

void ComicReader::cycleFitMode()
{
    switch (m_fitMode) {
    case FitMode::FitPage:   m_fitMode = FitMode::FitWidth;  break;
    case FitMode::FitWidth:  m_fitMode = FitMode::FitHeight; break;
    case FitMode::FitHeight: m_fitMode = FitMode::FitPage;   break;
    }
    static const QMap<FitMode, QString> names = {
        {FitMode::FitPage, "Fit Page"}, {FitMode::FitWidth, "Fit Width"}, {FitMode::FitHeight, "Fit Height"},
    };
    showToast(names[m_fitMode]);
    displayCurrentPage();
}

void ComicReader::showToast(const QString& text)
{
    if (!m_toastLabel) {
        m_toastLabel = new QLabel(this);
        m_toastLabel->setAlignment(Qt::AlignCenter);
        m_toastLabel->setStyleSheet(
            "background: rgba(0,0,0,0.75); color: white; border-radius: 12px; padding: 8px 20px; font-size: 13px;");
        connect(&m_toastTimer, &QTimer::timeout, m_toastLabel, &QLabel::hide);
    }
    m_toastLabel->setText(text);
    m_toastLabel->adjustSize();
    // G3: position above toolbar when visible, otherwise near bottom
    int toastY = m_toolbar->isVisible()
        ? m_toolbar->y() - m_toastLabel->height() - 8
        : height() - m_toastLabel->height() - 80;
    m_toastLabel->move((width() - m_toastLabel->width()) / 2, toastY);
    m_toastLabel->show();
    m_toastLabel->raise();
    m_toastTimer.start();
}

// ── Reader Mode Cycling ─────────────────────────────────────────────────────

void ComicReader::cycleReaderMode()
{
    // Save scroll position before switching
    int page = (m_readerMode == ReaderMode::ScrollStrip) ? computePageInView() : m_currentPage;

    switch (m_readerMode) {
    case ReaderMode::SinglePage:
        m_readerMode = ReaderMode::DoublePage;
        m_hudPinned = true;  // DoublePage = pinned HUD
        clearScrollStrip();
        m_imageLabel->show();
        buildCanonicalPairingUnits();
        m_portraitBtn->setVisible(false);
        m_modeBtn->setText("Double");
        showToast("Double Page");
        break;
    case ReaderMode::DoublePage:
        m_readerMode = ReaderMode::ScrollStrip;
        m_hudPinned = false; // ScrollStrip = auto-hide HUD
        invalidatePairing();
        m_imageLabel->hide();
        m_portraitBtn->setVisible(true);
        m_modeBtn->setText("Scroll");
        buildScrollStrip();
        showToast("Scroll Strip");
        // Delay scroll-to-page until layout settles and initial pages decode
        QTimer::singleShot(250, this, [this, page]() {
            if (m_isScrollStrip && m_stripCanvas) {
                double targetY = m_stripCanvas->pageTopY(page);
                if (auto* vbar = m_scrollArea->verticalScrollBar())
                    vbar->setValue(static_cast<int>(targetY));
                m_currentPage = page;
                updatePageLabel();
                refreshVisibleStripPages();
            }
        });
        return; // skip the showPage(page) at the bottom
    case ReaderMode::ScrollStrip:
        m_readerMode = ReaderMode::SinglePage;
        m_hudPinned = true;  // SinglePage = pinned HUD
        clearScrollStrip();
        m_imageLabel->show();
        invalidatePairing();
        m_portraitBtn->setVisible(true);
        m_modeBtn->setText("Single");
        showToast("Single Page");
        break;
    }

    showPage(page);
}

// ── Coupling Nudge ──────────────────────────────────────────────────────────

void ComicReader::toggleCouplingNudge()
{
    if (!m_isDoublePage) return;
    m_couplingPhase = (m_couplingPhase == "normal") ? "shifted" : "normal";
    m_couplingMode = "manual";
    m_couplingConfidence = 1.0f;
    invalidatePairing();
    buildCanonicalPairingUnits();
    showToast(m_couplingPhase == "normal" ? "Normal Pairing" : "Shifted Pairing");
    showPage(m_currentPage);
}

// ── Auto-Coupling ───────────────────────────────────────────────────────────

double ComicReader::edgeContinuityCost(const QPixmap& leftPix, const QPixmap& rightPix)
{
    if (leftPix.isNull() || rightPix.isNull()) return 1.0;
    QImage left = leftPix.toImage().scaled(8, 96, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    QImage right = rightPix.toImage().scaled(8, 96, Qt::IgnoreAspectRatio, Qt::FastTransformation);

    double cost = 0.0;
    for (int y = 0; y < 96; ++y) {
        QColor lc = left.pixelColor(7, y);
        QColor rc = right.pixelColor(0, y);
        double lumL = 0.299 * lc.redF() + 0.587 * lc.greenF() + 0.114 * lc.blueF();
        double lumR = 0.299 * rc.redF() + 0.587 * rc.greenF() + 0.114 * rc.blueF();
        cost += std::abs(lumL - lumR);
    }
    return cost / 96.0;
}

QVector<int> ComicReader::autoPhaseSampleIndexes(bool shifted)
{
    QVector<int> samples;
    int total = m_pageNames.size();
    int probeLimit = qMin(total - 1, COUPLING_PROBE_MAX_PAGES);
    int nudge = shifted ? 1 : 0;
    int extraSlots = 0;

    for (int idx = 1; idx < probeLimit && samples.size() < COUPLING_MAX_SAMPLES; ++idx) {
        if (resolveSpread(idx)) { extraSlots++; continue; }
        int parity = (idx + extraSlots + nudge) % 2;
        if (parity == 1 && idx + 1 < total && !resolveSpread(idx + 1))
            samples.append(idx);
    }
    return samples;
}

double ComicReader::scorePhase(bool shifted)
{
    auto samples = autoPhaseSampleIndexes(shifted);
    if (samples.isEmpty()) return 1.0;

    double totalCost = 0.0;
    int validCount = 0;
    for (int rightIdx : samples) {
        int leftIdx = rightIdx + 1;
        if (!m_cache.contains(rightIdx) || !m_cache.contains(leftIdx)) continue;
        QPixmap rPix = m_cache.get(rightIdx);
        QPixmap lPix = m_cache.get(leftIdx);
        totalCost += edgeContinuityCost(rPix, lPix);
        validCount++;
    }
    return validCount > 0 ? totalCost / validCount : 1.0;
}

void ComicReader::maybeRunAutoCoupling()
{
    if (m_couplingResolved || m_couplingMode != "auto") return;
    if (m_pageNames.size() <= 3) { m_couplingResolved = true; return; }

    double normalScore = scorePhase(false);
    double shiftedScore = scorePhase(true);

    // Check if we have enough samples
    auto normalSamples = autoPhaseSampleIndexes(false);
    auto shiftedSamples = autoPhaseSampleIndexes(true);
    bool hasNormalData = false, hasShiftedData = false;
    for (int idx : normalSamples) if (m_cache.contains(idx) && m_cache.contains(idx+1)) { hasNormalData = true; break; }
    for (int idx : shiftedSamples) if (m_cache.contains(idx) && m_cache.contains(idx+1)) { hasShiftedData = true; break; }

    if (!hasNormalData || !hasShiftedData) {
        m_couplingProbeAttempts++;
        if (m_couplingProbeAttempts >= COUPLING_MAX_PROBE_ATTEMPTS) {
            m_couplingResolved = true;
            return;
        }
        // Request probe pages
        for (bool sh : {false, true}) {
            for (int idx : autoPhaseSampleIndexes(sh)) {
                requestDecode(idx);
                requestDecode(idx + 1);
            }
        }
        return;
    }

    double base = std::max(0.001, normalScore + shiftedScore);
    double confidence = std::abs(normalScore - shiftedScore) / base;

    if (shiftedScore < normalScore && confidence >= COUPLING_MIN_CONFIDENCE) {
        m_couplingPhase = "shifted";
    } else {
        m_couplingPhase = "normal";
    }
    m_couplingConfidence = static_cast<float>(confidence);
    m_couplingResolved = true;

    if (m_isDoublePage) {
        invalidatePairing();
        buildCanonicalPairingUnits();
        showPage(m_currentPage);
    }
}

// ── Spread Override ─────────────────────────────────────────────────────────

void ComicReader::showSpreadOverrideMenu(int pageIndex, const QPoint& globalPos)
{
    if (pageIndex <= 0 || pageIndex >= m_pageMeta.size()) return;

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background: rgba(8,8,8,0.92); border: 1px solid rgba(255,255,255,0.12); border-radius: 8px; padding: 4px; }"
        "QMenu::item { color: #eee; padding: 6px 16px; font-size: 11px; }"
        "QMenu::item:selected { background: rgba(199,167,107,0.22); }"
    );

    bool currentlySpread = resolveSpread(pageIndex);
    auto* toggleAction = menu.addAction(currentlySpread ? "Mark as Normal" : "Mark as Spread");
    menu.addSeparator();
    auto* resetAction = menu.addAction("Reset All Overrides");

    auto* chosen = menu.exec(globalPos);
    if (chosen == toggleAction) {
        m_spreadOverrides[pageIndex] = !currentlySpread;
        invalidatePairing();
        if (m_isDoublePage) {
            buildCanonicalPairingUnits();
            showPage(m_currentPage);
        }
    } else if (chosen == resetAction) {
        m_spreadOverrides.clear();
        invalidatePairing();
        if (m_isDoublePage) {
            buildCanonicalPairingUnits();
            showPage(m_currentPage);
        }
    }
}

// ── Reading Direction ───────────────────────────────────────────────────────

void ComicReader::toggleReadingDirection()
{
    if (!m_isDoublePage) return;
    m_rtl = !m_rtl;
    showToast(m_rtl ? "Right to Left" : "Left to Right");
    displayCurrentPage();
}

// ── Portrait Width ──────────────────────────────────────────────────────────

void ComicReader::showPortraitWidthMenu()
{
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background: rgba(8,8,8,0.92); border: 1px solid rgba(255,255,255,0.12); border-radius: 8px; padding: 4px; }"
        "QMenu::item { color: #eee; padding: 6px 16px; font-size: 11px; }"
        "QMenu::item:selected { background: rgba(199,167,107,0.22); }"
        "QMenu::item:checked { color: #c7a76b; }"
    );
    for (int pct : PORTRAIT_PRESETS) {
        auto* action = menu.addAction(QString::number(pct) + "%");
        action->setCheckable(true);
        action->setChecked(pct == m_portraitWidthPct);
        action->setData(pct);
    }
    QPoint pos = m_portraitBtn->mapToGlobal(QPoint(0, -menu.sizeHint().height()));
    auto* chosen = menu.exec(pos);
    if (chosen) setPortraitWidthPct(chosen->data().toInt());
}

void ComicReader::setPortraitWidthPct(int pct)
{
    pct = qBound(50, pct, 100);
    if (pct == m_portraitWidthPct) return;
    m_portraitWidthPct = pct;
    m_portraitBtn->setText(QString::number(pct) + "%");
    if (m_isScrollStrip) {
        reflowScrollStrip();
        refreshVisibleStripPages();
    } else {
        displayCurrentPage();
    }
}

// ── Zoom & Pan ──────────────────────────────────────────────────────────────

void ComicReader::setZoom(int pct)
{
    m_zoomPct = qBound(100, pct, 260);
    displayCurrentPage();
}

void ComicReader::zoomBy(int delta)
{
    if (!m_isDoublePage) return;
    setZoom(m_zoomPct + delta);
    showToast(QString("Zoom: %1%").arg(m_zoomPct));
}

void ComicReader::resetZoomPan()
{
    m_zoomPct = 100;
    m_panX = 0;
    m_panY = 0;
}

void ComicReader::applyPan()
{
    auto* hbar = m_scrollArea->horizontalScrollBar();
    if (hbar) {
        int maxPanX = hbar->maximum();
        if (maxPanX <= 0) { m_panX = 0; }
        else { m_panX = qBound(0, m_panX, maxPanX); hbar->setValue(m_panX); }
    }
    // E1: Y-axis pan
    auto* vbar = m_scrollArea->verticalScrollBar();
    if (vbar) {
        int maxPanY = vbar->maximum();
        if (maxPanY <= 0) { m_panY = 0; }
        else { m_panY = qBound(0, m_panY, maxPanY); vbar->setValue(m_panY); }
    }
}

// ── Click Zones ─────────────────────────────────────────────────────────────

QString ComicReader::clickZone(const QPoint& pos) const
{
    int w = width();
    double third = w / 3.0;
    if (pos.x() < third) return "left";
    if (pos.x() >= w - third) return "right";
    return "mid";
}

void ComicReader::flashClickZone(const QString& side, bool blocked)
{
    auto* flash = new QWidget(this);
    // G2: amber tint when navigation is blocked (strip mode), white when normal
    flash->setStyleSheet(blocked ? "background: rgba(255,200,100,22);"
                                 : "background: rgba(255,255,255,38);");

    int w = width() / 3;
    if (side == "left") flash->setGeometry(0, 0, w, height());
    else if (side == "right") flash->setGeometry(width() - w, 0, w, height());
    else return;

    flash->show();
    flash->raise();

    auto* effect = new QGraphicsOpacityEffect(flash);
    flash->setGraphicsEffect(effect);
    auto* anim = new QPropertyAnimation(effect, "opacity", flash);
    anim->setDuration(250);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    connect(anim, &QPropertyAnimation::finished, flash, &QWidget::deleteLater);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

// ── Go-to-Page Dialog ───────────────────────────────────────────────────────

void ComicReader::showGoToDialog()
{
    closeAllOverlays();
    if (!m_gotoOverlay) {
        m_gotoOverlay = new QWidget(this);
        m_gotoOverlay->setStyleSheet("background: rgba(0,0,0,0.70); border-radius: 12px;");
        m_gotoOverlay->setFixedSize(260, 80);
        auto* layout = new QVBoxLayout(m_gotoOverlay);
        layout->setContentsMargins(16, 12, 16, 12);
        layout->setSpacing(8);
        auto* label = new QLabel(m_gotoOverlay);
        label->setObjectName("gotoLabel");
        label->setStyleSheet("color: rgba(255,255,255,0.78); font-size: 12px; background: transparent;");
        layout->addWidget(label);
        m_gotoInput = new QLineEdit(m_gotoOverlay);
        m_gotoInput->setStyleSheet(
            "background: rgba(255,255,255,0.10); color: white; "
            "border: 1px solid rgba(255,255,255,0.20); border-radius: 6px; padding: 4px 8px; font-size: 13px;");
        layout->addWidget(m_gotoInput);
        connect(m_gotoInput, &QLineEdit::returnPressed, this, [this]() {
            bool ok; int page = m_gotoInput->text().toInt(&ok);
            if (ok && page >= 1 && page <= m_pageNames.size()) showPage(page - 1);
            hideGoToDialog();
        });
    }
    if (auto* label = m_gotoOverlay->findChild<QLabel*>("gotoLabel"))
        label->setText(QString("Go to page (1-%1):").arg(m_pageNames.size()));
    m_gotoInput->clear();
    m_gotoOverlay->move((width() - m_gotoOverlay->width()) / 2, (height() - m_gotoOverlay->height()) / 2);
    // H1: scrim behind dialog
    if (!m_gotoScrim) {
        auto* scrim = new ClickScrim(this);
        scrim->setStyleSheet("background: rgba(0,0,0,90);");
        connect(scrim, &ClickScrim::clicked, this, &ComicReader::hideGoToDialog);
        m_gotoScrim = scrim;
    }
    m_gotoScrim->setGeometry(0, 0, width(), height());
    m_gotoScrim->show();
    m_gotoOverlay->show(); m_gotoOverlay->raise(); m_gotoInput->setFocus();
}

void ComicReader::hideGoToDialog()
{
    if (m_gotoScrim)   m_gotoScrim->hide();
    if (m_gotoOverlay) m_gotoOverlay->hide();
    setFocus();
}

// ── Volume Navigation ───────────────────────────────────────────────────────

void ComicReader::openVolumeByIndex(int volumeIndex)
{
    if (volumeIndex < 0 || volumeIndex >= m_seriesCbzList.size()) return;
    saveCurrentProgress();
    openBook(m_seriesCbzList[volumeIndex], m_seriesCbzList, m_seriesName);
}

void ComicReader::prevVolume()
{
    int idx = m_seriesCbzList.indexOf(m_cbzPath);
    if (idx > 0) openVolumeByIndex(idx - 1);
}

void ComicReader::nextVolume()
{
    int idx = m_seriesCbzList.indexOf(m_cbzPath);
    if (idx >= 0 && idx < m_seriesCbzList.size() - 1) openVolumeByIndex(idx + 1);
}

// ── Scroll Strip Mode ────────────────────────────────────────────────────────

void ComicReader::buildScrollStrip()
{
    clearScrollStrip();

    // H2: arrows don't apply in strip mode
    if (m_leftArrow)  m_leftArrow->hide();
    if (m_rightArrow) m_rightArrow->hide();

    m_stripCanvas = new ScrollStripCanvas();
    m_stripCanvas->setPageCount(m_pageNames.size());
    m_stripCanvas->setViewportWidth(m_scrollArea->viewport()->width());
    m_stripCanvas->setPortraitWidthPct(m_portraitWidthPct);
    m_stripCanvas->setScalingQuality(m_scalingQuality);

    // Feed any already-decoded pages
    for (int i = 0; i < m_pageNames.size(); ++i) {
        if (i < m_pageMeta.size() && m_pageMeta[i].decoded && m_cache.contains(i)) {
            m_stripCanvas->onPageDecoded(i, m_cache.get(i),
                                          m_pageMeta[i].width, m_pageMeta[i].height);
        }
    }

    m_scrollArea->setWidget(m_stripCanvas);
    m_scrollArea->setWidgetResizable(true);

    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &ComicReader::onStripScrollChanged);

    // H4: vertical scroll thumb
    m_verticalThumb = new VerticalThumb(this);
    m_verticalThumb->setGeometry(width() - 14, 0, 14, height());
    m_verticalThumb->show();
    m_verticalThumb->raise();
    connect(m_verticalThumb, &VerticalThumb::progressRequested, this, [this](double f) {
        auto* vbar = m_scrollArea->verticalScrollBar();
        if (vbar && vbar->maximum() > 0) {
            int val = static_cast<int>(f * vbar->maximum());
            m_scrollArea->syncExternalScroll(val);
            vbar->setValue(val);
        }
    });

    // Trigger initial prefetch after layout settles
    // Initial prefetch — delay to let layout settle, then refresh twice for reliability
    QTimer::singleShot(100, this, &ComicReader::refreshVisibleStripPages);
    QTimer::singleShot(500, this, &ComicReader::refreshVisibleStripPages);
}

void ComicReader::clearScrollStrip()
{
    if (m_verticalThumb) {
        m_verticalThumb->deleteLater();
        m_verticalThumb = nullptr;
    }
    if (m_stripCanvas) {
        if (m_scrollArea->verticalScrollBar())
            disconnect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
                      this, &ComicReader::onStripScrollChanged);

        m_scrollArea->setWidget(nullptr);
        m_stripCanvas->deleteLater();
        m_stripCanvas = nullptr;

        // Restore single-image label
        m_imageLabel = new QLabel();
        m_imageLabel->setAlignment(Qt::AlignCenter);
        m_imageLabel->setStyleSheet("background: transparent;");
        m_scrollArea->setWidget(m_imageLabel);
        m_scrollArea->setWidgetResizable(false);
    }
}

void ComicReader::reflowScrollStrip()
{
    if (!m_isScrollStrip || !m_stripCanvas) return;

    int viewW = m_scrollArea->viewport()->width();
    if (viewW <= 0) return;

    m_stripCanvas->setViewportWidth(viewW);
    m_stripCanvas->setPortraitWidthPct(m_portraitWidthPct);
    m_stripCanvas->invalidateScaledCache();

    // Re-feed all decoded pages so scaled cache gets rebuilt
    for (int i = 0; i < m_pageNames.size(); ++i) {
        if (i < m_pageMeta.size() && m_pageMeta[i].decoded && m_cache.contains(i)) {
            m_stripCanvas->onPageDecoded(i, m_cache.get(i),
                                          m_pageMeta[i].width, m_pageMeta[i].height);
        }
    }
}

int ComicReader::computePageInView() const
{
    if (!m_stripCanvas) return m_currentPage;
    return m_stripCanvas->pageAtCenter(m_scrollArea->viewport()->height());
}

void ComicReader::refreshVisibleStripPages()
{
    if (!m_isScrollStrip || !m_stripCanvas) return;

    int viewH = qMax(1, m_scrollArea->viewport()->height());
    auto needed = m_stripCanvas->pagesNeedingDecode(viewH, 1.2);

    // Pin needed pages in cache, request decode for missing ones
    for (int idx : needed) {
        m_cache.pin(idx);
        if (m_cache.contains(idx)) {
            // Feed cached pages that the canvas doesn't have scaled yet
            if (!m_stripCanvas->hasScaled(idx) && idx < m_pageMeta.size() && m_pageMeta[idx].decoded) {
                m_stripCanvas->onPageDecoded(idx, m_cache.get(idx),
                                              m_pageMeta[idx].width, m_pageMeta[idx].height);
            }
        } else {
            requestDecode(idx);
        }
    }

    // Evict scaled entries far outside the viewport to cap memory
    m_stripCanvas->evictScaledOutsideZone(viewH, 2.0);
}

void ComicReader::onStripScrollChanged()
{
    if (!m_isScrollStrip || !m_stripCanvas) return;

    // Update scroll offset on the canvas
    auto* vbar = m_scrollArea->verticalScrollBar();
    if (vbar)
        m_stripCanvas->setScrollOffset(vbar->value());

    int page = computePageInView();
    if (page != m_currentPage) {
        m_currentPage = page;
        updatePageLabel();
    }

    // Coalesced refresh via 16ms timer
    m_stripRefreshTimer.start();
}

// ── Toolbar ─────────────────────────────────────────────────────────────────

void ComicReader::showToolbar()
{
    m_toolbar->show(); m_toolbar->raise();
    if (m_toastLabel) m_toastLabel->raise();
    // Restore cursor when toolbar appears
    setCursor(Qt::ArrowCursor);
    m_hudAutoHideTimer.start();
}

void ComicReader::hideToolbar()
{
    m_toolbar->hide();
    m_hudAutoHideTimer.stop();
}

void ComicReader::toggleToolbar()
{
    if (m_toolbar->isVisible()) {
        m_hudExplicitlyHidden = true;
        hideToolbar();
    } else {
        m_hudExplicitlyHidden = false;
        showToolbar();
    }
}

void ComicReader::showEndOverlay()
{
    closeAllOverlays();
    if (!m_endOverlay) return;

    // Determine if next volume exists
    int currentIdx = m_seriesCbzList.indexOf(m_cbzPath);
    bool hasNext = (currentIdx >= 0 && currentIdx + 1 < m_seriesCbzList.size());

    m_endNextBtn->setVisible(hasNext);

    if (hasNext) {
        QFileInfo fi(m_seriesCbzList[currentIdx + 1]);
        m_endSubtitle->setText("Next: " + fi.completeBaseName());
    } else {
        m_endSubtitle->setText("No more volumes");
    }

    m_endOverlay->setGeometry(0, 0, width(), height());
    m_endOverlay->show();
    m_endOverlay->raise();
    setCursor(Qt::ArrowCursor);
}

void ComicReader::hideEndOverlay()
{
    if (m_endOverlay) m_endOverlay->hide();
}

void ComicReader::showVolumeNavigator()
{
    closeAllOverlays();
    if (!m_volOverlay || m_seriesCbzList.size() <= 1) return;

    // D3: Title with count
    QString title = m_seriesName.isEmpty() ? "Volumes" : m_seriesName;
    m_volTitle->setText(title + QString::fromUtf8(" \xc2\xb7 ") + QString::number(m_seriesCbzList.size()) + " volumes");
    m_volList->clear();
    m_volSearch->clear();

    int currentIdx = m_seriesCbzList.indexOf(m_cbzPath);
    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    for (int i = 0; i < m_seriesCbzList.size(); ++i) {
        QFileInfo fi(m_seriesCbzList[i]);
        QString baseName = fi.completeBaseName();
        QString display = baseName;

        // D1: Progress metadata
        if (m_bridge) {
            QString key = itemIdForPath(m_seriesCbzList[i]);
            QJsonObject prog = m_bridge->progress("comics", key);
            int pg = prog["page"].toInt(-1);
            int pgCount = prog["pageCount"].toInt(0);
            if (pg > 0 && pgCount > 0) {
                display += QString("  \xe2\x80\x94  Continue \xc2\xb7 page %1 / %2").arg(pg + 1).arg(pgCount);
            }
            qint64 updatedAt = static_cast<qint64>(prog["updatedAt"].toDouble(0));
            if (updatedAt > 0) {
                qint64 diffMs = nowMs - updatedAt;
                qint64 diffMin = diffMs / 60000;
                qint64 diffHrs = diffMin / 60;
                qint64 diffDays = diffHrs / 24;
                QString ago;
                if (diffMin < 1)       ago = "just now";
                else if (diffMin < 60) ago = QString::number(diffMin) + " min ago";
                else if (diffHrs < 24) ago = QString::number(diffHrs) + "h ago";
                else                   ago = QString::number(diffDays) + "d ago";
                display += "  " + ago;
            }
        }

        // D2: Current volume marker
        if (i == currentIdx)
            display += QString::fromUtf8(" \xe2\x97\x80");

        auto* item = new QListWidgetItem(display);
        item->setData(Qt::UserRole, i);
        item->setData(Qt::UserRole + 1, baseName); // store original name for search
        if (i == currentIdx) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
        m_volList->addItem(item);
        if (i == currentIdx)
            m_volList->setCurrentItem(item);
    }

    m_volOverlay->setGeometry(0, 0, width(), height());
    m_volOverlay->show();
    m_volOverlay->raise();
    m_volSearch->setFocus();
    setCursor(Qt::ArrowCursor);
}

void ComicReader::hideVolumeNavigator()
{
    if (m_volOverlay) m_volOverlay->hide();
    setFocus();
}

// ── Events ──────────────────────────────────────────────────────────────────

void ComicReader::keyPressEvent(QKeyEvent* event)
{
    // D10 — GoToPage gate (input layer owns all keys while open)
    if (m_gotoOverlay && m_gotoOverlay->isVisible()) {
        if (event->key() == Qt::Key_Escape) hideGoToDialog();
        event->accept(); return;
    }

    // D1 — Keys overlay gate
    if (m_keysOverlay && m_keysOverlay->isVisible()) {
        if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_K)
            toggleKeysOverlay();
        event->accept(); return;
    }

    // Volume navigator keyboard routing
    if (m_volOverlay && m_volOverlay->isVisible()) {
        switch (event->key()) {
        case Qt::Key_Escape:
            hideVolumeNavigator();
            break;
        case Qt::Key_Return: case Qt::Key_Enter: {
            auto* item = m_volList->currentItem();
            if (item && !item->isHidden()) {
                int idx = item->data(Qt::UserRole).toInt();
                hideVolumeNavigator();
                openVolumeByIndex(idx);
            }
            break;
        }
        default:
            // Let QListWidget and QLineEdit handle Up/Down/typing
            QWidget::keyPressEvent(event);
            return;
        }
        event->accept();
        return;
    }

    // End-of-volume overlay keyboard routing
    if (m_endOverlay && m_endOverlay->isVisible()) {
        switch (event->key()) {
        case Qt::Key_Space: case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Right: case Qt::Key_PageDown:
            hideEndOverlay();
            if (m_endNextBtn->isVisible()) nextVolume();
            else showPage(0); // replay
            break;
        case Qt::Key_Escape: case Qt::Key_Backspace:
            saveCurrentProgress();
            emit closeRequested();
            break;
        default: break; // block all other keys
        }
        event->accept();
        return;
    }

    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_G) {
        showGoToDialog(); return;
    }

    // C3 — Ctrl+0: reset series settings
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_0) {
        resetSeriesSettings(); return;
    }

    // D6 — Alt+Left/Right: previous/next volume
    if (event->modifiers() & Qt::AltModifier) {
        if (event->key() == Qt::Key_Left  && !isAnyOverlayOpen()) { prevVolume(); event->accept(); return; }
        if (event->key() == Qt::Key_Right && !isAnyOverlayOpen()) { nextVolume(); event->accept(); return; }
    }

    // I1 + E1: arrow keys pan instead of navigate when zoomed in double-page mode
    if (m_isDoublePage && m_zoomPct > 100 && !isAnyOverlayOpen()) {
        if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
            int step = width() / 6;
            m_panX += (event->key() == Qt::Key_Right) ? step : -step;
            applyPan();
            event->accept();
            return;
        }
        // E1: Y-axis pan
        if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up) {
            int step = height() / 6;
            m_panY += (event->key() == Qt::Key_Down) ? step : -step;
            applyPan();
            event->accept();
            return;
        }
    }

    switch (event->key()) {
    case Qt::Key_Right: case Qt::Key_Space: case Qt::Key_PageDown: nextPage(); break;
    case Qt::Key_Left: case Qt::Key_PageUp: prevPage(); break;
    case Qt::Key_Home: showPage(0); break;
    case Qt::Key_End: showPage(m_pageNames.size() - 1); break;
    case Qt::Key_Escape:
        if (m_toolbar->isVisible()) hideToolbar();
        else { saveCurrentProgress(); emit closeRequested(); }
        break;
    case Qt::Key_H: toggleToolbar(); break;
    case Qt::Key_K: toggleKeysOverlay(); break;                    // D1
    case Qt::Key_B: toggleBookmark(); break;                       // D2
    case Qt::Key_Z: instantReplay(); break;                        // D3
    case Qt::Key_R: clearResume(); break;                          // D4
    case Qt::Key_S: saveCheckpoint(); break;                       // D5
    case Qt::Key_F11: {
        QWidget* win = window();
        emit fullscreenRequested(!win->isFullScreen());
        break;
    }
    case Qt::Key_F: cycleFitMode(); break;
    case Qt::Key_M: cycleReaderMode(); break;
    case Qt::Key_O: showVolumeNavigator(); break;
    case Qt::Key_P: toggleCouplingNudge(); break;
    case Qt::Key_I: toggleReadingDirection(); break;
    case Qt::Key_Up:
        if (m_isDoublePage) {
            auto* vbar = m_scrollArea->verticalScrollBar();
            if (vbar && vbar->maximum() > 0) { vbar->setValue(vbar->value() - 80); }
            else prevPage();
        }
        break;
    case Qt::Key_Down:
        if (m_isDoublePage) {
            auto* vbar = m_scrollArea->verticalScrollBar();
            if (vbar && vbar->maximum() > 0) { vbar->setValue(vbar->value() + 80); }
            else nextPage();
        }
        break;
    default: QWidget::keyPressEvent(event);
    }

    // A4: Reset auto-hide timer on every keypress when HUD is visible and not pinned
    if (m_toolbar->isVisible() && !m_hudPinned)
        m_hudAutoHideTimer.start();
}

void ComicReader::wheelEvent(QWheelEvent* event)
{
    // Scroll strip mode: let the scroll area handle everything
    if (m_isScrollStrip) {
        m_scrollArea->handleWheel(event);
        return;
    }

    // Ctrl+wheel: zoom in double-page mode
    if (m_isDoublePage && event->modifiers() & Qt::ControlModifier) {
        zoomBy(event->angleDelta().y() > 0 ? 20 : -20);
        event->accept();
        return;
    }

    // Double-page or FitWidth: scroll within page, navigate at boundaries
    if (m_isDoublePage || m_fitMode == FitMode::FitWidth) {
        auto* vbar = m_scrollArea->verticalScrollBar();
        if (vbar && vbar->maximum() > 0) {
            int val = vbar->value();
            if (event->angleDelta().y() < 0 && val >= vbar->maximum()) nextPage();
            else if (event->angleDelta().y() > 0 && val <= vbar->minimum()) prevPage();
            else m_scrollArea->handleWheel(event);
            return;
        }
    }

    // No scrollbar (content fits) or single-page FitPage/FitHeight: wheel = page nav
    if (event->angleDelta().y() < 0) nextPage();
    else if (event->angleDelta().y() > 0) prevPage();
}

void ComicReader::mousePressEvent(QMouseEvent* event)
{
    // G1: pass clicks through to overlay widgets when any overlay is open
    if (isAnyOverlayOpen()) { QWidget::mousePressEvent(event); return; }

    if (event->button() == Qt::LeftButton) {
        QString zone = clickZone(event->pos());

        // Left/right zones: INSTANT navigation (no debounce)
        if (zone == "left") {
            if (m_isScrollStrip) {
                flashClickZone("left", true);  // G2: blocked feedback for strip
            } else {
                flashClickZone("left");
                m_rtl ? nextPage() : prevPage();
            }
            event->accept();
            return;
        }
        if (zone == "right") {
            if (m_isScrollStrip) {
                flashClickZone("right", true);  // G2: blocked feedback for strip
            } else {
                flashClickZone("right");
                m_rtl ? prevPage() : nextPage();
            }
            event->accept();
            return;
        }

        // Center zone: 220ms debounce (wait for possible double-click)
        // I1: also start pan drag tracking when zoomed in double-page mode
        if (zone == "mid") {
            if (m_isDoublePage && m_zoomPct > 100) {
                m_panDragging = true;
                m_panDragStartX = event->pos().x();
                m_panDragStartPanX = m_panX;
            }
            m_clickTimer.start();
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void ComicReader::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_panDragging) {
        m_panDragging = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ComicReader::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        QString zone = clickZone(event->pos());
        // Cancel pending single click
        m_clickTimer.stop();

        // Double-click center: toggle fullscreen
        if (zone == "mid") {
            QWidget* win = window();
            if (win->isFullScreen())
                emit fullscreenRequested(false);
            else
                emit fullscreenRequested(true);
            event->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ComicReader::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu* menu = ContextMenuHelper::createMenu(this);

    // C1: Double-page specific items (spread toggle, gutter shadow)
    if (m_isDoublePage) {
        bool isSpread = resolveSpread(m_currentPage);
        auto* spreadAct = menu->addAction(isSpread ? "Mark as Single Page" : "Mark as Spread");
        connect(spreadAct, &QAction::triggered, this, [this]() {
            int p = m_currentPage;
            bool cur = resolveSpread(p);
            m_spreadOverrides[p] = !cur;
            invalidatePairing();
            buildCanonicalPairingUnits();
            displayCurrentPage();
        });

        QMenu* shadowMenu = menu->addMenu("Gutter Shadow");
        struct { const char* label; double val; } presets[] = {
            {"Off",    0.00},
            {"Subtle", 0.22},
            {"Medium", 0.35},
            {"Strong", 0.55},
        };
        for (auto& pr : presets) {
            QString label = pr.label;
            if (qAbs(m_gutterShadow - pr.val) < 0.01) label += " *";
            auto* act = shadowMenu->addAction(label);
            double v = pr.val;
            connect(act, &QAction::triggered, this, [this, v]() {
                m_gutterShadow = v;
                displayCurrentPage();
                saveSeriesSettings();
            });
        }
        menu->addSeparator();
    }

    // C1: Go to Page — all modes
    auto* gotoAct = menu->addAction("Go to Page...");
    connect(gotoAct, &QAction::triggered, this, &ComicReader::showGoToDialog);

    // C2: Bookmarks jump list — if non-empty, up to last 6
    if (!m_bookmarks.isEmpty()) {
        QMenu* bmMenu = menu->addMenu("Bookmarks");
        QList<int> bmList(m_bookmarks.begin(), m_bookmarks.end());
        std::sort(bmList.begin(), bmList.end());
        int count = qMin(bmList.size(), 6);
        for (int i = bmList.size() - count; i < bmList.size(); ++i) {
            int pg = bmList[i];
            auto* bmAct = bmMenu->addAction(QString("Page %1").arg(pg + 1));
            connect(bmAct, &QAction::triggered, this, [this, pg]() { showPage(pg); });
        }
    }

    menu->addSeparator();

    // C4: Image Quality submenu — all modes
    QMenu* qualMenu = menu->addMenu("Image Quality");
    auto* smoothAct = qualMenu->addAction("Smooth");
    smoothAct->setCheckable(true);
    smoothAct->setChecked(m_scalingQuality == Qt::SmoothTransformation);
    connect(smoothAct, &QAction::triggered, this, [this]() {
        m_scalingQuality = Qt::SmoothTransformation;
        if (m_stripCanvas) { m_stripCanvas->setScalingQuality(m_scalingQuality); m_stripCanvas->invalidateScaledCache(); }
        displayCurrentPage();
        saveSeriesSettings();
    });
    auto* fastAct = qualMenu->addAction("Fast");
    fastAct->setCheckable(true);
    fastAct->setChecked(m_scalingQuality == Qt::FastTransformation);
    connect(fastAct, &QAction::triggered, this, [this]() {
        m_scalingQuality = Qt::FastTransformation;
        if (m_stripCanvas) { m_stripCanvas->setScalingQuality(m_scalingQuality); m_stripCanvas->invalidateScaledCache(); }
        displayCurrentPage();
        saveSeriesSettings();
    });

    menu->addSeparator();

    // C1: Copy/Reveal — all modes
    auto* copyAct = menu->addAction("Copy Volume Path");
    connect(copyAct, &QAction::triggered, this, [this]() {
        ContextMenuHelper::copyToClipboard(m_cbzPath);
    });
    auto* revealAct = menu->addAction("Reveal in File Explorer");
    connect(revealAct, &QAction::triggered, this, [this]() {
        ContextMenuHelper::revealInExplorer(m_cbzPath);
    });

    // J2: memory saver — all modes
    menu->addSeparator();
    auto* memAct = menu->addAction(m_memorySaver ? "Memory Saver: On *" : "Memory Saver: Off");
    connect(memAct, &QAction::triggered, this, &ComicReader::toggleMemorySaver);

    menu->exec(event->globalPos());
    menu->deleteLater();
}

void ComicReader::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_scrollArea->setGeometry(0, 0, width(), height());
    m_toolbar->setGeometry(0, height() - m_toolbar->height(), width(), m_toolbar->height());
    if (m_endOverlay && m_endOverlay->isVisible())
        m_endOverlay->setGeometry(0, 0, width(), height());
    if (m_volOverlay && m_volOverlay->isVisible())
        m_volOverlay->setGeometry(0, 0, width(), height());
    if (m_gotoScrim && m_gotoScrim->isVisible())
        m_gotoScrim->setGeometry(0, 0, width(), height());
    if (m_leftArrow)  m_leftArrow->setGeometry(0, 0, 60, height());
    if (m_rightArrow) m_rightArrow->setGeometry(width() - 60, 0, 60, height());
    if (m_verticalThumb) m_verticalThumb->setGeometry(width() - 14, 0, 14, height());
    if (m_isScrollStrip) {
        // F2: preserve scroll position across resize — capture fraction, reflow, restore
        auto* vbar = m_scrollArea->verticalScrollBar();
        double fraction = (vbar && vbar->maximum() > 0) ? double(vbar->value()) / vbar->maximum() : 0.0;
        reflowScrollStrip();
        refreshVisibleStripPages();
        if (vbar && vbar->maximum() > 0)
            m_scrollArea->syncExternalScroll(int(fraction * vbar->maximum()));
    } else {
        displayCurrentPage();
    }
}

void ComicReader::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);

    // Show cursor on any movement
    setCursor(Qt::ArrowCursor);

    // Only start cursor hide timer when toolbar is NOT visible
    // (cursor should stay visible while interacting with HUD)
    if (!m_toolbar->isVisible())
        m_cursorTimer.start();
    else {
        m_cursorTimer.stop();
        m_hudAutoHideTimer.start();
    }

    // Edge proximity: show HUD when mouse is within 60px of bottom edge
    // Only if HUD is hidden, not explicitly toggled off, and cooldown expired
    int bottomDist = height() - event->pos().y();
    if (bottomDist <= 60 && !m_toolbar->isVisible() && !m_edgeCooldown) {
        m_edgeCooldown = true;
        m_hudExplicitlyHidden = false;  // edge hover overrides explicit hide
        showToolbar();
        QTimer::singleShot(600, this, [this]() { m_edgeCooldown = false; });
    }

    // I1: horizontal pan drag in double-page zoomed mode
    if (m_panDragging) {
        int delta = m_panDragStartX - event->pos().x(); // drag left = reveal right content
        if (qAbs(delta) > 4) m_clickTimer.stop();
        m_panX = m_panDragStartPanX + delta;
        applyPan();
    }

    // H2: side nav arrows — only in non-strip modes
    if (m_leftArrow && m_rightArrow && !m_isScrollStrip) {
        QString zone = clickZone(event->pos());
        m_leftArrow->setHovered(zone == "left");
        m_rightArrow->setHovered(zone == "right");
        m_leftArrow->show();  m_leftArrow->raise();
        m_rightArrow->show(); m_rightArrow->raise();
    }
}

// ── Batch D — Session features ───────────────────────────────────────────────

bool ComicReader::isAnyOverlayOpen() const
{
    if (m_endOverlay    && m_endOverlay->isVisible())    return true;
    if (m_volOverlay    && m_volOverlay->isVisible())    return true;
    if (m_gotoOverlay   && m_gotoOverlay->isVisible())   return true;
    if (m_keysOverlay   && m_keysOverlay->isVisible())   return true;
    if (m_verticalThumb && m_verticalThumb->isDragging()) return true;
    return false;
}

// F2: Close all overlays — ensures only one is ever visible at a time
void ComicReader::closeAllOverlays()
{
    if (m_volOverlay  && m_volOverlay->isVisible())  m_volOverlay->hide();
    if (m_gotoOverlay && m_gotoOverlay->isVisible()) m_gotoOverlay->hide();
    if (m_gotoScrim   && m_gotoScrim->isVisible())   m_gotoScrim->hide();
    if (m_keysOverlay && m_keysOverlay->isVisible())  m_keysOverlay->hide();
    if (m_endOverlay  && m_endOverlay->isVisible())   m_endOverlay->hide();
}

void ComicReader::toggleKeysOverlay()
{
    if (m_keysOverlay && m_keysOverlay->isVisible()) {
        m_keysOverlay->hide();
        setFocus();
        return;
    }
    closeAllOverlays();

    // Lazy create
    if (!m_keysOverlay) {
        m_keysOverlay = new QWidget(this);
        m_keysOverlay->setStyleSheet(
            "background: rgba(0,0,0,210); border: 1px solid rgba(255,255,255,0.12);"
            "border-radius: 10px;"
        );

        auto* outerLayout = new QVBoxLayout(m_keysOverlay);
        outerLayout->setContentsMargins(28, 22, 28, 22);
        outerLayout->setSpacing(14);

        auto* title = new QLabel("Keyboard Shortcuts", m_keysOverlay);
        title->setStyleSheet("color: white; font-size: 15px; font-weight: 700; background: transparent; border: none;");
        outerLayout->addWidget(title);

        auto makeRow = [&](const QString& key, const QString& desc) -> QWidget* {
            auto* row = new QWidget(m_keysOverlay);
            row->setStyleSheet("background: transparent; border: none;");
            auto* hl = new QHBoxLayout(row);
            hl->setContentsMargins(0,0,0,0);
            hl->setSpacing(12);
            auto* keyLbl = new QLabel(key, row);
            keyLbl->setFixedWidth(120);
            keyLbl->setStyleSheet(
                "color: rgba(255,255,255,0.60); font-size: 12px;"
                "font-family: monospace; background: transparent; border: none;"
            );
            auto* descLbl = new QLabel(desc, row);
            descLbl->setStyleSheet("color: white; font-size: 12px; background: transparent; border: none;");
            hl->addWidget(keyLbl);
            hl->addWidget(descLbl, 1);
            return row;
        };

        // Two-column grid
        auto* cols = new QWidget(m_keysOverlay);
        cols->setStyleSheet("background: transparent; border: none;");
        auto* colLayout = new QHBoxLayout(cols);
        colLayout->setContentsMargins(0,0,0,0);
        colLayout->setSpacing(32);

        auto* left = new QWidget(cols);
        left->setStyleSheet("background: transparent; border: none;");
        auto* leftL = new QVBoxLayout(left);
        leftL->setContentsMargins(0,0,0,0); leftL->setSpacing(6);

        auto* right = new QWidget(cols);
        right->setStyleSheet("background: transparent; border: none;");
        auto* rightL = new QVBoxLayout(right);
        rightL->setContentsMargins(0,0,0,0); rightL->setSpacing(6);

        leftL->addWidget(makeRow("Right / Space / PgDn", "Next page"));
        leftL->addWidget(makeRow("Left / PgUp",          "Previous page"));
        leftL->addWidget(makeRow("Home / End",            "First / Last page"));
        leftL->addWidget(makeRow("F",                     "Cycle fit mode"));
        leftL->addWidget(makeRow("M",                     "Cycle reader mode"));
        leftL->addWidget(makeRow("H",                     "Toggle HUD"));
        leftL->addWidget(makeRow("P",                     "Coupling nudge"));
        leftL->addWidget(makeRow("I",                     "Toggle RTL/LTR"));
        leftL->addWidget(makeRow("Ctrl+G",                "Go to page"));

        rightL->addWidget(makeRow("K",                    "This shortcuts overlay"));
        rightL->addWidget(makeRow("B",                    "Toggle bookmark"));
        rightL->addWidget(makeRow("Z",                    "Instant replay"));
        rightL->addWidget(makeRow("R",                    "Clear resume"));
        rightL->addWidget(makeRow("S",                    "Save checkpoint"));
        rightL->addWidget(makeRow("O",                    "Volume navigator"));
        rightL->addWidget(makeRow("Alt+Left/Right",       "Previous/Next volume"));
        rightL->addWidget(makeRow("F11",                  "Toggle fullscreen"));
        rightL->addWidget(makeRow("Ctrl+0",               "Reset series settings"));
        rightL->addWidget(makeRow("Esc",                  "Hide HUD / Close"));

        colLayout->addWidget(left);
        colLayout->addWidget(right);
        outerLayout->addWidget(cols);

        m_keysOverlay->adjustSize();
    }

    int ox = (width() - m_keysOverlay->width()) / 2;
    int oy = (height() - m_keysOverlay->height()) / 2;
    m_keysOverlay->move(ox, oy);
    m_keysOverlay->show();
    m_keysOverlay->raise();
}

void ComicReader::toggleBookmark()
{
    if (m_pageNames.isEmpty()) return;
    if (m_bookmarks.contains(m_currentPage)) {
        m_bookmarks.remove(m_currentPage);
        showToast("Bookmark removed");
    } else {
        m_bookmarks.insert(m_currentPage);
        showToast("Bookmarked");
    }
    saveCurrentProgress();
}

void ComicReader::instantReplay()
{
    if (m_pageNames.isEmpty()) return;
    if (m_isScrollStrip) {
        if (auto* vbar = m_scrollArea->verticalScrollBar())
            vbar->setValue(qMax(0, vbar->value() - int(m_scrollArea->viewport()->height() * 0.30)));
    } else {
        prevPage();
    }
    showToast("Replay");
}

void ComicReader::clearResume()
{
    if (!m_bridge || m_pageNames.isEmpty()) return;
    m_bookmarks.clear();
    QJsonObject data;
    data["page"]      = 0;
    data["pageCount"] = m_pageNames.size();
    data["path"]      = m_cbzPath;
    m_bridge->saveProgress("comics", itemIdForPath(m_cbzPath), data);
    showToast("Resume cleared");
}

void ComicReader::saveCheckpoint()
{
    saveCurrentProgress();
    showToast("Checkpoint saved");
}

QString ComicReader::seriesSettingsKey() const
{
    QString seed = m_seriesName.isEmpty() ? m_cbzPath : m_seriesName;
    return "series/" + QString(QCryptographicHash::hash(seed.toUtf8(),
                                QCryptographicHash::Sha1).toHex().left(20));
}

void ComicReader::toggleMemorySaver()
{
    m_memorySaver = !m_memorySaver;
    m_cache.setBudget(m_memorySaver ? 256LL * 1024 * 1024 : 512LL * 1024 * 1024);
    QSettings s("Tankoban", "Tankoban");
    s.setValue("memorySaver", m_memorySaver);
    showToast(m_memorySaver ? "Memory Saver On" : "Memory Saver Off");
}

void ComicReader::resetSeriesSettings()
{
    QSettings s("Tankoban", "Tankoban");
    QString k = seriesSettingsKey();
    s.remove(k);
    // Reset to defaults
    m_portraitWidthPct = 78;
    m_readerMode = ReaderMode::SinglePage;
    m_hudPinned = true;
    m_couplingPhase = "normal";
    m_gutterShadow = 0.35;
    m_scalingQuality = Qt::SmoothTransformation;
    m_zoomPct = 100;
    m_panX = 0;
    // Rebuild UI state
    clearScrollStrip();
    m_imageLabel->show();
    invalidatePairing();
    m_portraitBtn->setVisible(true);
    m_modeBtn->setText("Single");
    displayCurrentPage();
    showToast("Series settings reset");
}

void ComicReader::saveSeriesSettings()
{
    QSettings s("Tankoban", "Tankoban");
    QString k = seriesSettingsKey();
    s.setValue(k + "/portraitWidthPct", m_portraitWidthPct);
    s.setValue(k + "/readerMode",       static_cast<int>(m_readerMode));
    s.setValue(k + "/couplingPhase",    m_couplingPhase);
    s.setValue(k + "/gutterShadow",     m_gutterShadow);
    s.setValue(k + "/scalingQuality",   m_scalingQuality == Qt::SmoothTransformation ? 0 : 1);

    // F1: Write last-saved as migration seed for new series
    QVariantMap last;
    last["portraitWidthPct"] = m_portraitWidthPct;
    last["readerMode"]       = static_cast<int>(m_readerMode);
    last["couplingPhase"]    = m_couplingPhase;
    last["gutterShadow"]     = m_gutterShadow;
    last["scalingQuality"]   = m_scalingQuality == Qt::SmoothTransformation ? 0 : 1;
    s.setValue("last_saved_series_settings", last);
}

void ComicReader::applySeriesSettings()
{
    QSettings s("Tankoban", "Tankoban");
    QString k = seriesSettingsKey();

    // F1: If no per-series settings, seed from last-saved migration
    if (!s.contains(k + "/portraitWidthPct")) {
        QVariant lastVar = s.value("last_saved_series_settings");
        if (lastVar.isValid()) {
            QVariantMap last = lastVar.toMap();
            if (last.contains("portraitWidthPct"))
                setPortraitWidthPct(last["portraitWidthPct"].toInt());
            if (last.contains("readerMode")) {
                int mode = last["readerMode"].toInt();
                if (mode >= 0 && mode <= 2) m_readerMode = static_cast<ReaderMode>(mode);
            }
            if (last.contains("couplingPhase"))
                m_couplingPhase = last["couplingPhase"].toString();
            if (last.contains("gutterShadow"))
                m_gutterShadow = last["gutterShadow"].toDouble();
            if (last.contains("scalingQuality"))
                m_scalingQuality = last["scalingQuality"].toInt() == 1 ? Qt::FastTransformation : Qt::SmoothTransformation;
        }
        return;
    }

    int pct = s.value(k + "/portraitWidthPct", m_portraitWidthPct).toInt();
    setPortraitWidthPct(pct);

    int mode = s.value(k + "/readerMode", static_cast<int>(m_readerMode)).toInt();
    if (mode >= 0 && mode <= 2)
        m_readerMode = static_cast<ReaderMode>(mode);

    QString phase = s.value(k + "/couplingPhase", m_couplingPhase).toString();
    if (!phase.isEmpty()) m_couplingPhase = phase;

    m_gutterShadow = s.value(k + "/gutterShadow", m_gutterShadow).toDouble();
    int sq = s.value(k + "/scalingQuality", 0).toInt();
    m_scalingQuality = (sq == 1) ? Qt::FastTransformation : Qt::SmoothTransformation;
}
