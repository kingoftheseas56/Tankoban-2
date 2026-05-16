// src/core/manga/PremiumCatalog.h
#pragma once

#include "PremiumCatalogSchema.h"

#include <QObject>
#include <QHash>
#include <QList>
#include <QString>
#include <optional>

namespace tankoban::manga::premium {

// Loads all *.json files under <catalogsDir> at construction time, merges
// them into one keyed lookup, and exposes a read-only query surface.
//
// v1 ships one bundled file under resources/manga_premium_catalogs/. The
// door-left-open multi-file merge makes dropping a second catalog later a
// zero-code-change extension.
//
// The strict validator gates every entry against the rules in brainstorm
// section 20 + section 24. Diagnostics are emitted to the result and to qDebug
// at load time; invalid entries are dropped per their severity class.
//
// Thread safety: after the constructor returns, all read methods
// (isPremiumSeries / entryForTitle / entryById / allEntries / diagnostics)
// are safe to call from any thread. There is no reload path in v1; the
// internal maps are not mutated post-ctor. Phase 3's TorrentVolumeProvider
// reads from the libtorrent alert worker thread; that is supported here.
// Q_OBJECT is retained for the planned catalogLoaded / catalogReloaded
// signals (forward-compat with brainstorm section 24 community-catalog
// gating).
class PremiumCatalog : public QObject
{
    Q_OBJECT
public:
    explicit PremiumCatalog(const QString& catalogsDir,
                            QObject*       parent = nullptr);
    ~PremiumCatalog() override;

    // Returns true if any catalog entry matches the title (primary or
    // alternate, case-insensitive). Used by ComicsTankoyomiSearchWidget for
    // tile dedup + Premium-section bucketing.
    bool isPremiumSeries(const QString& title) const;

    // Returns the matching entry if any, otherwise std::nullopt. Match is
    // case-insensitive across primary + alternate titles. If multiple entries
    // collide (should never happen for v1; the validator should reject), the
    // first-loaded wins.
    std::optional<PremiumCatalogEntry> entryForTitle(const QString& title) const;

    // Returns the entry by seriesId (exact match, case-sensitive). Used by
    // TorrentVolumeProvider when re-attaching from the request ledger.
    std::optional<PremiumCatalogEntry> entryById(const QString& seriesId) const;

    // All loaded entries. Iteration order is loader-determined (file order +
    // entry order within file). Not stable across reloads.
    QList<PremiumCatalogEntry> allEntries() const;

    // Diagnostics collected during the load. Cleared after construction; the
    // intended consumer is a startup log + (later) a developer-build UI panel.
    QList<ValidationDiagnostic> diagnostics() const;

private:
    // Lowercased title -> seriesId map for isPremiumSeries / entryForTitle.
    QHash<QString, QString>                   m_titleLookup;
    QHash<QString, PremiumCatalogEntry>       m_byId;
    QList<PremiumCatalogEntry>                m_orderedEntries;
    QList<ValidationDiagnostic>               m_diagnostics;
};

} // namespace tankoban::manga::premium
