#include "PerModeNavController.h"

// PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- static-init metatype register.
// For Qt6 direct connections + QSignalSpy, Q_DECLARE_METATYPE in LayerEntry.h
// already covers the QMetaType::fromType<T>() path that signal payloads use,
// making this call effectively dead code today. Kept intentionally as
// defensive future-proofing: if any future task switches a controller signal
// to Qt::QueuedConnection (cross-thread / cross-event-loop dispatch), the
// string-keyed qRegisterMetaType becomes load-bearing -- removing it would
// produce a silent runtime failure at the connect() site. Per Task 4 code-
// quality review (2026-05-17), the redundancy is acknowledged + accepted.
namespace {
struct LayerEntryMetaRegister {
    LayerEntryMetaRegister() { qRegisterMetaType<tankoban::ui::LayerEntry>("tankoban::ui::LayerEntry"); }
} sLayerEntryMetaRegister;
}

namespace tankoban::ui {

PerModeNavController::PerModeNavController(QObject* parent) : QObject(parent) {}
PerModeNavController::~PerModeNavController() = default;

void PerModeNavController::setActiveMode(const QString& pageId) {
    if (m_activeMode == pageId) return;
    m_activeMode = pageId;
    emitBackAvailability();
}

void PerModeNavController::setRootLayer(const QString& pageId, const LayerEntry& root) {
    // NAV_BACK_ROOT_SEED 2026-05-21 (Agent 5) -- register the persistent root
    // for pageId. If the stack is currently empty (typical at app startup),
    // push the root immediately so the next pushLayer lands on top of it.
    m_roots[pageId] = root;
    auto& s = m_stacks[pageId];
    if (s.isEmpty()) s.push(root);
    if (pageId == m_activeMode) emitBackAvailability();
}

void PerModeNavController::pushLayer(const QString& pageId, const LayerEntry& entry) {
    m_stacks[pageId].push(entry);
    if (pageId == m_activeMode) emitBackAvailability();
}

void PerModeNavController::popLayer(const QString& pageId) {
    auto it = m_stacks.find(pageId);
    if (it == m_stacks.end() || it->isEmpty()) return;
    // NAV_BACK_ROOT_SEED 2026-05-21 (Agent 5) -- never pop the root layer.
    // popLayer is invoked from ComicsPage::onDetailBack / similar page-internal
    // back paths; those should pop the deep entry and leave the root intact.
    if (m_roots.contains(pageId) && it->size() == 1) return;
    it->pop();
    if (pageId == m_activeMode) emitBackAvailability();
}

LayerEntry PerModeNavController::peekBack(const QString& pageId) const {
    const auto it = m_stacks.find(pageId);
    if (it == m_stacks.end() || it->size() < 2) return {};
    return (*it)[it->size() - 2];
}

bool PerModeNavController::canGoBack(const QString& pageId) const {
    const auto it = m_stacks.find(pageId);
    return it != m_stacks.end() && it->size() >= 2;
}

void PerModeNavController::goBack(const QString& pageId) {
    auto it = m_stacks.find(pageId);
    if (it == m_stacks.end() || it->size() < 2) return;
    it->pop();                                 // remove current top
    const LayerEntry target = it->top();       // entry behind is now top
    emit layerRestoreRequested(target);
    if (pageId == m_activeMode) emitBackAvailability();
}

void PerModeNavController::resetMode(const QString& pageId) {
    auto& s = m_stacks[pageId];
    s.clear();
    // NAV_BACK_ROOT_SEED 2026-05-21 (Agent 5) -- re-seed the root if one was
    // registered for this pageId. Without this, mode-pill double-tap on
    // Comics would clear the stack and the next deep-nav push (e.g. opening
    // a series tile) would land alone, leaving Back disabled.
    auto rit = m_roots.find(pageId);
    if (rit != m_roots.end()) s.push(*rit);
    if (pageId == m_activeMode) emitBackAvailability();
}

void PerModeNavController::emitBackAvailability() {
    const bool available = canGoBack(m_activeMode);
    if (available != m_lastBackAvailable) {
        m_lastBackAvailable = available;
        emit backAvailableChanged(available);
    }
    const QString label = available ? peekBack(m_activeMode).label : QString();
    if (label != m_lastBackLabel) {
        m_lastBackLabel = label;
        emit backDestinationLabelChanged(label);
    }
}

}  // namespace tankoban::ui
