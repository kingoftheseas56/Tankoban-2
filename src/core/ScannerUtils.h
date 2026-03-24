#pragma once

#include <QString>
#include <QStringList>
#include <QMap>

namespace ScannerUtils {

// Returns true if dirName is hidden or a known system/junk directory
bool isIgnoredDir(const QString& dirName);

// List immediate subdirectories of rootPath, skipping hidden/ignored dirs.
QStringList listImmediateSubdirs(const QString& rootPath);

// Recursively collect files matching nameFilters under dirPath,
// skipping ignored directories. Returns absolute file paths.
QStringList walkFiles(const QString& dirPath, const QStringList& nameFilters);

// For a list of root folders, group discovered files by first-level subdirectory.
// Files directly in a root folder are grouped under that root's path.
// Key = absolute path of series/show folder, Value = list of file paths.
QMap<QString, QStringList> groupByFirstLevelSubdir(
    const QStringList& rootFolders,
    const QStringList& nameFilters);

// Clean up a media folder name for display:
// replaces underscores/dots, strips bracket noise, removes quality markers,
// trailing release groups, trailing years. Returns original if cleanup is too aggressive.
QString cleanMediaFolderTitle(const QString& rawName);

} // namespace ScannerUtils
