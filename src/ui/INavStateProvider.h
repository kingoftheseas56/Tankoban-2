#pragma once

#include <QJsonObject>
#include <QString>

// INavStateProvider — implemented by every navigable page in Tankoban so
// the global NavHistory (src/ui/NavHistory.h) can snapshot and restore
// per-page state on Back / Forward.
//
// Contract:
//   captureNavState() — called BEFORE navigating away from this page.
//   Must be cheap (no I/O, no thread waits). The returned blob is
//   opaque to NavHistory; each page defines its own schema.
//
//   restoreNavState(blob) — called when Back / Forward lands on a
//   previously-captured entry for this page. Return true on success;
//   false if the blob references data that no longer exists (e.g.,
//   deleted show id). NavHistory drops failing entries and tries the
//   next one.
//
//   navStateLabel() — short human-readable tag for logging / debug.
//
// Multiple inheritance is intentional: implementing classes already
// inherit QWidget (or a subclass). INavStateProvider is a non-Q_OBJECT
// pure interface so this is C++-clean. Implementing classes MUST NOT
// add Q_OBJECT to this interface.
class INavStateProvider {
public:
    virtual ~INavStateProvider() = default;

    virtual QJsonObject captureNavState() const = 0;
    virtual bool restoreNavState(const QJsonObject& blob) = 0;
    virtual QString navStateLabel() const = 0;
};
