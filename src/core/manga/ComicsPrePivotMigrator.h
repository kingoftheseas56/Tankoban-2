// src/core/manga/ComicsPrePivotMigrator.h
#pragma once

#include <QObject>
#include <QString>

namespace tankoban::manga {

// One-time pre-pivot to post-pivot migrator. Runs at app startup before
// MainWindow constructs ComicsPage. Idempotent: subsequent launches see
// the backup dir already exists and no-op.
//
// Migration behavior:
//   - move <appData>/data/comics_library.json -> <appData>/comics_pre_pivot_backup/
//   - move <appData>/data/manga_downloads_index.json -> same
//   - on-disk chapter folders left in place (LibraryScanner just won't see them
//     after the index wipe)
//   - manga_posters/ thumb cache preserved
//   - sidecar files (.tankoyomi-meta.json) left on disk (harmless after wipe)
//
// Detection: if `comics_pre_pivot_backup/MIGRATED` marker exists, skip.
class ComicsPrePivotMigrator
{
public:
    explicit ComicsPrePivotMigrator(const QString& appDataDir);

    // Returns true if a migration actually ran this launch (false = already
    // migrated or no pre-pivot files found).
    bool migrate();

    // Surfaces whether a backup was created at any point in the past (used
    // by Settings page to expose a 'Show backup folder' button).
    bool hasBackup() const;

    QString backupDir() const;

private:
    const QString m_appDataDir;
    const QString m_backupDir;
    const QString m_markerFile;
};

} // namespace tankoban::manga
