// src/ui/pages/comics/ComicsSourceCard.cpp
#include "ComicsSourceCard.h"

#include "core/manga/TrustedUploaders.h"
#include "core/manga/anilist/AniListTypes.h"  // kVolumeXNumber

#include <QEnterEvent>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStringList>
#include <QStyle>
#include <QVBoxLayout>

namespace tankoban::manga::comics {

namespace {

// Card stylesheet per spec §3.7. The [fallback="true"] selector flips the
// border + button background + host color for WeebCentral-fallback cards.
QString cardStyleSheet(bool hovered, bool selected)
{
    QString base = QStringLiteral(
        // Card container
        "#ComicsSourceCard {"
        " background: #1c1c22;"
        " border: 1px solid #2d2d35;"
        " border-radius: 5px;"
        " }"
        // Fallback card (WeebCentral): dimmer border, slightly subordinated
        "#ComicsSourceCard[fallback=\"true\"] {"
        " border: 1px solid #3a3a45;"
        " }"
        "#ComicsSourceCard QLabel { background: transparent; }"
        // Release title -- white, semi-bold
        "#ComicsSourceCardTitle {"
        " color: #ffffff;"
        " font-weight: 600;"
        " font-size: 11px;"
        " }"
        // Meta line (RichText, contains green seed-count span + badges)
        "#ComicsSourceCardMeta {"
        " color: #8b8b95;"
        " font-size: 10px;"
        " }"
        // Download button -- purple gradient action
        "#ComicsSourceCardDownload {"
        " background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "                              stop:0 #5e3a8b,"
        "                              stop:1 #7b4dba);"
        " color: #ffffff;"
        " font-size: 11px;"
        " font-weight: 600;"
        " padding: 4px 10px;"
        " border-radius: 3px;"
        " border: none;"
        " }"
        "#ComicsSourceCardDownload:hover {"
        " background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "                              stop:0 #6c46a0,"
        "                              stop:1 #8a5cd0);"
        " }"
        "#ComicsSourceCardDownload:pressed {"
        " background: #5e3a8b;"
        " }"
        // Download button -- muted on fallback cards
        "#ComicsSourceCard[fallback=\"true\"] #ComicsSourceCardDownload {"
        " background: #2a2a32;"
        " color: #d0d0d4;"
        " }"
        // Skeleton block
        "#ComicsSourceCardSkeletonBlock {"
        " background: rgba(255,255,255,0.08);"
        " border-radius: 4px;"
        " }");

    if (selected) {
        base += QStringLiteral(
            "#ComicsSourceCard {"
            " background: #232331;"
            " border: 1px solid #7b4dba;"
            " }");
    } else if (hovered) {
        base += QStringLiteral(
            "#ComicsSourceCard {"
            " background: #21212a;"
            " border-color: #3a3a45;"
            " }");
    }

    return base;
}

QLabel* makeSkeletonBlock(QWidget* parent, int minWidth, int height)
{
    auto* block = new QLabel(parent);
    block->setObjectName(QStringLiteral("ComicsSourceCardSkeletonBlock"));
    block->setMinimumWidth(minWidth);
    block->setFixedHeight(height);
    block->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return block;
}

ComicsSourceCard::HostType hostTypeFromKind(UnifiedSourceRow::Kind kind)
{
    switch (kind) {
    case UnifiedSourceRow::Kind::Catalog:        return ComicsSourceCard::HostType::Nyaa;
    case UnifiedSourceRow::Kind::NyaaRuntime:    return ComicsSourceCard::HostType::Nyaa;
    case UnifiedSourceRow::Kind::WeebCentralPacker:
        return ComicsSourceCard::HostType::WeebCentral;
    }
    return ComicsSourceCard::HostType::Other;
}

QString defaultHostNameFor(ComicsSourceCard::HostType type)
{
    switch (type) {
    case ComicsSourceCard::HostType::Nyaa:            return QStringLiteral("nyaa.si");
    case ComicsSourceCard::HostType::WeebCentral:     return QStringLiteral("WeebCentral");
    case ComicsSourceCard::HostType::TankoyomiSource: return QStringLiteral("Tankoyomi");
    case ComicsSourceCard::HostType::Other:           return QString();
    }
    return QString();
}

} // namespace

ComicsSourceCard::ComicsSourceCard(const UnifiedSourceRow& row, QWidget* parent)
    : QFrame(parent)
    , m_row(row)
{
    setObjectName(QStringLiteral("ComicsSourceCard"));
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_StyledBackground, true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    buildUi();
    seedFromUnifiedRow(row);
    applyStylePerType();
}

ComicsSourceCard::ComicsSourceCard(bool skeleton, QWidget* parent)
    : QFrame(parent)
    , m_skeleton(skeleton)
{
    setObjectName(QStringLiteral("ComicsSourceCard"));
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_StyledBackground, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    buildUiSkeleton();
    applyStylePerType();
}

ComicsSourceCard::~ComicsSourceCard() = default;

void ComicsSourceCard::buildUi()
{
    // Spec §3.7 root layout: VBox -> top HBox (title left / host right) ->
    // meta line -> action row with download button.
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 9, 10, 9);
    root->setSpacing(6);

    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(8);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("ComicsSourceCardTitle"));
    m_titleLabel->setTextFormat(Qt::PlainText);
    m_titleLabel->setWordWrap(false);
    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    topRow->addWidget(m_titleLabel, /*stretch=*/1);

    root->addLayout(topRow);

    m_metaLabel = new QLabel(this);
    m_metaLabel->setObjectName(QStringLiteral("ComicsSourceCardMeta"));
    // RichText so the green seed-count span and trusted/fallback badges
    // render with inline styling. Per spec §3.7 the seed count number is
    // colored green via <span style="color:#5fb87b">.
    m_metaLabel->setTextFormat(Qt::RichText);
    m_metaLabel->setWordWrap(false);
    root->addWidget(m_metaLabel);

    auto* actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(0);
    m_downloadButton = new QPushButton(this);
    m_downloadButton->setObjectName(QStringLiteral("ComicsSourceCardDownload"));
    m_downloadButton->setCursor(Qt::PointingHandCursor);
    m_downloadButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    connect(m_downloadButton, &QPushButton::clicked, this, [this]() {
        emit downloadClicked(m_row);
    });
    actionRow->addWidget(m_downloadButton, 0, Qt::AlignLeft);
    actionRow->addStretch(1);
    root->addLayout(actionRow);

    rebuildDownloadButtonLabel();
}

