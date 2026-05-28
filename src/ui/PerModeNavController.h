#pragma once

#include <QHash>
#include <QObject>
#include <QStack>
#include <QString>
#include "LayerEntry.h"

namespace tankoban::ui {

// PHASE 1 NAV REDESIGN 2026-05-17 (Agent 5) -- per-mode back stack
// controller. Holds one QStack<LayerEntry> per pageId. The active
// page emits enteredLayer/exitedLayer; the controller pushes/pops
// the active mode's stack. canGoBack only inspects the active mode's
// stack -- cross-mode entries cannot be reached via Back. Mode-pill
// clicks call resetMode(target) which wipes that mode's stack to
// the root layer (or empty if root has not been pushed yet).
class PerModeNavController : public QObject {
    Q_OBJECT
public:
    explicit PerModeNavController(QObject* parent = nullptr);
    ~PerModeNavController() override;

    void       setActiveMode(const QString& pageId);
    QString    activeMode() const { return m_activeMode; }

    // NAV_BACK_ROOT_SEED 2026-05-21 (Agent 5) -- register a persistent root
    // layer for pageId. The root is pushed immediately if the stack is
    // empty, and resetMode now clears down TO the root (not to []). popLayer
    // refuses to remove the root. This makes canGoBack (size >= 2) work
    // correctly when the first deep-nav push happens from the mode's root
    // landing surface (e.g. clicking a Comics library tile at startup).
    // Without a root seed the seriesView push lands alone on an empty stack
    // and the topbar Back chevron stays disabled.
    void       setRootLayer(const QString& pageId, const LayerEntry& root);

    void       pushLayer(const QString& pageId, const LayerEntry& entry);
    void       popLayer(const QString& pageId);
    LayerEntry peekBack(const QString& pageId) const;  // returns entry BEHIND the current top, or default-constructed if none
    bool       canGoBack(const QString& pageId) const;
    void       goBack(const QString& pageId);          // emits layerRestoreRequested
    void       resetMode(const QString& pageId);       // clears stack down to root (or [] if no root set)

signals:
    void layerRestoreRequested(const LayerEntry& target);
    void backAvailableChanged(bool available);
    void backDestinationLabelChanged(const QString& label);

private:
    void emitBackAvailability();

    QString                                  m_activeMode;
    QHash<QString, QStack<LayerEntry>>       m_stacks;
    QHash<QString, LayerEntry>               m_roots;  // NAV_BACK_ROOT_SEED 2026-05-21 (Agent 5)
    bool                                     m_lastBackAvailable = false;
    QString                                  m_lastBackLabel;
};

}  // namespace tankoban::ui
