#include "StreamSourceCard.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

namespace tankostream::stream {

namespace {

QString buildCardStyleSheet(bool hovered, bool selected)
{
    // Base card + state overlays. Object-name-scoped so the card's child
    // labels don't inherit background styling they shouldn't.
    //
    // STREAM_SOURCE_CARD_TITLE_FIX 2026-05-06 — Stremio-parity hierarchy:
    //   • Title (release name) — primary, brighter (#f3f4f6), 13px 600
    //   • Pack chip — slightly muted bg, distinct from quality pill
    //   • Addon footer — 11px, very subdued (#6b7280)
    // StreamSourceCardAddon kept for any legacy QSS lookups (no current
    // consumer; safe to remove on next pass). StreamSourceCardFilename
    // retired alongside the secondary filename label removal.
    const QString base = QStringLiteral(
        "#StreamSourceCard { background: rgba(255,255,255,0.04);"
        " border: 1px solid rgba(255,255,255,0.08);"
        " border-radius: 8px; }"
        "#StreamSourceCard QLabel { background: transparent; }"
        "#StreamSourceCardBadge { background: rgba(255,255,255,0.06);"
        " border-radius: 6px; color: #9ca3af;"
        " font-size: 13px; font-weight: 600; }"
        "#StreamSourceCardTitle { color: #f3f4f6; font-size: 13px; font-weight: 600; }"
        "#StreamSourceCardChip { color: #9ca3af; font-size: 11px; }"
        "#StreamSourceCardPackChip { background: rgba(255,255,255,0.07);"
        " border-radius: 3px; color: #d1d5db;"
        " font-size: 10px; font-weight: 600; padding: 1px 6px; }"
        "#StreamSourceCardQuality { background: rgba(255,255,255,0.10);"
        " border-radius: 4px; color: #d1d5db;"
        " font-size: 11px; font-weight: 600; padding: 2px 8px; }"
        "#StreamSourceCardBadgeLabel { background: rgba(255,255,255,0.08);"
        " border-radius: 3px; color: #d1d5db;"
        " font-size: 10px; font-weight: 600; padding: 1px 5px; }"
        "#StreamSourceCardAddonFooter { color: #6b7280; font-size: 11px; }"
        // THEATRE_STREAMING_RESTORE P2 — per-source Play / Download buttons.
        // Gray/black/white palette only (repo UI rule, Codex review 2026-06-10):
        // Play is the brighter (primary) white-alpha; Download is the muted one.
        "#StreamSourceCardPlayBtn { background: rgba(255,255,255,0.14);"
        " border: 1px solid rgba(255,255,255,0.22); border-radius: 6px;"
        " color: #f3f4f6; font-size: 11px; font-weight: 600; padding: 4px 12px; }"
        "#StreamSourceCardPlayBtn:hover { background: rgba(255,255,255,0.22); }"
        "#StreamSourceCardDownloadBtn { background: rgba(255,255,255,0.05);"
        " border: 1px solid rgba(255,255,255,0.12); border-radius: 6px;"
        " color: #cbd1d9; font-size: 11px; font-weight: 600; padding: 4px 12px; }"
        "#StreamSourceCardDownloadBtn:hover { background: rgba(255,255,255,0.11); }");

    if (selected) {
        return base + QStringLiteral(
            "#StreamSourceCard { background: rgba(255,255,255,0.12);"
            " border-color: rgba(255,255,255,0.22); }");
    }
    if (hovered) {
        return base + QStringLiteral(
            "#StreamSourceCard { background: rgba(255,255,255,0.08);"
            " border-color: rgba(255,255,255,0.14); }");
    }
    return base;
}

}

StreamSourceCard::StreamSourceCard(const StreamPickerChoice& choice, QWidget* parent)
    : QFrame(parent)
    , m_choice(choice)
{
    setObjectName(QStringLiteral("StreamSourceCard"));
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_StyledBackground, true);
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(76);

    buildUI();
    applyStateStyle();
}

