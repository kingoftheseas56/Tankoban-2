#include "PerModeNavController.h"

namespace tankoban::ui {

PerModeNavController::PerModeNavController(QObject* parent) : QObject(parent) {}
PerModeNavController::~PerModeNavController() = default;

void PerModeNavController::setActiveMode(const QString& pageId) {
    if (m_activeMode == pageId) return;
    m_activeMode = pageId;
    emitBackAvailability();
}

void PerModeNavController::pushLayer(const QString&, const LayerEntry&) {
    // skeleton -- filled in Task 3
}

void PerModeNavController::popLayer(const QString&) {
    // skeleton -- filled in Task 3
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

void PerModeNavController::goBack(const QString&) {
    // skeleton -- filled in Task 4
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
