// src/ui/pages/comics/VolumeTile.cpp
//
// COMICS_CATALOG_SERIES_VIEW Phase 2 Task 9 (2026-05-23) — TDD green.
// Provides computeState + all linker-satisfying stubs so the 6
// VolumeTileComputeState tests pass. Full UI body (buildUi, applyState,
// action wiring) lands in Task 10.

#include "ui/pages/comics/VolumeTile.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
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
    // Full buildUi() wired in Task 10.
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

void VolumeTile::setCoverFromDisk(const QString& /*coverPath*/) { /* Task 10 */ }
void VolumeTile::setCoverFromUrl(const QString& /*url*/)        { /* Task 10 */ }

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

void VolumeTile::onActionClicked() { /* Task 10 — routes to per-state signal */ }

void VolumeTile::buildUi()    { /* Task 10 */ }
void VolumeTile::applyState() { /* Task 10 — chip + action button painting; no-op for now */ }

} // namespace tankoban::ui::comics
