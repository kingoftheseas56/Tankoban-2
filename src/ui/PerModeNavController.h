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

    void       pushLayer(const QString& pageId, const LayerEntry& entry);
    void       popLayer(const QString& pageId);
    LayerEntry peekBack(const QString& pageId) const;  // returns entry BEHIND the current top, or default-constructed if none
    bool       canGoBack(const QString& pageId) const;
    void       goBack(const QString& pageId);          // emits layerRestoreRequested
    void       resetMode(const QString& pageId);       // clears stack to []

signals:
    void layerRestoreRequested(const LayerEntry& target);
    void backAvailableChanged(bool available);
    void backDestinationLabelChanged(const QString& label);

private:
    void emitBackAvailability();

    QString                                  m_activeMode;
    QHash<QString, QStack<LayerEntry>>       m_stacks;
    bool                                     m_lastBackAvailable = false;
    QString                                  m_lastBackLabel;
};

}  // namespace tankoban::ui
