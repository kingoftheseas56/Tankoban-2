#include "NavHistory.h"

#include <QDateTime>

#include "INavStateProvider.h"

NavHistory::NavHistory(QObject* parent) : QObject(parent) {
    // Task 3: loadFromDisk();
}
NavHistory::~NavHistory() = default;

void NavHistory::setActiveProvider(const QString& pageId, INavStateProvider* provider) {
    m_activePageId = pageId;
    m_activeProvider = provider;
}

void NavHistory::notifyAvailability() {
    const bool back = canGoBack();
    const bool fwd = canGoForward();
    if (back != m_lastBackAvailable) {
        m_lastBackAvailable = back;
        emit backAvailableChanged(back);
    }
    if (fwd != m_lastForwardAvailable) {
        m_lastForwardAvailable = fwd;
        emit forwardAvailableChanged(fwd);
    }
}

void NavHistory::captureCurrent() {
    if (m_cursor < 0 || m_cursor >= m_stack.size()) return;
    if (!m_activeProvider) return;
    // Capture into the current entry so a future Back restores us
    // to where the user actually was at the moment they navigated.
    m_stack[m_cursor].stateBlob = m_activeProvider->captureNavState();
}

void NavHistory::recordNavEvent(const QString& targetPageId) {
    // Step 1: snapshot the current page's state into the current entry,
    // so that if the user later Backs to here, we restore where they were.
    captureCurrent();

    // Step 2: truncate any forward entries above the cursor (browser-style).
    if (m_cursor >= 0 && m_cursor < m_stack.size() - 1) {
        m_stack.resize(m_cursor + 1);
    }

    // Step 3: evict oldest if at capacity.
    while (m_stack.size() >= kMaxEntries) {
        m_stack.removeFirst();
        if (m_cursor >= 0) m_cursor--;
    }

    // Step 4: push a fresh entry. State blob is empty for now; it will
    // be filled when this page next captures (either at next navigation
    // or via setActiveProvider's callback chain).
    NavHistoryEntry entry;
    entry.pageId = targetPageId;
    entry.timestampMs = QDateTime::currentMSecsSinceEpoch();
    m_stack.append(entry);
    m_cursor = m_stack.size() - 1;

    notifyAvailability();
}

void NavHistory::back() {
    if (!canGoBack()) return;
    // Capture current page state before shifting so the entry we're
    // leaving has the latest snapshot (round-trip integrity).
    captureCurrent();

    // Walk backward, skipping entries whose page fails to restore
    // (e.g., target showId was deleted between sessions). Stop at
    // m_cursor == 0 (stack-bottom).
    while (m_cursor > 0) {
        m_cursor--;
        const NavHistoryEntry& target = m_stack[m_cursor];
        emit entryRequested(target);
        // entryRequested handler in MainWindow activates the page and
        // calls restoreNavState. If the page's restore returns false,
        // MainWindow will call back() again to skip the stale entry.
        // We break here; the recursive skip happens via MainWindow's
        // slot, NOT inside back().
        break;
    }
    notifyAvailability();
}

void NavHistory::forward() {
    if (!canGoForward()) return;
    captureCurrent();
    while (m_cursor < m_stack.size() - 1) {
        m_cursor++;
        const NavHistoryEntry& target = m_stack[m_cursor];
        emit entryRequested(target);
        break;
    }
    notifyAvailability();
}

bool NavHistory::canGoBack() const { return m_cursor > 0; }
bool NavHistory::canGoForward() const { return m_cursor >= 0 && m_cursor < m_stack.size() - 1; }
void NavHistory::flushToDisk() { /* Task 3 */ }
QString NavHistory::persistencePath() const { return {}; /* Task 3 */ }
void NavHistory::loadFromDisk() { /* Task 3 */ }