void StreamSourceCard::buildUI()
{
    // STREAM_SOURCE_CARD_TITLE_FIX 2026-05-06 — Stremio-parity hierarchy.
    // Layout: [Badge] | [Title (release name) + Quality pill]
    //                  / [Pack chip + chips]
    //                  / [Addon footer]
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(12);

    // ── Badge (addon initials) ────────────────────────────────────────────
    // Reads addonName directly now that displayTitle was repurposed to the
    // release name. addonInitials("Torrentio") → "T", addonInitials(release)
    // would yield e.g. "TH" from "The.Boys..." — wrong semantic.
    auto* badge = new QLabel(addonInitials(m_choice.addonName), this);
    badge->setObjectName(QStringLiteral("StreamSourceCardBadge"));
    badge->setFixedSize(36, 36);
    badge->setAlignment(Qt::AlignCenter);
    root->addWidget(badge);

    // ── Text column ───────────────────────────────────────────────────────
    auto* textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(3);

    // Top row: release name (primary, brighter) + quality pill (right-aligned).
    // Title label cached on the card so resizeEvent → reelideTitle() can
    // re-elide as the panel width changes. Tooltip carries the full
    // untruncated string so the user can recover the long name on hover.
    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(6);

    m_titleLabel = new QLabel(m_choice.displayTitle, this);
    m_titleLabel->setObjectName(QStringLiteral("StreamSourceCardTitle"));
    m_titleLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_titleLabel->setWordWrap(false);
    m_titleLabel->setToolTip(m_choice.displayTitle);
    // Allow the label to shrink so reelideTitle can elide; absent this
    // the layout reserves the full sizeHint width and clipping happens at
    // the pane edge instead of inside the label.
    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    topRow->addWidget(m_titleLabel, 1);

    if (!m_choice.displayQuality.isEmpty()
     && m_choice.displayQuality != QLatin1String("-")) {
        auto* quality = new QLabel(m_choice.displayQuality, this);
        quality->setObjectName(QStringLiteral("StreamSourceCardQuality"));
        quality->setAlignment(Qt::AlignCenter);
        topRow->addWidget(quality, 0);
    }

    textCol->addLayout(topRow);

    // ── Chip row: pack chip (NEW) + seeders / size / badges ───────────────
    auto* chipRow = new QHBoxLayout();
    chipRow->setContentsMargins(0, 0, 0, 0);
    chipRow->setSpacing(8);

    // Pack chip first — Stremio-style "S03E04" / "Season 3" / "Complete
    // Series" indicator. Hidden when packLabel is empty (movies, ad-hoc
    // streams, anything detectPackType couldn't classify).
    if (!m_choice.packLabel.isEmpty()) {
        auto* pack = new QLabel(m_choice.packLabel, this);
        pack->setObjectName(QStringLiteral("StreamSourceCardPackChip"));
        const QString tipKind = (m_choice.packType == QLatin1String("episode"))
                                    ? QStringLiteral("Single episode")
                                : (m_choice.packType == QLatin1String("season"))
                                    ? QStringLiteral("Season pack")
                                : (m_choice.packType == QLatin1String("series"))
                                    ? QStringLiteral("Full series pack")
                                : QStringLiteral("Release shape");
        pack->setToolTip(tipKind);
        chipRow->addWidget(pack);
    }

    if (m_choice.seeders >= 0) {
        // U+2191 upward arrow acts as a peer-count glyph; monochrome,
        // no-emoji-palette-compliant.
        auto* peers = new QLabel(
            QStringLiteral("\u2191 %1").arg(m_choice.seeders), this);
        peers->setObjectName(QStringLiteral("StreamSourceCardChip"));
        peers->setToolTip(QStringLiteral("Seeders"));
        chipRow->addWidget(peers);
    }

    if (m_choice.sizeBytes > 0) {
        auto* size = new QLabel(
            QStringLiteral("\u2022 %1").arg(humanSize(m_choice.sizeBytes)), this);
        size->setObjectName(QStringLiteral("StreamSourceCardChip"));
        size->setToolTip(QStringLiteral("File size"));
        chipRow->addWidget(size);
    }

    for (const QString& badgeText : m_choice.badges) {
        auto* b = new QLabel(badgeText, this);
        b->setObjectName(QStringLiteral("StreamSourceCardBadgeLabel"));
        chipRow->addWidget(b);
    }

    chipRow->addStretch();
    textCol->addLayout(chipRow);

    // ── Addon footer (NEW): "Torrentio" or "Torrentio · YTS" ─────────────
    // Was previously the bold primary surface; demoted to a quiet footer
    // line per Stremio parity. Hidden entirely when both addonName and
    // trackerSource are empty (defensive — addonName usually populated by
    // resolveAddonLabel's "Unknown addon" fallback).
    QString footerText = m_choice.addonName;
    if (!m_choice.trackerSource.isEmpty()) {
        if (footerText.isEmpty()) {
            footerText = m_choice.trackerSource;
        } else {
            footerText += QStringLiteral(" · ") + m_choice.trackerSource;
        }
    }
    if (!footerText.isEmpty()) {
        auto* footer = new QLabel(footerText, this);
        footer->setObjectName(QStringLiteral("StreamSourceCardAddonFooter"));
        footer->setToolTip(m_choice.addonName.isEmpty()
                             ? QStringLiteral("Tracker source")
                             : QStringLiteral("Addon"));
        textCol->addWidget(footer);
    }

    root->addLayout(textCol, 1);

    // ── Action column: explicit Play (stream) + Download buttons ──────────────
    // THEATRE_STREAMING_RESTORE P2 (2026-06-10) — Hemanth chose pick-first with
    // "both a Play (stream) and a Download action" visible per source. Play emits
    // the existing `clicked` signal (StreamPage routes it to the Stremio stream
    // engine); Download emits the existing `directDownloadRequested` (libtorrent).
    // Whole-card left-click also still emits `clicked` (Play) as a convenience.
    auto* actionCol = new QVBoxLayout();
    actionCol->setContentsMargins(0, 0, 0, 0);
    actionCol->setSpacing(6);

    auto* playBtn = new QToolButton(this);
    playBtn->setObjectName(QStringLiteral("StreamSourceCardPlayBtn"));
    playBtn->setText(tr("Play"));
    playBtn->setIcon(QIcon(QStringLiteral(":/icons/play-circle.svg")));
    playBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    playBtn->setCursor(Qt::PointingHandCursor);
    playBtn->setToolTip(tr("Stream this source"));
    connect(playBtn, &QToolButton::clicked, this,
            [this]() { emit clicked(m_choice); });

    auto* downloadBtn = new QToolButton(this);
    downloadBtn->setObjectName(QStringLiteral("StreamSourceCardDownloadBtn"));
    downloadBtn->setText(tr("Download"));
    downloadBtn->setIcon(QIcon(QStringLiteral(":/icons/download.svg")));
    downloadBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    downloadBtn->setCursor(Qt::PointingHandCursor);
    downloadBtn->setToolTip(tr("Download this source for offline"));
    // T10: morph instantly to "Queued" so the user sees feedback at click time,
    // before the magnet-resolution round-trip completes.
    connect(downloadBtn, &QToolButton::clicked, this,
            [this, downloadBtn]() {
                downloadBtn->setText(tr("Queued"));
                downloadBtn->setEnabled(false);
                emit directDownloadRequested(m_choice);
            });

    actionCol->addWidget(playBtn);
    actionCol->addWidget(downloadBtn);
    actionCol->addStretch();
    root->addLayout(actionCol, 0);
}

