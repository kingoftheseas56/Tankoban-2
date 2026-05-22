#include "ui/pages/stream/EpisodeTile.h"

#include <QCheckBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QSignalBlocker>

namespace tankoban::stream::theatre {

EpisodeTile::EpisodeTile(const EpisodeTileData& data, QWidget* parent)
    : QFrame(parent), m_data(data) {
    setObjectName(QStringLiteral("EpisodeTile"));
    setMinimumHeight(36);
    buildUI();
}

void EpisodeTile::buildUI() {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(8);

    m_checkBox = new QCheckBox(this);
    // Default check: true unless the episode is already-have (smart skip).
    m_checkBox->setChecked(!m_data.alreadyHave);
    // THEATRE_BULK_PICKER_SHIFT_RANGE 2026-05-22 — re-emit two signals on
    // every checkbox toggle. The legacy `toggled(bool)` keeps the old
    // contract for consumers that don't care about modifier state. The
    // new `toggledShift(bool, bool)` carries the keyboard-modifier state
    // at click time so the bulk picker can implement shift+click range
    // fill. QGuiApplication::keyboardModifiers() returns the modifier
    // state at the moment the event is processed, which is what we want
    // (shift is still held during the click that triggers toggled).
    connect(m_checkBox, &QCheckBox::toggled, this, [this](bool checked) {
        const bool shiftHeld =
            QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
        emit toggled(checked);
        emit toggledShift(checked, shiftHeld);
    });
    row->addWidget(m_checkBox);

    m_seLabel = new QLabel(
        QStringLiteral("S%1E%2")
            .arg(m_data.season, 2, 10, QLatin1Char('0'))
            .arg(m_data.episode, 2, 10, QLatin1Char('0')),
        this);
    m_seLabel->setFixedWidth(56);
    m_seLabel->setStyleSheet("font-size: 11px; font-weight: 600; color: rgba(255,255,255,0.78);");
    row->addWidget(m_seLabel);

    m_titleLabel = new QLabel(m_data.title, this);
    m_titleLabel->setStyleSheet("font-size: 11px; color: #e0e0e0;");
    row->addWidget(m_titleLabel, /*stretch=*/1);

    m_sizeLabel = new QLabel(this);
    if (m_data.sizeBytes > 0) {
        const double mb = m_data.sizeBytes / 1'000'000.0;
        m_sizeLabel->setText(QStringLiteral("%1 MB").arg(mb, 0, 'f', 0));
    } else {
        m_sizeLabel->setText(QStringLiteral("—"));
    }
    m_sizeLabel->setStyleSheet("font-size: 10px; color: rgba(255,255,255,0.48);");
    row->addWidget(m_sizeLabel);

    m_haveBadge = new QLabel(tr("Have"), this);
    m_haveBadge->setObjectName(QStringLiteral("EpisodeTileHaveBadge"));
    m_haveBadge->setStyleSheet(
        "QLabel#EpisodeTileHaveBadge {"
        "  background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.18);"
        "  border-radius: 2px;"
        "  padding: 1px 5px;"
        "  font-size: 9px; font-weight: 700;"
        "  color: rgba(255,255,255,0.62); }");
    m_haveBadge->setVisible(m_data.alreadyHave);
    row->addWidget(m_haveBadge);

    setStyleSheet(
        "EpisodeTile { background: transparent; border-bottom: 1px solid rgba(255,255,255,0.04); }"
        "EpisodeTile:hover { background: rgba(255,255,255,0.03); }");
}

bool EpisodeTile::isChecked() const { return m_checkBox && m_checkBox->isChecked(); }

void EpisodeTile::setChecked(bool checked) {
    if (m_checkBox) m_checkBox->setChecked(checked);
}

void EpisodeTile::setCheckedQuiet(bool checked) {
    if (!m_checkBox) return;
    // QSignalBlocker RAII-suppresses signal emission for the lifetime of
    // the scope. Both legacy `toggled(bool)` and new `toggledShift(bool,
    // bool)` are routed through the m_checkBox->toggled connection set up
    // in buildUI, so blocking the checkbox suppresses both.
    QSignalBlocker blocker(m_checkBox);
    m_checkBox->setChecked(checked);
}

// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — state-input setter. Stores the
// new state and triggers a repaint; the actual paint logic for the chip +
// progress strip + completion glyph lands in Task 13.
void EpisodeTile::setEpisodeState(const EpisodeTileState& s) {
    m_episodeState = s;
    update();
}

// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 Task 15 — identity setter +
// live-state wiring. setImdbId is idempotent; setStreamDownloadIndex performs
// disconnect/reconnect of the entryStateChanged subscription and triggers an
// immediate refreshFromIndex() so the chip reflects current state at attach.
void EpisodeTile::setImdbId(const QString& imdbId) {
    m_imdbId = imdbId;
    if (m_streamDownloadIndex)
        refreshFromIndex();
}

void EpisodeTile::setStreamDownloadIndex(StreamDownloadIndex* idx) {
    if (m_streamDownloadIndex == idx) return;
    if (m_streamDownloadIndex)
        disconnect(m_streamDownloadIndex, nullptr, this, nullptr);
    m_streamDownloadIndex = idx;
    if (m_streamDownloadIndex) {
        connect(m_streamDownloadIndex, &StreamDownloadIndex::entryStateChanged,
                this,
                [this](const QString& imdbId, int season, int episode) {
                    if (imdbId == m_imdbId
                        && season == m_data.season
                        && episode == m_data.episode) {
                        refreshFromIndex();
                    }
                },
                Qt::QueuedConnection);
        refreshFromIndex();
    }
}

void EpisodeTile::refreshFromIndex() {
    if (!m_streamDownloadIndex || m_imdbId.isEmpty()) {
        if (m_hasIndexEntry) {
            m_hasIndexEntry = false;
            update();
        }
        return;
    }
    const auto entries = m_streamDownloadIndex->entriesForImdb(m_imdbId);
    EpisodeTileState s;
    bool found = false;
    for (const auto& e : entries) {
        if (e.season == m_data.season && e.episode == m_data.episode) {
            s.state = e.state;
            s.progressPct = e.progressPct;
            if (e.sourceGroupId.startsWith(QStringLiteral("tankorent:")))
                s.provenance = EpisodeTileState::Tankorent;
            else if (e.sourceGroupId.isEmpty())
                s.provenance = EpisodeTileState::LocalScan;
            else
                s.provenance = EpisodeTileState::AddonBulk;
            found = true;
            break;
        }
    }
    if (found) {
        m_hasIndexEntry = true;
        // THEATRE_POLISH 2026-05-22 — the painted state chip ("Downloaded" /
        // "Downloading %1%" / "Queued" / "Failed") is strictly richer than
        // the static "Have" badge, so hide the badge whenever the chip is
        // active. Without this, both indicators render at the same right
        // edge and the badge's border + letter caps poke through above and
        // below the chip's 18px pill ("Download_ed_aded" garble).
        if (m_haveBadge) m_haveBadge->setVisible(false);
        setEpisodeState(s);  // also calls update()
    } else if (m_hasIndexEntry) {
        m_hasIndexEntry = false;
        // Restore the "Have" badge for the no-index-entry case (legacy
        // local-only file with no download metadata).
        if (m_haveBadge) m_haveBadge->setVisible(m_data.alreadyHave);
        update();
    }
}

// TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 — state-aware paint pass.
// Renders the download-state chip on the right side of the tile + an optional
// progress strip along the bottom edge for Downloading state. Defaults
// (state=Complete + provenance=AddonBulk) render as a "Downloaded" chip;
// the visible-regression-pre-Task-15 window is acceptable per plan §Task 17
// smoke-checkpoint scope. Amber-tint based on provenance lands in Task 20
// once tone is ratified; for now all provenances render identically.
void EpisodeTile::paintEvent(QPaintEvent* event) {
    QFrame::paintEvent(event);

    // TANKORENT_CINEMETA_PACK_MAPPING 2026-05-18 Task 15 — only render the
    // state chip when the tile has been bound to an index entry. Untracked
    // tiles (no setStreamDownloadIndex+setImdbId attached, or no matching
    // entry in the index) skip chip rendering to avoid a misleading default
    // "Downloaded" chip.
    if (!m_hasIndexEntry)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Right-anchored chip rect: 80x18 with 8px right margin, vertically centered.
    constexpr int chipWidth   = 80;
    constexpr int chipHeight  = 18;
    constexpr int rightMargin = 8;
    const QRect chipRect(width() - rightMargin - chipWidth,
                         (height() - chipHeight) / 2,
                         chipWidth, chipHeight);

    QString chipText;
    bool drawProgressStrip = false;
    QColor chipBg(64, 64, 64, 220);  // default gray

    switch (m_episodeState.state) {
    case StreamDownloadIndex::Entry::Pending:
        chipText = QStringLiteral("Queued");
        break;
    case StreamDownloadIndex::Entry::Downloading:
        chipText = QStringLiteral("%1%").arg(m_episodeState.progressPct);
        drawProgressStrip = true;
        break;
    case StreamDownloadIndex::Entry::Complete:
        chipText = QStringLiteral("Downloaded");
        // TODO Task 20 polish: add ✓ glyph in chip when amber-tone is ratified.
        break;
    case StreamDownloadIndex::Entry::Failed:
        chipText = QStringLiteral("Failed");
        chipBg = QColor(180, 60, 60, 220);  // muted red
        break;
    }

    // Chip background (rounded corners).
    painter.setBrush(chipBg);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(chipRect, 3, 3);

    // Chip text — small bold white.
    painter.setPen(QColor(255, 255, 255, 220));
    QFont chipFont = painter.font();
    chipFont.setPointSize(8);
    chipFont.setBold(true);
    painter.setFont(chipFont);
    painter.drawText(chipRect, Qt::AlignCenter, chipText);

    if (drawProgressStrip) {
        constexpr int stripHeight = 2;
        const int progressW =
            (width() * m_episodeState.progressPct) / 100;
        painter.fillRect(
            QRect(0, height() - stripHeight, progressW, stripHeight),
            palette().color(QPalette::Highlight));
    }
}

}  // namespace tankoban::stream::theatre
