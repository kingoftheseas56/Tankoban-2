#pragma once

#include <QString>
#include <QStringList>
#include <QMap>
#include <atomic>

namespace ScannerUtils {

// REPO_HYGIENE Phase 4 P4.4 (2026-04-26) — cooperative cancellation token.
// Scanners pass a pointer to one of these into walkFiles / groupByFirstLevelSubdir;
// the recursive walker checks isCancelled() between directory entries and bails
// early if set. Pre-fix, a scan in flight had to run to completion even when
// the user destroyed the page or quit the app — visible as 30+ second app-close
// hang on a freshly-attached external drive with deep directory trees.
struct CancellationToken {
    std::atomic<bool> cancelled{false};
    bool isCancelled() const { return cancelled.load(std::memory_order_acquire); }
    void cancel() { cancelled.store(true, std::memory_order_release); }
};

// Maximum recursion depth for walkFiles. Caps unbounded recursion on adversarial
// directory trees (audit finding 7). Real media libraries rarely exceed depth 8;
// 32 leaves headroom for show/season/disc/episode folder layouts plus edge cases.
constexpr int kMaxWalkDepth = 32;

// Returns true if dirName is hidden or a known system/junk directory
bool isIgnoredDir(const QString& dirName);

// List immediate subdirectories of rootPath, skipping hidden/ignored dirs.
QStringList listImmediateSubdirs(const QString& rootPath);

// Recursively collect files matching nameFilters under dirPath,
// skipping ignored directories. Returns absolute file paths.
//
// REPO_HYGIENE Phase 4 P4.4 (2026-04-26): pass a non-null `cancel` token to
// allow cooperative early-exit. Pass nullptr to retain pre-fix behavior
// (no cancellation). The walker enforces depth cap kMaxWalkDepth and tracks
// canonical paths to skip symlink loops.
QStringList walkFiles(const QString& dirPath,
                      const QStringList& nameFilters,
                      const CancellationToken* cancel = nullptr);

// For a list of root folders, group discovered files by first-level subdirectory.
// Files directly in a root folder are grouped under that root's path.
// Key = absolute path of series/show folder, Value = list of file paths.
//
// REPO_HYGIENE Phase 4 P4.4 (2026-04-26): pass a non-null `cancel` token to
// allow cooperative early-exit between subdirectories.
QMap<QString, QStringList> groupByFirstLevelSubdir(
    const QStringList& rootFolders,
    const QStringList& nameFilters,
    const CancellationToken* cancel = nullptr);

// Clean up a media folder name for display:
// replaces underscores/dots, strips bracket noise, removes quality markers,
// trailing release groups, trailing years. Returns original if cleanup is too aggressive.
QString cleanMediaFolderTitle(const QString& rawName);

} // namespace ScannerUtils
