#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

// NavHistory — global back/forward stack controller owned by MainWindow.
// Single source of truth for the topbar chevrons, Alt+Left/Right keyboard
// shortcuts, and mouse buttons 4/5.
//
// Each navigable page implements INavStateProvider (see INavStateProvider.h).
// MainWindow registers an active provider via setActiveProvider() so
// NavHistory can call captureNavState() at the moment a nav event fires.
//
// Persistence: load on ctor (best-effort; corrupted file -> empty stack);
// save on flushToDisk() (called from MainWindow::closeEvent).
//
// Stack cap: kMaxEntries (100). Oldest evicted from front on push.

class INavStateProvider;

struct NavHistoryEntry {
    QString pageId;          // e.g. "videos", "stream", "tankoyomi"
    QJsonObject stateBlob;   // opaque to NavHistory; page-defined schema
    qint64 timestampMs = 0;  // QDateTime::currentMSecsSinceEpoch() at push
};

class NavHistory : public QObject {
    Q_OBJECT
public:
    static constexpr int kMaxEntries = 100;
    static constexpr int kSchemaVersion = 1;

    explicit NavHistory(QObject* parent = nullptr);
    ~NavHistory() override;

    // MainWindow notifies which page is currently active.
    // Provider may be nullptr (e.g., during teardown).
    void setActiveProvider(const QString& pageId, INavStateProvider* provider);

    // Called by MainWindow when a nav event fires (page switch or
    // detail open). Captures current page state into the current entry
    // first, then pushes a fresh entry tagged with targetPageId.
    void recordNavEvent(const QString& targetPageId);

    // User-driven Back / Forward.
    void back();
    void forward();

    // Chevron / shortcut enable state.
    bool canGoBack() const;
    bool canGoForward() const;

    // Explicit "snapshot the active page right now" — called from
    // MainWindow::closeEvent before persistence flush, so the latest
    // page state is captured even though no navigation happened.
    void captureCurrent();

    // Persistence flush. Called from MainWindow::closeEvent.
    void flushToDisk();

signals:
    // Emitted when back() / forward() shifts the cursor.
    // MainWindow listens and activates the target page + hands the
    // blob to the page's restoreNavState.
    void entryRequested(const NavHistoryEntry& entry);

    // Emitted whenever back/forward availability changes.
    void backAvailableChanged(bool available);
    void forwardAvailableChanged(bool available);

private:
    // Disk paths.
    QString persistencePath() const;
    void loadFromDisk();
    void notifyAvailability();

    QVector<NavHistoryEntry> m_stack;
    int m_cursor = -1;  // -1 = empty stack; otherwise index of current entry

    QString m_activePageId;
    INavStateProvider* m_activeProvider = nullptr;

    bool m_lastBackAvailable = false;
    bool m_lastForwardAvailable = false;
};
