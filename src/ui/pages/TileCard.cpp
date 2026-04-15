#include "TileCard.h"

#include <QVBoxLayout>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QTimer>

static constexpr int CORNER_RADIUS = 8;

TileCard::TileCard(const QString& thumbPath,
                   const QString& title,
                   const QString& subtitle,
                   QWidget* parent)
    : QFrame(parent)
    , m_thumbPath(thumbPath)
    , m_title(title)
    , m_subtitle(subtitle)
    , m_cardWidth(DEFAULT_WIDTH)
    , m_imageHeight(DEFAULT_IMAGE_HEIGHT)
{
    setObjectName("TileCard");
    setFixedWidth(m_cardWidth);
    setCursor(Qt::PointingHandCursor);
    setProperty("tileTitle", title);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);  // uniform 6px image↔title↔subtitle (original 129c9e7)

    m_imageWrap = new QFrame(this);
    m_imageWrap->setObjectName("TileImageWrap");
    m_imageWrap->setFixedSize(m_cardWidth, m_imageHeight);
    m_imageWrap->setStyleSheet(
        "#TileImageWrap { border: 1px solid rgba(255,255,255,0.10); "
        "background: rgba(255,255,255,0.04); border-radius: 12px; }");

    m_imageLabel = new QLabel(m_imageWrap);
    m_imageLabel->setFixedSize(m_cardWidth, m_imageHeight);
    m_imageLabel->setAlignment(Qt::AlignCenter);

    setCardSize(m_cardWidth, m_imageHeight);

    layout->addWidget(m_imageWrap);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName("TileTitle");
    m_titleLabel->setFixedWidth(m_cardWidth);
    m_titleLabel->setWordWrap(false);
    QFontMetrics titleFm(m_titleLabel->font());
    m_titleLabel->setText(titleFm.elidedText(title, Qt::ElideRight, m_cardWidth - 4));
    layout->addWidget(m_titleLabel);

    m_subtitleLabel = new QLabel(this);
    m_subtitleLabel->setObjectName("TileSubtitle");
    m_subtitleLabel->setFixedWidth(m_cardWidth);
    m_subtitleLabel->setWordWrap(false);
    QFontMetrics subFm(m_subtitleLabel->font());
    m_subtitleLabel->setText(subFm.elidedText(subtitle, Qt::ElideRight, m_cardWidth - 4));
    m_subtitleLabel->setVisible(!subtitle.isEmpty());
    layout->addWidget(m_subtitleLabel);

    layout->addStretch();
}

/* ── helpers ─────────────────────────────────────────────── */

QPixmap TileCard::roundPixmap(const QPixmap& src, int radius)
{
    if (src.isNull()) return src;
    QPixmap result(src.size());
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(result.rect()), radius, radius);
    p.setClipPath(path);
    p.drawPixmap(0, 0, src);
    return result;
}

/* ── setCardSize ─────────────────────────────────────────── */

void TileCard::setCardSize(int width, int imageHeight)
{
    if (m_cardWidth == width && m_imageHeight == imageHeight)
        return;

    m_cardWidth = width;
    m_imageHeight = imageHeight;

    setFixedWidth(width);
    // Reserve a fixed text zone below the cover (matches the continue-strip
    // spec `setFixedHeight(imageH + 56)` in TileStrip.cpp). Without this the
    // tile collapses to its sizeHint minimum and the title visually kisses
    // the cover regardless of layout setSpacing(6).
    setFixedHeight(imageHeight + 56);
    m_imageWrap->setFixedSize(width, imageHeight);
    m_imageLabel->setFixedSize(width, imageHeight);

    if (m_titleLabel) {
        m_titleLabel->setFixedWidth(width);
        QFontMetrics fm(m_titleLabel->font());
        m_titleLabel->setText(fm.elidedText(m_title, Qt::ElideRight, width - 4));
    }
    if (m_subtitleLabel) {
        m_subtitleLabel->setFixedWidth(width);
        QFontMetrics fm(m_subtitleLabel->font());
        m_subtitleLabel->setText(fm.elidedText(m_subtitle, Qt::ElideRight, width - 4));
    }

    // ── render cover ────────────────────────────────────
    if (!m_thumbPath.isEmpty()) {
        QPixmap pix(m_thumbPath);
        if (!pix.isNull()) {
            QPixmap scaled = pix.scaled(width, imageHeight,
                                        Qt::KeepAspectRatioByExpanding,
                                        Qt::SmoothTransformation);
            int cx = (scaled.width() - width) / 2;
            int cy = (scaled.height() - imageHeight) / 2;
            m_basePixmap = roundPixmap(scaled.copy(cx, cy, width, imageHeight),
                                       CORNER_RADIUS);
            applyBadges();
            return;
        }
    }

    // ── placeholder (groundwork spec) ───────────────────
    QPixmap ph(width, imageHeight);
    ph.fill(Qt::transparent);
    QPainter p(&ph);
    p.setRenderHint(QPainter::Antialiasing);

    // semi-transparent black background with rounded corners
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 89));
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, width, imageHeight),
                        CORNER_RADIUS, CORNER_RADIUS);
    p.drawPath(clip);

    // first alphabetic character, uppercased, centered
    QString initial;
    for (const QChar& c : m_title) {
        if (c.isLetter()) { initial = c.toUpper(); break; }
    }
    if (initial.isEmpty()) initial = "?";

    QFont font;
    font.setPointSize(28);
    font.setBold(true);
    p.setFont(font);
    p.setPen(QColor(0x9c, 0xa3, 0xaf));
    p.drawText(QRect(0, 0, width, imageHeight), Qt::AlignCenter, initial);
    p.end();

    m_basePixmap = ph;
    applyBadges();
}