void ComicsSourceCard::buildUiSkeleton()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 9, 10, 9);
    root->setSpacing(6);

    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(8);
    topRow->addWidget(makeSkeletonBlock(this, 140, 12), 1);
    auto* hostBlock = makeSkeletonBlock(this, 60, 10);
    hostBlock->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    topRow->addWidget(hostBlock, 0, Qt::AlignRight);
    root->addLayout(topRow);

    root->addWidget(makeSkeletonBlock(this, 200, 10));

    auto* actionBlock = makeSkeletonBlock(this, 110, 22);
    actionBlock->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto* actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->addWidget(actionBlock, 0, Qt::AlignLeft);
    actionRow->addStretch(1);
    root->addLayout(actionRow);

    auto* effect = new QGraphicsOpacityEffect(this);
    effect->setOpacity(0.45);
    setGraphicsEffect(effect);

    auto* anim = new QPropertyAnimation(effect, "opacity", this);
    anim->setDuration(1500);
    anim->setStartValue(0.30);
    anim->setEndValue(0.70);
    anim->setEasingCurve(QEasingCurve::InOutSine);
    anim->setLoopCount(-1);
    anim->start();
}

void ComicsSourceCard::seedFromUnifiedRow(const UnifiedSourceRow& row)
{
    m_releaseTitle  = row.title;
    m_uploader      = row.uploaderHint;
    m_sizeBytes     = row.sizeBytes;
    m_seedCount     = row.seeders;     // already -1 for WeebCentral
    m_hostType      = hostTypeFromKind(row.kind);
    m_isFallback    = (row.kind == UnifiedSourceRow::Kind::WeebCentralPacker);
    m_hostName      = defaultHostNameFor(m_hostType);
    // m_volumeNumber stays at its default 0 -- callers wire it via
    // setVolumeNumber() once Task 5's ComicsSourcesPanel migration lands.

    if (m_titleLabel) {
        m_titleLabel->setText(m_releaseTitle);
        m_titleLabel->setToolTip(m_releaseTitle);
    }
    rebuildMetaLine();
    rebuildDownloadButtonLabel();
}

// --- Spec §3.7 setter API ---

void ComicsSourceCard::setReleaseTitle(const QString& title)
{
    m_releaseTitle = title;
    if (m_titleLabel) {
        m_titleLabel->setText(title);
        m_titleLabel->setToolTip(title);
    }
    m_row.title = title;
    reelideTitle();
}

void ComicsSourceCard::setHostName(const QString& host)
{
    m_hostName = host;
}

void ComicsSourceCard::setHostType(HostType type)
{
    m_hostType = type;
    if (m_hostName.isEmpty()) {
        m_hostName = defaultHostNameFor(type);
    }
    applyStylePerType();
}

void ComicsSourceCard::setUploader(const QString& uploader)
{
    m_uploader = uploader;
    m_row.uploaderHint = uploader;
    rebuildMetaLine();
}

