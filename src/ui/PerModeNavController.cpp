#include "PerModeNavController.h"

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

void PerModeNavController::pushLayer(const QString& pageId, const LayerEntry& entry) {
    m_stacks[pageId].push(entry);
    if (pageId == m_activeMode) emitBackAvailability();
}

void PerModeNavController::popLayer(const QString& pageId) {
    auto it = m_stacks.find(pageId);
    if (it == m_stacks.end() || it->isEmpty()) return;
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