/* ── thumb path update (async poster loading) ───────────── */

void TileCard::setThumbPath(const QString& path)
{
    m_thumbPath = path;
    // Force re-render by temporarily invalidating dimensions
    int w = m_cardWidth, h = m_imageHeight;
    m_cardWidth = 0;
    m_imageHeight = 0;
    setCardSize(w, h);
}

/* ── setters ─────────────────────────────────────────────── */

void TileCard::setBadges(double progressFraction, const QString& pageBadge,
                         const QString& countBadge, const QString& status)
{
    m_progressFraction = progressFraction;
    m_pageBadge        = pageBadge;
    m_countBadge       = countBadge;
    m_status           = status;
    applyBadges();
}

void TileCard::setIsNew(bool isNew)
{
    if (m_isNew == isNew) return;
    m_isNew = isNew;
    applyBadges();
}

void TileCard::setIsFolder(bool isFolder)
{
    if (m_isFolder == isFolder) return;
    m_isFolder = isFolder;
    applyBadges();
}

/* ── applyBadges ─────────────────────────────────────────── */

void TileCard::applyBadges()
{
    if (m_basePixmap.isNull()) return;

    QPixmap result = m_basePixmap.copy();
    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);

    const int w = result.width();
    const int h = result.height();

    // ── progress bar (3px, bottom edge of cover) ────────
    bool showBar = m_progressFraction > 0.0 || m_status == "finished";
    if (showBar) {
        constexpr int barH = 3;
        const int barY = h - barH;
        const double frac = (m_status == "finished") ? 1.0
                                                     : m_progressFraction;
        const QColor fill = (m_status == "finished") ? QColor(0x4C, 0xAF, 0x50)
                                                     : QColor(0x94, 0xa3, 0xb8);
        p.setPen(Qt::NoPen);

        // track
        p.setBrush(QColor(0, 0, 0, 77));
        p.drawRect(0, barY, w, barH);

        // fill
        p.setBrush(fill);
        p.drawRect(0, barY, qRound(w * frac), barH);
    }

    // ── badge pill helpers ──────────────────────────────
    auto drawPill = [&](const QString& text, int anchorX, int anchorY,
                        bool alignRight) {
        QFont bf;
        bf.setPixelSize(11);
        bf.setWeight(QFont::DemiBold);
        p.setFont(bf);
        QFontMetrics bfm(bf);

        const int textW = bfm.horizontalAdvance(text);
        const int pillH = bfm.height() + 4;
        const int pillW = textW + 12;           // 6px padding each side
        const int pillR = pillH / 2;            // fully rounded capsule

        const int px = alignRight ? (anchorX - pillW) : anchorX;
        const int py = anchorY - pillH;

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 115));
        p.drawRoundedRect(px, py, pillW, pillH, pillR, pillR);

        p.setPen(QColor(0xf0, 0xf0, 0xf0));
        p.drawText(QRect(px, py, pillW, pillH), Qt::AlignCenter, text);
    };

    constexpr int margin = 6;
    const bool finished = (m_status == "finished");

    // ── count badge (bottom-right) — hidden when finished ──
    if (!m_countBadge.isEmpty() && !finished)
        drawPill(m_countBadge, w - margin, h - margin, true);

    // ── page badge (bottom-left) — hidden when finished ────
    if (!m_pageBadge.isEmpty() && !finished)
        drawPill(m_pageBadge, margin, h - margin, false);

    // ── folder icon overlay (top-left) ──────────────────
    // Two rounded rects forming an open-folder shape
    if (m_isFolder) {
        const QColor folderColor(255, 255, 255, 140);
        p.setPen(Qt::NoPen);
        p.setBrush(folderColor);
        // tab
        p.drawRoundedRect(8, 8, 10, 4, 1, 1);
        // body
        p.drawRoundedRect(6, 11, 16, 11, 2, 2);
    }

    // ── "New" dot (top-left, 10x10) ────────────────────
    // Offset right if folder icon is present
    if (m_isNew) {
        const int dotX = m_isFolder ? 26 : 8;
        const int dotY = 8;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x94, 0xa3, 0xb8));
        p.drawEllipse(dotX, dotY, 10, 10);
    }

    p.end();
    m_imageLabel->setPixmap(result);
}

