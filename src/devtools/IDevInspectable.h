// src/devtools/IDevInspectable.h
//
// OBS-10 / OBS-19 - Pillar 1 residue strategy (2026-06-05, Agent 5).
// Formalizes the pervasive `QJsonObject devSnapshot() const` convention into a
// minimal polymorphic interface so the generic `introspect-object` dev-bridge
// verb can reach the live state of custom-painted widgets whose data lives in
// plain C++ members that Qt's meta-object reflection cannot see (VolumeTile,
// TileCard, TileStrip, ComicReader, SeekSlider, EpisodeTile, ...).
//
// A widget adopts it by multiple-inheriting alongside its QWidget base:
//     class VolumeTile : public QFrame, public tankoban::devtools::IDevInspectable
// and overriding devSnapshot(). introspect-object recovers it via
//     dynamic_cast<const tankoban::devtools::IDevInspectable*>(qobjectPtr)
// (RTTI is on by default under MSVC /GR). The interface is intentionally NOT a
// QObject and declares NO signals/slots, so it composes cleanly with moc when a
// QObject-derived widget multiply-inherits it (QObject base stays first).

#pragma once

#include <QJsonObject>

namespace tankoban::devtools {

class IDevInspectable {
public:
    virtual ~IDevInspectable() = default;

    // Cheap, side-effect-free snapshot of the object's live state. MUST NOT
    // allocate unboundedly, trigger network/disk I/O, or mutate the object. It
    // runs on the GUI thread inside the dev-control bridge handler, so keep it
    // fast - introspection must never become the perf event it observes.
    virtual QJsonObject devSnapshot() const = 0;
};

} // namespace tankoban::devtools
