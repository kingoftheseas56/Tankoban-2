#include "NavHistory.h"

#include "INavStateProvider.h"

NavHistory::NavHistory(QObject* parent) : QObject(parent) {}
NavHistory::~NavHistory() = default;

void NavHistory::setActiveProvider(const QString& pageId, INavStateProvider* provider) {
    m_activePageId = pageId;
    m_activeProvider = provider;
}

void NavHistory::recordNavEvent(const QString&) { /* Task 2 */ }
void NavHistory::back() { /* Task 2 */ }
void NavHistory::forward() { /* Task 2 */ }
bool NavHistory::canGoBack() const { return m_cursor > 0; }
bool NavHistory::canGoForward() const { return m_cursor >= 0 && m_cursor < m_stack.size() - 1; }
void NavHistory::captureCurrent() { /* Task 2 */ }
void NavHistory::flushToDisk() { /* Task 3 */ }
QString NavHistory::persistencePath() const { return {}; /* Task 3 */ }
void NavHistory::loadFromDisk() { /* Task 3 */ }
void NavHistory::notifyAvailability() { /* Task 2 */ }