void ComicsSourceCard::setSizeBytes(qint64 size)
{
    m_sizeBytes = size;
    m_row.sizeBytes = size;
    rebuildMetaLine();
}

void ComicsSourceCard::setSeedCount(int seeds)
{
    m_seedCount = seeds;
    m_row.seeders = seeds;
    rebuildMetaLine();
}

void ComicsSourceCard::setIsFallback(bool fallback)
{
    if (m_isFallback == fallback) return;
    m_isFallback = fallback;
    rebuildMetaLine();
    applyStylePerType();
}

void ComicsSourceCard::setVolumeNumber(int volumeN)
{
    m_volumeNumber = volumeN;
    rebuildDownloadButtonLabel();
}

void ComicsSourceCard::setSelected(bool selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    applyStylePerType();
}

// --- Event handlers ---

void ComicsSourceCard::enterEvent(QEnterEvent* event)
{
    if (!m_skeleton) {
        m_hovered = true;
        applyStylePerType();
    }
    QFrame::enterEvent(event);
}

void ComicsSourceCard::leaveEvent(QEvent* event)
{
    if (!m_skeleton) {
        m_hovered = false;
        applyStylePerType();
    }
    QFrame::leaveEvent(event);
}

void ComicsSourceCard::mouseReleaseEvent(QMouseEvent* event)
{
    // Body click still emits clicked() for panel selection (legacy
    // contract). Download button has its own dedicated signal via
    // m_downloadButton -> downloadClicked(m_row).
    if (!m_skeleton
        && event->button() == Qt::LeftButton
        && rect().contains(event->pos())) {
        emit clicked(m_row);
    }
    QFrame::mouseReleaseEvent(event);
}

void ComicsSourceCard::resizeEvent(QResizeEvent* event)
{
    QFrame::resizeEvent(event);
    reelideTitle();
}

// --- Private builders ---

void ComicsSourceCard::rebuildMetaLine()
{
    if (!m_metaLabel) return;

    QStringList parts;

    if (m_isFallback) {
        // Meta line intentionally suppressed for fallback cards per Hemanth polish-mode override, 2026-05-25.
        m_metaLabel->setText(QString());
        m_metaLabel->hide();
        return;
    } else {
        m_metaLabel->show();
        // Spec §3.7: nyaa-style meta line.
        // "<uploader> · <size formatted> · ▲ <seedCount> seeds<optional " · trusted">"
        if (!m_uploader.isEmpty()) {
            parts << m_uploader.toHtmlEscaped();
        }
        if (m_sizeBytes > 0) {
            parts << QLocale().formattedDataSize(m_sizeBytes);
        }
        if (m_seedCount >= 0) {
            // Green ▲ + count + " seeds". &#9650; is U+25B2 BLACK UP-POINTING
            // TRIANGLE.
            parts << QStringLiteral(
                         "<span style=\"color:#5fb87b\">&#9650; %1 seeds</span>")
                         .arg(m_seedCount);
        }
        if (TrustedUploaders::isTrusted(m_uploader)) {
            parts << QStringLiteral(
                "<span style=\"color:#c0a0ff;font-weight:600\">trusted</span>");
        }
    }

    m_metaLabel->setText(parts.join(QStringLiteral(" &middot; ")));
}

void ComicsSourceCard::applyStylePerType()
{
    // Update the [fallback="true"] dynamic property so the QSS selector
    // flips between the recognised-host and fallback card visuals.
    const QVariant prev = property("fallback");
    setProperty("fallback", m_isFallback);
    if (prev.toBool() != m_isFallback) {
        style()->unpolish(this);
        style()->polish(this);
    }
    setStyleSheet(cardStyleSheet(m_hovered, m_selected));
    update();
}

void ComicsSourceCard::rebuildDownloadButtonLabel()
{
    if (!m_downloadButton) return;
    if (m_volumeNumber == tankoban::manga::anilist::kVolumeXNumber) {
        m_downloadButton->setText(QStringLiteral("Download Vol X"));
    } else if (m_volumeNumber > 0) {
        m_downloadButton->setText(
            QStringLiteral("Download Vol %1").arg(m_volumeNumber));
    } else {
        m_downloadButton->setText(QStringLiteral("Download"));
    }
}

void ComicsSourceCard::reelideTitle()
{
    if (!m_titleLabel) return;
    const int width = m_titleLabel->width();
    if (width <= 0) return;
    const QFontMetrics fm(m_titleLabel->font());
    m_titleLabel->setText(fm.elidedText(m_releaseTitle, Qt::ElideRight, width));
}

} // namespace tankoban::manga::comics
