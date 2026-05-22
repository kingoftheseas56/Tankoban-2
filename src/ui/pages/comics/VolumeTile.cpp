// src/ui/pages/comics/VolumeTile.cpp
//
// COMICS_CATALOG_SERIES_VIEW Phase 2 Task 9 (2026-05-23) — TDD green.
// Provides computeState + all linker-satisfying stubs so the 6
// VolumeTileComputeState tests pass. Full UI body (buildUi, applyState,
// action wiring) lands in Task 10.

#include "ui/pages/comics/VolumeTile.h"

#include <QCheckBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/manga/MangaDownloadIndex.h"

namespace tankoban::ui::comics {

// =======================================================================
// computeState — pure-logic state mapping (fully implemented in Task 9).
// Presence in MangaDownloadIndex always wins over transient statusText,
// defensive against stale dispatch state that wasn't cleared.
// =======================================================================
VolumeTileState::State VolumeTile::computeState(bool hasIndexEntry,
                                                  const QString& statusText)
{
    if (hasIndexEntry) return VolumeTileState::Complete;

    if (statusText.startsWith(QStringLiteral("Queued"),      Qt::CaseInsensitive))
        return VolumeTileState::Queued;
    if (statusText.startsWith(QStringLiteral("Downloading"), Qt::CaseInsensitive))
        return VolumeTileState::Downloading;
    if (statusText.startsWith(QStringLiteral("Failed"),      Qt::CaseInsensitive))
        return VolumeTileState::Failed;

    return VolumeTileState::NotStarted;
}

// =======================================================================
// All other members — STUB-defined for Task 9. Bodies fleshed out at
// Task 10. Constructor leaves widget pointers null; tile will not paint
// or accept clicks until buildUi() is called.
// =======================================================================

VolumeTile::VolumeTile(const VolumeTileData& data, QWidget* parent)
    : QFrame(parent), m_data(data)
{
    buildUi();
}

bool VolumeTile::isChecked() const
{
    return m_checkbox && m_checkbox->isChecked();
}

void VolumeTile::setChecked(bool c)
{
    if (m_checkbox) m_checkbox->setChecked(c);
}

void VolumeTile::setCheckedQuiet(bool c)
{
    if (!m_checkbox) return;
    const bool wasBlocked = m_checkbox->blockSignals(true);
    m_checkbox->setChecked(c);
    m_checkbox->blockSignals(wasBlocked);
}

void VolumeTile::setVolumeState(const VolumeTileState& s)
{
    m_state = s;
    applyState();
}

void VolumeTile::setCoverFromDisk(const QString& coverPath)
{
    if (coverPath.isEmpty() || !m_coverLabel) return;
    QPixmap pm(coverPath);
    if (pm.isNull()) return;
    m_coverLabel->setPixmap(pm.scaled(80, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void VolumeTile::setCoverFromUrl(const QString& url)
{
    if (url.isEmpty()) return;
    m_data.coverUrl = url;
    // Cover loading via ComicsSeriesView's existing QNAM path -- this setter
    // is called from outside (e.g. AniList rows). No QNAM inside VolumeTile
    // to keep it widget-only; ComicsSeriesView orchestrates the fetch and
    // calls setCoverFromDisk once the pixmap is decoded.
}

void VolumeTile::setCoverFromPixmap(const QPixmap& pm)
{
    // Task 16a: async-fetch paint path. applyPixmapToVolumeRow in
    // ComicsSeriesView routes the decoded QPixmap here after QNAM fetch +
    // QPixmapCache insert -- no temp-file round-trip needed.
    if (pm.isNull() || !m_coverLabel) return;
    m_coverLabel->setPixmap(pm.scaled(80, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void VolumeTile::setStatusText(const QString& t)
{
    m_state.statusText = t;
    applyState();
}

void VolumeTile::setMangaDownloadIndex(MangaDownloadIndex* idx)
{
    m_idx = idx;
    if (m_idx) {
        connect(m_idx.data(), &MangaDownloadIndex::entriesChanged,
                this, &VolumeTile::onIndexEntriesChanged);
        onIndexEntriesChanged(); // initial sync
    }
}

void VolumeTile::paintEvent(QPaintEvent* event)
{
    QFrame::paintEvent(event);
    if (m_state.provenance.isEmpty() || !m_coverLabel) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect coverRect = m_coverLabel->geometry();
    const QRect chipRect(coverRect.left() + 4, coverRect.top() + 4, 60, 14);
    p.fillRect(chipRect, QColor(0, 0, 0, 180));
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setPixelSize(9);
    p.setFont(f);
    p.drawText(chipRect, Qt::AlignCenter, m_state.provenance.toUpper());
}

void VolumeTile::mousePressEvent(QMouseEvent* event)
{
    QFrame::mousePressEvent(event);
}

void VolumeTile::onIndexEntriesChanged()
{
    const bool has = m_idx
        && m_idx->entryForSeriesAndVolume(m_data.sourceId, m_data.seriesId,
                                           m_data.volumeNumber).has_value();
    m_state.state = computeState(has, m_state.statusText);
    applyState();
}

void VolumeTile::onActionClicked()
{
    switch (m_state.state) {
    case VolumeTileState::NotStarted:  emit downloadRequested(m_data.volumeNumber); break;
    case VolumeTileState::Queued:
    case VolumeTileState::Downloading: emit cancelRequested(m_data.volumeNumber);   break;
    case VolumeTileState::Complete:    emit openRequested(m_data.volumeNumber);     break;
    case VolumeTileState::Failed:      emit retryRequested(m_data.volumeNumber);    break;
    }
}

void VolumeTile::buildUi()
{
    setFrameShape(QFrame::NoFrame);
    setFixedHeight(130);
    setStyleSheet(
        "VolumeTile { background: transparent; border-bottom: 1px solid #1a1a1a; }"
        "VolumeTile:hover { background: rgba(255,255,255,0.03); }"
    );

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 4, 10, 4);
    layout->setSpacing(12);

    m_checkbox = new QCheckBox(this);
    layout->addWidget(m_checkbox);

    m_numberLabel = new QLabel(QString::number(m_data.volumeNumber), this);
    m_numberLabel->setFixedWidth(32);
    m_numberLabel->setAlignment(Qt::AlignCenter);
    m_numberLabel->setStyleSheet("color:#aaa;font-size:14px;font-weight:700;");
    layout->addWidget(m_numberLabel);

    m_coverLabel = new QLabel(this);
    m_coverLabel->setFixedSize(80, 120);
    m_coverLabel->setStyleSheet("background:#3a3a3a;border-radius:3px;");
    m_coverLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_coverLabel);

    auto* contentLayout = new QVBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(4);

    m_titleLabel = new QLabel(m_data.title.isEmpty()
                              ? QStringLiteral("Volume %1").arg(m_data.volumeNumber)
                              : m_data.title, this);
    m_titleLabel->setStyleSheet("color:#eee;font-size:13px;font-weight:700;");
    contentLayout->addWidget(m_titleLabel);

    QString meta = m_data.chapterRange;
    if (m_data.pages > 0)              meta += QStringLiteral(" \xC2\xB7 %1 pages").arg(m_data.pages);
    if (!m_data.publishDate.isEmpty()) meta += QStringLiteral(" \xC2\xB7 %1").arg(m_data.publishDate);
    m_metaLabel = new QLabel(meta, this);
    m_metaLabel->setStyleSheet("color:#aaa;font-size:11px;");
    contentLayout->addWidget(m_metaLabel);

    auto* chipRow = new QHBoxLayout();
    chipRow->setContentsMargins(0, 0, 0, 0);
    chipRow->setSpacing(6);
    m_chipLabel = new QLabel(this);
    m_chipLabel->setStyleSheet("background:#2a2a2a;color:#aaa;padding:2px 8px;border-radius:10px;font-size:10px;");
    chipRow->addWidget(m_chipLabel);
    m_progressLabel = new QLabel(this);
    m_progressLabel->setStyleSheet("color:#888;font-size:10px;");
    chipRow->addWidget(m_progressLabel);
    chipRow->addStretch();
    contentLayout->addLayout(chipRow);

    layout->addLayout(contentLayout, /*stretch=*/1);

    m_actionBtn = new QPushButton(this);
    m_actionBtn->setStyleSheet(
        "QPushButton { color:#4a8a4a; border:1px solid #2a4a2a; border-radius:3px;"
        "  padding:6px 12px; font-size:11px; background:transparent; }"
        "QPushButton:hover { background:rgba(74,138,74,0.1); }"
    );
    layout->addWidget(m_actionBtn);

    connect(m_actionBtn, &QPushButton::clicked, this, &VolumeTile::onActionClicked);
    connect(m_checkbox, &QCheckBox::toggled, this, [this](bool checked) {
        const bool shiftHeld =
            QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
        emit toggled(checked);
        emit toggledShift(checked, shiftHeld);
    });

    applyState();
}

void VolumeTile::applyState()
{
    if (!m_chipLabel || !m_actionBtn) return;

    QString chipText, chipStyle, actionText;
    switch (m_state.state) {
    case VolumeTileState::NotStarted:
        chipText  = QStringLiteral("Not started");
        chipStyle = "background:#2a2a2a;color:#aaa;";
        actionText = QStringLiteral("Download");
        m_progressLabel->clear();
        break;
    case VolumeTileState::Queued:
        chipText  = m_state.statusText.isEmpty() ? QStringLiteral("Queued") : m_state.statusText;
        chipStyle = "background:#3a3a2a;color:#e0d0a0;";
        actionText = QStringLiteral("Cancel");
        m_progressLabel->clear();
        break;
    case VolumeTileState::Downloading:
        chipText  = QStringLiteral("Downloading");
        chipStyle = "background:#2a3a5a;color:#aeb0f0;";
        actionText = QStringLiteral("Cancel");
        m_progressLabel->setText(m_state.progressPct >= 0
                                  ? QStringLiteral("%1%").arg(m_state.progressPct)
                                  : QString());
        break;
    case VolumeTileState::Complete:
        chipText  = QStringLiteral("Downloaded");
        chipStyle = "background:#2a4a2a;color:#aef0ae;";
        actionText = QStringLiteral("Open");
        m_progressLabel->clear();
        break;
    case VolumeTileState::Failed:
        chipText  = m_state.statusText.isEmpty() ? QStringLiteral("Failed") : m_state.statusText;
        chipStyle = "background:#4a2a2a;color:#f0a0a0;";
        actionText = QStringLiteral("Retry");
        m_progressLabel->clear();
        break;
    }

    m_chipLabel->setText(chipText);
    m_chipLabel->setStyleSheet(QStringLiteral(
        "padding:2px 8px;border-radius:10px;font-size:10px;%1").arg(chipStyle));
    m_actionBtn->setText(actionText);
}

} // namespace tankoban::ui::comics