void StreamSourceCard::reelideTitle()
{
    if (!m_titleLabel) return;
    const int avail = m_titleLabel->width();
    if (avail <= 0) return;
    const QFontMetrics fm(m_titleLabel->font());
    m_titleLabel->setText(
        fm.elidedText(m_choice.displayTitle, Qt::ElideRight, avail));
}

void StreamSourceCard::resizeEvent(QResizeEvent* event)
{
    QFrame::resizeEvent(event);
    reelideTitle();
}

void StreamSourceCard::setSelected(bool selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    applyStateStyle();
}

void StreamSourceCard::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    applyStateStyle();
    QFrame::enterEvent(event);
}

void StreamSourceCard::leaveEvent(QEvent* event)
{
    m_hovered = false;
    applyStateStyle();
    QFrame::leaveEvent(event);
}

void StreamSourceCard::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        emit clicked(m_choice);
    }
    QFrame::mouseReleaseEvent(event);
}

void StreamSourceCard::contextMenuEvent(QContextMenuEvent* event)
{
    // Suppress the menu entirely on non-magnet cards (HTTP/URL/youtube
    // direct streams). Tankorent only consumes magnets; presenting the
    // action greyed-out adds visual noise. Hemanth call 2026-05-06.
    if (m_choice.sourceKind != QLatin1String("magnet")
     || m_choice.magnetUri.isEmpty()) {
        QFrame::contextMenuEvent(event);
        return;
    }

    QMenu menu(this);
    // THEATRE_DOWNLOAD_OVERHAUL UI refinement 2026-05-17 - Theatre-native
    // "Download" item dispatches the right-clicked stream directly to
    // TorrentClient (Theatre library), in contrast to "Add torrent to
    // Tankorent" which routes into the Tankorent tab download manager.
    auto* downloadAction = menu.addAction(tr("Download"));
    connect(downloadAction, &QAction::triggered, this, [this]() {
        emit directDownloadRequested(m_choice);
    });
    auto* addAction = menu.addAction(tr("Add torrent to Tankorent"));
    connect(addAction, &QAction::triggered, this, [this]() {
        emit addToTankorentRequested(m_choice);
    });
    menu.exec(event->globalPos());
    event->accept();
}

void StreamSourceCard::applyStateStyle()
{
    setStyleSheet(buildCardStyleSheet(m_hovered, m_selected));
}

QString StreamSourceCard::addonInitials(const QString& addonName)
{
    const QString trimmed = addonName.trimmed();
    if (trimmed.isEmpty()) return QStringLiteral("?");

    // Take up to two uppercase-worthy initials. "Torrentio" → "T".
    // "Open Subtitles" → "OS". "com.linvo.cinemeta" → "CI".
    QString cleaned = trimmed;
    // Strip common addon-id prefixes if they leak through.
    if (cleaned.startsWith(QStringLiteral("com."), Qt::CaseInsensitive)) {
        const int dot = cleaned.lastIndexOf('.');
        if (dot >= 0 && dot + 1 < cleaned.size())
            cleaned = cleaned.mid(dot + 1);
    }

    QStringList tokens = cleaned.split(QRegularExpression(QStringLiteral("[\\s\\-_.]+")),
                                        Qt::SkipEmptyParts);
    if (tokens.isEmpty()) return cleaned.left(1).toUpper();

    if (tokens.size() == 1) return tokens.first().left(1).toUpper();
    return (tokens.at(0).left(1) + tokens.at(1).left(1)).toUpper();
}

}
