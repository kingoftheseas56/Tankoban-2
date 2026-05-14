#include "NavHistory.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>

#include "INavStateProvider.h"

NavHistory::NavHistory(QObject* parent) : QObject(parent) {
    loadFromDisk();
    // Note: do NOT emit availability signals from the ctor — listeners
    // aren't connected yet. notifyAvailability is called after the first
    // recordNavEvent or after explicit notifyAvailability() from
    // MainWindow's setup code.
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
    // Invariant after step 2: m_stack.size() == m_cursor + 1 (or 0 when empty).
    // Since m_cursor < kMaxEntries by induction, step 3 fires at most once.
    Q_ASSERT(m_stack.size() <= kMaxEntries);

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
        // Skip-stale walk happens via MainWindow's entryRequested slot, NOT
        // here. Mirror of the back() pattern above.
        break;
    }
    notifyAvailability();
}

bool NavHistory::canGoBack() const { return m_cursor > 0; }
bool NavHistory::canGoForward() const { return m_cursor >= 0 && m_cursor < m_stack.size() - 1; }
QString NavHistory::persistencePath() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) return {};
    QDir().mkpath(dir);
    return dir + "/nav_history.json";
}

void NavHistory::loadFromDisk() {
    const QString path = persistencePath();
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return;
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;

    const QJsonObject root = doc.object();
    if (root.value("schemaVersion").toInt(0) != kSchemaVersion) return;

    const QJsonArray entries = root.value("entries").toArray();
    m_stack.clear();
    m_stack.reserve(entries.size());
    for (const QJsonValue& v : entries) {
        if (!v.isObject()) continue;
        const QJsonObject e = v.toObject();
        NavHistoryEntry entry;
        entry.pageId = e.value("pageId").toString();
        entry.stateBlob = e.value("stateBlob").toObject();
        entry.timestampMs = static_cast<qint64>(e.value("timestampMs").toDouble(0));
        if (entry.pageId.isEmpty()) continue;  // skip malformed
        m_stack.append(entry);
    }

    // Trim to cap if a future version had a higher one.
    while (m_stack.size() > kMaxEntries) m_stack.removeFirst();

    const int savedCursor = root.value("cursor").toInt(-1);
    if (savedCursor >= 0 && savedCursor < m_stack.size()) {
        m_cursor = savedCursor;
    } else if (!m_stack.isEmpty()) {
        m_cursor = m_stack.size() - 1;  // land at the newest entry
    } else {
        m_cursor = -1;
    }
}

void NavHistory::flushToDisk() {
    // Snapshot the current page's latest state so the persisted entry
    // reflects what the user was looking at right before quit.
    captureCurrent();

    const QString path = persistencePath();
    if (path.isEmpty()) return;

    QJsonArray entries;
    for (const NavHistoryEntry& e : m_stack) {
        QJsonObject obj;
        obj["pageId"] = e.pageId;
        obj["stateBlob"] = e.stateBlob;
        obj["timestampMs"] = static_cast<double>(e.timestampMs);
        entries.append(obj);
    }

    QJsonObject root;
    root["schemaVersion"] = kSchemaVersion;
    root["cursor"] = m_cursor;
    root["entries"] = entries;

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.commit();
}