/* ── selection / focus ────────────────────────────────────── */

void TileCard::setSelected(bool selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    updateBorder();
}

void TileCard::setFocused(bool focused)
{
    if (m_focused == focused) return;
    m_focused = focused;
    // Focus ring on the TileCard itself (1px dotted, keyboard navigation)
    if (focused)
        setStyleSheet("#TileCard { border: 1px dotted rgba(255,255,255,0.5); }");
    else
        setStyleSheet("");
}

/* ── border helper ───────────────────────────────────────── */

void TileCard::updateBorder()
{
    if (m_selected || m_hovered || m_flashing)
        m_imageWrap->setStyleSheet(
            "#TileImageWrap { border: 1px solid rgba(255,255,255,0.20); "
            "background: rgba(255,255,255,0.04); border-radius: 12px; }");
    else
        m_imageWrap->setStyleSheet(
            "#TileImageWrap { border: 1px solid rgba(255,255,255,0.10); "
            "background: rgba(255,255,255,0.04); border-radius: 12px; }");
}

/* ── mouse / hover ───────────────────────────────────────── */

void TileCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // 120ms click flash — border shown, removed after 400ms
        m_flashing = true;
        updateBorder();
        QTimer::singleShot(400, this, [this]() {
            m_flashing = false;
            updateBorder();
        });
        emit clicked();
    }
    QFrame::mousePressEvent(event);
}

void TileCard::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    updateBorder();
    QFrame::enterEvent(event);
}

void TileCard::leaveEvent(QEvent* event)
{
    m_hovered = false;
    updateBorder();
    QFrame::leaveEvent(event);
}

/* ── inline rename ───────────────────────────────────────── */

void TileCard::beginRename()
{
    if (m_renameEdit) return;
    auto* layout = qobject_cast<QVBoxLayout*>(this->layout());
    if (!layout || !m_titleLabel) return;

    m_renameEdit = new QLineEdit(this);
    m_renameEdit->setObjectName("TileRenameEdit");
    m_renameEdit->setText(m_title);
    m_renameEdit->setFixedWidth(m_cardWidth);
    m_renameEdit->setFont(m_titleLabel->font());
    m_renameEdit->setStyleSheet(
        "#TileRenameEdit { background: rgba(255,255,255,0.10); "
        "color: #f0f0f0; border: 1px solid rgba(255,255,255,0.30); "
        "border-radius: 2px; padding: 1px 3px; }");

    const int idx = layout->indexOf(m_titleLabel);
    layout->insertWidget(idx, m_renameEdit);
    m_titleLabel->hide();

    m_renameEdit->installEventFilter(this);
    connect(m_renameEdit, &QLineEdit::returnPressed, this, [this]() { endRename(true); });
    connect(m_renameEdit, &QLineEdit::editingFinished, this, [this]() { endRename(true); });

    m_renameEdit->setFocus();
    m_renameEdit->selectAll();
}

void TileCard::endRename(bool accepted)
{
    if (!m_renameEdit) return;

    const QString newName = m_renameEdit->text().trimmed();
    QLineEdit* edit = m_renameEdit;
    m_renameEdit = nullptr;           // prevent re-entry from editingFinished

    edit->disconnect();
    edit->hide();
    edit->deleteLater();

    if (m_titleLabel) m_titleLabel->show();

    const bool commit = accepted && !newName.isEmpty() && newName != m_title;
    emit renameCompleted(commit, newName);
}

bool TileCard::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_renameEdit && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            endRename(false);
            return true;
        }
    }
    return QFrame::eventFilter(obj, event);
}
