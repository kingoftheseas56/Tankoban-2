// src/core/manga/ComicsPrePivotMigrator.cpp
#include "ComicsPrePivotMigrator.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace tankoban::manga {

ComicsPrePivotMigrator::ComicsPrePivotMigrator(const QString& appDataDir)
    : m_appDataDir(appDataDir)
    , m_backupDir(appDataDir + QStringLiteral("/comics_pre_pivot_backup"))
    , m_markerFile(m_backupDir + QStringLiteral("/MIGRATED"))
{
}

bool ComicsPrePivotMigrator::hasBackup() const
{
    return QFile::exists(m_markerFile);
}

QString ComicsPrePivotMigrator::backupDir() const
{
    return m_backupDir;
}

bool ComicsPrePivotMigrator::migrate()
{
    if (QFile::exists(m_markerFile)) {
        // Already migrated.
        return false;
    }

    const QString dataDir = m_appDataDir + QStringLiteral("/data");
    const QStringList candidates {
        dataDir + QStringLiteral("/comics_library.json"),
        dataDir + QStringLiteral("/manga_downloads_index.json"),
    };

    // PHASE 7+: track partial-failure state (anyFailed alongside anyMoved)
    // and surface to Settings UI so a half-migrated state (some files moved,
    // some failed) becomes visible to the user rather than silently leaving
    // stranded JSON in the live data dir.
    bool anyMoved = false;
    for (const auto& src : candidates) {
        if (!QFile::exists(src)) continue;
        QDir().mkpath(m_backupDir);
        const QString filename = QFileInfo(src).fileName();
        const QString dst = m_backupDir + QChar('/') + filename;
        if (QFile::exists(dst)) QFile::remove(dst); // idempotency safety
        if (QFile::rename(src, dst)) {
            anyMoved = true;
            qInfo().noquote() << QStringLiteral("[ComicsPrePivotMigrator] moved")
                              << src << QStringLiteral("->") << dst;
        } else {
            qWarning().noquote() << QStringLiteral("[ComicsPrePivotMigrator] FAILED to move")
                                 << src;
        }
    }

    // Always write the marker. Even if no pre-pivot files existed (fresh
    // install on a clean machine), we want subsequent launches to skip
    // the detection logic.
    // v2: switch to QSaveFile for atomic marker write; a crash mid-write here
    // leaves a zero-byte marker that QFile::exists still treats as migrated,
    // which is correct-by-coincidence rather than correct-by-design.
    QDir().mkpath(m_backupDir);
    QFile marker(m_markerFile);
    if (marker.open(QIODevice::WriteOnly)) {
        marker.write(QStringLiteral("Migrated at %1\n")
                        .arg(QDateTime::currentDateTime().toString(Qt::ISODate)).toUtf8());
        marker.close();
    } else {
        // Failure to write the marker means the next launch will re-enter
        // migrate() and log "no-op" misleadingly (no files left to move, but
        // a prior run actually moved them). Surface so smoke evidence can
        // pick up the inconsistency rather than swallowing it silently.
        qWarning().noquote() << QStringLiteral("[ComicsPrePivotMigrator] FAILED to write marker")
                             << m_markerFile;
    }

    return anyMoved;
}

} // namespace tankoban::manga
