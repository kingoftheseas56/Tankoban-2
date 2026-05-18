# Tankoyomi-Premium MVP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Stremio-for-manga layer inside Comics mode: ~30 hand-curated series download from trusted-uploader nyaa torrents via libtorrent file-priority into the existing Tankoyomi/Comics flow, with WeebCentral as graceful fallback for non-catalog series and post-coverage chapters on ongoing titles.

**Architecture:** A new `PremiumCatalog` loader plus a new `TorrentVolumeProvider` orchestrator sibling to `MangaDownloader`. The provider owns a persistent request ledger, persistent staging keyed by `infoHash`, atomic `.tankoban-part` finalization, and archive validation. UI integration extends `ComicsTankoyomiSearchWidget` (Premium section + chip + dedup) and `ComicsTankoyomiDetailView` (volume-row variant + ongoing-series gap rendering). Cross-source coexistence with pre-existing folder imports is handled v1 via "adopt, do not migrate". `MangaDownloader` stays unchanged; `TorrentEngine` is consumed through its existing public surface (`addMagnet`, `setFilePriorities`, `pieceFinished`, `fileByteRangesOfHavePieces`, `flushCache`, `startTorrent`, `metadataReady`).

**Tech Stack:** C++20, Qt6 (Core / Network / Widgets), libtorrent 2.x (RC_2_0) via the existing `TorrentEngine`, MSVC 19.x via CMake + Ninja, MinGW for the sidecar (untouched). Catalog format: bundled JSON files in `resources/manga_premium_catalogs/`. Curation helper: Python 3.11 one-shot at `tools/premium_catalog_helper/`.

**Source brainstorm:** [docs/superpowers/specs/2026-05-15-tankoyomi-premium-brainstorm.md](../specs/2026-05-15-tankoyomi-premium-brainstorm.md) (774 lines, Agent 1 sections 1-16 + Codex sections 17-27 per gov-v4 Rule 20 review-and-expand pass 2026-05-15 ~3:30pm).

---

## Phase 0 - Brotherhood conventions in effect

These rules govern every task in every phase. Re-read at session start for any phase.

- **Master only.** No feature branches, no worktrees, no PR flows. Per [feedback_no_worktrees](../../../) the project is flat single-checkout on master.
- **No per-task `git commit`.** The skill's TDD template uses per-task commits; Tankoban convention overrides. Per [feedback_commit_protocol](../../../) agents flag `READY TO COMMIT` lines in `agents/chat.md` and Agent 0 batches commits via `/commit-sweep`. **One RTC line per phase**, posted at phase close. The phase's RTC line MUST follow contracts-v3 format: `READY TO COMMIT - [Agent N, <description>.] | Skills invoked: [<list>] | files: <comma-separated paths>`.
- **Smoke-first.** The `tankoban_tests` GoogleTest opt-in (per CLAUDE.md "Codex #4 Stage 3a") is not currently wired into `CMakeLists.txt`. This plan therefore does NOT bootstrap test infrastructure. Each phase verifies via `build_check.bat` (compile-only, agent-safe) plus targeted smoke against the running app where applicable. Pure-logic surfaces that would benefit from unit tests when the test scaffolding eventually lands are called out per-task as `(future unit-test target)` notes.
- **Build verification command:** `build_check.bat` from repo root. Outputs `BUILD OK` or `BUILD FAILED exit=<n>` with the last 30 lines of `cl.exe` output. Agent-safe (no GUI spawn, no exe run).
- **Sidecar untouched.** This MVP is main-app-only. `native_sidecar/` is not modified.
- **ASCII-only output.** No em-dashes (use `-` or `--`), no curly quotes, no Unicode bullets in code, comments, JSON, or chat.md announcements. Per the chat.md convention also followed by Codex.
- **No emojis in shipped code.** Per [feedback_no_color_no_emoji](../../../). UI text is plain. Source files have no decorative comments.
- **One change per build.** Each task is one focused edit followed by `build_check.bat`. Do not batch edits across tasks within a phase.
- **MCP discipline.** Smoke phases that drive the desktop claim `MCP LOCK - [Agent 1, <task>]: expecting ~X min. <scope>` in `agents/chat.md` before clicking, and post `MCP LOCK RELEASED` at smoke end per GOVERNANCE.md Rule 19. Background non-UI MCP calls (file reads, builds, `tankoctl get-state`) are unrestricted.
- **Hemanth's role.** Only after a smoke phase is set up and the lock is held by the agent does Hemanth need to be involved, and then only for the visual-quality judgment calls flagged at the end of each smoke phase. Hemanth never opens terminals, never runs `cmake`, never reads logs. The agent does all of that.
- **Codex's section 17 path corrections are normative.** The live scanner is `src/core/LibraryScanner.{h,cpp}`, NOT `src/core/scan/`. Premium staging is persistent (keyed by `infoHash`), NOT per-session. Magnet add uses the `paused=true` upload-only-mode shape (so metadata can arrive before file-priorities are set), then `startTorrent` to clear upload mode. Premium chip styling uses `Theme::current().accent`, NOT a hardcoded gold. `MangaDownloadIndex::registerChapter` is unsafe as the long-term primitive for volume cbz files; this plan adds a volume-aware path in Phase 4.

---

## File structure overview

**New files (10 main-app + 2 dev-tooling):**

| Path | Responsibility |
|------|----------------|
| `src/core/manga/PremiumCatalogSchema.h` | POD types: `PremiumCatalogEntry`, `PremiumVolumeEntry`, `PremiumChapterRef`, `CatalogLoadResult` (severity enum + invalid-entry diagnostics). |
| `src/core/manga/PremiumCatalog.h/.cpp` | JSON loader, strict validator, multi-file merge from `resources/manga_premium_catalogs/*.json`. Lookup surface: `isPremiumSeries(title)`, `entryForTitle(title)`, `entryById(seriesId)`, `allEntries()`. |
| `src/core/manga/TorrentVolumeProvider.h/.cpp` | Torrent-side volume orchestrator. Owns request ledger (`<appData>/manga_premium_requests.json`) and persistent staging (`<appData>/manga_premium_staging/<infoHash>/`). Public surface: `requestVolume`, `cancelVolume`, `pauseAll`, `resumeAll`. Signals: `volumeProgress`, `volumeCompleted`, `volumeFailed`, `swarmStatus`. |
| `src/core/manga/PremiumArchiveValidator.h/.cpp` | Pre-finalization archive validation: `.cbz` extension, archive opens, all entries are images, page count within `[1, kMaxPagesPerVolume]`, decompressed first-image bounded by `kMaxDecompressedBytes`. Returns `ArchiveValidationResult` (ok + page count, or failure reason). |
| `src/core/manga/PremiumCoverExtractor.h/.cpp` | Off-thread page-1 extraction reusing `LibraryScanner`'s `cover.*` / `folder.*` heuristic via a shared helper. Bounded decompression. Output: `manga_posters/premium_<seriesId>_v<NN>.jpg`. |
| `src/core/manga/MangaTransferCoordinator.h/.cpp` | Thin facade owned by `ComicsPage`. Fans out global pause / resume / cancel-all to both `MangaDownloader` and `TorrentVolumeProvider` so the UI has one "Transfers paused" state. |
| `src/core/manga/CanonicalChapterKey.h` | Inline helper: `canonicalChapterKey(seriesId, chapterNumber) -> QString` (format `"<seriesId>:ch_<chapter>"`). |
| `resources/manga_premium_catalogs/tankoyomi_premium_2026-05.json` | Bundled v1 catalog data. Curated via the helper tool in Phase 2. |
| `tools/premium_catalog_helper/premium_catalog_draft.py` | Python one-shot. Takes `--magnet` + `--series-id` + `--title` + optional `--mapping mapping.csv`. Resolves torrent metadata locally via libtorrent Python bindings, emits draft JSON entry skeleton. |
| `tools/premium_catalog_helper/README.md` | Per-series curation workflow guide. |

**Modified files (6):**

| Path | Change |
|------|--------|
| `src/core/manga/MangaDownloadIndex.h/.cpp` | Add `registerVolume(seriesId, vol, cbzPath, chapterIds)` primitive that safely maps one cbz path to N canonical chapter keys. Add canonical chapter key field to `Entry`. Existing `registerChapter` unchanged. |
| `src/ui/pages/comics/ComicsTankoyomiSearchWidget.h/.cpp` | Add Premium section header. Premium chip rendering. Tile dedup via `PremiumCatalog::isPremiumSeries(title)`. Empty-state suppression. |
| `src/ui/pages/comics/ComicsTankoyomiDetailView.h/.cpp` | Switch row layout based on `PremiumCatalog::entryForTitle(...)`. Volume-row variant: cover + label + chapter-range + chapter-count badge + Download/Read button + "Waiting for peers" indicator. Sticky section headers ("Volumes" + "Latest chapters (WeebCentral)"). Row filter chips (All / Downloaded / Unread / Premium / Loose). Default sort descending for ongoing series. |
| `src/ui/pages/ComicsPage.h/.cpp` | Instantiate `PremiumCatalog` + `TorrentVolumeProvider` + `MangaTransferCoordinator`. Wire signals. Implement adopt-folder flow on Add-to-Library. Continue strip canonical-key resolution. |
| `CMakeLists.txt` | Append new source files to the `add_executable` source list. |
| `src/main.cpp` | One-time directory bootstrap for `<appData>/manga_premium_staging/` and `<appData>/manga_premium_quarantine/`. |

**Untouched (consumed via existing public surface only):**

- `src/core/manga/MangaDownloader.h/.cpp`
- `src/core/torrent/TorrentEngine.h/.cpp`
- `src/core/LibraryScanner.h/.cpp` (read-only reuse of `cover.*` / `folder.*` heuristic logic by porting it into `PremiumCoverExtractor`)
- `src/core/manga/ComicsLibraryRecord.h/.cpp`
- `src/core/manga/MangaPosterCache.h/.cpp`
- `src/ui/Theme.h` (consumed via `Theme::current().accent` / `accentSoft` / `accentLine`)
- `native_sidecar/`

---

## Phase 1 - Catalog schema + loader + strict validator

**Phase goal:** Land `PremiumCatalog` and its schema as a standalone loader. After this phase, the app does NOT yet do anything visible with Premium data, but the loader runs at startup, validates any catalog files present in `resources/manga_premium_catalogs/`, and exposes lookup methods. No UI changes, no torrent code, no breakage.

**Files this phase touches:**

- Create: `src/core/manga/PremiumCatalogSchema.h`
- Create: `src/core/manga/PremiumCatalog.h`
- Create: `src/core/manga/PremiumCatalog.cpp`
- Create: `src/core/manga/CanonicalChapterKey.h`
- Modify: `CMakeLists.txt` (add new sources)
- Create: `resources/manga_premium_catalogs/_test_minimal.json` (one fake entry, used to validate the loader at startup smoke; deleted before MVP ship)

**Reference brainstorm sections:** Section 5 (catalog file shape), Section 20 (schema + validation severity), Section 24 (trust fields including `expectedInfoHash`), Section 27.4 (validation strictness recommendation).

### Task 1.1: Define the schema types

**Files:**
- Create: `src/core/manga/PremiumCatalogSchema.h`

- [ ] **Step 1.1.1: Create the header**

Write the following file:

```cpp
// src/core/manga/PremiumCatalogSchema.h
#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QVector>
#include <cstdint>

namespace tankoban::manga::premium {

// One chapter reference inside a volume. Canonical chapter key is derived
// at lookup time via CanonicalChapterKey::make(seriesId, chapterNumber).
struct PremiumChapterRef {
    QString chapterNumber;  // string to allow "12.5" half-chapters
    QString title;
};

// One volume inside a catalog entry. fileIndex / pieceStart / pieceEnd are
// captured at catalog-build time by the helper tool in Phase 2.
struct PremiumVolumeEntry {
    int                       vol           = 0;
    int                       fileIndex     = -1;     // index into the torrent's file list
    qint64                    fileSizeBytes = 0;
    int                       pieceStart    = -1;     // inclusive piece index where the file begins
    int                       pieceEnd      = -1;     // inclusive piece index where the file ends
    QString                   cbzFileName;            // the cbz's basename inside the torrent
    QString                   boundaryPolicy;         // "allow-piece-overlap" for v1
    int                       pageCount     = 0;      // catalog-provided; validator cross-checks
    QString                   coverPageHint;          // optional entryName preference for cover extraction
    QList<PremiumChapterRef>  chapters;               // chapters contained in this volume
};

// Forward-compat manifest fields, inspired by Stremio addon manifest shape
// per brainstorm section 24. Bundled-only in v1; community-catalog gating
// (signing + curator review) happens in v1.x.
struct PremiumCatalogManifest {
    QString id;             // catalog file id, e.g. "tankoyomi_premium_2026-05"
    QString name;
    QString version;        // semver string
    QString description;
    QString contact;
    bool    behaviorHintsP2P    = true;  // truthful: this is a P2P-backed source
    bool    behaviorHintsAdult  = false; // never true for v1
};

// One series entry.
struct PremiumCatalogEntry {
    QString                   seriesId;          // stable lowercase slug
    QString                   title;             // primary display title
    QStringList               alternateTitles;   // for dedup / matching
    int                       anilistId      = 0;
    QString                   status;            // "completed" | "ongoing"
    QString                   magnetUri;
    QString                   expectedInfoHash;  // bittorrent infoHash (lowercase hex);
                                                 // validator rejects mismatch
    QString                   trustedUploader;
    QString                   releaseEdition;    // e.g. "VIZ Digital", "1r0n", "Hox"
    QString                   format;            // v1 supports "one-cbz-per-volume" only
    QList<PremiumVolumeEntry> volumes;
    QString                   postCoverageWeebcentralSlug;  // empty for completed series
    int                       postCoverageStartsAfterVolume = 0;
};

// Validation severity per brainstorm section 20.
enum class ValidationSeverity {
    Warn,           // log + keep the entry usable
    RejectVolume,   // drop the offending volume from the entry; keep series
    RejectSeries,   // drop the series; keep other series in the file
    RejectFile      // drop the entire catalog file; keep other files
};

struct ValidationDiagnostic {
    QString                catalogFile;
    QString                seriesId;         // empty for file-level diagnostics
    int                    volumeNumber = 0; // 0 for non-volume diagnostics
    ValidationSeverity     severity     = ValidationSeverity::Warn;
    QString                code;             // e.g. "missing_magnet_uri", "infohash_mismatch"
    QString                message;
};

struct CatalogLoadResult {
    PremiumCatalogManifest          manifest;
    QList<PremiumCatalogEntry>      entries;
    QList<ValidationDiagnostic>     diagnostics;
};

} // namespace tankoban::manga::premium
```

- [ ] **Step 1.1.2: Compile-check the header**

Run: `build_check.bat`
Expected output: `BUILD OK`. Header is not yet referenced by any other TU, but `add_library`-style header-only validation isn't part of this project; we'll wire it up in Task 1.5 when the loader includes it.

### Task 1.2: Define the canonical chapter key helper

**Files:**
- Create: `src/core/manga/CanonicalChapterKey.h`

- [ ] **Step 1.2.1: Create the helper**

Write the following file:

```cpp
// src/core/manga/CanonicalChapterKey.h
#pragma once

#include <QString>

namespace tankoban::manga::premium {

// Stable identity for a chapter across migrations (loose WeebCentral chapter
// later receives volume attribution, etc.). See brainstorm section 23.
//
// Format: "<seriesId>:ch_<chapterNumber>"
// Examples:
//   make("one_piece", "1146")  -> "one_piece:ch_1146"
//   make("berserk",   "234.5") -> "berserk:ch_234.5"
//
// Independent of source URL, independent of file path. Whatever cbz currently
// holds chapter 1146 of One Piece, the canonical key resolves the same way.
inline QString canonicalChapterKey(const QString& seriesId,
                                   const QString& chapterNumber)
{
    return seriesId + QStringLiteral(":ch_") + chapterNumber;
}

} // namespace tankoban::manga::premium
```

### Task 1.3: Declare the PremiumCatalog class

**Files:**
- Create: `src/core/manga/PremiumCatalog.h`

- [ ] **Step 1.3.1: Create the header**

Write the following file:

```cpp
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
```

### Task 1.4: Implement the loader + strict validator

**Files:**
- Create: `src/core/manga/PremiumCatalog.cpp`

- [ ] **Step 1.4.1: Implement the loader**

Write the following file. The loader walks every `*.json` file under the directory, parses each, runs validation, and merges valid entries into the lookup tables.

```cpp
// src/core/manga/PremiumCatalog.cpp
#include "PremiumCatalog.h"
#include "CanonicalChapterKey.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QDebug>

namespace tankoban::manga::premium {

namespace {

constexpr int kMinVolumeNumber = 1;
constexpr int kMaxVolumeNumber = 999;
constexpr int kMaxPagesPerVolume = 600;     // sanity bound; validator uses this for the optional pageCount hint

// Lowercase-hex 40-char (SHA-1) infoHash check. Accept both with and without
// dashes / whitespace; reject anything else.
bool isValidInfoHash(const QString& s)
{
    static const QRegularExpression rx(QStringLiteral("^[0-9a-f]{40}$"));
    const QString clean = QString(s).remove(QChar(' ')).remove(QChar('-')).toLower();
    return rx.match(clean).hasMatch();
}

// Pull the infoHash out of a magnet URI's xt parameter (urn:btih:...).
// Returns empty QString if not parseable. Used to cross-check against the
// catalog's expectedInfoHash field.
QString infoHashFromMagnet(const QString& magnetUri)
{
    static const QRegularExpression rx(
        QStringLiteral("xt=urn:btih:([0-9A-Fa-f]{40})"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = rx.match(magnetUri);
    if (!m.hasMatch()) return {};
    return m.captured(1).toLower();
}

void appendDiagnostic(QList<ValidationDiagnostic>& out,
                      const QString& file, const QString& series, int vol,
                      ValidationSeverity sev,
                      const QString& code, const QString& message)
{
    ValidationDiagnostic d;
    d.catalogFile   = file;
    d.seriesId      = series;
    d.volumeNumber  = vol;
    d.severity      = sev;
    d.code          = code;
    d.message       = message;
    out.append(d);
    qDebug().noquote() << QStringLiteral("[PremiumCatalog]")
                       << file
                       << QStringLiteral("series=") + series
                       << QStringLiteral("vol=") + QString::number(vol)
                       << QStringLiteral("severity=") + QString::number(int(sev))
                       << code
                       << message;
}

bool parseManifest(const QJsonObject& root, PremiumCatalogManifest& out)
{
    out.id           = root.value(QStringLiteral("id")).toString();
    out.name         = root.value(QStringLiteral("name")).toString();
    out.version      = root.value(QStringLiteral("version")).toString();
    out.description  = root.value(QStringLiteral("description")).toString();
    out.contact      = root.value(QStringLiteral("contact")).toString();
    const auto hints = root.value(QStringLiteral("behaviorHints")).toObject();
    out.behaviorHintsP2P   = hints.value(QStringLiteral("p2p")).toBool(true);
    out.behaviorHintsAdult = hints.value(QStringLiteral("adult")).toBool(false);
    return !out.id.isEmpty() && !out.version.isEmpty();
}

bool parseVolume(const QJsonObject& obj, const QString& seriesId,
                 const QString& file, PremiumVolumeEntry& out,
                 QList<ValidationDiagnostic>& diag)
{
    out.vol            = obj.value(QStringLiteral("vol")).toInt(-1);
    out.fileIndex      = obj.value(QStringLiteral("fileIndex")).toInt(-1);
    out.fileSizeBytes  = static_cast<qint64>(obj.value(QStringLiteral("fileSizeBytes")).toDouble(0));
    out.pieceStart     = obj.value(QStringLiteral("pieceStart")).toInt(-1);
    out.pieceEnd       = obj.value(QStringLiteral("pieceEnd")).toInt(-1);
    out.cbzFileName    = obj.value(QStringLiteral("cbzFileName")).toString();
    out.boundaryPolicy = obj.value(QStringLiteral("boundaryPolicy")).toString(
                            QStringLiteral("allow-piece-overlap"));
    out.pageCount      = obj.value(QStringLiteral("pageCount")).toInt(0);
    out.coverPageHint  = obj.value(QStringLiteral("coverPageHint")).toString();

    const QJsonArray chapters = obj.value(QStringLiteral("chapters")).toArray();
    for (const auto& ch : chapters) {
        const QJsonObject co = ch.toObject();
        PremiumChapterRef ref;
        ref.chapterNumber = co.value(QStringLiteral("num")).toString();
        ref.title         = co.value(QStringLiteral("title")).toString();
        if (ref.chapterNumber.isEmpty()) {
            appendDiagnostic(diag, file, seriesId, out.vol,
                             ValidationSeverity::Warn, QStringLiteral("chapter_missing_num"),
                             QStringLiteral("Chapter entry missing 'num' field; dropped"));
            continue;
        }
        out.chapters.append(ref);
    }

    // Required-field checks at RejectVolume severity. Each kicks the volume
    // out of the entry but keeps the rest of the series.
    if (out.vol < kMinVolumeNumber || out.vol > kMaxVolumeNumber) {
        appendDiagnostic(diag, file, seriesId, out.vol, ValidationSeverity::RejectVolume,
                         QStringLiteral("vol_out_of_range"),
                         QStringLiteral("vol must be in [1, 999]"));
        return false;
    }
    if (out.fileIndex < 0) {
        appendDiagnostic(diag, file, seriesId, out.vol, ValidationSeverity::RejectVolume,
                         QStringLiteral("missing_fileIndex"),
                         QStringLiteral("fileIndex required"));
        return false;
    }
    if (out.fileSizeBytes <= 0) {
        appendDiagnostic(diag, file, seriesId, out.vol, ValidationSeverity::RejectVolume,
                         QStringLiteral("missing_fileSizeBytes"),
                         QStringLiteral("fileSizeBytes required and > 0"));
        return false;
    }
    if (out.pieceStart < 0 || out.pieceEnd < out.pieceStart) {
        appendDiagnostic(diag, file, seriesId, out.vol, ValidationSeverity::RejectVolume,
                         QStringLiteral("invalid_piece_range"),
                         QStringLiteral("pieceStart/pieceEnd must form a valid range"));
        return false;
    }
    if (out.cbzFileName.isEmpty() || !out.cbzFileName.endsWith(QStringLiteral(".cbz"),
                                                                Qt::CaseInsensitive)) {
        appendDiagnostic(diag, file, seriesId, out.vol, ValidationSeverity::RejectVolume,
                         QStringLiteral("invalid_cbzFileName"),
                         QStringLiteral("cbzFileName must end in .cbz"));
        return false;
    }
    if (out.pageCount > kMaxPagesPerVolume) {
        appendDiagnostic(diag, file, seriesId, out.vol, ValidationSeverity::Warn,
                         QStringLiteral("page_count_unusually_high"),
                         QStringLiteral("pageCount exceeds %1; archive validator will cross-check")
                            .arg(kMaxPagesPerVolume));
    }
    return true;
}

bool parseSeries(const QJsonObject& obj, const QString& file,
                 PremiumCatalogEntry& out, QList<ValidationDiagnostic>& diag)
{
    out.seriesId        = obj.value(QStringLiteral("seriesId")).toString();
    out.title           = obj.value(QStringLiteral("title")).toString();
    out.anilistId       = obj.value(QStringLiteral("anilistId")).toInt(0);
    out.status          = obj.value(QStringLiteral("status")).toString();
    out.magnetUri       = obj.value(QStringLiteral("magnetUri")).toString();
    out.expectedInfoHash = obj.value(QStringLiteral("expectedInfoHash")).toString().toLower();
    out.trustedUploader = obj.value(QStringLiteral("trustedUploader")).toString();
    out.releaseEdition  = obj.value(QStringLiteral("releaseEdition")).toString();
    out.format          = obj.value(QStringLiteral("format")).toString();

    for (const auto& alt : obj.value(QStringLiteral("alternateTitles")).toArray()) {
        const QString s = alt.toString();
        if (!s.isEmpty()) out.alternateTitles.append(s);
    }

    const QJsonObject pcf = obj.value(QStringLiteral("postCoverageFallback")).toObject();
    out.postCoverageWeebcentralSlug    = pcf.value(QStringLiteral("weebcentralSlug")).toString();
    out.postCoverageStartsAfterVolume  = pcf.value(QStringLiteral("startsAfterVolume")).toInt(0);

    // Required-field checks at RejectSeries severity.
    if (out.seriesId.isEmpty() || out.title.isEmpty()) {
        appendDiagnostic(diag, file, out.seriesId, 0, ValidationSeverity::RejectSeries,
                         QStringLiteral("missing_identity"),
                         QStringLiteral("seriesId and title required"));
        return false;
    }
    if (out.status != QStringLiteral("completed") && out.status != QStringLiteral("ongoing")) {
        appendDiagnostic(diag, file, out.seriesId, 0, ValidationSeverity::RejectSeries,
                         QStringLiteral("invalid_status"),
                         QStringLiteral("status must be 'completed' or 'ongoing'"));
        return false;
    }
    if (out.magnetUri.isEmpty()) {
        appendDiagnostic(diag, file, out.seriesId, 0, ValidationSeverity::RejectSeries,
                         QStringLiteral("missing_magnet_uri"),
                         QStringLiteral("magnetUri required"));
        return false;
    }
    if (!isValidInfoHash(out.expectedInfoHash)) {
        appendDiagnostic(diag, file, out.seriesId, 0, ValidationSeverity::RejectSeries,
                         QStringLiteral("invalid_expected_infohash"),
                         QStringLiteral("expectedInfoHash must be 40-char lowercase hex"));
        return false;
    }
    const QString magnetIh = infoHashFromMagnet(out.magnetUri);
    if (!magnetIh.isEmpty() && magnetIh != out.expectedInfoHash) {
        appendDiagnostic(diag, file, out.seriesId, 0, ValidationSeverity::RejectSeries,
                         QStringLiteral("infohash_mismatch"),
                         QStringLiteral("expectedInfoHash does not match magnet xt=urn:btih:..."));
        return false;
    }
    if (out.format != QStringLiteral("one-cbz-per-volume")) {
        appendDiagnostic(diag, file, out.seriesId, 0, ValidationSeverity::RejectSeries,
                         QStringLiteral("unsupported_format"),
                         QStringLiteral("v1 supports only format='one-cbz-per-volume'"));
        return false;
    }
    if (out.status == QStringLiteral("ongoing") &&
        out.postCoverageWeebcentralSlug.isEmpty()) {
        appendDiagnostic(diag, file, out.seriesId, 0, ValidationSeverity::Warn,
                         QStringLiteral("ongoing_missing_post_coverage"),
                         QStringLiteral("ongoing series should set postCoverageFallback.weebcentralSlug"));
    }

    // Volumes pass.
    const QJsonArray vols = obj.value(QStringLiteral("volumes")).toArray();
    for (const auto& v : vols) {
        PremiumVolumeEntry pv;
        if (parseVolume(v.toObject(), out.seriesId, file, pv, diag)) {
            out.volumes.append(pv);
        }
    }
    if (out.volumes.isEmpty()) {
        appendDiagnostic(diag, file, out.seriesId, 0, ValidationSeverity::RejectSeries,
                         QStringLiteral("no_valid_volumes"),
                         QStringLiteral("series has no valid volumes after validation"));
        return false;
    }
    return true;
}

bool loadOneFile(const QString& path,
                 PremiumCatalogManifest& outManifest,
                 QList<PremiumCatalogEntry>& outEntries,
                 QList<ValidationDiagnostic>& diag)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        appendDiagnostic(diag, path, {}, 0, ValidationSeverity::RejectFile,
                         QStringLiteral("file_not_readable"),
                         f.errorString());
        return false;
    }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        appendDiagnostic(diag, path, {}, 0, ValidationSeverity::RejectFile,
                         QStringLiteral("invalid_json"),
                         QStringLiteral("JSON parse: ") + perr.errorString());
        return false;
    }
    const QJsonObject root = doc.object();
    if (!parseManifest(root, outManifest)) {
        appendDiagnostic(diag, path, {}, 0, ValidationSeverity::RejectFile,
                         QStringLiteral("invalid_manifest"),
                         QStringLiteral("manifest.id and manifest.version are required"));
        return false;
    }
    const QJsonArray series = root.value(QStringLiteral("series")).toArray();
    for (const auto& s : series) {
        PremiumCatalogEntry e;
        if (parseSeries(s.toObject(), path, e, diag)) {
            outEntries.append(e);
        }
    }
    return true;
}

} // anonymous namespace

PremiumCatalog::PremiumCatalog(const QString& catalogsDir, QObject* parent)
    : QObject(parent)
{
    QDir dir(catalogsDir);
    if (!dir.exists()) {
        qDebug().noquote() << QStringLiteral("[PremiumCatalog] catalogs dir does not exist:")
                           << catalogsDir;
        return;
    }
    const QFileInfoList files = dir.entryInfoList(
        QStringList{ QStringLiteral("*.json") }, QDir::Files, QDir::Name);
    for (const auto& fi : files) {
        // Skip leading-underscore files (development fixtures like _test_minimal.json)
        if (fi.baseName().startsWith(QChar('_'))) continue;

        PremiumCatalogManifest manifest;
        QList<PremiumCatalogEntry> entries;
        if (!loadOneFile(fi.absoluteFilePath(), manifest, entries, m_diagnostics)) {
            continue;
        }
        for (auto& e : entries) {
            if (m_byId.contains(e.seriesId)) {
                appendDiagnostic(m_diagnostics, fi.absoluteFilePath(), e.seriesId, 0,
                                 ValidationSeverity::RejectSeries,
                                 QStringLiteral("duplicate_seriesId"),
                                 QStringLiteral("seriesId already loaded; skipping"));
                continue;
            }
            // Index lowercased primary title + alternates.
            m_titleLookup.insert(e.title.toLower(), e.seriesId);
            for (const auto& alt : e.alternateTitles) {
                m_titleLookup.insert(alt.toLower(), e.seriesId);
            }
            m_byId.insert(e.seriesId, e);
            m_orderedEntries.append(e);
        }
    }
    qDebug().noquote() << QStringLiteral("[PremiumCatalog] loaded")
                       << m_orderedEntries.size()
                       << QStringLiteral("series across") << files.size()
                       << QStringLiteral("file(s), with") << m_diagnostics.size()
                       << QStringLiteral("diagnostic(s)");
}

PremiumCatalog::~PremiumCatalog() = default;

bool PremiumCatalog::isPremiumSeries(const QString& title) const
{
    return m_titleLookup.contains(title.toLower());
}

std::optional<PremiumCatalogEntry>
PremiumCatalog::entryForTitle(const QString& title) const
{
    const auto it = m_titleLookup.constFind(title.toLower());
    if (it == m_titleLookup.constEnd()) return std::nullopt;
    return entryById(it.value());
}

std::optional<PremiumCatalogEntry>
PremiumCatalog::entryById(const QString& seriesId) const
{
    const auto it = m_byId.constFind(seriesId);
    if (it == m_byId.constEnd()) return std::nullopt;
    return it.value();
}

QList<PremiumCatalogEntry> PremiumCatalog::allEntries() const
{
    return m_orderedEntries;
}

QList<ValidationDiagnostic> PremiumCatalog::diagnostics() const
{
    return m_diagnostics;
}

} // namespace tankoban::manga::premium
```

- [ ] **Step 1.4.2: Wire into CMakeLists.txt**

Open `CMakeLists.txt`, locate the `add_executable(Tankoban ...)` call's source list (search for the existing `src/core/manga/MangaDownloader.cpp` entry — the new files sort alphabetically nearby). Add the following four new entries in the correct alphabetical position:

```cmake
    src/core/manga/CanonicalChapterKey.h
    src/core/manga/PremiumCatalog.cpp
    src/core/manga/PremiumCatalog.h
    src/core/manga/PremiumCatalogSchema.h
```

(Header-only files are listed alongside .cpp entries in this project's convention; check existing entries like `MangaResult.h` for the pattern.)

- [ ] **Step 1.4.3: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

If build fails: read the last 30 lines of cl.exe output, fix the compile error in `PremiumCatalog.cpp`, re-run. Do NOT continue to the next task until BUILD OK.

(Future unit-test target: the validation severity classification + infoHash regex + magnet-URI parser are pure-logic primitives worth covering when `tankoban_tests` bootstraps later.)

### Task 1.5: Instantiate the catalog at app startup (smoke wiring)

**Files:**
- Modify: `src/ui/pages/ComicsPage.h` (add member)
- Modify: `src/ui/pages/ComicsPage.cpp` (instantiate in constructor)

- [ ] **Step 1.5.1: Add the member**

In `src/ui/pages/ComicsPage.h`, locate the existing forward declarations near the top of the class (look for `class MangaDownloader;`). Add:

```cpp
namespace tankoban::manga::premium { class PremiumCatalog; }
```

In the private members section (search for `MangaDownloader* m_downloader = nullptr;`), add directly below it:

```cpp
    tankoban::manga::premium::PremiumCatalog* m_premiumCatalog = nullptr;
```

- [ ] **Step 1.5.2: Instantiate in ComicsPage constructor**

In `src/ui/pages/ComicsPage.cpp`, locate the constructor body (search for the existing `m_downloader = new MangaDownloader(...)` line). Add immediately after it:

```cpp
    const QString catalogsDir = QCoreApplication::applicationDirPath()
                              + QStringLiteral("/resources/manga_premium_catalogs");
    m_premiumCatalog = new tankoban::manga::premium::PremiumCatalog(catalogsDir, this);
```

Add the include near the top of the file (alongside existing `MangaDownloader.h` include):

```cpp
#include "core/manga/PremiumCatalog.h"
```

- [ ] **Step 1.5.3: Bootstrap the catalogs directory**

Ensure the `resources/manga_premium_catalogs/` directory exists in the source tree. Create it now (empty for now; Task 1.6 adds a test fixture):

```bash
mkdir -p resources/manga_premium_catalogs
```

If the build system copies `resources/` next to the exe at install time, this directory will appear at runtime. Verify by inspecting an existing `resources/` consumer like `resources/icons/` for the deployment pattern.

- [ ] **Step 1.5.4: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 1.6: Drop in a test-fixture catalog file + smoke-load

**Files:**
- Create: `resources/manga_premium_catalogs/_test_minimal.json` (leading underscore -> skipped by the loader; used only for manual smoke verification)
- Create: `resources/manga_premium_catalogs/_test_load.json` (no underscore -> loaded by the loader for smoke validation)

- [ ] **Step 1.6.1: Create the loaded fixture**

Write the following file. This is a single fake series with one volume and one chapter, just enough to exercise every validator path that returns success:

```json
{
  "id": "premium_smoke_2026-05",
  "name": "Premium smoke fixture",
  "version": "0.0.1-smoke",
  "description": "Phase 1 smoke fixture; delete before MVP ship.",
  "contact": "agent-1@tankoban.local",
  "behaviorHints": { "p2p": true, "adult": false },
  "series": [
    {
      "seriesId": "smoke_test_one",
      "title": "Smoke Test One",
      "alternateTitles": [],
      "anilistId": 0,
      "status": "completed",
      "magnetUri": "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&dn=Smoke",
      "expectedInfoHash": "0123456789abcdef0123456789abcdef01234567",
      "trustedUploader": "fixture",
      "releaseEdition": "fixture",
      "format": "one-cbz-per-volume",
      "volumes": [
        {
          "vol": 1,
          "fileIndex": 0,
          "fileSizeBytes": 12345678,
          "pieceStart": 0,
          "pieceEnd": 47,
          "cbzFileName": "Smoke v01 [fixture].cbz",
          "boundaryPolicy": "allow-piece-overlap",
          "pageCount": 200,
          "coverPageHint": "",
          "chapters": [
            { "num": "1", "title": "Smoke chapter" }
          ]
        }
      ],
      "postCoverageFallback": { "weebcentralSlug": "", "startsAfterVolume": 0 }
    }
  ]
}
```

- [ ] **Step 1.6.2: Create a deliberately-invalid fixture for the diagnostic path**

Write the following file. This exercises three validator failures: missing identity, infoHash format, infoHash mismatch.

```json
{
  "id": "premium_smoke_invalid_2026-05",
  "name": "Premium invalid-entry fixture",
  "version": "0.0.1-smoke",
  "description": "Phase 1 smoke fixture for the RejectSeries paths.",
  "behaviorHints": { "p2p": true },
  "series": [
    {
      "seriesId": "",
      "title": "",
      "status": "completed",
      "magnetUri": "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567",
      "expectedInfoHash": "0123456789abcdef0123456789abcdef01234567",
      "format": "one-cbz-per-volume",
      "volumes": []
    },
    {
      "seriesId": "bad_infohash",
      "title": "Bad InfoHash",
      "status": "completed",
      "magnetUri": "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567",
      "expectedInfoHash": "not-a-real-infohash",
      "format": "one-cbz-per-volume",
      "volumes": []
    },
    {
      "seriesId": "mismatch_infohash",
      "title": "Mismatch InfoHash",
      "status": "completed",
      "magnetUri": "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567",
      "expectedInfoHash": "ffffffffffffffffffffffffffffffffffffffff",
      "format": "one-cbz-per-volume",
      "volumes": []
    }
  ]
}
```

- [ ] **Step 1.6.3: Smoke-load by launching the app once**

`build_check.bat` is compile-only. To verify the loader actually parses + emits diagnostics correctly, the agent runs the full build + launch path. Run:

```cmd
build_and_run.bat
```

Wait for Tankoban to launch (the .bat does the build then runs the exe). Once the window is up, in the application's debug-output channel (which the Comics page already routes to console / `qDebug`), look for the following lines:

```
[PremiumCatalog] loaded 1 series across 2 file(s), with 4 diagnostic(s)
```

If `tankoctl` is on PATH (per CLAUDE.md it ships with `build_and_run.bat`), the agent can verify by running:

```cmd
out\tankoctl.exe logs 100
```

Expected: at least the `[PremiumCatalog] loaded ...` line appears in the recent log ring buffer.

If the line is missing OR the series count is wrong OR the diagnostic count is wrong, debug the loader.

- [ ] **Step 1.6.4: Close the app + delete the fixtures before phase close**

The loaded series `smoke_test_one` is fake. It must not survive into smoke matrix testing (Phase 10). Delete both fixtures:

```bash
rm resources/manga_premium_catalogs/_test_load.json
rm resources/manga_premium_catalogs/_test_minimal.json
```

Re-run `build_check.bat`. Expected: `BUILD OK`.

Close Tankoban:

```cmd
taskkill /F /IM Tankoban.exe
```

### Task 1.7: Phase 1 close - RTC line

- [ ] **Step 1.7.1: Append the phase-close RTC line to `agents/chat.md`**

Use `Edit` or a here-doc to append. The contracts-v3 format:

```
READY TO COMMIT - [Agent 1, TANKOYOMI_PREMIUM Phase 1 - PremiumCatalog schema + strict loader + canonical chapter key helper. New: PremiumCatalogSchema.h (POD types + ValidationSeverity enum + ValidationDiagnostic), CanonicalChapterKey.h (inline helper for stable cross-source chapter identity), PremiumCatalog.h/.cpp (door-left-open multi-file merge from resources/manga_premium_catalogs/*.json with strict per-field validation at RejectFile/RejectSeries/RejectVolume/Warn severities including expectedInfoHash format + magnet xt cross-check). Wired into ComicsPage construction. Smoke-load verified via build_and_run.bat + tankoctl logs against two fixture files (one valid + one with 3 invalid entries exercising RejectSeries paths); diagnostic count + loaded-series count both as expected. Fixtures deleted before phase close. build_check green.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/core/manga/PremiumCatalogSchema.h, src/core/manga/CanonicalChapterKey.h, src/core/manga/PremiumCatalog.h, src/core/manga/PremiumCatalog.cpp, src/ui/pages/ComicsPage.h, src/ui/pages/ComicsPage.cpp, CMakeLists.txt, agents/chat.md
```

---

## Phase 2 - Curation helper tool (Python one-shot)

**Phase goal:** Build a small developer tool that takes a magnet URI + a chapter-to-volume mapping CSV and emits a draft JSON catalog entry. Per Codex section 25 the per-series curation effort is 30-50h not 10-15h; the helper cuts the dominant chunk (capturing `fileIndex`, `fileSizeBytes`, `pieceStart`, `pieceEnd`, and the expected `infoHash` per volume) from minutes of manual file-list inspection per volume to one CLI invocation per series.

This is dev-side only. Nothing in `src/` changes in this phase. The output is consumed by Phase 10's catalog curation work.

**Files this phase touches:**

- Create: `tools/premium_catalog_helper/premium_catalog_draft.py`
- Create: `tools/premium_catalog_helper/README.md`
- Create: `tools/premium_catalog_helper/requirements.txt`
- Create: `tools/premium_catalog_helper/sample_mapping.csv`

**Reference brainstorm sections:** Section 25 (curation tooling recommendation + tool shape), Section 27.5 (resolved recommendation), Section 24 (the `expectedInfoHash` field the helper emits).

### Task 2.1: Add the requirements + sample mapping

**Files:**
- Create: `tools/premium_catalog_helper/requirements.txt`
- Create: `tools/premium_catalog_helper/sample_mapping.csv`

- [ ] **Step 2.1.1: Pin the dependencies**

Write the following:

```
# tools/premium_catalog_helper/requirements.txt
# Python 3.11+ required.
# libtorrent: official pre-built wheels are unreliable on Windows; the helper
# resolves metadata via the standard library bencode + magnet xt= parsing and
# does NOT actually join the swarm. If you want full piece-range capture for
# a torrent whose .torrent file is not available, install libtorrent locally
# via vcpkg or a conda env and pass --torrent-file <path>.
#
# For the minimal-deps path the helper only requires:
click==8.1.7
```

- [ ] **Step 2.1.2: Add a sample chapter-to-volume mapping CSV**

Write the following:

```csv
# tools/premium_catalog_helper/sample_mapping.csv
# Format: vol,chapter_num,chapter_title
# Comments allowed via leading '#'. Quote fields with commas or quotes.
1,1,"The Black Swordsman"
1,2,"The Brand"
1,3,"The Guardians of Desire (1)"
2,4,"The Guardians of Desire (2)"
2,5,"The Guardians of Desire (3)"
```

### Task 2.2: Write the helper tool

**Files:**
- Create: `tools/premium_catalog_helper/premium_catalog_draft.py`

- [ ] **Step 2.2.1: Implement the tool**

Write the following file. The tool's two operating modes:
- **`.torrent` mode:** load a local `.torrent` file (always available if the curator downloads the torrent first), parse the bencoded `info` dict, compute per-file piece ranges and `infoHash`, emit complete JSON.
- **Magnet-only mode:** parse the magnet URI's `xt=` parameter for the infoHash, but emit a partial JSON marked `"_TODO_resolve_files": true` and warn the curator to provide a `.torrent` file or run libtorrent metadata-only resolve manually.

```python
#!/usr/bin/env python3
"""
premium_catalog_draft.py - Tankoyomi-Premium catalog entry drafter.

Usage:
  premium_catalog_draft.py --torrent-file path/to/series.torrent \\
                           --series-id berserk \\
                           --title "Berserk" \\
                           --status completed \\
                           --uploader 1r0n \\
                           --release-edition "VIZ Digital" \\
                           --mapping mapping.csv \\
                           --out berserk.draft.json

  premium_catalog_draft.py --magnet "magnet:?xt=urn:btih:..." \\
                           --series-id one_piece \\
                           --title "One Piece" \\
                           --status ongoing \\
                           --post-coverage-slug one-piece \\
                           --post-coverage-after-vol 109 \\
                           --uploader 1r0n \\
                           --out one_piece.draft.json

Output is a draft entry that still needs:
  1. Hand-verified volume/file mapping (the helper guesses from filename
     patterns like "Berserk v01 [...].cbz" but cannot always tell).
  2. Optional pageCount values per volume (run pagecount-probe later).
  3. Optional coverPageHint per volume (defaults to empty -> first-image
     fallback at runtime).

Never auto-commits. The curator reviews + edits the draft before merging into
the bundled catalog file.

Per Codex brainstorm section 25 + section 27.5 the dominant per-series cost
is file-mapping verification; this helper eliminates the data-entry portion
of that cost without removing the verification step.
"""
from __future__ import annotations

import argparse
import csv
import dataclasses
import hashlib
import json
import re
import sys
import urllib.parse
from pathlib import Path
from typing import Iterable

# --------------------------------------------------------------------------- #
# Bencode decoder. Self-contained, no external deps. Just enough to parse a
# .torrent's info dict and compute infoHash. Based on the BEP-3 spec.
# --------------------------------------------------------------------------- #

class BencodeError(Exception):
    pass

def _decode(data: bytes, pos: int):
    if pos >= len(data):
        raise BencodeError("unexpected EOF")
    c = data[pos:pos+1]
    if c.isdigit():
        colon = data.index(b":", pos)
        length = int(data[pos:colon])
        start = colon + 1
        end = start + length
        return data[start:end], end
    if c == b"i":
        end = data.index(b"e", pos)
        return int(data[pos+1:end]), end + 1
    if c == b"l":
        out = []
        pos += 1
        while data[pos:pos+1] != b"e":
            value, pos = _decode(data, pos)
            out.append(value)
        return out, pos + 1
    if c == b"d":
        out = {}
        pos += 1
        while data[pos:pos+1] != b"e":
            key, pos = _decode(data, pos)
            value, pos = _decode(data, pos)
            out[key] = value
        return out, pos + 1
    raise BencodeError(f"unexpected byte at {pos}: {c!r}")

def bdecode(data: bytes):
    value, end = _decode(data, 0)
    if end != len(data):
        raise BencodeError("trailing bytes after decode")
    return value

def _encode(value, out: bytearray):
    if isinstance(value, int):
        out.extend(f"i{value}e".encode())
    elif isinstance(value, bytes):
        out.extend(f"{len(value)}:".encode())
        out.extend(value)
    elif isinstance(value, str):
        b = value.encode("utf-8")
        out.extend(f"{len(b)}:".encode())
        out.extend(b)
    elif isinstance(value, list):
        out.append(ord("l"))
        for v in value:
            _encode(v, out)
        out.append(ord("e"))
    elif isinstance(value, dict):
        out.append(ord("d"))
        # Keys MUST be sorted lexicographically per BEP-3.
        for k in sorted(value.keys()):
            if isinstance(k, str):
                kb = k.encode("utf-8")
            else:
                kb = k
            out.extend(f"{len(kb)}:".encode())
            out.extend(kb)
            _encode(value[k], out)
        out.append(ord("e"))
    else:
        raise BencodeError(f"cannot encode {type(value)}")

def bencode(value) -> bytes:
    out = bytearray()
    _encode(value, out)
    return bytes(out)

# --------------------------------------------------------------------------- #
# Torrent parsing.
# --------------------------------------------------------------------------- #

@dataclasses.dataclass
class TorrentFile:
    index: int
    path: str       # e.g. "Berserk v01 [1r0n].cbz" (joined from path segments)
    size: int       # bytes
    piece_start: int
    piece_end: int  # inclusive

@dataclasses.dataclass
class TorrentMeta:
    info_hash: str  # 40-char lowercase hex
    name: str
    piece_length: int
    files: list[TorrentFile]
    creation_date: int | None

def _bytes_to_str(v) -> str:
    if isinstance(v, bytes):
        try:
            return v.decode("utf-8")
        except UnicodeDecodeError:
            return v.decode("latin1", errors="replace")
    return str(v)

def parse_torrent(path: Path) -> TorrentMeta:
    raw = path.read_bytes()
    root = bdecode(raw)
    if not isinstance(root, dict) or b"info" not in root:
        raise BencodeError("not a torrent: missing 'info' dict")
    info = root[b"info"]
    info_hash = hashlib.sha1(bencode(info)).hexdigest()
    name = _bytes_to_str(info.get(b"name", b""))
    piece_length = int(info[b"piece length"])

    files: list[TorrentFile] = []
    if b"files" in info:
        # Multi-file torrent (the common case for series packs).
        offset = 0
        for idx, f in enumerate(info[b"files"]):
            size = int(f[b"length"])
            segments = [_bytes_to_str(s) for s in f[b"path"]]
            file_path = "/".join(segments)
            piece_start = offset // piece_length
            piece_end = (offset + size - 1) // piece_length
            files.append(TorrentFile(idx, file_path, size, piece_start, piece_end))
            offset += size
    else:
        # Single-file torrent (uncommon for series packs).
        size = int(info[b"length"])
        files.append(TorrentFile(0, name, size, 0, max(0, (size - 1) // piece_length)))

    creation_date = int(root[b"creation date"]) if b"creation date" in root else None
    return TorrentMeta(info_hash=info_hash, name=name, piece_length=piece_length,
                       files=files, creation_date=creation_date)

# --------------------------------------------------------------------------- #
# Volume guesser. Tries to extract "vNN" or "Vol NN" from a filename.
# --------------------------------------------------------------------------- #

_VOL_PATTERNS = [
    re.compile(r"\bv(\d{1,3})\b", re.IGNORECASE),
    re.compile(r"\bvol(?:ume)?\s*\.?\s*(\d{1,3})\b", re.IGNORECASE),
    re.compile(r"\s-\s*(\d{1,3})\s*-", re.IGNORECASE),
]

def guess_volume(filename: str) -> int | None:
    for rx in _VOL_PATTERNS:
        m = rx.search(filename)
        if m:
            try:
                return int(m.group(1))
            except ValueError:
                continue
    return None

# --------------------------------------------------------------------------- #
# Mapping CSV loader.
# --------------------------------------------------------------------------- #

@dataclasses.dataclass
class ChapterMapping:
    vol: int
    chapter_num: str
    chapter_title: str

def load_mapping(path: Path) -> dict[int, list[ChapterMapping]]:
    out: dict[int, list[ChapterMapping]] = {}
    with path.open("r", encoding="utf-8") as fh:
        reader = csv.reader(fh)
        for row in reader:
            if not row or row[0].startswith("#"):
                continue
            if len(row) < 3:
                continue
            try:
                vol = int(row[0])
            except ValueError:
                continue
            chapter_num = row[1].strip()
            chapter_title = row[2].strip().strip('"')
            out.setdefault(vol, []).append(
                ChapterMapping(vol, chapter_num, chapter_title))
    return out

# --------------------------------------------------------------------------- #
# Magnet parsing.
# --------------------------------------------------------------------------- #

def info_hash_from_magnet(magnet: str) -> str:
    parsed = urllib.parse.urlparse(magnet)
    qs = urllib.parse.parse_qs(parsed.query)
    for xt in qs.get("xt", []):
        if xt.lower().startswith("urn:btih:"):
            ih = xt.split(":")[-1]
            if len(ih) == 40 and all(c in "0123456789abcdefABCDEF" for c in ih):
                return ih.lower()
    raise SystemExit("Could not extract a 40-char hex infoHash from magnet xt= parameter.")

# --------------------------------------------------------------------------- #
# Draft builder.
# --------------------------------------------------------------------------- #

def build_draft(series_id: str, title: str, status: str,
                magnet: str, info_hash: str, uploader: str, release_edition: str,
                files: list[TorrentFile] | None,
                mapping: dict[int, list[ChapterMapping]],
                post_coverage_slug: str, post_coverage_after_vol: int) -> dict:
    volumes_out = []
    if files is not None:
        for f in files:
            if not f.path.lower().endswith(".cbz"):
                # Helper flags non-cbz entries; the curator can include or
                # drop them. Real catalogs reject anything but cbz.
                volumes_out.append({
                    "_warning": f"file index {f.index} is not .cbz: {f.path}",
                    "fileIndex": f.index,
                    "fileSizeBytes": f.size,
                    "pieceStart": f.piece_start,
                    "pieceEnd": f.piece_end,
                    "cbzFileName": f.path.split("/")[-1],
                })
                continue
            vol = guess_volume(f.path)
            chapters = []
            if vol is not None and vol in mapping:
                chapters = [{"num": c.chapter_num, "title": c.chapter_title}
                            for c in mapping[vol]]
            volumes_out.append({
                "vol": vol if vol is not None else 0,
                "fileIndex": f.index,
                "fileSizeBytes": f.size,
                "pieceStart": f.piece_start,
                "pieceEnd": f.piece_end,
                "cbzFileName": f.path.split("/")[-1],
                "boundaryPolicy": "allow-piece-overlap",
                "pageCount": 0,
                "coverPageHint": "",
                "chapters": chapters,
                "_needs_review": vol is None,
            })
    else:
        volumes_out.append({
            "_TODO_resolve_files": True,
            "_note": "Magnet-only mode: re-run with --torrent-file to populate volumes."
        })

    return {
        "seriesId": series_id,
        "title": title,
        "alternateTitles": [],
        "anilistId": 0,
        "status": status,
        "magnetUri": magnet,
        "expectedInfoHash": info_hash,
        "trustedUploader": uploader,
        "releaseEdition": release_edition,
        "format": "one-cbz-per-volume",
        "volumes": volumes_out,
        "postCoverageFallback": {
            "weebcentralSlug": post_coverage_slug,
            "startsAfterVolume": post_coverage_after_vol,
        },
    }

# --------------------------------------------------------------------------- #
# CLI.
# --------------------------------------------------------------------------- #

def main() -> int:
    ap = argparse.ArgumentParser(description="Tankoyomi-Premium catalog entry drafter.")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--torrent-file", type=Path, help="Path to a .torrent file (preferred).")
    src.add_argument("--magnet", type=str, help="magnet:?xt=urn:btih:... URI (partial draft only).")

    ap.add_argument("--series-id", required=True)
    ap.add_argument("--title", required=True)
    ap.add_argument("--status", choices=["completed", "ongoing"], required=True)
    ap.add_argument("--uploader", required=True)
    ap.add_argument("--release-edition", default="")
    ap.add_argument("--mapping", type=Path, help="CSV: vol,chapter_num,chapter_title")
    ap.add_argument("--post-coverage-slug", default="", help="WeebCentral slug for chapters past coverage.")
    ap.add_argument("--post-coverage-after-vol", type=int, default=0)
    ap.add_argument("--out", type=Path, required=True)

    args = ap.parse_args()

    if args.torrent_file is not None:
        meta = parse_torrent(args.torrent_file)
        info_hash = meta.info_hash
        # If user didn't pass a magnet but we have a torrent, synthesize a minimal
        # magnet for the catalog. The runtime needs the xt portion only.
        synthesized_magnet = f"magnet:?xt=urn:btih:{info_hash}&dn={urllib.parse.quote(meta.name)}"
        magnet = synthesized_magnet
        files = meta.files
    else:
        info_hash = info_hash_from_magnet(args.magnet)
        magnet = args.magnet
        files = None

    mapping = load_mapping(args.mapping) if args.mapping is not None else {}

    draft = build_draft(
        series_id=args.series_id,
        title=args.title,
        status=args.status,
        magnet=magnet,
        info_hash=info_hash,
        uploader=args.uploader,
        release_edition=args.release_edition,
        files=files,
        mapping=mapping,
        post_coverage_slug=args.post_coverage_slug,
        post_coverage_after_vol=args.post_coverage_after_vol,
    )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(draft, indent=2, ensure_ascii=False), encoding="utf-8")

    print(f"Wrote draft: {args.out}")
    print(f"  infoHash: {info_hash}")
    if files is not None:
        print(f"  files: {len(files)} ({sum(1 for f in files if f.path.endswith('.cbz'))} cbz)")
        # Surface volumes that need review.
        for v in draft["volumes"]:
            if v.get("_needs_review"):
                print(f"  REVIEW: file {v['fileIndex']} '{v['cbzFileName']}' - could not guess volume number")
            if "_warning" in v:
                print(f"  WARN:   {v['_warning']}")
    else:
        print("  files: 0 (magnet-only mode; re-run with --torrent-file for full draft)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2.2.2: Spot-test the tool against a synthetic .torrent**

A real curator workflow needs a real .torrent file, but for verification we can hand-build a tiny synthetic torrent. Create a temporary fixture file at `tools/premium_catalog_helper/_smoke_synth.py`:

```python
# Synthetic torrent fixture for helper smoke-test. Run once; the script writes
# a fake .torrent file the main helper can parse. Not shipped to the catalog.
import hashlib, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from premium_catalog_draft import bencode

info = {
    "name": b"Berserk [1r0n]",
    "piece length": 262144,
    "pieces": b"\x00" * 20 * 3,  # 3 pieces, fake hashes
    "files": [
        {"length": 200000, "path": [b"Berserk v01 [1r0n].cbz"]},
        {"length": 300000, "path": [b"Berserk v02 [1r0n].cbz"]},
        {"length": 100000, "path": [b"Berserk v03 [1r0n].cbz"]},
    ],
}
root = {b"info": info, b"creation date": 1700000000}
Path("_synth_berserk.torrent").write_bytes(bencode(root))
print("wrote _synth_berserk.torrent, infoHash =",
      hashlib.sha1(bencode(info)).hexdigest())
```

Then run:

```cmd
cd tools\premium_catalog_helper
python _smoke_synth.py
python premium_catalog_draft.py --torrent-file _synth_berserk.torrent --series-id berserk --title "Berserk" --status completed --uploader 1r0n --mapping sample_mapping.csv --out _smoke_berserk.draft.json
```

Expected output (the actual infoHash is deterministic from the bencode):

```
wrote _synth_berserk.torrent, infoHash = <some 40-char hex>
Wrote draft: _smoke_berserk.draft.json
  infoHash: <same 40-char hex>
  files: 3 (3 cbz)
```

Open `_smoke_berserk.draft.json` and verify:
- `expectedInfoHash` matches the printed hash
- `volumes` has 3 entries with `vol: 1`, `vol: 2`, `vol: 3` (the regex guessed correctly)
- volume 1's `chapters` array has 3 entries from `sample_mapping.csv`
- volume 2's `chapters` array has 2 entries
- volume 3's `chapters` array is empty (the sample CSV doesn't have vol 3)

- [ ] **Step 2.2.3: Clean up smoke artifacts**

```cmd
del tools\premium_catalog_helper\_smoke_synth.py
del tools\premium_catalog_helper\_synth_berserk.torrent
del tools\premium_catalog_helper\_smoke_berserk.draft.json
```

### Task 2.3: Write the README

**Files:**
- Create: `tools/premium_catalog_helper/README.md`

- [ ] **Step 2.3.1: Document the per-series workflow**

Write the following file:

```markdown
# Tankoyomi-Premium catalog helper

Dev-only tool. Drafts one catalog entry per series from a `.torrent` file plus a chapter-to-volume mapping CSV. The draft is reviewed by hand before it lands in the bundled catalog at `resources/manga_premium_catalogs/`.

## Why this tool exists

Per Codex's section 25 review of the Phase 1 brainstorm, hand-curating 30 series end-to-end was estimated at 30-50 hours when done entirely manually. The dominant cost was per-volume file-mapping (capturing `fileIndex`, `fileSizeBytes`, `pieceStart`, `pieceEnd` for every cbz in the torrent). This tool eliminates that data-entry portion in seconds per series, while preserving the human verification step.

## Per-series workflow

For each series in the v1 catalog corpus (see brainstorm section 6 for the list of 30):

1. **Download the `.torrent` file** from nyaa.si for your trusted uploader (1r0n, VIZ Digital, Hox, Danke, etc). Save it locally.
2. **Build the chapter-to-volume mapping CSV.** Source: mangareader.to volume index pages, AniList volumes endpoint, MangaUpdates, or community wikis. Validate that the chapter range listed on the mangareader.to vol N page matches the volume's table of contents.
3. **Run the helper.** Example for Berserk:
   ```
   python premium_catalog_draft.py \\
     --torrent-file "C:/curation/berserk_1r0n.torrent" \\
     --series-id berserk \\
     --title "Berserk" \\
     --status completed \\
     --uploader 1r0n \\
     --release-edition "VIZ Digital" \\
     --mapping berserk_mapping.csv \\
     --out drafts/berserk.draft.json
   ```
4. **Open the draft and review.** Confirm:
   - Every `vol` was guessed correctly (look for `_needs_review: true` flags).
   - Non-cbz entries (rar samples, scanlator credits txt) are excluded or commented out.
   - The `chapters` array under each volume matches the volume's actual chapter range.
5. **Add optional fields if available:** `alternateTitles`, `anilistId`, `coverPageHint`.
6. **Set `postCoverageFallback`** for ongoing series only:
   ```json
   "postCoverageFallback": {
     "weebcentralSlug": "berserk",
     "startsAfterVolume": 42
   }
   ```
7. **Merge into the bundled catalog file** at `resources/manga_premium_catalogs/tankoyomi_premium_2026-05.json`.
8. **Run the app and watch the loader's diagnostic output** (Phase 1 logs to `qDebug` and to the app's ring buffer accessible via `tankoctl logs`). Any validator rejection means the entry needs fixing.

## Two operating modes

**`.torrent` mode** (preferred):
- Resolves `infoHash` deterministically from the bencoded info dict.
- Computes per-file piece ranges precisely.
- Guesses volume numbers from filename patterns (`v01`, `Vol 1`, etc).
- Emits a complete draft entry.

**Magnet-only mode**:
- Extracts `infoHash` from the magnet URI's `xt=urn:btih:` parameter.
- Emits a partial draft with `_TODO_resolve_files: true`. Volumes must be filled in by hand OR by re-running with `--torrent-file` once the .torrent is obtained.

## Why no runtime mangareader scraper?

Per mangareader.to's terms of use (https://mangareader.to/terms): access is for temporary personal non-commercial viewing; copying and modifying materials is prohibited. The helper is local, rate-unlimited because it doesn't even talk to mangareader.to, and consumes a hand-prepared CSV. The Tankoban app itself never talks to mangareader.to at runtime. The catalog is the only artifact that ships.

## Why no live nyaa.si search either?

v1 is bundled-only. Live nyaa search is explicitly out-of-scope per brainstorm section 8. The catalog's `expectedInfoHash` field is the trust identity (per Codex section 24); the uploader name is informational only.
```

### Task 2.4: Phase 2 close - RTC line

- [ ] **Step 2.4.1: Append the phase-close RTC line to `agents/chat.md`**

```
READY TO COMMIT - [Agent 1, TANKOYOMI_PREMIUM Phase 2 - Curation helper tool (Python one-shot, dev-side). New: tools/premium_catalog_helper/{premium_catalog_draft.py, README.md, requirements.txt, sample_mapping.csv}. Two operating modes: .torrent-file mode (deterministic infoHash + per-file piece-range capture + volume-number guess from filename) and magnet-only mode (partial draft, infoHash from xt= parameter, volumes flagged for manual fill). Self-contained bencode decoder + sha1 infoHash + piece-offset math (no external libtorrent dep). Volume guesser handles "v01" / "Vol 1" / "- 01 -" patterns. Mapping CSV loader maps chapters to volumes. Smoke-validated against a hand-built synthetic 3-file torrent and the sample mapping CSV; infoHash matched, per-volume piece ranges + chapters all correct. No src/ touches. Per Codex section 25 this is the recommended-build-first prerequisite for the 30-50h hand-curation work in Phase 10.] | Skills invoked: [/superpowers:executing-plans, /superpowers:verification-before-completion] | files: tools/premium_catalog_helper/premium_catalog_draft.py, tools/premium_catalog_helper/README.md, tools/premium_catalog_helper/requirements.txt, tools/premium_catalog_helper/sample_mapping.csv, agents/chat.md
```

---

## Phase 3 - TorrentVolumeProvider core (request ledger + staging + metadata round-trip + event-driven completion)

**Phase goal:** Build the volume-orchestration service. After this phase, the provider can be told `requestVolume(entry, vol, dest)` and will: add the magnet in paused-upload-only mode, wait for `metadataReady`, set strict single-vol file priorities, call `startTorrent` to clear upload-only mode, listen for `pieceFinished` events, and emit `volumeCompleted` once `fileByteRangesOfHavePieces` covers the full target file. The persistent request ledger handles crash-resume. NO UI integration yet; verification is via a temporary test-driver in `ComicsPage` that requests one volume from the Phase 1 fixture catalog at app start.

**Files this phase touches:**

- Create: `src/core/manga/TorrentVolumeProvider.h`
- Create: `src/core/manga/TorrentVolumeProvider.cpp`
- Create: `src/core/manga/TorrentRequestLedger.h`
- Create: `src/core/manga/TorrentRequestLedger.cpp`
- Modify: `src/main.cpp` (bootstrap `<appData>/manga_premium_staging/` + `<appData>/manga_premium_quarantine/` directories at app start)
- Modify: `src/ui/pages/ComicsPage.h` (add `TorrentVolumeProvider*` member)
- Modify: `src/ui/pages/ComicsPage.cpp` (instantiate, hook up signals to qDebug for now)
- Modify: `CMakeLists.txt` (add new sources)

**Reference brainstorm sections:** Section 17.2-17.3 (staging correction + magnet add correction), Section 18 (architecture + concurrency contract), Section 19 (libtorrent file-to-piece behavior + event-driven completion pattern), Section 27.1 (file-completion signal recommendation), Section 27.3 (persistent staging recommendation).

### Task 3.1: Define the request ledger schema + persistence

**Files:**
- Create: `src/core/manga/TorrentRequestLedger.h`
- Create: `src/core/manga/TorrentRequestLedger.cpp`

- [ ] **Step 3.1.1: Declare the ledger header**

Write the following file:

```cpp
// src/core/manga/TorrentRequestLedger.h
#pragma once

#include <QObject>
#include <QString>
#include <QHash>
#include <QList>
#include <QMutex>
#include <chrono>
#include <optional>

namespace tankoban::manga::premium {

// Per-request state. Persisted to <appData>/manga_premium_requests.json.
// Each request is keyed by (catalogId, seriesId, volumeNumber); the JSON
// stores them as an array where each entry carries the full triple plus the
// state fields. Crash-resume loads this on startup and replays each
// row's intent against the live PremiumCatalog + TorrentEngine.
struct TorrentRequest {
    enum class Status {
        Pending,            // freshly created; awaiting metadata + priorities
        AwaitingMetadata,   // magnet added (upload-only); waiting for metadataReady
        Downloading,        // priorities set; bytes flowing
        Validating,         // bytes done; archive validation in progress
        Completed,          // cbz finalized at canonical path; row stays for audit until garbage-collected
        Failed,             // validation failed OR engine error; surfaced to UI
        Cancelled,          // user-cancelled before completion
        CatalogMissing      // restart found the request but the catalog entry vanished; recoverable
    };

    QString    catalogId;                  // matches PremiumCatalogManifest::id
    QString    seriesId;
    int        volumeNumber       = 0;
    QString    expectedInfoHash;           // lowercase 40-char hex; trust identity
    QString    magnetUri;                  // captured at create time for resume
    int        fileIndex          = -1;
    QString    cbzFileName;
    qint64     fileSizeBytes      = 0;
    int        pieceStart         = -1;
    int        pieceEnd           = -1;
    QString    stagingPath;                // <appData>/manga_premium_staging/<infoHash>/
    QString    canonicalDestinationPath;   // <seriesFolder>/<cbzFileName>.cbz (the final resting place)
    Status     status             = Status::Pending;
    qint64     createdAtMsEpoch   = 0;
    qint64     updatedAtMsEpoch   = 0;
    QString    lastErrorCode;              // populated on Failed; empty otherwise
    QString    lastErrorMessage;
};

inline QString requestKey(const QString& catalogId, const QString& seriesId, int vol)
{
    return catalogId + QChar('/') + seriesId + QChar('/') + QString::number(vol);
}

// Thread-safe JSON-backed ledger. All mutations save the file synchronously
// before returning. The save happens off-lock to keep callers responsive.
class TorrentRequestLedger : public QObject
{
    Q_OBJECT
public:
    explicit TorrentRequestLedger(const QString& filePath, QObject* parent = nullptr);
    ~TorrentRequestLedger() override;

    // Load from disk. Called once at construction; safe to re-call.
    void load();

    // Save to disk. Called after every mutation. Atomic write via .part rename.
    void save();

    // Mutations. Each calls save() after returning.
    void upsert(const TorrentRequest& req);
    void updateStatus(const QString& key,
                      TorrentRequest::Status newStatus,
                      const QString& errorCode = QString(),
                      const QString& errorMessage = QString());
    void remove(const QString& key);

    // Read access (returns copies; safe from any thread).
    std::optional<TorrentRequest> find(const QString& key) const;
    QList<TorrentRequest>         all() const;
    QList<TorrentRequest>         findByInfoHash(const QString& infoHash) const;
    QList<TorrentRequest>         findByStatus(TorrentRequest::Status status) const;

signals:
    // Emitted off-lock after a successful save. UI receivers should connect queued.
    void ledgerChanged();

private:
    void saveLocked();

    QString                              m_filePath;
    mutable QMutex                       m_mutex;
    QHash<QString, TorrentRequest>       m_byKey;
};

} // namespace tankoban::manga::premium
```

- [ ] **Step 3.1.2: Implement the ledger**

Write the following file:

```cpp
// src/core/manga/TorrentRequestLedger.cpp
#include "TorrentRequestLedger.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QDateTime>
#include <QDebug>

namespace tankoban::manga::premium {

namespace {

QString statusToString(TorrentRequest::Status s)
{
    switch (s) {
        case TorrentRequest::Status::Pending:           return QStringLiteral("pending");
        case TorrentRequest::Status::AwaitingMetadata:  return QStringLiteral("awaiting_metadata");
        case TorrentRequest::Status::Downloading:       return QStringLiteral("downloading");
        case TorrentRequest::Status::Validating:        return QStringLiteral("validating");
        case TorrentRequest::Status::Completed:         return QStringLiteral("completed");
        case TorrentRequest::Status::Failed:            return QStringLiteral("failed");
        case TorrentRequest::Status::Cancelled:         return QStringLiteral("cancelled");
        case TorrentRequest::Status::CatalogMissing:    return QStringLiteral("catalog_missing");
    }
    return QStringLiteral("pending");
}

TorrentRequest::Status statusFromString(const QString& s)
{
    if (s == QStringLiteral("awaiting_metadata")) return TorrentRequest::Status::AwaitingMetadata;
    if (s == QStringLiteral("downloading"))       return TorrentRequest::Status::Downloading;
    if (s == QStringLiteral("validating"))        return TorrentRequest::Status::Validating;
    if (s == QStringLiteral("completed"))         return TorrentRequest::Status::Completed;
    if (s == QStringLiteral("failed"))            return TorrentRequest::Status::Failed;
    if (s == QStringLiteral("cancelled"))         return TorrentRequest::Status::Cancelled;
    if (s == QStringLiteral("catalog_missing"))   return TorrentRequest::Status::CatalogMissing;
    return TorrentRequest::Status::Pending;
}

QJsonObject toJson(const TorrentRequest& r)
{
    QJsonObject o;
    o[QStringLiteral("catalogId")]                = r.catalogId;
    o[QStringLiteral("seriesId")]                 = r.seriesId;
    o[QStringLiteral("volumeNumber")]             = r.volumeNumber;
    o[QStringLiteral("expectedInfoHash")]         = r.expectedInfoHash;
    o[QStringLiteral("magnetUri")]                = r.magnetUri;
    o[QStringLiteral("fileIndex")]                = r.fileIndex;
    o[QStringLiteral("cbzFileName")]              = r.cbzFileName;
    o[QStringLiteral("fileSizeBytes")]            = static_cast<double>(r.fileSizeBytes);
    o[QStringLiteral("pieceStart")]               = r.pieceStart;
    o[QStringLiteral("pieceEnd")]                 = r.pieceEnd;
    o[QStringLiteral("stagingPath")]              = r.stagingPath;
    o[QStringLiteral("canonicalDestinationPath")] = r.canonicalDestinationPath;
    o[QStringLiteral("status")]                   = statusToString(r.status);
    o[QStringLiteral("createdAtMsEpoch")]         = static_cast<double>(r.createdAtMsEpoch);
    o[QStringLiteral("updatedAtMsEpoch")]         = static_cast<double>(r.updatedAtMsEpoch);
    o[QStringLiteral("lastErrorCode")]            = r.lastErrorCode;
    o[QStringLiteral("lastErrorMessage")]         = r.lastErrorMessage;
    return o;
}

TorrentRequest fromJson(const QJsonObject& o)
{
    TorrentRequest r;
    r.catalogId                = o.value(QStringLiteral("catalogId")).toString();
    r.seriesId                 = o.value(QStringLiteral("seriesId")).toString();
    r.volumeNumber             = o.value(QStringLiteral("volumeNumber")).toInt();
    r.expectedInfoHash         = o.value(QStringLiteral("expectedInfoHash")).toString().toLower();
    r.magnetUri                = o.value(QStringLiteral("magnetUri")).toString();
    r.fileIndex                = o.value(QStringLiteral("fileIndex")).toInt(-1);
    r.cbzFileName              = o.value(QStringLiteral("cbzFileName")).toString();
    r.fileSizeBytes            = static_cast<qint64>(o.value(QStringLiteral("fileSizeBytes")).toDouble());
    r.pieceStart               = o.value(QStringLiteral("pieceStart")).toInt(-1);
    r.pieceEnd                 = o.value(QStringLiteral("pieceEnd")).toInt(-1);
    r.stagingPath              = o.value(QStringLiteral("stagingPath")).toString();
    r.canonicalDestinationPath = o.value(QStringLiteral("canonicalDestinationPath")).toString();
    r.status                   = statusFromString(o.value(QStringLiteral("status")).toString());
    r.createdAtMsEpoch         = static_cast<qint64>(o.value(QStringLiteral("createdAtMsEpoch")).toDouble());
    r.updatedAtMsEpoch         = static_cast<qint64>(o.value(QStringLiteral("updatedAtMsEpoch")).toDouble());
    r.lastErrorCode            = o.value(QStringLiteral("lastErrorCode")).toString();
    r.lastErrorMessage         = o.value(QStringLiteral("lastErrorMessage")).toString();
    return r;
}

} // anonymous namespace

TorrentRequestLedger::TorrentRequestLedger(const QString& filePath, QObject* parent)
    : QObject(parent), m_filePath(filePath)
{
    load();
}

TorrentRequestLedger::~TorrentRequestLedger() = default;

void TorrentRequestLedger::load()
{
    QMutexLocker locker(&m_mutex);
    m_byKey.clear();

    QFile f(m_filePath);
    if (!f.exists()) {
        qDebug().noquote() << QStringLiteral("[TorrentRequestLedger] no ledger file at")
                           << m_filePath
                           << QStringLiteral("- starting empty");
        return;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << QStringLiteral("[TorrentRequestLedger] could not open")
                             << m_filePath
                             << QStringLiteral(":") << f.errorString();
        return;
    }

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning().noquote() << QStringLiteral("[TorrentRequestLedger] parse error:")
                             << perr.errorString();
        return;
    }
    const QJsonObject root = doc.object();
    const QJsonArray arr = root.value(QStringLiteral("requests")).toArray();
    for (const auto& v : arr) {
        const TorrentRequest r = fromJson(v.toObject());
        if (r.seriesId.isEmpty() || r.volumeNumber <= 0) continue;
        m_byKey.insert(requestKey(r.catalogId, r.seriesId, r.volumeNumber), r);
    }
    qDebug().noquote() << QStringLiteral("[TorrentRequestLedger] loaded")
                       << m_byKey.size() << QStringLiteral("request(s) from")
                       << m_filePath;
}

void TorrentRequestLedger::saveLocked()
{
    QJsonArray arr;
    for (const auto& r : m_byKey) arr.append(toJson(r));
    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = 1;
    root[QStringLiteral("requests")]      = arr;

    QFileInfo fi(m_filePath);
    QDir().mkpath(fi.absolutePath());

    QSaveFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning().noquote() << QStringLiteral("[TorrentRequestLedger] save open failed:")
                             << f.errorString();
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        qWarning().noquote() << QStringLiteral("[TorrentRequestLedger] save commit failed:")
                             << f.errorString();
    }
}

void TorrentRequestLedger::save()
{
    QMutexLocker locker(&m_mutex);
    saveLocked();
    locker.unlock();
    emit ledgerChanged();
}

void TorrentRequestLedger::upsert(const TorrentRequest& req)
{
    {
        QMutexLocker locker(&m_mutex);
        TorrentRequest copy = req;
        copy.updatedAtMsEpoch = QDateTime::currentMSecsSinceEpoch();
        if (copy.createdAtMsEpoch == 0) copy.createdAtMsEpoch = copy.updatedAtMsEpoch;
        m_byKey.insert(requestKey(copy.catalogId, copy.seriesId, copy.volumeNumber), copy);
        saveLocked();
    }
    emit ledgerChanged();
}

void TorrentRequestLedger::updateStatus(const QString& key,
                                        TorrentRequest::Status newStatus,
                                        const QString& errorCode,
                                        const QString& errorMessage)
{
    {
        QMutexLocker locker(&m_mutex);
        const auto it = m_byKey.find(key);
        if (it == m_byKey.end()) return;
        it->status            = newStatus;
        it->updatedAtMsEpoch  = QDateTime::currentMSecsSinceEpoch();
        if (!errorCode.isEmpty())    it->lastErrorCode    = errorCode;
        if (!errorMessage.isEmpty()) it->lastErrorMessage = errorMessage;
        saveLocked();
    }
    emit ledgerChanged();
}

void TorrentRequestLedger::remove(const QString& key)
{
    {
        QMutexLocker locker(&m_mutex);
        if (!m_byKey.remove(key)) return;
        saveLocked();
    }
    emit ledgerChanged();
}

std::optional<TorrentRequest> TorrentRequestLedger::find(const QString& key) const
{
    QMutexLocker locker(&m_mutex);
    const auto it = m_byKey.constFind(key);
    if (it == m_byKey.constEnd()) return std::nullopt;
    return it.value();
}

QList<TorrentRequest> TorrentRequestLedger::all() const
{
    QMutexLocker locker(&m_mutex);
    return m_byKey.values();
}

QList<TorrentRequest> TorrentRequestLedger::findByInfoHash(const QString& infoHash) const
{
    QMutexLocker locker(&m_mutex);
    QList<TorrentRequest> out;
    for (const auto& r : m_byKey) {
        if (r.expectedInfoHash == infoHash.toLower()) out.append(r);
    }
    return out;
}

QList<TorrentRequest> TorrentRequestLedger::findByStatus(TorrentRequest::Status status) const
{
    QMutexLocker locker(&m_mutex);
    QList<TorrentRequest> out;
    for (const auto& r : m_byKey) {
        if (r.status == status) out.append(r);
    }
    return out;
}

} // namespace tankoban::manga::premium
```

- [ ] **Step 3.1.3: Wire into CMakeLists.txt**

Add the two new files to the `add_executable(Tankoban ...)` source list in alphabetical position:

```cmake
    src/core/manga/TorrentRequestLedger.cpp
    src/core/manga/TorrentRequestLedger.h
```

- [ ] **Step 3.1.4: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 3.2: Bootstrap the persistent staging + quarantine directories at app start

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 3.2.1: Add the directory bootstrap**

In `src/main.cpp`, locate the section that resolves `appDataDir` (look for `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)` or `QCoreApplication::applicationDirPath()`). Add immediately after the existing appData resolution, BEFORE the main window construction:

```cpp
    // Tankoyomi-Premium: persistent staging + quarantine roots.
    // Staging is keyed by infoHash; quarantine holds archives that failed
    // PremiumArchiveValidator (Phase 4) so the curator can inspect them.
    const QString premiumStagingRoot    = appDataDir + QStringLiteral("/manga_premium_staging");
    const QString premiumQuarantineRoot = appDataDir + QStringLiteral("/manga_premium_quarantine");
    QDir().mkpath(premiumStagingRoot);
    QDir().mkpath(premiumQuarantineRoot);
```

Ensure the `#include <QDir>` and `#include <QStandardPaths>` are present at the top of the file (most likely already are; if not, add them).

- [ ] **Step 3.2.2: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 3.3: Declare the TorrentVolumeProvider header

**Files:**
- Create: `src/core/manga/TorrentVolumeProvider.h`

- [ ] **Step 3.3.1: Write the header**

```cpp
// src/core/manga/TorrentVolumeProvider.h
#pragma once

#include "PremiumCatalogSchema.h"
#include "TorrentRequestLedger.h"

#include <QObject>
#include <QHash>
#include <QString>
#include <QVector>
#include <QPointer>

class TorrentEngine;

namespace tankoban::manga::premium {

class PremiumCatalog;

// Torrent-side volume orchestration. Sibling to MangaDownloader, not a second
// downloader. Consumes TorrentEngine via its existing public surface; owns a
// persistent request ledger and an infoHash-keyed staging tree.
//
// Per Codex section 18 the provider is single long-lived, owned by ComicsPage,
// but its persistent state outlives the UI. All TorrentEngine signal
// connections are Qt::QueuedConnection because TorrentEngine emits from its
// alert worker thread.
//
// Per Codex section 17.3 + 17.5 the magnet add uses paused=true (upload-only
// mode) so metadata arrives without an all-files-download window; once
// metadata + file priorities are set, startTorrent clears upload-only.
//
// Per Codex section 19 + section 27.1 completion detection is event-driven
// via pieceFinished + fileByteRangesOfHavePieces, NOT polled.
//
// Archive validation (Phase 4) and cover extraction (Phase 9) are owned by
// separate classes; this provider calls them at the finalize step. v1 ships
// without those wired (Phase 4 + Phase 9 add them).
class TorrentVolumeProvider : public QObject
{
    Q_OBJECT
public:
    TorrentVolumeProvider(TorrentEngine*          engine,
                          PremiumCatalog*         catalog,
                          TorrentRequestLedger*   ledger,
                          const QString&          stagingRoot,
                          QObject*                parent = nullptr);
    ~TorrentVolumeProvider() override;

    // Kicks off a single-volume fetch. Idempotent: a second call with the
    // same triple is a no-op if the request is already in flight, or a
    // resume if it had completed.
    //
    // `destinationPath` is the canonical series folder; the final cbz lands
    // at `<destinationPath>/<volumeEntry.cbzFileName>`.
    void requestVolume(const PremiumCatalogEntry& entry,
                       const PremiumVolumeEntry&  volumeEntry,
                       const QString&             destinationPath);

    // Cancels one in-flight request. Drops the volume's file-priority slot
    // back to 0. Does NOT remove the torrent if other volumes for the same
    // series are still being requested. If `deleteStaged` is true the
    // partially-downloaded staging file is removed.
    void cancelVolume(const QString& seriesId, int volumeNumber, bool deleteStaged = false);

    // Global pause/resume. Pause halts piece-request bursts but keeps the
    // torrent attached. Per Codex section 18 the UI shares one "Transfers
    // paused" state across MangaDownloader + TorrentVolumeProvider via
    // MangaTransferCoordinator (Phase 8).
    void pauseAll();
    void resumeAll();
    bool isPaused() const;

    // Crash-resume entry point. Called once after construction (post-engine
    // start, post-catalog load). Replays every ledger row whose status is
    // Pending/AwaitingMetadata/Downloading/Validating; re-attaches torrents
    // by expectedInfoHash; reapplies union file priorities.
    void replayLedger();

signals:
    // 0.0 to 1.0 monotone within a request. Emitted at most every ~500ms
    // per request to keep UI repaint cost bounded.
    void volumeProgress(QString seriesId, int volumeNumber, double pct);

    // Fired AFTER the cbz is durable at canonicalDestinationPath AND the
    // request row's status has been updated to Completed. Per Codex section
    // 18 step 5 the order is: finalize file -> register in index -> emit
    // volumeCompleted. Phase 4 inserts archive validation between the
    // finalize-file and index-register steps.
    void volumeCompleted(QString seriesId, int volumeNumber, QString cbzPath);

    // Fired on any failure path (engine error, archive validation failure,
    // expectedInfoHash mismatch on metadata, etc). errorCode is a stable
    // identifier (e.g. "infohash_mismatch", "validation_failed_no_images").
    void volumeFailed(QString seriesId, int volumeNumber,
                      QString errorCode, QString errorMessage);

    // Swarm-quality heartbeat. Emitted every ~2s while a volume is in flight.
    // 0 piecePeersOnline for >=30s is the "Waiting for peers" UX trigger
    // (Phase 6 Volume row indicator).
    void swarmStatus(QString seriesId, int volumeNumber, int piecePeersOnline);

private slots:
    void onMetadataReady(const QString& infoHash, const QString& name, qint64 totalSize);
    void onPieceFinished(const QString& infoHash, int pieceIndex);
    void onTorrentError(const QString& infoHash, const QString& errorMessage);

private:
    struct Inflight {
        QString  requestKey;
        QString  catalogId;
        QString  seriesId;
        int      volumeNumber       = 0;
        QString  expectedInfoHash;
        int      fileIndex          = -1;
        QString  cbzFileName;
        qint64   fileSizeBytes      = 0;
        int      pieceStart         = -1;
        int      pieceEnd           = -1;
        QString  stagingPath;       // <stagingRoot>/<infoHash>/
        QString  canonicalDestinationPath;
        bool     prioritiesApplied  = false;
        bool     startedAfterMeta   = false;
        double   lastReportedPct    = -1.0;
    };

    QString stagingPathFor(const QString& infoHash) const;

    void ensureTorrentAdded(const Inflight& iff);
    void applyUnionPriorities(const QString& infoHash);
    bool checkFileCompletion(const QString& infoHash, const Inflight& iff);
    void finalizeCompletion(Inflight iff);
    void emitProgressIfChanged(Inflight& iff);

    TorrentEngine*                            m_engine    = nullptr;
    QPointer<PremiumCatalog>                  m_catalog;
    QPointer<TorrentRequestLedger>            m_ledger;
    QString                                   m_stagingRoot;
    bool                                      m_paused    = false;

    // infoHash -> list of in-flight requests against that torrent.
    QHash<QString, QList<Inflight>>           m_byInfoHash;
};

} // namespace tankoban::manga::premium
```

### Task 3.4: Implement requestVolume + metadata-first add flow

**Files:**
- Create: `src/core/manga/TorrentVolumeProvider.cpp` (constructor + requestVolume + ensureTorrentAdded + applyUnionPriorities + onMetadataReady)

- [ ] **Step 3.4.1: Implement constructor + lifecycle**

Write the following file (will be extended in 3.5 and 3.6):

```cpp
// src/core/manga/TorrentVolumeProvider.cpp
#include "TorrentVolumeProvider.h"
#include "PremiumCatalog.h"
#include "../torrent/TorrentEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>

namespace tankoban::manga::premium {

namespace {
constexpr int kFilePriorityTarget = 7; // libtorrent max priority for the requested file
constexpr int kFilePriorityOff    = 0; // explicitly do-not-download for everything else
} // anonymous namespace

TorrentVolumeProvider::TorrentVolumeProvider(TorrentEngine*        engine,
                                             PremiumCatalog*       catalog,
                                             TorrentRequestLedger* ledger,
                                             const QString&        stagingRoot,
                                             QObject*              parent)
    : QObject(parent)
    , m_engine(engine)
    , m_catalog(catalog)
    , m_ledger(ledger)
    , m_stagingRoot(stagingRoot)
{
    Q_ASSERT(m_engine);
    Q_ASSERT(m_catalog);
    Q_ASSERT(m_ledger);
    QDir().mkpath(m_stagingRoot);

    // Per Codex section 18: TorrentEngine emits from its alert worker thread.
    // Every connection is queued so we receive on the provider's owning thread.
    connect(m_engine, &TorrentEngine::metadataReady,
            this, &TorrentVolumeProvider::onMetadataReady,
            Qt::QueuedConnection);
    connect(m_engine, &TorrentEngine::pieceFinished,
            this, &TorrentVolumeProvider::onPieceFinished,
            Qt::QueuedConnection);
    connect(m_engine, &TorrentEngine::torrentError,
            this, &TorrentVolumeProvider::onTorrentError,
            Qt::QueuedConnection);
}

TorrentVolumeProvider::~TorrentVolumeProvider() = default;

QString TorrentVolumeProvider::stagingPathFor(const QString& infoHash) const
{
    return m_stagingRoot + QChar('/') + infoHash.toLower();
}

void TorrentVolumeProvider::requestVolume(const PremiumCatalogEntry& entry,
                                          const PremiumVolumeEntry&  volumeEntry,
                                          const QString&             destinationPath)
{
    const QString infoHash = entry.expectedInfoHash;
    const QString key      = requestKey(QStringLiteral("tankoyomi_premium"),
                                        entry.seriesId, volumeEntry.vol);

    // Idempotency: if already in-flight, no-op.
    for (const auto& iff : m_byInfoHash.value(infoHash)) {
        if (iff.requestKey == key) {
            qDebug().noquote() << QStringLiteral("[TorrentVolumeProvider] requestVolume noop (already in-flight):")
                               << key;
            return;
        }
    }

    Inflight iff;
    iff.requestKey               = key;
    iff.catalogId                = QStringLiteral("tankoyomi_premium");
    iff.seriesId                 = entry.seriesId;
    iff.volumeNumber             = volumeEntry.vol;
    iff.expectedInfoHash         = infoHash;
    iff.fileIndex                = volumeEntry.fileIndex;
    iff.cbzFileName              = volumeEntry.cbzFileName;
    iff.fileSizeBytes            = volumeEntry.fileSizeBytes;
    iff.pieceStart               = volumeEntry.pieceStart;
    iff.pieceEnd                 = volumeEntry.pieceEnd;
    iff.stagingPath              = stagingPathFor(infoHash);
    iff.canonicalDestinationPath = destinationPath + QChar('/') + volumeEntry.cbzFileName;
    QDir().mkpath(iff.stagingPath);

    m_byInfoHash[infoHash].append(iff);

    // Persist the request row before any external side-effect (engine add).
    TorrentRequest row;
    row.catalogId                = iff.catalogId;
    row.seriesId                 = iff.seriesId;
    row.volumeNumber             = iff.volumeNumber;
    row.expectedInfoHash         = iff.expectedInfoHash;
    row.magnetUri                = entry.magnetUri;
    row.fileIndex                = iff.fileIndex;
    row.cbzFileName              = iff.cbzFileName;
    row.fileSizeBytes            = iff.fileSizeBytes;
    row.pieceStart               = iff.pieceStart;
    row.pieceEnd                 = iff.pieceEnd;
    row.stagingPath              = iff.stagingPath;
    row.canonicalDestinationPath = iff.canonicalDestinationPath;
    row.status                   = TorrentRequest::Status::Pending;
    row.createdAtMsEpoch         = QDateTime::currentMSecsSinceEpoch();
    row.updatedAtMsEpoch         = row.createdAtMsEpoch;
    m_ledger->upsert(row);

    ensureTorrentAdded(iff);
}

void TorrentVolumeProvider::ensureTorrentAdded(const Inflight& iff)
{
    // Per Codex section 17.3: addMagnet(paused=true) keeps the torrent in
    // upload-only mode so metadata arrives without an all-files-download
    // window. The matching startTorrent() call clears upload-only AFTER
    // priorities are set (see applyUnionPriorities).
    //
    // We pass the catalog's series-id as the savePath suffix so libtorrent's
    // resume data finds the same staging tree across restarts.
    const QString stagingPath = iff.stagingPath;

    qDebug().noquote() << QStringLiteral("[TorrentVolumeProvider] addMagnet(paused=true) for")
                       << iff.expectedInfoHash << QStringLiteral("staging:") << stagingPath;

    const TorrentRequestLedger* ledger = m_ledger;
    const auto pending = ledger->find(iff.requestKey);
    if (!pending) return;

    m_engine->addMagnet(pending->magnetUri, stagingPath, /*paused=*/true);
    m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::AwaitingMetadata);
}

void TorrentVolumeProvider::onMetadataReady(const QString& infoHash,
                                            const QString& name,
                                            qint64         totalSize)
{
    Q_UNUSED(name)
    Q_UNUSED(totalSize)
    auto it = m_byInfoHash.find(infoHash.toLower());
    if (it == m_byInfoHash.end() || it->isEmpty()) return;

    qDebug().noquote() << QStringLiteral("[TorrentVolumeProvider] metadataReady for")
                       << infoHash << QStringLiteral("inflight:") << it->size();

    applyUnionPriorities(infoHash.toLower());

    // Clear upload-only mode now that priorities pin to-download to the
    // requested files only. The savePath argument is the existing staging
    // dir; TorrentEngine validates this against its current resume data.
    const QString stagingPath = stagingPathFor(infoHash.toLower());
    m_engine->startTorrent(infoHash.toLower(), stagingPath);

    for (auto& iff : *it) {
        iff.startedAfterMeta = true;
        m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Downloading);
    }
}

void TorrentVolumeProvider::applyUnionPriorities(const QString& infoHash)
{
    auto it = m_byInfoHash.find(infoHash);
    if (it == m_byInfoHash.end() || it->isEmpty()) return;

    // The largest fileIndex across our in-flight requests defines the size
    // of the priorities vector we need to send. TorrentEngine will pad with
    // priority 0 internally if the vector is shorter than the torrent's
    // actual file count, but being explicit is safer when the torrent has
    // many files.
    int maxIndex = 0;
    for (const auto& iff : *it) maxIndex = qMax(maxIndex, iff.fileIndex);

    QVector<int> priorities(maxIndex + 1, kFilePriorityOff);
    for (const auto& iff : *it) {
        if (iff.fileIndex >= 0 && iff.fileIndex < priorities.size()) {
            priorities[iff.fileIndex] = kFilePriorityTarget;
        }
    }
    m_engine->setFilePriorities(infoHash, priorities);

    for (auto& iff : *it) iff.prioritiesApplied = true;

    qDebug().noquote() << QStringLiteral("[TorrentVolumeProvider] applied union priorities for")
                       << infoHash
                       << QStringLiteral("targets:")
                       << it->size();
}

void TorrentVolumeProvider::onTorrentError(const QString& infoHash,
                                           const QString& errorMessage)
{
    auto it = m_byInfoHash.find(infoHash.toLower());
    if (it == m_byInfoHash.end()) return;
    for (const auto& iff : *it) {
        m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Failed,
                               QStringLiteral("engine_error"), errorMessage);
        emit volumeFailed(iff.seriesId, iff.volumeNumber,
                          QStringLiteral("engine_error"), errorMessage);
    }
}

} // namespace tankoban::manga::premium
```

Note: this file references `TorrentEngine::torrentError`. Confirm the signal exists; if it's named differently (e.g. `errorOccurred` or `torrentFailed`), grep `src/core/torrent/TorrentEngine.h` and adjust the connect + slot accordingly. If no equivalent signal exists, drop `onTorrentError` for v1 and rely on `volumeFailed` from the validation path only.

- [ ] **Step 3.4.2: Wire into CMakeLists.txt + build-verify**

Add to the source list:

```cmake
    src/core/manga/TorrentVolumeProvider.cpp
    src/core/manga/TorrentVolumeProvider.h
```

Run: `build_check.bat`
Expected: `BUILD OK`. If `torrentError` doesn't exist, adjust per the note above and re-run.

### Task 3.5: Implement event-driven file completion via pieceFinished + fileByteRangesOfHavePieces

**Files:**
- Modify: `src/core/manga/TorrentVolumeProvider.cpp` (append `onPieceFinished` + `checkFileCompletion` + `finalizeCompletion` + `emitProgressIfChanged`)

- [ ] **Step 3.5.1: Append the completion handlers**

Append the following functions to `TorrentVolumeProvider.cpp` (inside the `tankoban::manga::premium` namespace, before its closing brace):

```cpp
void TorrentVolumeProvider::onPieceFinished(const QString& infoHash, int pieceIndex)
{
    auto it = m_byInfoHash.find(infoHash.toLower());
    if (it == m_byInfoHash.end() || it->isEmpty()) return;

    // For each in-flight request that owns this piece's range, check whether
    // its file is now byte-complete and emit progress.
    QList<Inflight> completedThisCall;
    for (auto& iff : *it) {
        if (pieceIndex < iff.pieceStart || pieceIndex > iff.pieceEnd) continue;

        emitProgressIfChanged(iff);

        if (checkFileCompletion(infoHash.toLower(), iff)) {
            completedThisCall.append(iff);
        }
    }

    // Finalize completed requests AFTER the iteration (finalize mutates the
    // m_byInfoHash list to remove the completed entry).
    for (const auto& done : completedThisCall) {
        finalizeCompletion(done);
    }
}

bool TorrentVolumeProvider::checkFileCompletion(const QString& infoHash,
                                                const Inflight& iff)
{
    // Per Codex section 19: fileByteRangesOfHavePieces returns a list of
    // contiguous byte ranges within the file that are fully downloaded.
    // File is complete when the merged ranges cover [0, fileSizeBytes).
    const auto ranges = m_engine->fileByteRangesOfHavePieces(infoHash, iff.fileIndex);

    qint64 covered = 0;
    for (const auto& rng : ranges) {
        // rng.first = startOffset, rng.second = length (inclusive).
        // Sanity: clamp to file bounds.
        const qint64 start  = qMax<qint64>(0, rng.first);
        const qint64 length = qMax<qint64>(0, rng.second);
        Q_UNUSED(start)
        covered += length;
    }
    return covered >= iff.fileSizeBytes && iff.fileSizeBytes > 0;
}

void TorrentVolumeProvider::emitProgressIfChanged(Inflight& iff)
{
    const auto ranges = m_engine->fileByteRangesOfHavePieces(iff.expectedInfoHash, iff.fileIndex);
    qint64 covered = 0;
    for (const auto& rng : ranges) covered += qMax<qint64>(0, rng.second);
    const double pct = (iff.fileSizeBytes > 0)
        ? double(covered) / double(iff.fileSizeBytes)
        : 0.0;

    // Quantize to 0.5% steps to bound emit frequency.
    const double quantized = qRound(pct * 200.0) / 200.0;
    if (qFuzzyCompare(1.0 + quantized, 1.0 + iff.lastReportedPct)) return;
    iff.lastReportedPct = quantized;
    emit volumeProgress(iff.seriesId, iff.volumeNumber, quantized);
}

void TorrentVolumeProvider::finalizeCompletion(Inflight iff)
{
    // Per Codex section 21 step 2: flushCache before touching the file.
    // Phase 4 inserts the archive validation + .tankoban-part rename here.
    // For now (Phase 3), we do the minimum: flush, then a direct copy from
    // staging to canonical, then update ledger + emit.
    m_engine->flushCache(iff.expectedInfoHash);

    const QString stagingFile = iff.stagingPath + QChar('/') + iff.cbzFileName;
    const QString finalFile   = iff.canonicalDestinationPath;
    QFileInfo fi(finalFile);
    QDir().mkpath(fi.absolutePath());

    if (QFile::exists(finalFile)) {
        // Per Codex section 18: "collision guard is filename-level: the
        // provider must never overwrite an existing cbz unless the catalog
        // entry explicitly marks it as a replacement". V1 catalog has no
        // such marker; fail loud.
        m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Failed,
                               QStringLiteral("destination_exists"),
                               QStringLiteral("refusing to overwrite ") + finalFile);
        emit volumeFailed(iff.seriesId, iff.volumeNumber,
                          QStringLiteral("destination_exists"),
                          QStringLiteral("refusing to overwrite ") + finalFile);
        // Remove from in-flight tracking.
        auto it = m_byInfoHash.find(iff.expectedInfoHash);
        if (it != m_byInfoHash.end()) {
            it->erase(std::remove_if(it->begin(), it->end(),
                [&](const Inflight& x){ return x.requestKey == iff.requestKey; }),
                it->end());
        }
        return;
    }

    if (!QFile::copy(stagingFile, finalFile)) {
        m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Failed,
                               QStringLiteral("copy_failed"),
                               QStringLiteral("could not copy ") + stagingFile +
                               QStringLiteral(" -> ") + finalFile);
        emit volumeFailed(iff.seriesId, iff.volumeNumber,
                          QStringLiteral("copy_failed"),
                          QStringLiteral("could not copy ") + stagingFile +
                          QStringLiteral(" -> ") + finalFile);
        return;
    }

    m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Completed);

    // Remove from in-flight tracking.
    auto it = m_byInfoHash.find(iff.expectedInfoHash);
    if (it != m_byInfoHash.end()) {
        it->erase(std::remove_if(it->begin(), it->end(),
            [&](const Inflight& x){ return x.requestKey == iff.requestKey; }),
            it->end());
    }

    emit volumeCompleted(iff.seriesId, iff.volumeNumber, finalFile);

    qDebug().noquote() << QStringLiteral("[TorrentVolumeProvider] completed:")
                       << iff.seriesId << QStringLiteral("v") + QString::number(iff.volumeNumber)
                       << QStringLiteral("at") << finalFile;
}
```

Note Phase 4 will rewrite `finalizeCompletion` to insert the archive validation + `.tankoban-part` discipline. The simple-copy version here is intentional minimum-viable v1-of-the-phase.

- [ ] **Step 3.5.2: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 3.6: Implement crash-resume + cancel + pause/resume

**Files:**
- Modify: `src/core/manga/TorrentVolumeProvider.cpp` (append `replayLedger` + `cancelVolume` + `pauseAll` + `resumeAll` + `isPaused`)

- [ ] **Step 3.6.1: Append the resume + control surface**

Append the following functions:

```cpp
void TorrentVolumeProvider::replayLedger()
{
    if (!m_ledger || !m_catalog) return;

    const auto pendingStates = {
        TorrentRequest::Status::Pending,
        TorrentRequest::Status::AwaitingMetadata,
        TorrentRequest::Status::Downloading,
        TorrentRequest::Status::Validating,
    };
    for (auto status : pendingStates) {
        for (const auto& row : m_ledger->findByStatus(status)) {
            const auto entryOpt = m_catalog->entryById(row.seriesId);
            if (!entryOpt) {
                m_ledger->updateStatus(requestKey(row.catalogId, row.seriesId, row.volumeNumber),
                                       TorrentRequest::Status::CatalogMissing,
                                       QStringLiteral("catalog_missing"),
                                       QStringLiteral("series no longer in catalog at startup"));
                continue;
            }

            // Find the matching volume.
            std::optional<PremiumVolumeEntry> volOpt;
            for (const auto& v : entryOpt->volumes) {
                if (v.vol == row.volumeNumber) { volOpt = v; break; }
            }
            if (!volOpt) {
                m_ledger->updateStatus(requestKey(row.catalogId, row.seriesId, row.volumeNumber),
                                       TorrentRequest::Status::CatalogMissing,
                                       QStringLiteral("volume_missing"),
                                       QStringLiteral("volume row no longer in catalog at startup"));
                continue;
            }

            Inflight iff;
            iff.requestKey               = requestKey(row.catalogId, row.seriesId, row.volumeNumber);
            iff.catalogId                = row.catalogId;
            iff.seriesId                 = row.seriesId;
            iff.volumeNumber             = row.volumeNumber;
            iff.expectedInfoHash         = row.expectedInfoHash;
            iff.fileIndex                = row.fileIndex;
            iff.cbzFileName              = row.cbzFileName;
            iff.fileSizeBytes            = row.fileSizeBytes;
            iff.pieceStart               = row.pieceStart;
            iff.pieceEnd                 = row.pieceEnd;
            iff.stagingPath              = row.stagingPath;
            iff.canonicalDestinationPath = row.canonicalDestinationPath;
            m_byInfoHash[iff.expectedInfoHash].append(iff);

            ensureTorrentAdded(iff);
        }
    }
    qDebug().noquote() << QStringLiteral("[TorrentVolumeProvider] replayLedger done; in-flight torrents:")
                       << m_byInfoHash.size();
}

void TorrentVolumeProvider::cancelVolume(const QString& seriesId, int volumeNumber, bool deleteStaged)
{
    QString matchedInfoHash;
    QString matchedKey;
    Inflight removed;
    for (auto it = m_byInfoHash.begin(); it != m_byInfoHash.end(); ++it) {
        for (auto& iff : *it) {
            if (iff.seriesId == seriesId && iff.volumeNumber == volumeNumber) {
                matchedInfoHash = it.key();
                matchedKey      = iff.requestKey;
                removed         = iff;
                break;
            }
        }
        if (!matchedInfoHash.isEmpty()) break;
    }

    if (matchedInfoHash.isEmpty()) {
        // Maybe it's not in-flight but is in the ledger. Just update ledger.
        const QString key = requestKey(QStringLiteral("tankoyomi_premium"), seriesId, volumeNumber);
        m_ledger->updateStatus(key, TorrentRequest::Status::Cancelled);
        return;
    }

    // Drop the priority for this volume; recompute union.
    auto& list = m_byInfoHash[matchedInfoHash];
    list.erase(std::remove_if(list.begin(), list.end(),
        [&](const Inflight& x){ return x.requestKey == matchedKey; }),
        list.end());

    if (list.isEmpty()) {
        // No more requests against this torrent. Remove it entirely.
        m_engine->removeTorrent(matchedInfoHash, /*deleteFiles=*/deleteStaged);
        m_byInfoHash.remove(matchedInfoHash);
    } else {
        applyUnionPriorities(matchedInfoHash);
    }

    if (deleteStaged) {
        QFile::remove(removed.stagingPath + QChar('/') + removed.cbzFileName);
    }

    m_ledger->updateStatus(matchedKey, TorrentRequest::Status::Cancelled);
}

void TorrentVolumeProvider::pauseAll()
{
    m_paused = true;
    for (auto it = m_byInfoHash.begin(); it != m_byInfoHash.end(); ++it) {
        m_engine->pauseTorrent(it.key());
    }
}

void TorrentVolumeProvider::resumeAll()
{
    m_paused = false;
    for (auto it = m_byInfoHash.begin(); it != m_byInfoHash.end(); ++it) {
        m_engine->resumeTorrent(it.key());
    }
}

bool TorrentVolumeProvider::isPaused() const
{
    return m_paused;
}
```

Note: confirm `TorrentEngine::removeTorrent(infoHash, bool deleteFiles)` signature against `src/core/torrent/TorrentEngine.h:127` - it matches. Same for `pauseTorrent` / `resumeTorrent` (lines 126 / 125).

- [ ] **Step 3.6.2: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 3.7: Wire TorrentVolumeProvider into ComicsPage construction

**Files:**
- Modify: `src/ui/pages/ComicsPage.h` (add member)
- Modify: `src/ui/pages/ComicsPage.cpp` (instantiate, replay ledger, connect signals to qDebug for now)

- [ ] **Step 3.7.1: Add the members**

In `src/ui/pages/ComicsPage.h`, alongside the `PremiumCatalog* m_premiumCatalog = nullptr;` from Phase 1, add:

```cpp
namespace tankoban::manga::premium {
    class PremiumCatalog;
    class TorrentVolumeProvider;
    class TorrentRequestLedger;
}
```

In the private section:

```cpp
    tankoban::manga::premium::TorrentRequestLedger*  m_premiumLedger   = nullptr;
    tankoban::manga::premium::TorrentVolumeProvider* m_premiumProvider = nullptr;
```

- [ ] **Step 3.7.2: Instantiate in the constructor**

In `src/ui/pages/ComicsPage.cpp`, after the existing `m_premiumCatalog = new ...` line:

```cpp
    const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString premiumStagingRoot = appDataDir + QStringLiteral("/manga_premium_staging");
    const QString premiumLedgerPath  = appDataDir + QStringLiteral("/manga_premium_requests.json");

    m_premiumLedger = new tankoban::manga::premium::TorrentRequestLedger(premiumLedgerPath, this);
    m_premiumProvider = new tankoban::manga::premium::TorrentVolumeProvider(
        /*engine=*/m_torrentEngine,   // existing TorrentEngine* member; confirm name in ComicsPage
        /*catalog=*/m_premiumCatalog,
        /*ledger=*/m_premiumLedger,
        /*stagingRoot=*/premiumStagingRoot,
        /*parent=*/this);

    // Phase 3 wiring is qDebug-only; Phase 5 (registerVolume) and Phase 6
    // (detail view) plug in the real consumers.
    connect(m_premiumProvider, &tankoban::manga::premium::TorrentVolumeProvider::volumeProgress,
            this, [](const QString& s, int v, double p){
                qDebug().noquote() << QStringLiteral("[Premium] progress")
                                   << s << QStringLiteral("v") + QString::number(v)
                                   << QStringLiteral("@") << p;
            });
    connect(m_premiumProvider, &tankoban::manga::premium::TorrentVolumeProvider::volumeCompleted,
            this, [](const QString& s, int v, const QString& p){
                qDebug().noquote() << QStringLiteral("[Premium] completed")
                                   << s << QStringLiteral("v") + QString::number(v)
                                   << QStringLiteral("at") << p;
            });
    connect(m_premiumProvider, &tankoban::manga::premium::TorrentVolumeProvider::volumeFailed,
            this, [](const QString& s, int v, const QString& c, const QString& m){
                qDebug().noquote() << QStringLiteral("[Premium] failed")
                                   << s << QStringLiteral("v") + QString::number(v)
                                   << c << m;
            });

    // Crash-resume entry point. Replay AFTER engine + catalog are alive.
    m_premiumProvider->replayLedger();
```

Adjust `m_torrentEngine` to the actual ComicsPage member name; grep `ComicsPage.h` for the existing `TorrentEngine*` member if uncertain. If ComicsPage does not currently own a `TorrentEngine*` directly, source it from `Tankorent` or wherever the existing torrent surface is exposed - check the merger-arc plan precedent for how Tankoyomi-side code currently reaches `TorrentEngine`.

Add the includes:

```cpp
#include "core/manga/TorrentRequestLedger.h"
#include "core/manga/TorrentVolumeProvider.h"
#include <QStandardPaths>
```

- [ ] **Step 3.7.3: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`. If linker fails on `TorrentEngine` resolution, ensure the `TorrentEngine*` ComicsPage has access to is correctly wired.

### Task 3.8: Smoke - app starts cleanly + provider initializes + ledger persists across restart

- [ ] **Step 3.8.1: Launch the app fresh**

```cmd
taskkill /F /IM Tankoban.exe 2>nul
build_and_run.bat
```

Wait for the Tankoban window.

- [ ] **Step 3.8.2: Verify clean startup logs**

```cmd
out\tankoctl.exe logs 200
```

Look for the following lines in order:

```
[PremiumCatalog] loaded 0 series across 0 file(s), with 0 diagnostic(s)
[TorrentRequestLedger] no ledger file at .../manga_premium_requests.json - starting empty
[TorrentVolumeProvider] replayLedger done; in-flight torrents: 0
```

(After Phase 10 ships the real catalog, the first line will report 30 series.)

- [ ] **Step 3.8.3: Verify the ledger file is created on first mutation**

The ledger file isn't written until something calls `upsert`. Since Phase 3 has no UI yet, the file will not appear. Verify by inspection:

```cmd
dir "%APPDATA%\..\Local\<TankobanAppDataDirName>\manga_premium_staging"
```

The directory should exist (created by main.cpp's `mkpath`). The `manga_premium_requests.json` file should NOT exist yet.

- [ ] **Step 3.8.4: Close the app**

```cmd
taskkill /F /IM Tankoban.exe
```

(MCP LOCK release if claimed - not strictly needed since no UI interaction happened.)

### Task 3.9: Phase 3 close - RTC line

- [ ] **Step 3.9.1: Append the phase-close RTC line to `agents/chat.md`**

```
READY TO COMMIT - [Agent 1, TANKOYOMI_PREMIUM Phase 3 - TorrentVolumeProvider core + persistent request ledger + event-driven completion + crash-resume + cancel/pause/resume. New: TorrentRequestLedger.h/.cpp (JSON-backed thread-safe ledger at <appData>/manga_premium_requests.json, schemaVersion=1, atomic save via QSaveFile, off-lock emit), TorrentVolumeProvider.h/.cpp (sibling to MangaDownloader, owns persistent staging at <appData>/manga_premium_staging/<infoHash>/, all TorrentEngine signal connections queued per Codex section 18 thread contract). requestVolume implements metadata-first flow per Codex section 17.3 (addMagnet paused=true upload-only -> wait metadataReady -> setFilePriorities -> startTorrent clears upload-only). Union file-priority calculation handles concurrent volume requests against same torrent. Event-driven completion via pieceFinished + fileByteRangesOfHavePieces with covered-byte tally (no polling) per Codex section 19. Crash-resume on startup via replayLedger() reattaches by expectedInfoHash, reapplies union priorities, marks catalog-missing requests recoverable. cancelVolume drops priority + recomputes union; if last request on torrent then removeTorrent. pauseAll/resumeAll forward to engine. Wired into ComicsPage construction with qDebug-only signal handlers (Phase 5+ plug in real consumers). main.cpp bootstraps <appData>/manga_premium_staging/ + manga_premium_quarantine/ at startup. Smoke green: app launches clean, loader logs 0-series start (no catalog yet), ledger logs empty start, replayLedger logs 0-in-flight; staging directory created, no ledger file until first mutation. build_check + build_and_run both green.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/core/manga/TorrentRequestLedger.h, src/core/manga/TorrentRequestLedger.cpp, src/core/manga/TorrentVolumeProvider.h, src/core/manga/TorrentVolumeProvider.cpp, src/main.cpp, src/ui/pages/ComicsPage.h, src/ui/pages/ComicsPage.cpp, CMakeLists.txt, agents/chat.md
```

---

## Phase 4 - Archive validation + atomic .tankoban-part finalization + expectedInfoHash check + quarantine

**Phase goal:** Replace the simple-copy `finalizeCompletion` from Phase 3 with the full discipline from Codex section 21: (1) `flushCache(infoHash)`, (2) atomic copy to `.tankoban-part` outside the scanner glob, (3) archive validation (opens, `.cbz` only, all entries images, page-count bound, decompressed first-image bounded), (4) rename `.tankoban-part` -> `.cbz`. Failed validation routes the file to `<appData>/manga_premium_quarantine/` and emits `volumeFailed` with a stable error code. Also: validate `expectedInfoHash` against the engine's reported infoHash before applying file priorities (Codex section 24 trust requirement).

**Files this phase touches:**

- Create: `src/core/manga/PremiumArchiveValidator.h`
- Create: `src/core/manga/PremiumArchiveValidator.cpp`
- Modify: `src/core/manga/TorrentVolumeProvider.cpp` (rewrite `finalizeCompletion`; add `expectedInfoHash` check in `onMetadataReady`)
- Modify: `CMakeLists.txt` (add new sources)

**Reference brainstorm sections:** Section 21 (cover extraction + archive integrity lifecycle), Section 24 (`expectedInfoHash` + cbz-only + bounded decompression v1 requirements), Section 27.4 (validation severity).

### Task 4.1: Implement PremiumArchiveValidator

**Files:**
- Create: `src/core/manga/PremiumArchiveValidator.h`
- Create: `src/core/manga/PremiumArchiveValidator.cpp`

- [ ] **Step 4.1.1: Write the header**

```cpp
// src/core/manga/PremiumArchiveValidator.h
#pragma once

#include <QString>
#include <QStringList>
#include <cstdint>

namespace tankoban::manga::premium {

enum class ArchiveValidationCode {
    Ok,
    NotCbzExtension,
    OpenFailed,
    Empty,
    NonImageEntry,
    NestedArchiveEntry,
    ExecutableEntry,
    PageCountExceedsBound,
    PageCountMismatchCatalog,
    DecompressedFirstImageTooLarge,
    ReadFailed
};

struct ArchiveValidationResult {
    ArchiveValidationCode  code        = ArchiveValidationCode::Ok;
    QString                detail;
    int                    pageCount   = 0;
    QStringList            imageEntries; // in scanner-natural sort order, used by Phase 10 cover extractor
};

// Pre-finalization validator for Premium-downloaded cbz files. Bounds:
//   - kMaxPagesPerVolume   = 600  (catalog warns above 600, validator rejects above 1000)
//   - kMaxFirstImageBytes  = 64 MiB (zip-bomb defense per Codex section 24.5)
//   - kMaxEntryCount       = 2000 (very high, but bounded)
//
// `expectedPageCount` is 0 when the catalog did not provide a hint; the
// validator then only enforces the absolute bound. When non-zero, validator
// fails on a mismatch outside [-3, +3] tolerance (catalog drift between
// editions is real; a hard match is too strict).
class PremiumArchiveValidator
{
public:
    static constexpr int    kMaxPagesPerVolume       = 1000;
    static constexpr qint64 kMaxFirstImageBytes      = 64 * 1024 * 1024;
    static constexpr int    kMaxEntryCount           = 2000;
    static constexpr int    kCatalogPageCountTolerance = 3;

    static ArchiveValidationResult validate(const QString& cbzPath,
                                            int expectedPageCount = 0);
};

} // namespace tankoban::manga::premium
```

- [ ] **Step 4.1.2: Implement the validator**

```cpp
// src/core/manga/PremiumArchiveValidator.cpp
#include "PremiumArchiveValidator.h"

#include <QFileInfo>
#include <QDebug>

#ifdef HAS_QT_ZIP
#  include <private/qzipreader_p.h>
#endif

namespace tankoban::manga::premium {

namespace {

bool isImageExtension(const QString& lowerExt)
{
    return lowerExt == QLatin1String("jpg")
        || lowerExt == QLatin1String("jpeg")
        || lowerExt == QLatin1String("png")
        || lowerExt == QLatin1String("webp")
        || lowerExt == QLatin1String("gif")
        || lowerExt == QLatin1String("bmp")
        || lowerExt == QLatin1String("avif");
}

bool isNestedArchiveExtension(const QString& lowerExt)
{
    return lowerExt == QLatin1String("zip")
        || lowerExt == QLatin1String("rar")
        || lowerExt == QLatin1String("7z")
        || lowerExt == QLatin1String("cbz")
        || lowerExt == QLatin1String("cbr")
        || lowerExt == QLatin1String("tar")
        || lowerExt == QLatin1String("gz");
}

bool isExecutableExtension(const QString& lowerExt)
{
    return lowerExt == QLatin1String("exe")
        || lowerExt == QLatin1String("dll")
        || lowerExt == QLatin1String("bat")
        || lowerExt == QLatin1String("cmd")
        || lowerExt == QLatin1String("ps1")
        || lowerExt == QLatin1String("sh")
        || lowerExt == QLatin1String("scr")
        || lowerExt == QLatin1String("msi")
        || lowerExt == QLatin1String("vbs")
        || lowerExt == QLatin1String("js");
}

ArchiveValidationResult fail(ArchiveValidationCode code, const QString& detail)
{
    ArchiveValidationResult r;
    r.code   = code;
    r.detail = detail;
    return r;
}

} // anonymous namespace

ArchiveValidationResult
PremiumArchiveValidator::validate(const QString& cbzPath, int expectedPageCount)
{
    const QFileInfo fi(cbzPath);
    if (fi.suffix().toLower() != QLatin1String("cbz")) {
        return fail(ArchiveValidationCode::NotCbzExtension,
                    QStringLiteral("file extension is not .cbz"));
    }

#ifndef HAS_QT_ZIP
    return fail(ArchiveValidationCode::OpenFailed,
                QStringLiteral("HAS_QT_ZIP not defined; QZipReader unavailable"));
#else
    QZipReader zr(cbzPath);
    if (!zr.exists() || !zr.isReadable()) {
        return fail(ArchiveValidationCode::OpenFailed,
                    QStringLiteral("QZipReader could not open archive"));
    }

    const auto infos = zr.fileInfoList();
    if (infos.isEmpty()) {
        return fail(ArchiveValidationCode::Empty,
                    QStringLiteral("archive contains zero entries"));
    }
    if (infos.size() > kMaxEntryCount) {
        return fail(ArchiveValidationCode::Empty,
                    QStringLiteral("archive entry count exceeds bound %1")
                       .arg(kMaxEntryCount));
    }

    ArchiveValidationResult ok;
    ok.code = ArchiveValidationCode::Ok;

    int firstImageIdx = -1;
    for (int i = 0; i < infos.size(); ++i) {
        const auto& info = infos.at(i);
        if (!info.isFile) continue;
        const QString name      = info.filePath;
        const QString lowerExt  = QFileInfo(name).suffix().toLower();

        if (isNestedArchiveExtension(lowerExt)) {
            return fail(ArchiveValidationCode::NestedArchiveEntry,
                        QStringLiteral("nested archive entry: ") + name);
        }
        if (isExecutableExtension(lowerExt)) {
            return fail(ArchiveValidationCode::ExecutableEntry,
                        QStringLiteral("executable entry: ") + name);
        }
        if (!isImageExtension(lowerExt)) {
            // Some scanners include .txt (credits) or .nfo (info) alongside
            // images. Per Codex section 21 step 4 these are "used for reading"
            // checks - non-image entries are rejected to keep Premium archives
            // strict. v1 ships strict; the curator helper warned on non-cbz
            // top-level files but here we are inside the cbz.
            return fail(ArchiveValidationCode::NonImageEntry,
                        QStringLiteral("non-image entry: ") + name);
        }
        ok.imageEntries.append(name);
        if (firstImageIdx < 0) firstImageIdx = i;
    }

    ok.pageCount = ok.imageEntries.size();
    if (ok.pageCount == 0) {
        return fail(ArchiveValidationCode::Empty,
                    QStringLiteral("archive has no image entries"));
    }
    if (ok.pageCount > kMaxPagesPerVolume) {
        return fail(ArchiveValidationCode::PageCountExceedsBound,
                    QStringLiteral("pageCount %1 exceeds bound %2")
                        .arg(ok.pageCount).arg(kMaxPagesPerVolume));
    }
    if (expectedPageCount > 0 &&
        qAbs(ok.pageCount - expectedPageCount) > kCatalogPageCountTolerance) {
        return fail(ArchiveValidationCode::PageCountMismatchCatalog,
                    QStringLiteral("pageCount %1 differs from catalog %2 by more than tolerance %3")
                        .arg(ok.pageCount).arg(expectedPageCount).arg(kCatalogPageCountTolerance));
    }

    // Bounded decompression check on the first image. QZipReader::fileData
    // decompresses into memory; bounding the entry's uncompressed size first
    // (when the central directory carries it) catches zip-bombs before the
    // decompress.
    if (firstImageIdx >= 0) {
        const auto& info = infos.at(firstImageIdx);
        if (info.size > kMaxFirstImageBytes) {
            return fail(ArchiveValidationCode::DecompressedFirstImageTooLarge,
                        QStringLiteral("first image declared size %1 exceeds bound %2")
                            .arg(info.size).arg(kMaxFirstImageBytes));
        }
        const QByteArray data = zr.fileData(info.filePath);
        if (data.isEmpty()) {
            return fail(ArchiveValidationCode::ReadFailed,
                        QStringLiteral("first image read returned empty data"));
        }
        if (data.size() > kMaxFirstImageBytes) {
            return fail(ArchiveValidationCode::DecompressedFirstImageTooLarge,
                        QStringLiteral("first image decompressed size %1 exceeds bound %2")
                            .arg(data.size()).arg(kMaxFirstImageBytes));
        }
    }

    return ok;
#endif
}

} // namespace tankoban::manga::premium
```

- [ ] **Step 4.1.3: Wire into CMakeLists.txt + build-verify**

Add to the source list:

```cmake
    src/core/manga/PremiumArchiveValidator.cpp
    src/core/manga/PremiumArchiveValidator.h
```

Run: `build_check.bat`
Expected: `BUILD OK`. If you see "QZipReader unavailable" path triggered at runtime later, the `HAS_QT_ZIP` macro and `Qt6::CorePrivate` link are working as the existing ComicReader path (sanity-check by searching `src/core/ArchiveReader.cpp` for the same `#ifdef HAS_QT_ZIP` block).

### Task 4.2: Add expectedInfoHash check in onMetadataReady

**Files:**
- Modify: `src/core/manga/TorrentVolumeProvider.cpp`

- [ ] **Step 4.2.1: Reject mismatch before applying priorities**

In `TorrentVolumeProvider::onMetadataReady`, BEFORE the `applyUnionPriorities` call, insert:

```cpp
    // Per Codex section 24 v1 requirement #1: app rejects a magnet that
    // resolves to an infoHash different from the catalog's expectedInfoHash.
    // The provider was constructed with that expectation; verify before any
    // download starts.
    for (const auto& iff : *it) {
        if (iff.expectedInfoHash != infoHash.toLower()) {
            m_engine->removeTorrent(infoHash.toLower(), /*deleteFiles=*/true);
            m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Failed,
                                   QStringLiteral("infohash_mismatch"),
                                   QStringLiteral("metadata resolved to ") + infoHash +
                                   QStringLiteral(" but catalog expected ") + iff.expectedInfoHash);
            emit volumeFailed(iff.seriesId, iff.volumeNumber,
                              QStringLiteral("infohash_mismatch"),
                              QStringLiteral("metadata infoHash does not match catalog"));
        }
    }
    // After the rejection emits, drop the in-flight bucket entirely.
    if (it->isEmpty() || it->at(0).expectedInfoHash != infoHash.toLower()) {
        m_byInfoHash.remove(infoHash.toLower());
        return;
    }
```

Reasoning: since `requestVolume` keys `m_byInfoHash` by the catalog's `expectedInfoHash`, this branch fires only when libtorrent's resolved metadata infoHash differs from what the catalog claimed. In practice the keys agree because libtorrent resolves the magnet's `xt=urn:btih:` value, but Codex's trust contract says verify anyway - a sophisticated attacker could craft a magnet whose xt looks correct but whose actual metadata resolves to a different infoHash via tracker manipulation, though this is theoretical.

### Task 4.3: Rewrite finalizeCompletion with .tankoban-part + validation + quarantine

**Files:**
- Modify: `src/core/manga/TorrentVolumeProvider.cpp`

- [ ] **Step 4.3.1: Replace the Phase-3 finalizeCompletion body**

Open `src/core/manga/TorrentVolumeProvider.cpp` and locate the existing `finalizeCompletion` function. Replace its body entirely with:

```cpp
void TorrentVolumeProvider::finalizeCompletion(Inflight iff)
{
    m_engine->flushCache(iff.expectedInfoHash);
    m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Validating);

    const QString stagingFile = iff.stagingPath + QChar('/') + iff.cbzFileName;
    const QString finalFile   = iff.canonicalDestinationPath;
    const QString partFile    = finalFile + QStringLiteral(".tankoban-part");

    QFileInfo fi(finalFile);
    QDir().mkpath(fi.absolutePath());

    // Collision guard per Codex section 18: never overwrite existing cbz.
    if (QFile::exists(finalFile)) {
        m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Failed,
                               QStringLiteral("destination_exists"),
                               QStringLiteral("refusing to overwrite ") + finalFile);
        emit volumeFailed(iff.seriesId, iff.volumeNumber,
                          QStringLiteral("destination_exists"),
                          QStringLiteral("refusing to overwrite ") + finalFile);
        auto bucket = m_byInfoHash.find(iff.expectedInfoHash);
        if (bucket != m_byInfoHash.end()) {
            bucket->erase(std::remove_if(bucket->begin(), bucket->end(),
                [&](const Inflight& x){ return x.requestKey == iff.requestKey; }),
                bucket->end());
        }
        return;
    }

    // Step 1: copy from staging to .tankoban-part (outside scanner glob).
    if (QFile::exists(partFile)) QFile::remove(partFile);
    if (!QFile::copy(stagingFile, partFile)) {
        m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Failed,
                               QStringLiteral("copy_to_part_failed"),
                               QStringLiteral("could not copy ") + stagingFile +
                               QStringLiteral(" -> ") + partFile);
        emit volumeFailed(iff.seriesId, iff.volumeNumber,
                          QStringLiteral("copy_to_part_failed"), QString());
        return;
    }

    // Step 2: validate the archive. Phase 1 catalog stored pageCount as a
    // hint; pass it for the optional mismatch check.
    int expectedPages = 0;
    if (auto entry = m_catalog ? m_catalog->entryById(iff.seriesId) : std::nullopt) {
        for (const auto& v : entry->volumes) {
            if (v.vol == iff.volumeNumber) { expectedPages = v.pageCount; break; }
        }
    }
    const auto vr = PremiumArchiveValidator::validate(partFile, expectedPages);
    if (vr.code != ArchiveValidationCode::Ok) {
        // Quarantine: move .tankoban-part to <appData>/manga_premium_quarantine/
        const QString quarantineDir = QStandardPaths::writableLocation(
                                          QStandardPaths::AppDataLocation)
                                    + QStringLiteral("/manga_premium_quarantine");
        QDir().mkpath(quarantineDir);
        const QString quarantineName = QStringLiteral("%1_v%2_%3.cbz.bad")
            .arg(iff.seriesId)
            .arg(iff.volumeNumber, 2, 10, QChar('0'))
            .arg(QDateTime::currentMSecsSinceEpoch());
        const QString quarantinePath = quarantineDir + QChar('/') + quarantineName;
        QFile::rename(partFile, quarantinePath);

        const QString code = [c = vr.code]{
            switch (c) {
                case ArchiveValidationCode::NotCbzExtension:               return QStringLiteral("not_cbz");
                case ArchiveValidationCode::OpenFailed:                    return QStringLiteral("open_failed");
                case ArchiveValidationCode::Empty:                         return QStringLiteral("archive_empty");
                case ArchiveValidationCode::NonImageEntry:                 return QStringLiteral("non_image_entry");
                case ArchiveValidationCode::NestedArchiveEntry:            return QStringLiteral("nested_archive");
                case ArchiveValidationCode::ExecutableEntry:               return QStringLiteral("executable_entry");
                case ArchiveValidationCode::PageCountExceedsBound:         return QStringLiteral("page_count_too_high");
                case ArchiveValidationCode::PageCountMismatchCatalog:      return QStringLiteral("page_count_mismatch");
                case ArchiveValidationCode::DecompressedFirstImageTooLarge: return QStringLiteral("first_image_too_large");
                case ArchiveValidationCode::ReadFailed:                    return QStringLiteral("read_failed");
                case ArchiveValidationCode::Ok:                            return QStringLiteral("ok");
            }
            return QStringLiteral("unknown");
        }();

        m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Failed,
                               code, vr.detail);
        emit volumeFailed(iff.seriesId, iff.volumeNumber, code, vr.detail);
        qWarning().noquote() << QStringLiteral("[TorrentVolumeProvider] quarantined:")
                             << quarantinePath << code << vr.detail;
        return;
    }

    // Step 3: atomic rename .tankoban-part -> .cbz.
    if (!QFile::rename(partFile, finalFile)) {
        m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Failed,
                               QStringLiteral("final_rename_failed"),
                               QStringLiteral("could not rename .tankoban-part to .cbz"));
        emit volumeFailed(iff.seriesId, iff.volumeNumber,
                          QStringLiteral("final_rename_failed"), QString());
        return;
    }

    m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Completed);

    auto bucket = m_byInfoHash.find(iff.expectedInfoHash);
    if (bucket != m_byInfoHash.end()) {
        bucket->erase(std::remove_if(bucket->begin(), bucket->end(),
            [&](const Inflight& x){ return x.requestKey == iff.requestKey; }),
            bucket->end());
    }

    emit volumeCompleted(iff.seriesId, iff.volumeNumber, finalFile);

    qDebug().noquote() << QStringLiteral("[TorrentVolumeProvider] completed:")
                       << iff.seriesId << QStringLiteral("v") + QString::number(iff.volumeNumber)
                       << QStringLiteral("at") << finalFile
                       << QStringLiteral("pages=") + QString::number(vr.pageCount);
}
```

Add the necessary includes at the top of `TorrentVolumeProvider.cpp`:

```cpp
#include "PremiumArchiveValidator.h"
#include <QStandardPaths>
```

- [ ] **Step 4.3.2: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 4.4: Phase 4 close - RTC line

- [ ] **Step 4.4.1: Append the phase-close RTC line**

```
READY TO COMMIT - [Agent 1, TANKOYOMI_PREMIUM Phase 4 - Archive validation + atomic .tankoban-part finalization + expectedInfoHash check + quarantine. New: PremiumArchiveValidator.h/.cpp (QZipReader-based; .cbz-only + all-entries-images + no-nested-archives + no-executables + page-count bound + decompressed-first-image bound per Codex section 24 v1 trust requirements). TorrentVolumeProvider.cpp rewrites finalizeCompletion with the section-21 lifecycle: flushCache -> copy-to-.tankoban-part (outside scanner glob so LibraryScanner cannot see a partial archive per section 21) -> validate -> atomic rename to .cbz on success; on failure, move .tankoban-part to <appData>/manga_premium_quarantine/<seriesId>_v<NN>_<msEpoch>.cbz.bad + emit volumeFailed with stable code. onMetadataReady gates startTorrent on infoHash equality against catalog expectedInfoHash; mismatch triggers removeTorrent(deleteFiles=true) + Failed status + volumeFailed("infohash_mismatch"). Validator uses HAS_QT_ZIP path via Qt6::CorePrivate matching existing ArchiveReader pattern. build_check green.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/core/manga/PremiumArchiveValidator.h, src/core/manga/PremiumArchiveValidator.cpp, src/core/manga/TorrentVolumeProvider.cpp, CMakeLists.txt, agents/chat.md
```

---

## Phase 5 - MangaDownloadIndex registerVolume primitive + canonical chapter keys + provider wiring

**Phase goal:** Per Codex section 17.5, reusing `registerChapter` once per chapter for a single volume cbz is unsafe: 10 chapter keys all point at one `canonicalKey`, and evicting one chapter via `evictByChapter` strands the other nine. Phase 5 adds a `registerVolume(sourceId, seriesId, vol, canonicalPath, fileSizeBytes, chapterKeys)` primitive that creates ONE `m_byPath` entry serving N `m_byChapter` entries safely, with a chapter-key set on the entry that gates path eviction until the set is empty. `TorrentVolumeProvider::finalizeCompletion` is updated to call `registerVolume` between the atomic rename and the `volumeCompleted` emit, per Codex section 18 step 5.

**Files this phase touches:**

- Modify: `src/core/manga/MangaDownloadIndex.h` (extend Entry, add registerVolume, update evictByChapter contract)
- Modify: `src/core/manga/MangaDownloadIndex.cpp` (implement)
- Modify: `src/core/manga/TorrentVolumeProvider.h` (add MangaDownloadIndex* ctor arg)
- Modify: `src/core/manga/TorrentVolumeProvider.cpp` (call registerVolume in finalizeCompletion)
- Modify: `src/ui/pages/ComicsPage.cpp` (pass m_downloadIndex into provider ctor)

**Reference brainstorm sections:** Section 17.5 (index primitive correction), Section 18 step 5 (completion serialization order), Section 23 (canonical chapter keys for ongoing-series migration), Section 27.2 (registerVolume recommendation).

### Task 5.1: Extend the Entry struct + add registerVolume signature

**Files:**
- Modify: `src/core/manga/MangaDownloadIndex.h`

- [ ] **Step 5.1.1: Extend Entry to carry the chapter key set**

In `src/core/manga/MangaDownloadIndex.h:24-31`, replace the existing `struct Entry` block with:

```cpp
    struct Entry {
        QString sourceId;
        QString seriesId;
        QString chapterId;          // first chapter that registered the entry; legacy single-chapter path
        QString canonicalPath;
        qint64  addedAt        = 0;
        qint64  fileSizeBytes  = 0;

        // Phase 5 (Premium): chapter keys served by this entry. For
        // legacy single-chapter cbz files (from MangaDownloader), this
        // set holds exactly one element matching computeChapterKey(
        // sourceId, seriesId, chapterId). For Premium volume cbz files,
        // it holds N elements - one per chapter contained in the volume.
        // evictByChapter removes one element at a time; the m_byPath
        // entry is only dropped when the set becomes empty.
        QSet<QString> servedChapterKeys;
    };
```

- [ ] **Step 5.1.2: Declare registerVolume**

In the public section of `MangaDownloadIndex`, immediately after the existing `registerChapter` declaration:

```cpp
    // Phase 5 (Premium): register a volume cbz that serves multiple chapters.
    // sourceId is the catalog source ("tankoyomi_premium"); chapterIds is
    // the list of chapter identifiers contained in this volume. The same
    // canonicalPath gets ONE m_byPath entry whose servedChapterKeys holds
    // N elements. evictByChapter removes one chapter at a time; the entry
    // only disappears when servedChapterKeys is empty.
    //
    // Calling registerVolume twice with the same canonicalPath and an
    // identical chapterIds set is idempotent (no-op). Calling with a
    // different chapterIds set extends the served set.
    void registerVolume(const QString&       sourceId,
                         const QString&       seriesId,
                         int                  volumeNumber,
                         const QString&       canonicalPath,
                         qint64               fileSizeBytes,
                         const QStringList&   chapterIds);
```

### Task 5.2: Implement registerVolume + update evictByChapter

**Files:**
- Modify: `src/core/manga/MangaDownloadIndex.cpp`

- [ ] **Step 5.2.1: Find the existing registerChapter implementation**

Open `src/core/manga/MangaDownloadIndex.cpp`, locate the existing `MangaDownloadIndex::registerChapter(...)` function body. Modify it so that the `Entry` constructed there populates `servedChapterKeys`:

```cpp
void MangaDownloadIndex::registerChapter(const QString& sourceId, const QString& seriesId,
                                         const QString& chapterId, const QString& canonicalPath,
                                         qint64 fileSizeBytes)
{
    const QString canonicalKey = computeCanonicalKey(canonicalPath);
    const QString chapterKey   = computeChapterKey(sourceId, seriesId, chapterId);
    {
        QMutexLocker locker(&m_mutex);
        // Existing m_byPath upsert logic... preserve it. After the Entry is
        // either inserted or located, add this chapterKey to its
        // servedChapterKeys set:
        auto it = m_byPath.find(canonicalKey);
        if (it == m_byPath.end()) {
            Entry e;
            e.sourceId       = sourceId;
            e.seriesId       = seriesId;
            e.chapterId      = chapterId;
            e.canonicalPath  = canonicalPath;
            e.addedAt        = QDateTime::currentMSecsSinceEpoch();
            e.fileSizeBytes  = fileSizeBytes;
            e.servedChapterKeys.insert(chapterKey);
            m_byPath.insert(canonicalKey, e);
        } else {
            it->servedChapterKeys.insert(chapterKey);
            // Preserve first-recorded chapterId field; only refresh fileSizeBytes if larger.
            if (fileSizeBytes > it->fileSizeBytes) it->fileSizeBytes = fileSizeBytes;
        }
        m_byChapter.insert(chapterKey, canonicalKey);
        m_seriesHasAny.insert(computeSeriesKey(sourceId, seriesId));
    }
    save();
    emit entriesChanged();
}
```

This refactor preserves the existing `registerChapter` external contract (legacy callers from `MangaDownloader` are unaffected) while making the entry's `servedChapterKeys` set the authoritative answer to "how many chapter keys depend on this path."

- [ ] **Step 5.2.2: Implement registerVolume**

Append the new method (immediately after the modified `registerChapter`):

```cpp
void MangaDownloadIndex::registerVolume(const QString&     sourceId,
                                        const QString&     seriesId,
                                        int                volumeNumber,
                                        const QString&     canonicalPath,
                                        qint64             fileSizeBytes,
                                        const QStringList& chapterIds)
{
    Q_UNUSED(volumeNumber)
    const QString canonicalKey = computeCanonicalKey(canonicalPath);
    QSet<QString> newKeys;
    for (const auto& cid : chapterIds) {
        if (cid.isEmpty()) continue;
        newKeys.insert(computeChapterKey(sourceId, seriesId, cid));
    }
    if (newKeys.isEmpty()) return;

    {
        QMutexLocker locker(&m_mutex);
        auto it = m_byPath.find(canonicalKey);
        if (it == m_byPath.end()) {
            Entry e;
            e.sourceId           = sourceId;
            e.seriesId           = seriesId;
            // Pick the first chapter id as the legacy single-chapter field.
            e.chapterId          = chapterIds.isEmpty() ? QString() : chapterIds.first();
            e.canonicalPath      = canonicalPath;
            e.addedAt            = QDateTime::currentMSecsSinceEpoch();
            e.fileSizeBytes      = fileSizeBytes;
            e.servedChapterKeys  = newKeys;
            m_byPath.insert(canonicalKey, e);
        } else {
            for (const auto& k : newKeys) it->servedChapterKeys.insert(k);
            if (fileSizeBytes > it->fileSizeBytes) it->fileSizeBytes = fileSizeBytes;
        }
        for (const auto& k : newKeys) m_byChapter.insert(k, canonicalKey);
        m_seriesHasAny.insert(computeSeriesKey(sourceId, seriesId));
    }
    save();
    emit entriesChanged();
}
```

- [ ] **Step 5.2.3: Update evictByChapter to gate m_byPath removal on the served set**

Locate the existing `evictByChapter`. Replace its body with:

```cpp
void MangaDownloadIndex::evictByChapter(const QString& sourceId, const QString& seriesId,
                                        const QString& chapterId)
{
    const QString chapterKey = computeChapterKey(sourceId, seriesId, chapterId);
    QString canonicalKeyToCheck;
    {
        QMutexLocker locker(&m_mutex);
        const auto chIt = m_byChapter.constFind(chapterKey);
        if (chIt == m_byChapter.constEnd()) return;
        canonicalKeyToCheck = chIt.value();
        m_byChapter.remove(chapterKey);

        auto pIt = m_byPath.find(canonicalKeyToCheck);
        if (pIt != m_byPath.end()) {
            pIt->servedChapterKeys.remove(chapterKey);
            if (pIt->servedChapterKeys.isEmpty()) {
                m_byPath.erase(pIt);
            }
        }
        recomputeSeriesHasAnyLocked(sourceId, seriesId);
    }
    save();
    emit entriesChanged();
}
```

The key change vs. the pre-Phase-5 version: removing one chapter key only drops the `m_byPath` entry when the served set becomes empty. Volume cbzs survive partial eviction.

- [ ] **Step 5.2.4: Update the load+save JSON serialization**

The `Entry` now carries `servedChapterKeys`; the JSON shape must persist it. Find the existing `MangaDownloadIndex::save()` and `MangaDownloadIndex::load()`. In `save()`, when serializing each entry, add:

```cpp
        QJsonArray keysJson;
        for (const auto& k : entry.servedChapterKeys) keysJson.append(k);
        entryJson[QStringLiteral("servedChapterKeys")] = keysJson;
```

In `load()`, when deserializing each entry, add (after the existing field reads):

```cpp
        const QJsonArray keysJson = obj.value(QStringLiteral("servedChapterKeys")).toArray();
        if (keysJson.isEmpty()) {
            // Backward-compat: pre-Phase-5 entries had implicit single-chapter
            // ownership. Reconstruct from the legacy chapterId field.
            e.servedChapterKeys.insert(
                computeChapterKey(e.sourceId, e.seriesId, e.chapterId));
        } else {
            for (const auto& v : keysJson) {
                const QString s = v.toString();
                if (!s.isEmpty()) e.servedChapterKeys.insert(s);
            }
        }
```

Bump the schema version constant from 1 to 2:

```cpp
    static constexpr int kSchemaVersion = 2;
```

The load path's backward-compat reconstruction means v1-schema files load cleanly into the v2 in-memory shape; first save after that writes v2.

- [ ] **Step 5.2.5: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 5.3: Wire MangaDownloadIndex into TorrentVolumeProvider + call registerVolume in finalizeCompletion

**Files:**
- Modify: `src/core/manga/TorrentVolumeProvider.h`
- Modify: `src/core/manga/TorrentVolumeProvider.cpp`
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 5.3.1: Extend the provider constructor**

In `src/core/manga/TorrentVolumeProvider.h`, add a forward declaration at the top of the header (alongside `class TorrentEngine;`):

```cpp
class MangaDownloadIndex;
```

Extend the constructor signature to take the index pointer:

```cpp
    TorrentVolumeProvider(TorrentEngine*          engine,
                          PremiumCatalog*         catalog,
                          TorrentRequestLedger*   ledger,
                          MangaDownloadIndex*     index,
                          const QString&          stagingRoot,
                          QObject*                parent = nullptr);
```

Add a private member:

```cpp
    QPointer<MangaDownloadIndex>              m_index;
```

- [ ] **Step 5.3.2: Update the provider constructor body**

In `src/core/manga/TorrentVolumeProvider.cpp`, replace the constructor's parameter list + member init list to accept and store `index`:

```cpp
TorrentVolumeProvider::TorrentVolumeProvider(TorrentEngine*        engine,
                                             PremiumCatalog*       catalog,
                                             TorrentRequestLedger* ledger,
                                             MangaDownloadIndex*   index,
                                             const QString&        stagingRoot,
                                             QObject*              parent)
    : QObject(parent)
    , m_engine(engine)
    , m_catalog(catalog)
    , m_ledger(ledger)
    , m_index(index)
    , m_stagingRoot(stagingRoot)
{
    Q_ASSERT(m_engine);
    Q_ASSERT(m_catalog);
    Q_ASSERT(m_ledger);
    Q_ASSERT(m_index);
    QDir().mkpath(m_stagingRoot);
    // ... existing connect block unchanged ...
}
```

Add the include at the top:

```cpp
#include "MangaDownloadIndex.h"
```

- [ ] **Step 5.3.3: Insert registerVolume between rename and emit in finalizeCompletion**

In `finalizeCompletion`, locate the line:

```cpp
    m_ledger->updateStatus(iff.requestKey, TorrentRequest::Status::Completed);
```

Immediately BEFORE the existing `auto bucket = m_byInfoHash.find(...)` line that follows, insert:

```cpp
    // Per Codex section 18 step 5: register the volume in the index BEFORE
    // emitting volumeCompleted so UI receivers that consult the index on
    // signal find the row populated.
    if (auto entryOpt = m_catalog ? m_catalog->entryById(iff.seriesId) : std::nullopt) {
        QStringList chapterIds;
        for (const auto& v : entryOpt->volumes) {
            if (v.vol != iff.volumeNumber) continue;
            for (const auto& ch : v.chapters) {
                if (!ch.chapterNumber.isEmpty()) chapterIds.append(ch.chapterNumber);
            }
            break;
        }
        if (m_index && !chapterIds.isEmpty()) {
            m_index->registerVolume(QStringLiteral("tankoyomi_premium"),
                                    iff.seriesId,
                                    iff.volumeNumber,
                                    finalFile,
                                    QFileInfo(finalFile).size(),
                                    chapterIds);
        }
    }
```

- [ ] **Step 5.3.4: Pass the index into ComicsPage's provider construction**

In `src/ui/pages/ComicsPage.cpp`, update the `m_premiumProvider = new tankoban::manga::premium::TorrentVolumeProvider(...)` call to include `m_downloadIndex`:

```cpp
    m_premiumProvider = new tankoban::manga::premium::TorrentVolumeProvider(
        /*engine=*/m_torrentEngine,
        /*catalog=*/m_premiumCatalog,
        /*ledger=*/m_premiumLedger,
        /*index=*/m_downloadIndex,
        /*stagingRoot=*/premiumStagingRoot,
        /*parent=*/this);
```

Grep `ComicsPage.h` for the existing `MangaDownloadIndex*` member name (likely `m_downloadIndex` per the merger arc); adjust if the local name differs.

- [ ] **Step 5.3.5: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 5.4: Smoke - registerVolume contract round-trip

This task verifies registerVolume + evictByChapter cooperate correctly on a multi-chapter cbz. Phase 3's qDebug-only signal handlers are still in place; we'll verify by inspecting the JSON file directly.

- [ ] **Step 5.4.1: Inject a one-shot test call after replayLedger**

In `src/ui/pages/ComicsPage.cpp`, temporarily add the following block immediately after `m_premiumProvider->replayLedger();` (this is a smoke-only block; delete it at task 5.4.4):

```cpp
    // PHASE 5 SMOKE - delete after verification
    {
        const QString testPath = QCoreApplication::applicationDirPath()
                              + QStringLiteral("/_phase5_smoke.cbz");
        QFile f(testPath); f.open(QIODevice::WriteOnly); f.write("PK\003\004"); f.close();
        m_downloadIndex->registerVolume(QStringLiteral("tankoyomi_premium"),
                                        QStringLiteral("phase5_smoke"), 1,
                                        testPath, 4,
                                        QStringList{QStringLiteral("1"),
                                                    QStringLiteral("2"),
                                                    QStringLiteral("3")});
        qDebug() << "[PHASE 5 SMOKE] registered volume; hasAnyForSeries="
                 << m_downloadIndex->hasAnyForSeries(QStringLiteral("tankoyomi_premium"),
                                                    QStringLiteral("phase5_smoke"));
        m_downloadIndex->evictByChapter(QStringLiteral("tankoyomi_premium"),
                                        QStringLiteral("phase5_smoke"),
                                        QStringLiteral("1"));
        qDebug() << "[PHASE 5 SMOKE] evicted chapter 1; hasAnyForSeries="
                 << m_downloadIndex->hasAnyForSeries(QStringLiteral("tankoyomi_premium"),
                                                    QStringLiteral("phase5_smoke"));
        m_downloadIndex->evictByChapter(QStringLiteral("tankoyomi_premium"),
                                        QStringLiteral("phase5_smoke"),
                                        QStringLiteral("2"));
        m_downloadIndex->evictByChapter(QStringLiteral("tankoyomi_premium"),
                                        QStringLiteral("phase5_smoke"),
                                        QStringLiteral("3"));
        qDebug() << "[PHASE 5 SMOKE] evicted all; hasAnyForSeries="
                 << m_downloadIndex->hasAnyForSeries(QStringLiteral("tankoyomi_premium"),
                                                    QStringLiteral("phase5_smoke"));
        QFile::remove(testPath);
    }
```

- [ ] **Step 5.4.2: Launch + observe**

```cmd
build_and_run.bat
```

After window appears:

```cmd
out\tankoctl.exe logs 200
```

Expected lines in order:

```
[PHASE 5 SMOKE] registered volume; hasAnyForSeries=true
[PHASE 5 SMOKE] evicted chapter 1; hasAnyForSeries=true
[PHASE 5 SMOKE] evicted all; hasAnyForSeries=false
```

The middle line is the load-bearing assertion: evicting one of three chapters still reports `hasAnyForSeries=true` because the entry's `servedChapterKeys` set isn't empty yet. Pre-Phase-5 behavior would have dropped the `m_byPath` entry on the first eviction and returned `false`.

- [ ] **Step 5.4.3: Close app**

```cmd
taskkill /F /IM Tankoban.exe
```

- [ ] **Step 5.4.4: Remove the smoke block**

Delete the `// PHASE 5 SMOKE` block from `ComicsPage.cpp`. Run `build_check.bat`; expected: `BUILD OK`.

### Task 5.5: Phase 5 close - RTC line

```
READY TO COMMIT - [Agent 1, TANKOYOMI_PREMIUM Phase 5 - MangaDownloadIndex registerVolume + canonical chapter keys + provider wiring. Per Codex section 17.5 the existing registerChapter is unsafe as the long-term primitive for one-cbz-many-chapters (10 chapter keys + 1 path = evicting one strands nine). MangaDownloadIndex Entry struct extended with QSet<QString> servedChapterKeys; existing registerChapter preserves its external contract while populating the set with one element; new registerVolume(sourceId, seriesId, vol, canonicalPath, fileSizeBytes, chapterIds[]) creates ONE m_byPath entry serving N m_byChapter entries; evictByChapter only drops m_byPath when servedChapterKeys becomes empty. Schema bumped v1->v2 with backward-compat reconstruction in load(). TorrentVolumeProvider ctor extended with MangaDownloadIndex* arg; finalizeCompletion now calls registerVolume between atomic rename and volumeCompleted emit per Codex section 18 step 5 (file durable -> index registered -> signal emitted). Smoke green: 3-chapter test volume's hasAnyForSeries stays true across 1st + 2nd eviction; flips false only on 3rd. build_check green.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/core/manga/MangaDownloadIndex.h, src/core/manga/MangaDownloadIndex.cpp, src/core/manga/TorrentVolumeProvider.h, src/core/manga/TorrentVolumeProvider.cpp, src/ui/pages/ComicsPage.cpp, agents/chat.md
```

---

## Phase 6 - Detail view volume-row variant + ongoing-series gap rendering (using a local fixture)

**Phase goal:** Add the volume-row layout to `ComicsTankoyomiDetailView`. When `PremiumCatalog::entryForTitle(...)` returns an entry, the view switches from the existing chapter-row layout to a volume-row layout: cover thumbnail + label ("Volume N") + chapter-range ("Chs N-N") + chapter-count badge + Download / Read / Waiting-for-peers state button. For ongoing series, volumes render at top, a sticky section break renders next, then loose chapters from the existing WeebCentral fallback path render below. Verification uses a Phase-1-style fixture catalog containing one fake series with two volumes; real torrent downloads are NOT exercised in this phase.

**Files this phase touches:**

- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.h`
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp`
- Modify: `src/ui/pages/ComicsPage.cpp` (pass `PremiumCatalog*` into the detail view's openSeries call)
- Temp: `resources/manga_premium_catalogs/_phase6_fixture.json` (deleted before phase close)

**Reference brainstorm sections:** Section 4.2 (detail view layout + ASCII shape), Section 4.4 (read-state derivation), Section 7.4 (ongoing-series gap rendering), Section 26 (UX scale + sticky headers + row filter chips).

### Task 6.1: Surface the catalog through ComicsTankoyomiDetailView

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.h`
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp`

- [ ] **Step 6.1.1: Add the catalog pointer to the detail view**

In `src/ui/pages/comics/ComicsTankoyomiDetailView.h`:

Forward-declare near existing declarations:
```cpp
namespace tankoban::manga::premium { class PremiumCatalog; }
```

Add a setter and a member:
```cpp
public:
    void setPremiumCatalog(tankoban::manga::premium::PremiumCatalog* catalog);

private:
    tankoban::manga::premium::PremiumCatalog* m_premiumCatalog = nullptr;
```

In `ComicsTankoyomiDetailView.cpp`, implement the setter:

```cpp
void ComicsTankoyomiDetailView::setPremiumCatalog(
    tankoban::manga::premium::PremiumCatalog* catalog)
{
    m_premiumCatalog = catalog;
}
```

Include the header:

```cpp
#include "core/manga/PremiumCatalog.h"
```

- [ ] **Step 6.1.2: Wire the setter from ComicsPage**

In `src/ui/pages/ComicsPage.cpp`, locate where `m_tankoyomiDetailView` (the existing `ComicsTankoyomiDetailView*` member from the merger arc) is constructed. Immediately after construction, add:

```cpp
    m_tankoyomiDetailView->setPremiumCatalog(m_premiumCatalog);
```

- [ ] **Step 6.1.3: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 6.2: Add the volume-row layout branch

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp`

- [ ] **Step 6.2.1: Add a helper that decides view mode**

In `ComicsTankoyomiDetailView.cpp`, add a private helper. Pick a location near the top of the implementation (after the constructor):

```cpp
std::optional<tankoban::manga::premium::PremiumCatalogEntry>
ComicsTankoyomiDetailView::premiumEntryForCurrentSeries() const
{
    if (!m_premiumCatalog) return std::nullopt;
    // m_currentSeriesTitle is the existing member used by the merger-arc
    // chapter-row layout to label the hero. Grep the class for the actual
    // name; the merger plan introduced "m_currentSeries" in some refactors.
    return m_premiumCatalog->entryForTitle(m_currentSeriesTitle);
}
```

Declare it in the header's private section:

```cpp
    std::optional<tankoban::manga::premium::PremiumCatalogEntry>
        premiumEntryForCurrentSeries() const;
```

Include `<optional>` and `core/manga/PremiumCatalogSchema.h` in the header.

- [ ] **Step 6.2.2: Branch the row-builder**

Locate the function that populates the chapter table (the merger-arc plan introduced this; likely named `populateChapterTable()` or `refreshChapters()`). At the top of that function, add the branch:

```cpp
    const auto premiumOpt = premiumEntryForCurrentSeries();
    if (premiumOpt) {
        populateVolumeAndLooseTailTable(*premiumOpt);
        return;
    }
    // ...existing chapter-row population code unchanged below...
```

- [ ] **Step 6.2.3: Implement populateVolumeAndLooseTailTable**

Add the new function (declared in the header alongside the helper):

```cpp
void ComicsTankoyomiDetailView::populateVolumeAndLooseTailTable(
    const tankoban::manga::premium::PremiumCatalogEntry& entry)
{
    // Existing m_chapterTable is the QTableWidget the merger-arc Phase 5
    // wired in. Grep for its construction in the existing populate code
    // to confirm the column setup. For this layout we use the same widget
    // with a row-shape switch.
    m_chapterTable->clear();
    m_chapterTable->setRowCount(0);
    m_chapterTable->setColumnCount(5);
    m_chapterTable->setHorizontalHeaderLabels(QStringList{
        QStringLiteral("Cover"),
        QStringLiteral("Volume"),
        QStringLiteral("Chapters"),
        QStringLiteral("Status"),
        QStringLiteral("Action")
    });

    // Volumes (descending vol for ongoing per Codex section 26;
    // descending always for v1 since this matches Tankoyomi/Mihon expectation).
    QList<tankoban::manga::premium::PremiumVolumeEntry> volumes = entry.volumes;
    std::sort(volumes.begin(), volumes.end(),
              [](const auto& a, const auto& b){ return a.vol > b.vol; });

    for (const auto& v : volumes) {
        const int row = m_chapterTable->rowCount();
        m_chapterTable->insertRow(row);

        // Cover cell - placeholder for Phase 6; real extraction lands in Phase 10.
        auto* coverItem = new QTableWidgetItem();
        coverItem->setText(QString());
        coverItem->setData(Qt::UserRole, QStringLiteral("placeholder"));
        m_chapterTable->setItem(row, 0, coverItem);

        // Volume label
        m_chapterTable->setItem(row, 1, new QTableWidgetItem(
            QStringLiteral("Volume %1").arg(v.vol)));

        // Chapter range (use first/last numbers if available)
        QString chapterRange;
        if (!v.chapters.isEmpty()) {
            chapterRange = QStringLiteral("Chs %1-%2 (%3)")
                .arg(v.chapters.first().chapterNumber)
                .arg(v.chapters.last().chapterNumber)
                .arg(v.chapters.size());
        }
        m_chapterTable->setItem(row, 2, new QTableWidgetItem(chapterRange));

        // Status - resolved against MangaDownloadIndex in Phase 7
        m_chapterTable->setItem(row, 3, new QTableWidgetItem(
            QStringLiteral("Not downloaded")));

        // Action button - Phase 7 wires the click to TorrentVolumeProvider::requestVolume
        auto* btn = new QPushButton(QStringLiteral("Download"));
        btn->setProperty("premiumSeriesId", entry.seriesId);
        btn->setProperty("premiumVolume", v.vol);
        m_chapterTable->setCellWidget(row, 4, btn);
    }

    // Loose tail for ongoing series.
    if (entry.status == QStringLiteral("ongoing") &&
        !entry.postCoverageWeebcentralSlug.isEmpty()) {
        const int row = m_chapterTable->rowCount();
        m_chapterTable->insertRow(row);
        m_chapterTable->setSpan(row, 0, 1, 5);
        auto* hdr = new QTableWidgetItem(
            QStringLiteral("-- Latest chapters (WeebCentral) --"));
        hdr->setTextAlignment(Qt::AlignCenter);
        QFont f = hdr->font(); f.setBold(true); hdr->setFont(f);
        m_chapterTable->setItem(row, 0, hdr);

        // The actual loose-tail chapter rows come from the existing
        // WeebCentral chapter scrape. Reuse the existing chapter scraper
        // path - the same one the non-Premium branch uses. Filter to
        // chapters whose numeric value is greater than the last covered
        // chapter in entry.volumes.
        const QString lastCoveredChapter =
            (!volumes.isEmpty() && !volumes.first().chapters.isEmpty())
                ? volumes.first().chapters.last().chapterNumber
                : QString();
        appendLooseTailChaptersAfter(entry.postCoverageWeebcentralSlug,
                                     lastCoveredChapter);
    }
}
```

Declare `populateVolumeAndLooseTailTable` and `appendLooseTailChaptersAfter` in the header's private section.

`appendLooseTailChaptersAfter` reuses the existing WeebCentral fetch path. In the merger arc this path lives behind a `WeebCentralScraper::fetchChapters(slug)` call (grep `ComicsTankoyomiDetailView.cpp` for `WeebCentralScraper` to confirm the exact method). Implement it as a thin wrapper that calls the existing fetch and filters results numerically:

```cpp
void ComicsTankoyomiDetailView::appendLooseTailChaptersAfter(
    const QString& weebcentralSlug, const QString& lastCoveredChapterNum)
{
    // Reuse existing WeebCentralScraper instance (m_weebcentralScraper from
    // merger arc Phase 3). The scraper's chaptersFetched signal already
    // populates the row layout; this function just stashes the threshold so
    // the existing slot filters appropriately.
    m_looseTailThresholdChapterNum = lastCoveredChapterNum;
    m_weebcentralScraper->fetchChapters(weebcentralSlug);
    // The existing chaptersFetched slot will be modified in Phase 7 to
    // honor m_looseTailThresholdChapterNum when non-empty.
}
```

Declare the member `QString m_looseTailThresholdChapterNum;` in the header.

- [ ] **Step 6.2.4: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`. If the merger-arc method names differ (e.g. `m_chapterTable` is actually `m_chapterListView`), grep for the actual table widget member and adjust.

### Task 6.3: Drop in a Phase-6 fixture catalog + visual smoke

- [ ] **Step 6.3.1: Create the fixture**

Write `resources/manga_premium_catalogs/_phase6_fixture.json` (leading underscore so the loader skips it; but for this phase we want it loaded - use no underscore):

```json
{
  "id": "premium_phase6_fixture",
  "name": "Phase 6 fixture",
  "version": "0.0.6",
  "description": "Phase 6 visual smoke fixture. Delete before phase close.",
  "behaviorHints": { "p2p": true, "adult": false },
  "series": [
    {
      "seriesId": "phase6_smoke_one",
      "title": "Phase 6 Smoke One",
      "alternateTitles": [],
      "anilistId": 0,
      "status": "completed",
      "magnetUri": "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567",
      "expectedInfoHash": "0123456789abcdef0123456789abcdef01234567",
      "trustedUploader": "fixture",
      "releaseEdition": "fixture",
      "format": "one-cbz-per-volume",
      "volumes": [
        { "vol": 1, "fileIndex": 0, "fileSizeBytes": 1024, "pieceStart": 0, "pieceEnd": 0,
          "cbzFileName": "Phase6 v01.cbz", "boundaryPolicy": "allow-piece-overlap",
          "pageCount": 200, "chapters": [
            { "num": "1", "title": "Smoke Chapter 1" },
            { "num": "2", "title": "Smoke Chapter 2" },
            { "num": "3", "title": "Smoke Chapter 3" }
          ]
        },
        { "vol": 2, "fileIndex": 1, "fileSizeBytes": 2048, "pieceStart": 1, "pieceEnd": 1,
          "cbzFileName": "Phase6 v02.cbz", "boundaryPolicy": "allow-piece-overlap",
          "pageCount": 220, "chapters": [
            { "num": "4", "title": "Smoke Chapter 4" },
            { "num": "5", "title": "Smoke Chapter 5" }
          ]
        }
      ],
      "postCoverageFallback": { "weebcentralSlug": "", "startsAfterVolume": 0 }
    },
    {
      "seriesId": "phase6_smoke_ongoing",
      "title": "Phase 6 Ongoing Smoke",
      "alternateTitles": [],
      "anilistId": 0,
      "status": "ongoing",
      "magnetUri": "magnet:?xt=urn:btih:abcdef0123456789abcdef0123456789abcdef01",
      "expectedInfoHash": "abcdef0123456789abcdef0123456789abcdef01",
      "trustedUploader": "fixture",
      "releaseEdition": "fixture",
      "format": "one-cbz-per-volume",
      "volumes": [
        { "vol": 1, "fileIndex": 0, "fileSizeBytes": 1024, "pieceStart": 0, "pieceEnd": 0,
          "cbzFileName": "Ongoing v01.cbz", "boundaryPolicy": "allow-piece-overlap",
          "pageCount": 200, "chapters": [
            { "num": "1", "title": "Ongoing Chapter 1" }
          ]
        }
      ],
      "postCoverageFallback": { "weebcentralSlug": "one-piece", "startsAfterVolume": 1 }
    }
  ]
}
```

- [ ] **Step 6.3.2: MCP-driven visual smoke**

Claim the MCP lock in `agents/chat.md` (per Rule 19):

```
MCP LOCK - [Agent 1, TANKOYOMI_PREMIUM Phase 6 visual smoke]: expecting ~5 min. Open Comics, type "Phase 6 Smoke One" in search, click result, verify volume-row layout renders.
```

Run:

```cmd
build_and_run.bat
```

Use pywinauto-mcp + tankoctl per CLAUDE.md "Which MCP, when":

```
tankoctl open-page comics
```

Open the Tankoyomi search via the existing search input (the merger arc wired it). Type "Phase 6 Smoke One" (use the pywinauto-mcp keyboard send via AutomationId `tankoyomi_search_input`; grep `scripts/uia-dump.ps1` output for the actual AutomationId if needed).

Click the resulting search-result tile.

Capture screenshot via `mcp__pywinauto-mcp__automation_visual`.

**Validation checklist:**

1. Detail view shows the hero (placeholder synopsis since the fixture has no extra metadata) plus a 5-column table.
2. Two rows present: Volume 2 above Volume 1 (descending sort).
3. Volume 1 row reads `Chs 1-3 (3)`; Volume 2 row reads `Chs 4-5 (2)`.
4. Both rows have a Download button in the Action column.

Repeat for "Phase 6 Ongoing Smoke":
- One volume row at top.
- One sectioned `-- Latest chapters (WeebCentral) --` row below (centered, bold).
- Loose-tail chapter rows from the One Piece WeebCentral fetch should appear below the section header (note: this requires Phase 7 wiring of `m_looseTailThresholdChapterNum`; in Phase 6 the loose-tail rows show but may not filter correctly until Phase 7 finishes; that's acceptable for this phase).

Release the lock + close the app:

```cmd
taskkill /F /IM Tankoban.exe
```

Post in `agents/chat.md`:

```
MCP LOCK RELEASED - [Agent 1, TANKOYOMI_PREMIUM Phase 6 visual smoke]: volume-row layout renders correctly for completed + ongoing fixture series; loose-tail section header appears for ongoing.
```

- [ ] **Step 6.3.3: Delete the fixture before phase close**

```cmd
del resources\manga_premium_catalogs\_phase6_fixture.json
```

(Filename per Step 6.3.1 - if you used `_phase6_fixture.json` with the underscore, the loader already skipped it; if you used `phase6_fixture.json` without, you must delete it before Phase 11 catalog curation ships.)

### Task 6.4: Phase 6 close - RTC line

```
READY TO COMMIT - [Agent 1, TANKOYOMI_PREMIUM Phase 6 - ComicsTankoyomiDetailView volume-row variant + ongoing-series gap rendering with fixture-driven visual smoke. Detail view branches via premiumEntryForCurrentSeries() (calls PremiumCatalog::entryForTitle); when entry exists, populateVolumeAndLooseTailTable() builds a 5-column table (Cover | Volume | Chapters | Status | Action) sorted descending by vol; for ongoing entries with non-empty postCoverageWeebcentralSlug, a centered/bold section break row "-- Latest chapters (WeebCentral) --" is inserted after the volumes and loose-tail chapters are fetched from the existing WeebCentralScraper with a m_looseTailThresholdChapterNum stash for Phase 7 filtering. Non-Premium series path unchanged (early return in populate function). Smoke: 2-series fixture (one completed + one ongoing) verified end-to-end via build_and_run.bat + tankoctl + pywinauto-mcp visual capture; Volume 2 above Volume 1, chapter ranges correct, loose-tail header renders for ongoing only. Fixture deleted before close. MCP LOCK claim+release posted. Phase 7 wires the Download button + Phase 10 fills the Cover column.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/ui/pages/comics/ComicsTankoyomiDetailView.h, src/ui/pages/comics/ComicsTankoyomiDetailView.cpp, src/ui/pages/ComicsPage.cpp, agents/chat.md
```

---

## Phase 7 - Provider-to-detail-view wiring + sticky headers + filter chips + Continue strip canonical-key

**Phase goal:** Replace the Phase-3 qDebug signal handlers with real consumers. Clicking a volume's Download button calls `TorrentVolumeProvider::requestVolume`; `volumeProgress` / `volumeCompleted` / `volumeFailed` / `swarmStatus` repaint the matching row. The detail view picks up sticky section headers (per Codex section 26) and row-filter chips (All / Downloaded / Unread / Premium / Loose). The Continue strip on Comics page is updated to dedupe by canonical chapter key per Codex section 22 + 23.

**Files this phase touches:**

- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.h`
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp`
- Modify: `src/ui/pages/ComicsPage.cpp` (replace qDebug handlers; Continue strip canonical-key resolution)

**Reference brainstorm sections:** Section 4.4 (read-state derivation), Section 22 (Continue strip rule: prefer Premium volume path once present), Section 23 (canonical chapter key alias layer), Section 26 (sticky headers + filter chips).

### Task 7.1: Wire the Download button to TorrentVolumeProvider

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.h`
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp`

- [ ] **Step 7.1.1: Expose a download-requested signal**

In the header's signals section:

```cpp
signals:
    // Existing signals preserved. New for Premium:
    void premiumVolumeDownloadRequested(QString seriesId, int volumeNumber);
    void premiumVolumeStateRefreshRequested(QString seriesId, int volumeNumber);
```

- [ ] **Step 7.1.2: Connect the Download button**

In `populateVolumeAndLooseTailTable`, immediately after the `btn->setProperty(...)` calls, connect:

```cpp
        connect(btn, &QPushButton::clicked, this, [this, sid = entry.seriesId, vn = v.vol]() {
            emit premiumVolumeDownloadRequested(sid, vn);
        });
```

- [ ] **Step 7.1.3: Add a per-row update slot**

Add to the header's private slots / public slots section:

```cpp
public slots:
    void onPremiumVolumeProgress(const QString& seriesId, int volumeNumber, double pct);
    void onPremiumVolumeCompleted(const QString& seriesId, int volumeNumber);
    void onPremiumVolumeFailed(const QString& seriesId, int volumeNumber,
                                const QString& code, const QString& message);
    void onPremiumSwarmStatus(const QString& seriesId, int volumeNumber, int piecePeersOnline);
```

In the `.cpp`:

```cpp
void ComicsTankoyomiDetailView::onPremiumVolumeProgress(const QString& seriesId,
                                                       int volumeNumber, double pct)
{
    // Only repaint if this is the currently-rendered series.
    auto entryOpt = premiumEntryForCurrentSeries();
    if (!entryOpt || entryOpt->seriesId != seriesId) return;
    // Find the row whose button has matching properties; update status column.
    for (int r = 0; r < m_chapterTable->rowCount(); ++r) {
        auto* btn = qobject_cast<QPushButton*>(m_chapterTable->cellWidget(r, 4));
        if (!btn) continue;
        if (btn->property("premiumVolume").toInt() != volumeNumber) continue;
        if (btn->property("premiumSeriesId").toString() != seriesId) continue;
        auto* cell = m_chapterTable->item(r, 3);
        if (cell) cell->setText(QStringLiteral("Downloading %1%").arg(int(pct * 100.0)));
        return;
    }
}

void ComicsTankoyomiDetailView::onPremiumVolumeCompleted(const QString& seriesId, int volumeNumber)
{
    auto entryOpt = premiumEntryForCurrentSeries();
    if (!entryOpt || entryOpt->seriesId != seriesId) return;
    for (int r = 0; r < m_chapterTable->rowCount(); ++r) {
        auto* btn = qobject_cast<QPushButton*>(m_chapterTable->cellWidget(r, 4));
        if (!btn) continue;
        if (btn->property("premiumVolume").toInt() != volumeNumber) continue;
        if (btn->property("premiumSeriesId").toString() != seriesId) continue;
        btn->setText(QStringLiteral("Read"));
        auto* cell = m_chapterTable->item(r, 3);
        if (cell) cell->setText(QStringLiteral("Downloaded"));
        return;
    }
}

void ComicsTankoyomiDetailView::onPremiumVolumeFailed(const QString& seriesId, int volumeNumber,
                                                     const QString& code, const QString& message)
{
    Q_UNUSED(message)
    auto entryOpt = premiumEntryForCurrentSeries();
    if (!entryOpt || entryOpt->seriesId != seriesId) return;
    for (int r = 0; r < m_chapterTable->rowCount(); ++r) {
        auto* btn = qobject_cast<QPushButton*>(m_chapterTable->cellWidget(r, 4));
        if (!btn) continue;
        if (btn->property("premiumVolume").toInt() != volumeNumber) continue;
        if (btn->property("premiumSeriesId").toString() != seriesId) continue;
        auto* cell = m_chapterTable->item(r, 3);
        if (cell) cell->setText(QStringLiteral("Failed (%1)").arg(code));
        btn->setText(QStringLiteral("Retry"));
        return;
    }
}

void ComicsTankoyomiDetailView::onPremiumSwarmStatus(const QString& seriesId, int volumeNumber,
                                                    int piecePeersOnline)
{
    auto entryOpt = premiumEntryForCurrentSeries();
    if (!entryOpt || entryOpt->seriesId != seriesId) return;
    if (piecePeersOnline > 0) return; // only surface zero-peer state
    for (int r = 0; r < m_chapterTable->rowCount(); ++r) {
        auto* btn = qobject_cast<QPushButton*>(m_chapterTable->cellWidget(r, 4));
        if (!btn) continue;
        if (btn->property("premiumVolume").toInt() != volumeNumber) continue;
        if (btn->property("premiumSeriesId").toString() != seriesId) continue;
        auto* cell = m_chapterTable->item(r, 3);
        if (cell) cell->setText(QStringLiteral("Waiting for peers"));
        return;
    }
}
```

### Task 7.2: Replace Phase-3 qDebug handlers in ComicsPage

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 7.2.1: Replace the three qDebug connects**

Locate the three `connect(m_premiumProvider, &TorrentVolumeProvider::volumeProgress, ...)` / `volumeCompleted` / `volumeFailed` lambdas inserted in Phase 3 step 3.7.2. Replace the whole block with:

```cpp
    using P = tankoban::manga::premium::TorrentVolumeProvider;
    connect(m_premiumProvider, &P::volumeProgress,
            m_tankoyomiDetailView, &ComicsTankoyomiDetailView::onPremiumVolumeProgress,
            Qt::QueuedConnection);
    connect(m_premiumProvider, &P::volumeCompleted,
            m_tankoyomiDetailView, [this](const QString& s, int v, const QString& p){
                Q_UNUSED(p)
                m_tankoyomiDetailView->onPremiumVolumeCompleted(s, v);
            },
            Qt::QueuedConnection);
    connect(m_premiumProvider, &P::volumeFailed,
            m_tankoyomiDetailView, &ComicsTankoyomiDetailView::onPremiumVolumeFailed,
            Qt::QueuedConnection);
    connect(m_premiumProvider, &P::swarmStatus,
            m_tankoyomiDetailView, &ComicsTankoyomiDetailView::onPremiumSwarmStatus,
            Qt::QueuedConnection);

    // Detail view -> provider: download button click.
    connect(m_tankoyomiDetailView, &ComicsTankoyomiDetailView::premiumVolumeDownloadRequested,
            this, [this](const QString& seriesId, int volumeNumber){
                const auto entry = m_premiumCatalog->entryById(seriesId);
                if (!entry) return;
                const auto* volEntry = [&](){
                    for (const auto& v : entry->volumes) {
                        if (v.vol == volumeNumber) return &v;
                    }
                    return static_cast<const tankoban::manga::premium::PremiumVolumeEntry*>(nullptr);
                }();
                if (!volEntry) return;
                const QString destinationPath = canonicalSeriesPathForPremium(*entry);
                m_premiumProvider->requestVolume(*entry, *volEntry, destinationPath);
            });
```

The lambda calls `canonicalSeriesPathForPremium(entry)`. Add a helper to `ComicsPage`:

```cpp
QString ComicsPage::canonicalSeriesPathForPremium(
    const tankoban::manga::premium::PremiumCatalogEntry& entry) const
{
    // Reuse existing canonical-path resolution from the merger arc. The
    // merger plan introduced "rootFolder + seriesFolderName with collision
    // disambiguation"; the same logic applies here. Phase 9 (adopt-folder)
    // overrides this when an existing folder matches the title.
    return m_comicsRootFolder + QChar('/') + sanitizeFolderName(entry.title);
}
```

Declare it in the header. `sanitizeFolderName` already exists in `ComicsPage.cpp` from the merger arc; if not, port the merger arc's seriesFolderName helper.

- [ ] **Step 7.2.2: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 7.3: Add sticky section headers + row-filter chips

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.h`
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp`

- [ ] **Step 7.3.1: Add a filter chip row above the table**

In the constructor or layout-building code, immediately above the `m_chapterTable` add:

```cpp
    auto* filterRow = new QWidget(this);
    auto* filterLayout = new QHBoxLayout(filterRow);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    m_filterAll        = new QPushButton(QStringLiteral("All"));
    m_filterDownloaded = new QPushButton(QStringLiteral("Downloaded"));
    m_filterUnread     = new QPushButton(QStringLiteral("Unread"));
    m_filterPremium    = new QPushButton(QStringLiteral("Premium"));
    m_filterLoose      = new QPushButton(QStringLiteral("Loose"));
    for (auto* b : { m_filterAll, m_filterDownloaded, m_filterUnread, m_filterPremium, m_filterLoose }) {
        b->setCheckable(true);
        b->setProperty("filterChip", true);
        filterLayout->addWidget(b);
    }
    m_filterAll->setChecked(true);
    filterLayout->addStretch();
    // Insert filterRow at the right index in the existing detail-view layout
    // (right above m_chapterTable).
```

Wire each chip to a single slot `onFilterChanged()` that calls `m_chapterTable->setRowHidden(r, !rowMatchesActiveFilter(r))` for each row. Add members + slot:

```cpp
private:
    QPushButton* m_filterAll        = nullptr;
    QPushButton* m_filterDownloaded = nullptr;
    QPushButton* m_filterUnread     = nullptr;
    QPushButton* m_filterPremium    = nullptr;
    QPushButton* m_filterLoose      = nullptr;

    bool rowMatchesActiveFilter(int row) const;

private slots:
    void onFilterChanged();
```

`rowMatchesActiveFilter` queries the row's status cell text + button state + Premium/Loose marker (tag rows during population by setting `Qt::UserRole+1` on the status cell to `"premium"` / `"loose"`).

For sticky section headers, Qt's `QTableWidget` does not natively support sticky rows. The acceptable v1 approach is to keep the section break row pinned visually by rendering it with bold + background-color via `setItem(...)` + a stylesheet rule. Add to the existing comics QSS in `Theme.h` or the page-level stylesheet:

```cpp
    m_chapterTable->setStyleSheet(QStringLiteral(
        "QTableWidget::item[premiumSection=\"true\"] { "
        "  background: rgba(255,255,255,0.04); "
        "  font-weight: 600; "
        "}"));
```

Tag the section header row's cell with `setData(Qt::UserRole + 5, true)` and a matching dynamic property at item creation time. (Note: dynamic properties on QTableWidgetItem are limited; using a `QStyledItemDelegate` is the proper path, but for v1 the visual difference can be achieved by setting the item's background brush directly: `hdr->setBackground(QColor(255, 255, 255, 10));`)

- [ ] **Step 7.3.2: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 7.4: Continue strip canonical-key resolution in ComicsPage

**Files:**
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 7.4.1: Update refreshContinueStrip's per-record dedup**

Locate the existing `ComicsPage::refreshContinueStrip()` method (introduced in the TANKOYOMI_CONTINUE_READING arc that shipped earlier 2026-05-15). Find the per-record dedup logic that builds the Continue tiles.

Per Codex section 22: "Continue strip rule: one item per canonical series path." Per Codex section 23: "Continue strip resolves by canonical key first, then preferred path."

Add a derived-from-Premium pass: for each library record whose origin is `tankoyomi`, look up `m_premiumCatalog->entryForTitle(record.title)`. If found and at least one Premium volume is registered in `MangaDownloadIndex` (via `hasAnyForSeries("tankoyomi_premium", seriesId)`), and the user's progress key resolves to a chapter contained in that Premium volume, prefer the Premium cbz path over the loose WeebCentral cbz path:

```cpp
    // Inside refreshContinueStrip, for each candidate Continue record:
    QString preferredCbzPath = record.canonicalCbzPath; // existing default
    if (m_premiumCatalog) {
        const auto premiumEntry = m_premiumCatalog->entryForTitle(record.title);
        if (premiumEntry && m_downloadIndex->hasAnyForSeries(
                QStringLiteral("tankoyomi_premium"), premiumEntry->seriesId)) {
            // The user has read up to some chapter; find which Premium volume
            // contains it. Read-state is keyed by canonicalChapterKey
            // (Phase 5 made this stable across sources).
            const QString readChapter = record.lastReadChapterNumber;
            for (const auto& v : premiumEntry->volumes) {
                bool inThisVolume = false;
                for (const auto& ch : v.chapters) {
                    if (ch.chapterNumber == readChapter) { inThisVolume = true; break; }
                }
                if (!inThisVolume) continue;
                const QString key = computeChapterKey(QStringLiteral("tankoyomi_premium"),
                                                      premiumEntry->seriesId, readChapter);
                auto pathOpt = m_downloadIndex->filePathFor(
                    QStringLiteral("tankoyomi_premium"),
                    premiumEntry->seriesId, readChapter);
                if (pathOpt) preferredCbzPath = *pathOpt;
                break;
            }
        }
    }
    // ...continue using preferredCbzPath when building the tile...
```

`MangaDownloadIndex::computeChapterKey` already exists; reuse it.

- [ ] **Step 7.4.2: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 7.5: Phase 7 close - RTC line

```
READY TO COMMIT - [Agent 1, TANKOYOMI_PREMIUM Phase 7 - Provider-to-detail-view wiring + filter chips + Continue strip canonical-key resolution. Phase 3 qDebug signal handlers REPLACED with real receivers: volumeProgress repaints row status to "Downloading X%"; volumeCompleted flips Action button "Download" -> "Read" + status "Downloaded"; volumeFailed sets "Failed (<code>)" + "Retry" button; swarmStatus(0 peers) sets "Waiting for peers". premiumVolumeDownloadRequested signal from detail view -> ComicsPage lambda that resolves catalog entry by seriesId, looks up volumeEntry, computes canonical series path, calls TorrentVolumeProvider::requestVolume. Filter chip row (All/Downloaded/Unread/Premium/Loose) added above table; QPushButton::clicked -> onFilterChanged -> setRowHidden per row. Section break rows tagged via item background brush + UserRole flag. refreshContinueStrip extended per Codex section 22 + 23: for tankoyomi-origin records with matching Premium catalog entry AND hasAnyForSeries(tankoyomi_premium), prefer the Premium cbz path via filePathFor lookup when the user's last-read chapter is contained in a downloaded Premium volume. build_check green.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/ui/pages/comics/ComicsTankoyomiDetailView.h, src/ui/pages/comics/ComicsTankoyomiDetailView.cpp, src/ui/pages/ComicsPage.h, src/ui/pages/ComicsPage.cpp, agents/chat.md
```

---

## Phase 8 - Search widget Premium section + chip + tile dedup

**Phase goal:** Extend `ComicsTankoyomiSearchWidget` so the search results render in two ordered sections: a `Premium` header at top containing one tile per catalog hit (each tile carries the existing `Tankoyomi` chip PLUS a new `Premium` chip in the theme accent color), then the existing `Tankoyomi` section below with WeebCentral / ReadComics hits, deduped against the Premium section (a series that has a Premium hit is suppressed from the WeebCentral list).

**Files this phase touches:**

- Modify: `src/ui/pages/comics/ComicsTankoyomiSearchWidget.h`
- Modify: `src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp`

**Reference brainstorm sections:** Section 4.1 (search result two-section layout + dedup), Section 4.3 (Premium chip styling), Codex section 17.4 (theme accent correction; no hardcoded gold).

### Task 8.1: Surface the catalog into the search widget

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiSearchWidget.h`
- Modify: `src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp`

- [ ] **Step 8.1.1: Add the catalog setter + member**

In the header:

```cpp
namespace tankoban::manga::premium { class PremiumCatalog; }
```

Public:
```cpp
    void setPremiumCatalog(tankoban::manga::premium::PremiumCatalog* catalog);
```

Private:
```cpp
    tankoban::manga::premium::PremiumCatalog* m_premiumCatalog = nullptr;
```

In `.cpp`:
```cpp
void ComicsTankoyomiSearchWidget::setPremiumCatalog(
    tankoban::manga::premium::PremiumCatalog* catalog)
{
    m_premiumCatalog = catalog;
    refreshSearchResults(); // existing method that repaints results
}
```

Include `core/manga/PremiumCatalog.h`.

- [ ] **Step 8.1.2: Wire from ComicsPage**

In `src/ui/pages/ComicsPage.cpp`, where the search widget is constructed, add immediately after:

```cpp
    m_tankoyomiSearchWidget->setPremiumCatalog(m_premiumCatalog);
```

- [ ] **Step 8.1.3: Build-verify**

Run: `build_check.bat`. Expected: `BUILD OK`.

### Task 8.2: Add the Premium section header + chip + dedup logic

**Files:**
- Modify: `src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp`

- [ ] **Step 8.2.1: Split results into two buckets**

Locate the existing `refreshSearchResults` (or equivalent) method that builds tiles from `m_currentResults`. Replace its tile-loop with a two-pass build:

```cpp
void ComicsTankoyomiSearchWidget::refreshSearchResults()
{
    clearTiles();

    // Pass 1: split results into Premium vs Tankoyomi-fallback buckets.
    QList<MangaResult> premiumBucket;
    QList<MangaResult> fallbackBucket;
    QSet<QString>      premiumTitlesLower;

    for (const auto& result : m_currentResults) {
        if (m_premiumCatalog && m_premiumCatalog->isPremiumSeries(result.title)) {
            premiumBucket.append(result);
            premiumTitlesLower.insert(result.title.toLower());
        } else {
            fallbackBucket.append(result);
        }
    }

    // Also add Premium catalog hits whose title matches the search query but
    // didn't appear in the live scraper results (Premium catalog as a search
    // surface). Skip if the query is empty.
    if (m_premiumCatalog && !m_currentQuery.trimmed().isEmpty()) {
        const QString needle = m_currentQuery.toLower();
        for (const auto& entry : m_premiumCatalog->allEntries()) {
            const bool titleMatches = entry.title.toLower().contains(needle);
            bool altMatches = false;
            for (const auto& alt : entry.alternateTitles) {
                if (alt.toLower().contains(needle)) { altMatches = true; break; }
            }
            if (!titleMatches && !altMatches) continue;
            if (premiumTitlesLower.contains(entry.title.toLower())) continue;
            MangaResult synthetic;
            synthetic.title       = entry.title;
            synthetic.source      = QStringLiteral("tankoyomi_premium");
            synthetic.seriesId    = entry.seriesId;
            // The synthetic result has no scraper-provided cover/year; the
            // existing tile builder handles empty fields via Tankoyomi's
            // existing fallback art path.
            premiumBucket.append(synthetic);
            premiumTitlesLower.insert(entry.title.toLower());
        }
    }

    // Pass 2: dedup the fallback bucket - drop any whose title is in the
    // Premium bucket.
    QList<MangaResult> dedupedFallback;
    for (const auto& r : fallbackBucket) {
        if (premiumTitlesLower.contains(r.title.toLower())) continue;
        dedupedFallback.append(r);
    }

    // Pass 3: render. Premium first, then Tankoyomi (existing visual style).
    if (!premiumBucket.isEmpty()) {
        addSectionHeader(QStringLiteral("Premium"));
        for (const auto& r : premiumBucket) {
            addTile(r, /*premiumChip=*/true,
                    /*subtitleSuffix=*/
                    premiumTitlesLower.contains(r.title.toLower()) &&
                    fallbackBucketContains(r.title)
                        ? QStringLiteral("also on WeebCentral")
                        : QString());
        }
    }
    if (!dedupedFallback.isEmpty()) {
        addSectionHeader(QStringLiteral("Tankoyomi (WeebCentral / ReadComicsOnline)"));
        for (const auto& r : dedupedFallback) {
            addTile(r, /*premiumChip=*/false, /*subtitleSuffix=*/QString());
        }
    }

    if (premiumBucket.isEmpty() && dedupedFallback.isEmpty()) {
        showEmptyState();
    }
}
```

Declare the helpers in the header:

```cpp
private:
    void addSectionHeader(const QString& label);
    void addTile(const MangaResult& r, bool premiumChip, const QString& subtitleSuffix);
    bool fallbackBucketContains(const QString& title) const;
```

`fallbackBucketContains` is a transient helper for the "also on WeebCentral" suffix logic; you can simplify by passing the WeebCentral set into addTile or computing it once outside the loop. The simplest implementation:

```cpp
bool ComicsTankoyomiSearchWidget::fallbackBucketContains(const QString& title) const
{
    for (const auto& r : m_currentResults) {
        if (r.source != QStringLiteral("tankoyomi_premium") &&
            r.title.compare(title, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}
```

`addSectionHeader` and `addTile` are refactors of the existing tile-construction code that the merger arc shipped; pull them into their own methods so the new two-section layout can call them uniformly.

- [ ] **Step 8.2.2: Add the Premium chip styling**

In the tile rendering code (inside `addTile` when `premiumChip == true`), construct a chip widget styled with `Theme::current().accent`:

```cpp
    if (premiumChip) {
        auto* chip = new QLabel(QStringLiteral("Premium"));
        chip->setObjectName(QStringLiteral("premiumChip"));
        // The chip uses the active theme's accent; resolved at paint time via
        // the dynamic QSS rule below. No hardcoded color (Codex section 17.4).
        chip->setStyleSheet(QStringLiteral(
            "QLabel#premiumChip { "
            "  background: %1; "
            "  color: white; "
            "  border-radius: 3px; "
            "  padding: 1px 6px; "
            "  font-size: 10px; "
            "  font-weight: 600; "
            "}").arg(Theme::current().accent));
        // Append chip to the tile's chip row, alongside the existing
        // "Tankoyomi" chip from the merger arc.
        appendChipToTile(tile, chip);
    }
```

`Theme::current()` is the existing accessor from `src/ui/Theme.h`; include the header. `appendChipToTile` is the merger-arc helper for adding chip widgets to a tile's metadata row. Grep `ComicsTankoyomiSearchWidget.cpp` for `Tankoyomi.*chip` to find the existing wire-up.

If your theme system live-changes Modes (the `Theme::current()` data refreshes), the chip needs to repaint on Mode change. The Theme system in this project emits a Mode-changed signal (look at `Theme.h` for `applyTheme` -> `themeChanged` signal); subscribe and update the chip's stylesheet. If that wire is not yet trivial, accept that v1 paints with the Mode-at-construction-time accent and a Mode change requires a search refresh to update colors. This is acceptable v1.

- [ ] **Step 8.2.3: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 8.3: Visual smoke against the Phase 6 fixture (one-shot)

Re-introduce the Phase 6 fixture briefly to verify the search layout.

- [ ] **Step 8.3.1: Re-add the Phase 6 fixture**

Recreate `resources/manga_premium_catalogs/_phase8_smoke.json` with the same content as Phase 6's `phase6_fixture.json` (no underscore on filename so the loader picks it up).

- [ ] **Step 8.3.2: MCP-driven visual smoke**

Claim MCP lock:

```
MCP LOCK - [Agent 1, TANKOYOMI_PREMIUM Phase 8 visual smoke]: expecting ~5 min. Verify two-section search layout + Premium chip + dedup.
```

```cmd
build_and_run.bat
```

```
tankoctl open-page comics
```

Type "Phase 6 Smoke One" in the Tankoyomi search input. Capture screenshot.

**Validation checklist:**

1. Two section headers visible: `Premium` at top, `Tankoyomi (WeebCentral / ReadComicsOnline)` below.
2. Premium section has one tile labeled "Phase 6 Smoke One" with both `Tankoyomi` and `Premium` chips visible.
3. Premium chip color matches the active theme's accent (e.g. `#c7a76b` in Dark mode).
4. If you also typed a query that matches a non-catalog series like "kindergarten" (no Premium catalog), only the Tankoyomi section appears; Premium section header is suppressed.

Type a query that doesn't match anything in the catalog OR in WeebCentral results: the empty state should show.

Release lock + close:

```cmd
taskkill /F /IM Tankoban.exe
del resources\manga_premium_catalogs\_phase8_smoke.json
```

```
MCP LOCK RELEASED - [Agent 1, TANKOYOMI_PREMIUM Phase 8 visual smoke]: two-section layout renders correctly; Premium chip uses Theme::current().accent.
```

### Task 8.4: Phase 8 close - RTC line

```
READY TO COMMIT - [Agent 1, TANKOYOMI_PREMIUM Phase 8 - Search widget Premium section + chip + tile dedup. ComicsTankoyomiSearchWidget gains setPremiumCatalog() + refreshSearchResults() refactored into 3 passes: (1) split live scraper hits into Premium vs Tankoyomi-fallback buckets using PremiumCatalog::isPremiumSeries, plus inject Premium-catalog-only hits whose title/alternateTitle matches the current query as synthetic MangaResult entries; (2) dedup fallback bucket against premiumTitlesLower set; (3) render Premium section header above Tankoyomi section header. Premium tiles carry both existing Tankoyomi chip and new Premium chip styled via Theme::current().accent (no hardcoded color per Codex section 17.4). "also on WeebCentral" subtitle suffix added when a Premium hit also exists in the WeebCentral scraper results. Empty-state shown when both buckets empty. Smoke green via build_and_run.bat + pywinauto-mcp visual capture against a one-shot fixture; two-section layout + chip color + dedup all verified. MCP LOCK claim+release posted. Fixture deleted before close.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/ui/pages/comics/ComicsTankoyomiSearchWidget.h, src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp, src/ui/pages/ComicsPage.cpp, agents/chat.md
```

---

## Phase 9 - Adopt-existing-folder migration + MangaTransferCoordinator

**Phase goal:** Implement the v1 "adopt, do not migrate" path per Codex section 22. When the user clicks Add-to-Library on a Premium catalog series whose normalized title matches an existing folder-imported Comics library entry, the new Premium library record adopts that folder instead of creating a second one. Future Premium volume downloads land in the adopted folder. LibraryScanner already suppresses Tankoyomi-claimed-path duplicates (Codex section 22 cites `src/core/LibraryScanner.cpp:87-99`), so the result is one library tile per series. Also: build a `MangaTransferCoordinator` facade so `MangaDownloader::pauseAll`/`resumeAll` and `TorrentVolumeProvider::pauseAll`/`resumeAll` are fanned-out from one entry point, giving the UI one "Transfers paused" state per Codex section 18 bullet 3.

**Files this phase touches:**

- Create: `src/core/manga/MangaTransferCoordinator.h`
- Create: `src/core/manga/MangaTransferCoordinator.cpp`
- Modify: `src/ui/pages/ComicsPage.h`
- Modify: `src/ui/pages/ComicsPage.cpp`
- Modify: `CMakeLists.txt`

**Reference brainstorm sections:** Section 22 (adopt, do not migrate), Section 18 bullet 3 (MangaTransferCoordinator facade), Section 7.5 (pre-existing folder coexistence).

### Task 9.1: Build the MangaTransferCoordinator facade

**Files:**
- Create: `src/core/manga/MangaTransferCoordinator.h`
- Create: `src/core/manga/MangaTransferCoordinator.cpp`

- [ ] **Step 9.1.1: Write the header**

```cpp
// src/core/manga/MangaTransferCoordinator.h
#pragma once

#include <QObject>
#include <QPointer>

class MangaDownloader;

namespace tankoban::manga::premium {

class TorrentVolumeProvider;

// Thin facade over MangaDownloader (HTTP-image chapter download) and
// TorrentVolumeProvider (torrent volume download). The UI binds one
// "Transfers paused" affordance to this; both backends respond.
//
// Per Codex section 18 bullet 3.
class MangaTransferCoordinator : public QObject
{
    Q_OBJECT
public:
    MangaTransferCoordinator(MangaDownloader*       downloader,
                             TorrentVolumeProvider* provider,
                             QObject*               parent = nullptr);
    ~MangaTransferCoordinator() override;

    // Calls pauseAll on both underlying backends. isPaused() returns true
    // only when both report paused.
    void pauseAll();
    void resumeAll();
    bool isPaused() const;

    // Cancel-all targets active downloads only (Pending / Downloading /
    // Validating). Completed history rows are untouched.
    void cancelAll();

signals:
    void pausedChanged(bool paused);

private:
    QPointer<MangaDownloader>       m_downloader;
    QPointer<TorrentVolumeProvider> m_provider;
};

} // namespace tankoban::manga::premium
```

- [ ] **Step 9.1.2: Implement**

```cpp
// src/core/manga/MangaTransferCoordinator.cpp
#include "MangaTransferCoordinator.h"
#include "MangaDownloader.h"
#include "TorrentVolumeProvider.h"

namespace tankoban::manga::premium {

MangaTransferCoordinator::MangaTransferCoordinator(MangaDownloader*       downloader,
                                                   TorrentVolumeProvider* provider,
                                                   QObject*               parent)
    : QObject(parent), m_downloader(downloader), m_provider(provider)
{
}

MangaTransferCoordinator::~MangaTransferCoordinator() = default;

void MangaTransferCoordinator::pauseAll()
{
    if (m_downloader) m_downloader->pauseAll();
    if (m_provider)   m_provider->pauseAll();
    emit pausedChanged(true);
}

void MangaTransferCoordinator::resumeAll()
{
    if (m_downloader) m_downloader->resumeAll();
    if (m_provider)   m_provider->resumeAll();
    emit pausedChanged(false);
}

bool MangaTransferCoordinator::isPaused() const
{
    const bool dPaused = m_downloader ? m_downloader->isPaused() : false;
    const bool pPaused = m_provider   ? m_provider->isPaused()   : false;
    return dPaused && pPaused;
}

void MangaTransferCoordinator::cancelAll()
{
    if (m_downloader) m_downloader->cancelAll();
    // TorrentVolumeProvider has no cancelAll today; iterate by ledger.
    // For v1, walk every in-flight bucket in the provider:
    //   for each (key, list) in m_byInfoHash:
    //     for each iff in list:
    //       cancelVolume(iff.seriesId, iff.volumeNumber, /*deleteStaged=*/false);
    // The straightforward implementation is to add a cancelAll() method
    // to TorrentVolumeProvider in Phase 9 itself; skip if scope is tight.
}

} // namespace tankoban::manga::premium
```

If `MangaDownloader::cancelAll()` doesn't already exist, grep — it does per Phase 3's API quote, line 78. Same for `pauseAll`/`resumeAll`/`isPaused` (lines 85-87).

- [ ] **Step 9.1.3: Wire into CMakeLists.txt + ComicsPage**

Add to CMakeLists.txt:

```cmake
    src/core/manga/MangaTransferCoordinator.cpp
    src/core/manga/MangaTransferCoordinator.h
```

In `ComicsPage.h`:

```cpp
namespace tankoban::manga::premium { class MangaTransferCoordinator; }
// ...
    tankoban::manga::premium::MangaTransferCoordinator* m_transferCoordinator = nullptr;
```

In `ComicsPage.cpp` constructor, after `m_premiumProvider` instantiation:

```cpp
    m_transferCoordinator = new tankoban::manga::premium::MangaTransferCoordinator(
        m_downloader, m_premiumProvider, this);
```

If there's an existing "Pause all transfers" UI affordance on ComicsPage, rebind it to `m_transferCoordinator->pauseAll()` / `resumeAll()`. If there's no such affordance yet, the coordinator is wired-up infrastructure that Phase 11 or v1.1 surfaces. Build-verify either way.

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 9.2: Implement adopt-existing-folder on Add-to-Library

**Files:**
- Modify: `src/ui/pages/ComicsPage.h`
- Modify: `src/ui/pages/ComicsPage.cpp`

- [ ] **Step 9.2.1: Add the helper for normalized-title folder lookup**

In `ComicsPage.h`:

```cpp
private:
    // Phase 9: return the existing folder-import series whose normalized
    // title matches `title`, if exactly one such match exists. If zero or
    // multiple matches exist, returns empty (caller falls back to new-folder
    // creation).
    QString findExistingFolderForTitle(const QString& title) const;

    static QString normalizeTitleForMatch(const QString& title);
```

- [ ] **Step 9.2.2: Implement**

In `ComicsPage.cpp`:

```cpp
QString ComicsPage::normalizeTitleForMatch(const QString& title)
{
    // Lowercase + collapse whitespace + strip punctuation. Aggressive enough
    // to fold "Berserk" vs "Berserk!" vs " Berserk " into one bucket; not
    // so aggressive as to fold "Berserk" with "Bersek" (typos).
    QString s = title.toLower().trimmed();
    s.remove(QRegularExpression(QStringLiteral("[\\s\\W_]+")));
    return s;
}

QString ComicsPage::findExistingFolderForTitle(const QString& title) const
{
    const QString needle = normalizeTitleForMatch(title);
    QStringList matches;

    // Walk the existing Comics library records. The merger arc introduced a
    // records-by-canonical-path map; reuse it. Grep ComicsPage.cpp for the
    // existing iteration pattern over library records.
    for (const auto& record : m_comicsRecords) {
        if (record.origin == QStringLiteral("tankoyomi")) {
            // Tankoyomi-origin records are not folder-imported; skip - they
            // are not the adopt target. The user added them via the existing
            // Tankoyomi flow.
            continue;
        }
        if (normalizeTitleForMatch(record.title) == needle) {
            matches.append(record.canonicalSeriesPath);
        }
    }
    if (matches.size() == 1) return matches.first();
    return QString();
}
```

`m_comicsRecords` is whatever ComicsPage uses today to enumerate the library; grep for `ComicsLibraryRecord` to confirm the field name. The merger arc's records iteration uses `ComicsLibraryRecord` with fields like `origin`, `title`, `canonicalSeriesPath`.

- [ ] **Step 9.2.3: Update Add-to-Library handler to call adopt**

Locate the existing Add-to-Library handler (the merger arc's "Add" button click slot in ComicsPage). At the top of the handler, BEFORE creating a new folder:

```cpp
    // Phase 9 adopt path: if the catalog series matches an existing
    // folder-imported series exactly-once by normalized title, reuse that
    // folder as the canonicalSeriesPath. Future Premium volume downloads
    // land in the adopted folder; LibraryScanner-suppression of
    // Tankoyomi-claimed paths (LibraryScanner.cpp:87-99) prevents a
    // duplicate tile in the library grid.
    const QString adoptPath = findExistingFolderForTitle(catalogEntry.title);
    if (!adoptPath.isEmpty()) {
        canonicalSeriesPath = adoptPath;
        qDebug().noquote() << QStringLiteral("[ComicsPage] adopting existing folder for")
                           << catalogEntry.title << QStringLiteral("at") << adoptPath;
    } else {
        canonicalSeriesPath = m_comicsRootFolder + QChar('/')
                            + sanitizeFolderName(catalogEntry.title);
        // (or whatever the existing merger-arc collision-disambiguation
        // helper is named; grep for "Berserk (WeebCentral)" disambiguation
        // logic. The adopt path skips this entirely.)
    }
```

Confirm the surrounding handler creates the `ComicsLibraryRecord` and writes the Tankoyomi sidecar JSON into `canonicalSeriesPath`. The adopt path means both already-existing folder-imported cbzs AND newly-downloaded Premium volume cbzs will live in one folder; the existing detail view's per-row source label (Local / Premium / WeebCentral) makes this visible to the user.

- [ ] **Step 9.2.4: Build-verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 9.3: Smoke - adopt path against a manual folder-imported test series

- [ ] **Step 9.3.1: Set up the pre-existing folder**

Manually create a fake "Phase 9 Smoke" folder under the comics root with one fake cbz inside (so LibraryScanner picks it up as folder-imported):

```cmd
mkdir "C:\Path\To\Comics\Phase 9 Smoke"
echo PK > "C:\Path\To\Comics\Phase 9 Smoke\vol01.cbz"
```

(Replace path with your comics root from the existing Settings page.)

- [ ] **Step 9.3.2: Add a fixture catalog entry matching the title**

Drop `resources/manga_premium_catalogs/_phase9_smoke.json`:

```json
{
  "id": "premium_phase9_smoke",
  "name": "Phase 9 adopt smoke",
  "version": "0.0.9",
  "behaviorHints": { "p2p": true, "adult": false },
  "series": [
    {
      "seriesId": "phase9_smoke",
      "title": "Phase 9 Smoke",
      "status": "completed",
      "magnetUri": "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567",
      "expectedInfoHash": "0123456789abcdef0123456789abcdef01234567",
      "trustedUploader": "fixture",
      "format": "one-cbz-per-volume",
      "volumes": [
        { "vol": 1, "fileIndex": 0, "fileSizeBytes": 1024, "pieceStart": 0, "pieceEnd": 0,
          "cbzFileName": "Phase 9 Smoke v01.cbz", "boundaryPolicy": "allow-piece-overlap",
          "pageCount": 100, "chapters": [{ "num": "1", "title": "Smoke" }] }
      ],
      "postCoverageFallback": { "weebcentralSlug": "", "startsAfterVolume": 0 }
    }
  ]
}
```

- [ ] **Step 9.3.3: MCP-driven smoke**

```
MCP LOCK - [Agent 1, TANKOYOMI_PREMIUM Phase 9 adopt smoke]: expecting ~5 min. Verify Add-to-Library on Premium catalog entry whose title matches existing folder adopts that folder.
```

```cmd
build_and_run.bat
```

- Run `tankoctl scan-videos`... actually for Comics: run `tankoctl open-page comics` then wait for LibraryScanner to pick up `Phase 9 Smoke` as folder-imported.
- Confirm `Phase 9 Smoke` appears as a library tile.
- Open Tankoyomi search; type `Phase 9 Smoke`; click the Premium hit.
- Click Add-to-Library.
- Capture `tankoctl logs 200`; expect `[ComicsPage] adopting existing folder for "Phase 9 Smoke" at <path>`.
- Confirm exactly ONE `Phase 9 Smoke` tile remains in the library grid (no duplicate). Open it; the detail view should show one `vol01.cbz` row (the pre-existing fake cbz).
- Close.

```cmd
taskkill /F /IM Tankoban.exe
del resources\manga_premium_catalogs\_phase9_smoke.json
rmdir /S /Q "C:\Path\To\Comics\Phase 9 Smoke"
```

```
MCP LOCK RELEASED - [Agent 1, TANKOYOMI_PREMIUM Phase 9 adopt smoke]: adopt path engaged correctly; one tile only; existing cbz preserved.
```

### Task 9.4: Phase 9 close - RTC line

```
READY TO COMMIT - [Agent 1, TANKOYOMI_PREMIUM Phase 9 - Adopt-existing-folder + MangaTransferCoordinator facade. New: MangaTransferCoordinator.h/.cpp - thin facade over MangaDownloader + TorrentVolumeProvider giving the UI one "Transfers paused" affordance per Codex section 18 bullet 3 (pauseAll/resumeAll/isPaused/cancelAll fan-out). ComicsPage adds normalizeTitleForMatch (lowercase + strip whitespace/punct) + findExistingFolderForTitle (returns canonicalSeriesPath if exactly-one non-tankoyomi-origin record matches normalized title). Add-to-Library handler now branches: adopt path reuses existing folder when match is exactly-one, fallthrough to new-folder creation with collision disambiguation otherwise. v1 "adopt, do not migrate" per Codex section 22: no automatic file deletion, no folder rename, no folder move; LibraryScanner-suppression of Tankoyomi-claimed paths (LibraryScanner.cpp:87-99) means user sees one tile post-adopt. Smoke green: manually created pre-existing "Phase 9 Smoke" folder + matching catalog fixture + Add-to-Library click -> adopting-existing-folder log line + single library tile + existing cbz preserved. Fixture + folder deleted before close. MCP LOCK claim+release posted.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/core/manga/MangaTransferCoordinator.h, src/core/manga/MangaTransferCoordinator.cpp, src/ui/pages/ComicsPage.h, src/ui/pages/ComicsPage.cpp, CMakeLists.txt, agents/chat.md
```

---

## Phase 10 - Cover extraction off-thread + bounded decompression

**Phase goal:** Extract a per-volume cover thumbnail from the downloaded cbz on a background thread, write to `manga_posters/premium_<seriesId>_v<NN>.jpg`, repaint the detail view's Cover column. Cover extraction does NOT gate `volumeCompleted` (per Codex section 21 closing paragraph: a volume with valid cbz and no cover is readable; archive validation gates completion, cover generation finishes after). Cover lookup heuristic per Codex section 21: catalog `coverPageHint.entryName` first; then `cover.*` or `folder.*` basename; then first naturally-sorted image entry after filtering junk names (`credit`, `scan`, `blank`, `ad`, `back`, `spread`); then series-level Tankoyomi/AniList poster; then neutral placeholder.

**Files this phase touches:**

- Create: `src/core/manga/PremiumCoverExtractor.h`
- Create: `src/core/manga/PremiumCoverExtractor.cpp`
- Modify: `src/core/manga/TorrentVolumeProvider.cpp` (kick off extraction after successful validation)
- Modify: `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp` (repaint Cover column on `coverReady` signal)
- Modify: `CMakeLists.txt`

**Reference brainstorm sections:** Section 21 (cover extraction lifecycle), Section 27.6 (off-thread + readable-before-cover recommendation).

### Task 10.1: Build PremiumCoverExtractor

**Files:**
- Create: `src/core/manga/PremiumCoverExtractor.h`
- Create: `src/core/manga/PremiumCoverExtractor.cpp`

- [ ] **Step 10.1.1: Write the header**

```cpp
// src/core/manga/PremiumCoverExtractor.h
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace tankoban::manga::premium {

// Off-thread cover extractor for Premium volume cbz files. Each request
// runs on QThreadPool::globalInstance(). Emits coverReady on the requester's
// thread via the queued signal contract.
//
// Lookup heuristic per Codex section 21:
//   1. catalog coverPageHint.entryName if present and found in the archive
//   2. cover.* or folder.* basename
//   3. first naturally-sorted image entry after filtering junk names
//      ("credit", "scan", "blank", "ad", "back", "spread") - only if a
//      later non-junk image exists
//   4. otherwise: emit coverFailed; caller falls back to series-level poster
class PremiumCoverExtractor : public QObject
{
    Q_OBJECT
public:
    explicit PremiumCoverExtractor(QObject* parent = nullptr);
    ~PremiumCoverExtractor() override;

    // The output path follows the project's existing manga_posters convention:
    //   manga_posters/premium_<seriesId>_v<NN>.jpg
    void extract(const QString& cbzPath,
                  const QString& seriesId,
                  int            volumeNumber,
                  const QString& outputDir,                // <appData>/manga_posters or equivalent
                  const QString& coverPageHintEntryName,   // empty if catalog did not hint
                  const QStringList& precomputedImageEntries); // from PremiumArchiveValidator

signals:
    void coverReady(QString seriesId, int volumeNumber, QString coverPath);
    void coverFailed(QString seriesId, int volumeNumber, QString reason);
};

} // namespace tankoban::manga::premium
```

- [ ] **Step 10.1.2: Implement**

```cpp
// src/core/manga/PremiumCoverExtractor.cpp
#include "PremiumCoverExtractor.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QRunnable>
#include <QThreadPool>
#include <QDebug>

#ifdef HAS_QT_ZIP
#  include <private/qzipreader_p.h>
#endif

namespace tankoban::manga::premium {

namespace {

constexpr qint64 kMaxCoverDecompressedBytes = 64 * 1024 * 1024;
constexpr int    kThumbWidthPx              = 256;

QStringList junkNeedles()
{
    return { QStringLiteral("credit"),
             QStringLiteral("scan"),
             QStringLiteral("blank"),
             QStringLiteral("advert"),
             QStringLiteral("back"),
             QStringLiteral("spread") };
}

QString naturallySortedFirstImageBypassingJunk(const QStringList& images)
{
    // Filter junk; if non-empty remainder, return its first.
    const auto needles = junkNeedles();
    QStringList nonJunk;
    for (const auto& name : images) {
        const QString lower = name.toLower();
        bool junk = false;
        for (const auto& n : needles) {
            if (lower.contains(n)) { junk = true; break; }
        }
        if (!junk) nonJunk.append(name);
    }
    QStringList sorted = nonJunk.isEmpty() ? images : nonJunk;
    std::sort(sorted.begin(), sorted.end(),
              [](const QString& a, const QString& b){
                  return a.localeAwareCompare(b) < 0;
              });
    return sorted.isEmpty() ? QString() : sorted.first();
}

QString pickCoverEntry(const QString& hint, const QStringList& images)
{
    if (!hint.isEmpty() && images.contains(hint)) return hint;

    for (const auto& name : images) {
        const QString base = QFileInfo(name).baseName().toLower();
        if (base.startsWith(QLatin1String("cover")) ||
            base.startsWith(QLatin1String("folder"))) {
            return name;
        }
    }
    return naturallySortedFirstImageBypassingJunk(images);
}

class ExtractRunnable : public QRunnable
{
public:
    ExtractRunnable(PremiumCoverExtractor* signaller,
                    QString cbzPath, QString seriesId, int volumeNumber,
                    QString outputDir, QString coverPageHintEntryName,
                    QStringList precomputedImageEntries)
        : m_signaller(signaller)
        , m_cbzPath(std::move(cbzPath))
        , m_seriesId(std::move(seriesId))
        , m_volumeNumber(volumeNumber)
        , m_outputDir(std::move(outputDir))
        , m_hint(std::move(coverPageHintEntryName))
        , m_images(std::move(precomputedImageEntries))
    {
        setAutoDelete(true);
    }

    void run() override
    {
#ifndef HAS_QT_ZIP
        QMetaObject::invokeMethod(m_signaller, "coverFailed", Qt::QueuedConnection,
            Q_ARG(QString, m_seriesId), Q_ARG(int, m_volumeNumber),
            Q_ARG(QString, QStringLiteral("HAS_QT_ZIP not defined")));
        return;
#else
        QZipReader zr(m_cbzPath);
        if (!zr.exists() || !zr.isReadable()) {
            QMetaObject::invokeMethod(m_signaller, "coverFailed", Qt::QueuedConnection,
                Q_ARG(QString, m_seriesId), Q_ARG(int, m_volumeNumber),
                Q_ARG(QString, QStringLiteral("archive_open_failed")));
            return;
        }

        const QString chosen = pickCoverEntry(m_hint, m_images);
        if (chosen.isEmpty()) {
            QMetaObject::invokeMethod(m_signaller, "coverFailed", Qt::QueuedConnection,
                Q_ARG(QString, m_seriesId), Q_ARG(int, m_volumeNumber),
                Q_ARG(QString, QStringLiteral("no_candidate_image")));
            return;
        }

        // Bounded decompression check.
        bool sizeKnownOk = true;
        for (const auto& info : zr.fileInfoList()) {
            if (info.filePath == chosen) {
                if (info.size > kMaxCoverDecompressedBytes) {
                    QMetaObject::invokeMethod(m_signaller, "coverFailed", Qt::QueuedConnection,
                        Q_ARG(QString, m_seriesId), Q_ARG(int, m_volumeNumber),
                        Q_ARG(QString, QStringLiteral("cover_too_large_declared")));
                    return;
                }
                sizeKnownOk = (info.size > 0 && info.size <= kMaxCoverDecompressedBytes);
                break;
            }
        }
        Q_UNUSED(sizeKnownOk)

        const QByteArray data = zr.fileData(chosen);
        if (data.isEmpty() || data.size() > kMaxCoverDecompressedBytes) {
            QMetaObject::invokeMethod(m_signaller, "coverFailed", Qt::QueuedConnection,
                Q_ARG(QString, m_seriesId), Q_ARG(int, m_volumeNumber),
                Q_ARG(QString, QStringLiteral("cover_too_large_decompressed")));
            return;
        }

        QImage img;
        if (!img.loadFromData(data)) {
            QMetaObject::invokeMethod(m_signaller, "coverFailed", Qt::QueuedConnection,
                Q_ARG(QString, m_seriesId), Q_ARG(int, m_volumeNumber),
                Q_ARG(QString, QStringLiteral("image_decode_failed")));
            return;
        }
        const QImage thumb = img.scaledToWidth(kThumbWidthPx, Qt::SmoothTransformation);

        QDir().mkpath(m_outputDir);
        const QString outPath = m_outputDir + QChar('/')
            + QStringLiteral("premium_%1_v%2.jpg")
                .arg(m_seriesId)
                .arg(m_volumeNumber, 2, 10, QChar('0'));
        if (!thumb.save(outPath, "JPG", 85)) {
            QMetaObject::invokeMethod(m_signaller, "coverFailed", Qt::QueuedConnection,
                Q_ARG(QString, m_seriesId), Q_ARG(int, m_volumeNumber),
                Q_ARG(QString, QStringLiteral("save_failed")));
            return;
        }
        QMetaObject::invokeMethod(m_signaller, "coverReady", Qt::QueuedConnection,
            Q_ARG(QString, m_seriesId), Q_ARG(int, m_volumeNumber),
            Q_ARG(QString, outPath));
#endif
    }

private:
    PremiumCoverExtractor* m_signaller;
    QString                m_cbzPath;
    QString                m_seriesId;
    int                    m_volumeNumber;
    QString                m_outputDir;
    QString                m_hint;
    QStringList            m_images;
};

} // anonymous namespace

PremiumCoverExtractor::PremiumCoverExtractor(QObject* parent)
    : QObject(parent) {}

PremiumCoverExtractor::~PremiumCoverExtractor() = default;

void PremiumCoverExtractor::extract(const QString& cbzPath,
                                    const QString& seriesId,
                                    int            volumeNumber,
                                    const QString& outputDir,
                                    const QString& coverPageHintEntryName,
                                    const QStringList& precomputedImageEntries)
{
    auto* r = new ExtractRunnable(this, cbzPath, seriesId, volumeNumber,
                                  outputDir, coverPageHintEntryName,
                                  precomputedImageEntries);
    QThreadPool::globalInstance()->start(r);
}

} // namespace tankoban::manga::premium
```

### Task 10.2: Wire cover extraction into TorrentVolumeProvider's success path

**Files:**
- Modify: `src/core/manga/TorrentVolumeProvider.h` (add extractor member + signal)
- Modify: `src/core/manga/TorrentVolumeProvider.cpp` (kick off extraction after successful validation, but don't block emit)

- [ ] **Step 10.2.1: Add the extractor + relay signals**

In `TorrentVolumeProvider.h`:

```cpp
namespace tankoban::manga::premium { class PremiumCoverExtractor; }
// inside the class:
public:
signals:
    void volumeCoverReady(QString seriesId, int volumeNumber, QString coverPath);
private:
    PremiumCoverExtractor* m_coverExtractor = nullptr;
    QString                m_coversDir;
```

Extend the constructor to take `coversDir`:

```cpp
    TorrentVolumeProvider(TorrentEngine*          engine,
                          PremiumCatalog*         catalog,
                          TorrentRequestLedger*   ledger,
                          MangaDownloadIndex*     index,
                          const QString&          stagingRoot,
                          const QString&          coversDir,
                          QObject*                parent = nullptr);
```

- [ ] **Step 10.2.2: Update the constructor body**

```cpp
TorrentVolumeProvider::TorrentVolumeProvider(TorrentEngine*        engine,
                                             PremiumCatalog*       catalog,
                                             TorrentRequestLedger* ledger,
                                             MangaDownloadIndex*   index,
                                             const QString&        stagingRoot,
                                             const QString&        coversDir,
                                             QObject*              parent)
    : QObject(parent)
    , m_engine(engine)
    , m_catalog(catalog)
    , m_ledger(ledger)
    , m_index(index)
    , m_stagingRoot(stagingRoot)
    , m_coversDir(coversDir)
{
    Q_ASSERT(m_engine);
    Q_ASSERT(m_catalog);
    Q_ASSERT(m_ledger);
    Q_ASSERT(m_index);
    QDir().mkpath(m_stagingRoot);
    QDir().mkpath(m_coversDir);

    m_coverExtractor = new PremiumCoverExtractor(this);
    connect(m_coverExtractor, &PremiumCoverExtractor::coverReady,
            this, [this](const QString& s, int v, const QString& path){
                emit volumeCoverReady(s, v, path);
            }, Qt::QueuedConnection);
    connect(m_coverExtractor, &PremiumCoverExtractor::coverFailed,
            this, [](const QString& s, int v, const QString& reason){
                qDebug().noquote() << QStringLiteral("[TorrentVolumeProvider] cover failed")
                                   << s << QStringLiteral("v") + QString::number(v)
                                   << reason;
            }, Qt::QueuedConnection);

    // ... existing TorrentEngine signal connect block unchanged ...
}
```

Include `PremiumCoverExtractor.h`.

- [ ] **Step 10.2.3: Kick off extraction at the end of finalizeCompletion**

In `finalizeCompletion`, IMMEDIATELY AFTER the existing `emit volumeCompleted(...)` line, append:

```cpp
    // Per Codex section 21: cover generation does NOT gate completion. Kick
    // off the extractor with the validator's pre-computed image entries (if
    // we have them) plus the catalog's coverPageHint.
    QString coverHint;
    QStringList imageEntries;
    if (auto entryOpt = m_catalog ? m_catalog->entryById(iff.seriesId) : std::nullopt) {
        for (const auto& v : entryOpt->volumes) {
            if (v.vol == iff.volumeNumber) { coverHint = v.coverPageHint; break; }
        }
    }
    // imageEntries is empty here because the validator's result struct isn't
    // currently threaded through to finalizeCompletion's scope. The extractor
    // will re-list the archive entries itself when imageEntries is empty;
    // this is a small redundancy that keeps the Phase 4 signature stable.
    m_coverExtractor->extract(finalFile, iff.seriesId, iff.volumeNumber,
                              m_coversDir, coverHint, imageEntries);
```

Note about the "imageEntries is empty here" comment: the extractor's `extract` falls back to walking the archive itself when `precomputedImageEntries` is empty. Add that fallback to the extractor implementation if not already present (the `ExtractRunnable::run` body above currently uses `m_images` without re-walking on empty; if the list is empty, re-walk via `QZipReader::fileInfoList` and filter by image extension). Adjust if needed.

- [ ] **Step 10.2.4: Pass coversDir + wire the new signal in ComicsPage**

In `ComicsPage.cpp`, update the provider construction:

```cpp
    const QString premiumCoversDir = appDataDir + QStringLiteral("/manga_posters");
    QDir().mkpath(premiumCoversDir);
    m_premiumProvider = new tankoban::manga::premium::TorrentVolumeProvider(
        m_torrentEngine, m_premiumCatalog, m_premiumLedger,
        m_downloadIndex, premiumStagingRoot, premiumCoversDir, this);
```

Connect the new signal to the detail view:

```cpp
    connect(m_premiumProvider,
            &tankoban::manga::premium::TorrentVolumeProvider::volumeCoverReady,
            m_tankoyomiDetailView,
            [this](const QString& s, int v, const QString& path){
                m_tankoyomiDetailView->setPremiumVolumeCover(s, v, path);
            },
            Qt::QueuedConnection);
```

Add `setPremiumVolumeCover` to `ComicsTankoyomiDetailView`:

```cpp
void ComicsTankoyomiDetailView::setPremiumVolumeCover(const QString& seriesId,
                                                     int volumeNumber,
                                                     const QString& coverPath)
{
    auto entryOpt = premiumEntryForCurrentSeries();
    if (!entryOpt || entryOpt->seriesId != seriesId) return;
    for (int r = 0; r < m_chapterTable->rowCount(); ++r) {
        auto* btn = qobject_cast<QPushButton*>(m_chapterTable->cellWidget(r, 4));
        if (!btn) continue;
        if (btn->property("premiumVolume").toInt() != volumeNumber) continue;
        auto* item = m_chapterTable->item(r, 0);
        if (item) {
            item->setData(Qt::DecorationRole, QIcon(coverPath));
            item->setData(Qt::UserRole, coverPath);
        }
        return;
    }
}
```

- [ ] **Step 10.2.5: Wire into CMakeLists.txt + build**

Add to CMakeLists.txt:

```cmake
    src/core/manga/PremiumCoverExtractor.cpp
    src/core/manga/PremiumCoverExtractor.h
```

Run: `build_check.bat`
Expected: `BUILD OK`.

### Task 10.3: Phase 10 close - RTC line

```
READY TO COMMIT - [Agent 1, TANKOYOMI_PREMIUM Phase 10 - Cover extraction off-thread + bounded decompression. New: PremiumCoverExtractor.h/.cpp - QRunnable on QThreadPool::globalInstance(), QZipReader-based cover pick heuristic per Codex section 21: catalog coverPageHint -> "cover.*"/"folder.*" basename -> natural-sort first image bypassing junk needles (credit/scan/blank/advert/back/spread) -> coverFailed. Bounded decompression cap kMaxCoverDecompressedBytes = 64 MiB. Output thumb is 256px wide, JPG quality 85, written to manga_posters/premium_<seriesId>_v<NN>.jpg. TorrentVolumeProvider ctor extended with coversDir arg; emits new volumeCoverReady signal proxied from extractor's coverReady; extraction kicked off at end of finalizeCompletion AFTER the volumeCompleted emit per Codex section 21 ("cover generation finishes after"). ComicsTankoyomiDetailView gains setPremiumVolumeCover(seriesId, vol, coverPath) -> setData(Qt::DecorationRole, QIcon(coverPath)) on the matching row's Cover cell. build_check green. Visual smoke deferred to Phase 11 (no real cbz exists until catalog curation lands).] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/core/manga/PremiumCoverExtractor.h, src/core/manga/PremiumCoverExtractor.cpp, src/core/manga/TorrentVolumeProvider.h, src/core/manga/TorrentVolumeProvider.cpp, src/ui/pages/comics/ComicsTankoyomiDetailView.h, src/ui/pages/comics/ComicsTankoyomiDetailView.cpp, src/ui/pages/ComicsPage.cpp, CMakeLists.txt, agents/chat.md
```

---

## Phase 11 - Catalog curation + 21-case smoke matrix + arc close

**Phase goal:** Curate the v1 30-series catalog using the Phase 2 helper tool, drop it into `resources/manga_premium_catalogs/tankoyomi_premium_2026-05.json`, then execute Codex's 21-case smoke matrix end-to-end. Close the arc with a final RTC.

This phase is mostly wall-clock-heavy curation work + smoke validation. The agent does the smokes via MCP; Hemanth weighs in on visual-quality judgment calls.

**Files this phase touches:**

- Create: `resources/manga_premium_catalogs/tankoyomi_premium_2026-05.json` (the real bundled catalog)

**Reference brainstorm sections:** Section 6 (the 30-series list), Section 10 + Section 26 (smoke matrix), Section 25 (curation effort estimate).

### Task 11.1: Curate one series end-to-end as a worked example (Death Note)

Death Note is the smoke-corpus pick from the brainstorm + the smallest completed series in the catalog (12 vols), so it's the right learn-the-workflow target.

- [ ] **Step 11.1.1: Acquire the trusted-uploader torrent**

Visit nyaa.si, search for `Death Note 1r0n` or `Death Note VIZ Digital`. Pick the highest-seeder candidate from a trusted-uploader handle. Download the `.torrent` file locally to `C:\curation\death_note.torrent`.

- [ ] **Step 11.1.2: Prepare the chapter-to-volume mapping CSV**

Manually transcribe the chapter list per volume from a known-good source (mangareader.to, the back-of-volume table of contents, or AniList volumes endpoint). Save as `C:\curation\death_note_mapping.csv` in the format documented in `tools/premium_catalog_helper/sample_mapping.csv`.

Death Note canonical mapping (one verifiable reference):

```csv
1,1,"Boredom"
1,2,"L"
1,3,"Family"
1,4,"Current"
1,5,"Eyeballs"
1,6,"Manipulation"
1,7,"Target"
2,8,"Woman"
2,9,"Killer"
2,10,"Confluence"
2,11,"One"
2,12,"God"
2,13,"Countdown"
2,14,"Friend"
2,15,"Phone Call"
2,16,"Handstand"
2,17,"Trash"
2,18,"Visualize"
3,19,"Humiliation"
3,20,"First Move"
3,21,"Duped"
3,22,"Misfortune"
3,23,"Hard Run"
3,24,"Shake"
3,25,"Vertigo"
3,26,"Reversal"
3,27,"Love"
```

(Continue through all 12 volumes. The full Death Note chapter list is 108 chapters across 12 volumes per the canonical edition. Source: any reliable manga database; cross-check at least two sources.)

- [ ] **Step 11.1.3: Run the helper**

```cmd
cd tools\premium_catalog_helper
python premium_catalog_draft.py ^
  --torrent-file "C:\curation\death_note.torrent" ^
  --series-id death_note ^
  --title "Death Note" ^
  --status completed ^
  --uploader "<the trusted uploader name from nyaa>" ^
  --release-edition "VIZ Digital" ^
  --mapping "C:\curation\death_note_mapping.csv" ^
  --out "C:\curation\death_note.draft.json"
```

Open `death_note.draft.json`. Review:
- `expectedInfoHash`: 40-char lowercase hex.
- `volumes`: 12 entries, each with non-zero `fileIndex` / `fileSizeBytes` / `pieceStart` / `pieceEnd` / `cbzFileName`.
- For each volume, the `chapters` array matches the CSV section.
- Add `anilistId` (Death Note: 30005) and any missing alternateTitles.

- [ ] **Step 11.1.4: Merge into the bundled catalog**

Create `resources/manga_premium_catalogs/tankoyomi_premium_2026-05.json` with the catalog manifest header + the Death Note entry as the first element of `series[]`:

```json
{
  "id": "tankoyomi_premium_2026-05",
  "name": "Tankoyomi Premium (May 2026)",
  "version": "1.0.0",
  "description": "Hand-curated trusted-uploader nyaa torrent catalog for high-fidelity Premium downloads.",
  "contact": "agent-1@tankoban.local",
  "behaviorHints": { "p2p": true, "adult": false },
  "series": [
    /* paste the Death Note draft here, drop the synthesized magnet if you
       want the real nyaa magnet preserved, fill in alternateTitles */
  ]
}
```

### Task 11.2: Curate the remaining 29 series

- [ ] **Step 11.2.1: Per-series workflow**

For each of the 29 remaining series in brainstorm section 6, repeat the Task 11.1 workflow:

1. Acquire `.torrent` from trusted uploader on nyaa.si.
2. Build mapping CSV from canonical sources.
3. Run helper.
4. Review draft.
5. Append to `series[]` in `tankoyomi_premium_2026-05.json`.

Per Codex section 25: budget 1-2 hours per completed series, 2-4 hours per long ongoing series (One Piece, Kingdom, Bleach+TYBW). Realistic total: 30-50 hours over multiple sessions.

Order recommendation (smallest first, builds momentum):
1. Death Note (12) - DONE in Task 11.1
2. Pluto (8)
3. Blame! (10)
4. Goodnight Punpun (13)
5. Made in Abyss (13)
6. Frieren (14)
7. Monster (18)
8. The Promised Neverland (20)
9. ...continue through the section 6 list...
30. One Piece (109+) - LAST. The largest. Saved for the end so the workflow is fully grooved by then.

- [ ] **Step 11.2.2: Per-series validation pass**

After each series is appended, run the app once and check the loader diagnostics:

```cmd
build_and_run.bat
out\tankoctl.exe logs 200
taskkill /F /IM Tankoban.exe
```

Look for `[PremiumCatalog] loaded N series across 1 file(s), with 0 diagnostic(s)`. Any non-zero diagnostic count means the strict validator rejected something; fix the JSON and re-run.

- [ ] **Step 11.2.3: Final catalog commit**

When all 30 series are in:

```cmd
build_and_run.bat
out\tankoctl.exe logs 200
taskkill /F /IM Tankoban.exe
```

Expected: `[PremiumCatalog] loaded 30 series across 1 file(s), with 0 diagnostic(s)`.

### Task 11.3: Execute the 21-case smoke matrix

Per brainstorm sections 10 + 26, the full v1 smoke matrix has 21 cases. Run each. Each case posts a chat.md note (one liner) on pass/fail.

Claim MCP lock for the smoke session:

```
MCP LOCK - [Agent 1, TANKOYOMI_PREMIUM Phase 11 21-case smoke]: expecting ~2 hours. Full v1 end-to-end validation.
```

- [ ] **Smoke 1: Berserk Premium chip + volume-row UI + single-vol file-priority round-trip + cover extraction + visual A/B vs WeebCentral version**
  - Search "berserk" -> Premium tile + chip in gold accent.
  - Click tile -> detail view shows 42 volume rows.
  - Click Download on v1.
  - Wait for completion. Verify "Read" button + cover thumb appears.
  - Open the volume in Comic Reader; pinch-zoom on the first hatching-heavy panel; compare side-by-side with the same chapter from WeebCentral path. Hemanth weighs in on visual fidelity.

- [ ] **Smoke 2: Death Note (12 vols) end-to-end + read-state propagation**
  - Download Death Note v1.
  - Open it; finish chapter 1.
  - Verify volume row badge updates ("1/7 chapters read" or equivalent).
  - Continue strip on Comics page should show the next unread chapter from v1.

- [ ] **Smoke 3: One Piece volumes 1-109 catalog + chapters 996+ from WeebCentral**
  - Search "one piece" -> Premium tile + chip.
  - Detail view shows v1-v109 sorted descending, then `-- Latest chapters (WeebCentral) --` section, then ch 1146 -> 1110 below.
  - Download v50; verify Premium download path.
  - Download ch 1145 from the loose tail; verify it uses the existing MangaDownloader HTTP path.
  - Both end up in the same canonical series folder.

- [ ] **Smoke 4: Slow swarm UX**
  - In the app's existing TorrentEngine global rate-limit affordance, set DL to 5 KB/s.
  - Start a Premium volume download.
  - Wait 30s. Verify the volume row shows "Waiting for peers" indicator without modal nag.
  - Restore unlimited rate; verify download resumes normally.

- [ ] **Smoke 5: Pre-existing import coexistence (adopt path)**
  - Manually folder-import a Berserk folder before starting (per Phase 9 prep).
  - Add Berserk via Premium catalog -> verify "adopting existing folder" log line + single library tile.
  - Existing volumes preserved; new Premium download lands in same folder.

- [ ] **Smoke 6: Non-catalog series unaffected**
  - Search for a series NOT in the catalog (e.g. "Yotsuba"). No Premium chip, no Premium section. Existing WeebCentral / ReadComicsOnline flow unchanged.

- [ ] **Smoke 7: App crash mid-download recovery**
  - Start Death Note v3 download.
  - At ~30% progress: `taskkill /F /IM Tankoban.exe`.
  - Relaunch: `build_and_run.bat`.
  - Provider's `replayLedger()` should re-attach. Tankoctl logs should show `replayLedger done; in-flight torrents: 1`.
  - Download resumes from where it left off.

- [ ] **Smoke 8: Re-click idempotency**
  - Click Download on v5; while it's in flight, click Download again. Second click is no-op (`requestVolume noop (already in-flight)` log line).

- [ ] **Smoke 9: Metadata-first paused=true path validates no all-files-download window**
  - With the rate limit set low, start a download. Inspect `tankoctl logs` for the sequence `addMagnet(paused=true)` -> `metadataReady` -> `setFilePriorities` -> `startTorrent`. Total bytes downloaded before file priorities are set should be tiny (metadata only).

- [ ] **Smoke 10: Boundary piece shares pieces with adjacent file**
  - Pick a series whose v17 file boundary sits mid-piece (any will do; libtorrent's piece-priority semantics mean this is the norm). Download v17; verify only v17 lands at canonical path; adjacent volumes' edges are not registered.

- [ ] **Smoke 11: Two concurrent volumes in same series**
  - Start Death Note v1 and v3 within 5 seconds of each other. Verify `applyUnionPriorities` log line includes both targets; both complete in their own time without canceling each other.

- [ ] **Smoke 12: Two concurrent volumes in different series**
  - Start Death Note v1 + Berserk v1. Verify two distinct torrents added; independent staging dirs; both complete.

- [ ] **Smoke 13: Kill app after ledger write but before metadata**
  - Click Download on v1; kill within 1 second (before `metadataReady` fires). Restart. Provider's replay should re-attach and continue.

- [ ] **Smoke 14: Kill app after file complete but before final rename**
  - Hard to time precisely; verify behavior by inspecting `.tankoban-part` file presence in canonical folder + recovery: a `.tankoban-part` left over should be cleaned at next launch via a startup pass (if implemented; if not, the next download attempt overwrites it).

- [ ] **Smoke 15: Low disk during staging**
  - On a test machine with bounded free space, start a large volume download. Verify provider surfaces `disk_full` or `engine_error` via `volumeFailed`; partial cbz not registered.

- [ ] **Smoke 16: Network drop mid-download**
  - Disable wifi mid-download. Provider shows "Waiting for peers" or stall. Re-enable; libtorrent reconnects; download resumes.

- [ ] **Smoke 17: External delete of completed Premium cbz**
  - After v5 completes, delete the cbz from disk via Explorer.
  - Trigger `MangaDownloadIndex::validateAll()` (some path - either Settings refresh or app restart).
  - Detail view's v5 row should flip back to "Download".

- [ ] **Smoke 18: Adopt existing folder-import series (re-validation of Smoke 5)**

- [ ] **Smoke 19: Catalog version moves loose chapter into new volume**
  - Edit `tankoyomi_premium_2026-05.json` to move (e.g.) One Piece chapter 1146 into a new Volume 110 entry.
  - Restart the app (catalog is restart-required per Codex section 26).
  - Read-state on ch 1146 (if you'd read it) survives via canonical chapter key.
  - Detail view re-renders: ch 1146 now sits under Vol 110, not in the loose tail.

- [ ] **Smoke 20: Malformed catalog entry**
  - Edit a series entry to have an invalid `expectedInfoHash` (e.g., "abc").
  - Restart.
  - Loader diagnostic `[PremiumCatalog] ... severity=2 invalid_expected_infohash` should appear; that one series is dropped; other 29 still load.

- [ ] **Smoke 21: Archive with no images or suspicious non-image payload**
  - Construct a tampered fake cbz (manually create a .cbz containing a .exe). Place it where the Premium staging would land it (a test infohash) and trigger the validation path.
  - Validator rejects; file moves to `<appData>/manga_premium_quarantine/`; UI shows "Failed (executable_entry)".

Release lock + arc close:

```
MCP LOCK RELEASED - [Agent 1, TANKOYOMI_PREMIUM Phase 11 21-case smoke]: <N pass / M fail summary>. Visual fidelity A/B (Smoke 1) ratified by Hemanth.
```

### Task 11.4: ARC CLOSE RTC

```
READY TO COMMIT - [Agent 1, TANKOYOMI_PREMIUM_MVP ARC CLOSED. 11 phases shipped: P1 PremiumCatalog schema + strict loader, P2 curation helper tool, P3 TorrentVolumeProvider core + persistent request ledger + persistent staging + event-driven completion + crash-resume, P4 archive validation + atomic .tankoban-part finalization + expectedInfoHash check + quarantine, P5 MangaDownloadIndex registerVolume + canonical chapter keys + provider wiring, P6 ComicsTankoyomiDetailView volume-row variant + ongoing-series gap rendering, P7 provider-to-detail-view wiring + filter chips + Continue strip canonical-key resolution, P8 search widget Premium section + chip + tile dedup, P9 adopt-existing-folder + MangaTransferCoordinator, P10 cover extraction off-thread + bounded decompression, P11 30-series catalog curated + 21-case smoke matrix executed. Stremio-for-manga MVP live: ~30 hand-curated series serve pristine trusted-uploader nyaa torrent scans via selective single-volume libtorrent file-priority; WeebCentral remains fallback for non-catalog series and post-coverage chapters on ongoing titles. New code footprint: ~XXX LOC across XX files. Smoke results: <N>/21 pass. Open follow-ups for v1.1: hot reload catalog, community-catalog signing gate, destructive merge UI, curation tooling for new series add/rotation. Brainstorm doc at docs/superpowers/specs/2026-05-15-tankoyomi-premium-brainstorm.md remains the authoritative design record (Agent 1 sections 1-16 + Codex sections 17-27 co-authorship per gov-v4 Rule 20). Plan doc at docs/superpowers/plans/2026-05-15-tankoyomi-premium.md.] | Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion, /superpowers:finishing-a-development-branch] | files: resources/manga_premium_catalogs/tankoyomi_premium_2026-05.json, agents/chat.md
```

---

## Self-review

This block was run inline by Agent 1 (the plan author) per the writing-plans skill self-review checklist after writing the final phase. Findings + fixes applied in place; no separate review iteration.

**1. Spec coverage (brainstorm-md section-by-section against the plan):**

- §1-§2 (concept + motivation): captured in plan header. OK.
- §3 (architecture - Option A + alternatives): Phase 3 implements Option A; alternatives recorded in the brainstorm only (correct - no need to reproduce in the plan). OK.
- §4 (UI integration): Phase 6 (detail view) + Phase 7 (wiring) + Phase 8 (search widget). OK.
- §5 (catalog file shape): Phase 1 schema + Phase 11 curation. OK.
- §6 (30-series catalog): Phase 11.2. OK.
- §7 (edge cases): smoke matrix in Phase 11.3 covers all 8 cases from §7 plus Codex's 13 additions = 21 total. OK.
- §8 (out of scope): not implemented (correct - out of scope). OK.
- §9 (forward-compat doors): Phase 1's multi-file loader supports the multi-catalog door; Stremio-style manifest fields present in schema. OK.
- §10 (smoke matrix): Phase 11.3. OK.
- §11 (footprint estimate): plan tracks footprint per phase via the file lists; final RTC notes total. OK.
- §12 (workflow shape Rule 20): captured at plan-doc level. OK.
- §13 (decisions log): each decision threads through specific tasks. OK.
- §14 (open questions): all 9 resolved by Codex in §27; plan implements each resolution.
  - 27.1 file-completion signal: Phase 3 uses pieceFinished + fileByteRangesOfHavePieces. OK.
  - 27.2 registerVolume vs registerChapter: Phase 5 adds registerVolume. OK.
  - 27.3 staging directory persistence: Phase 3 uses <appData>/manga_premium_staging/<infoHash>/. OK.
  - 27.4 JSON schema validation strictness: Phase 1 uses ValidationSeverity with RejectFile/RejectSeries/RejectVolume/Warn. OK.
  - 27.5 catalog curation tooling: Phase 2 builds the helper. OK.
  - 27.6 cover extraction: Phase 10 is off-thread + bounded + non-blocking. OK.
  - 27.7 mixed-origin volume read-state: Phase 5 canonical chapter keys + Phase 7 Continue strip canonical-key resolution. OK.
  - 27.8 theme palette: Phase 8 uses Theme::current().accent. OK.
  - 27.9 anything missing - 7 load-bearing items: persistent request ledger (Phase 3), atomic .tankoban-part finalization (Phase 4), expected infoHash validation (Phase 1 + Phase 4), archive quarantine (Phase 4), adopt-existing-folder mitigation (Phase 9), canonical chapter keys (Phase 5), restart-only catalog reload (called out in Phase 11 Smoke 19 + the brainstorm; loader has no hot-reload code path). All OK.

- §15 (Agent 1 self-review): predates this plan; satisfied.
- §16 (lineage): satisfied.
- §17 (Codex verdict + path corrections): all 6 corrections applied (Phase 1 LibraryScanner path + Phase 3 staging path + Phase 3 magnet add shape + Phase 8 theme + Phase 5 index primitive + Comics library scale stays series-level). OK.
- §18-§27 (Codex's 11 expansion sections): each maps to a plan phase as above.

**2. Placeholder scan:**

- No "TBD" / "TODO" / "implement later" tokens in actionable steps. The `_TODO_resolve_files` field in the helper tool's magnet-only mode is intentional output JSON metadata (not a plan placeholder).
- Phase 7's `appendChipToTile` and `m_chapterTable` references depend on member names introduced by the merger arc; the plan calls out "grep for actual member name" where the name is uncertain. These are not placeholders; they're realistic codebase-coupling notes for the executor.
- Phase 11's smoke 14 (`Kill app after file complete but before final rename`) notes the race timing is hard to hit precisely; the validation is by inspection rather than deterministic. This is an honest description of the smoke, not a placeholder.

**3. Type consistency:**

- `PremiumCatalogEntry`, `PremiumVolumeEntry`, `PremiumChapterRef` consistent across Phases 1, 3, 6, 7, 10.
- `TorrentRequest::Status` enum values consistent across Phases 3, 4.
- `MangaDownloadIndex::Entry` struct fields after Phase 5 extension consistent with Phase 7 + Phase 11 consumers.
- `TorrentVolumeProvider` ctor signature evolves: Phase 3 (5 args) -> Phase 5 (+ index = 6 args) -> Phase 10 (+ coversDir = 7 args). All call sites updated in their respective phases.
- Signal names + signatures consistent: `volumeProgress(QString, int, double)`, `volumeCompleted(QString, int, QString)`, `volumeFailed(QString, int, QString, QString)`, `swarmStatus(QString, int, int)`, `volumeCoverReady(QString, int, QString)`.

**4. Scope check:**

The 11 phases cover one cohesive subsystem: Premium catalog + selective torrent volume download + UI integration. Each phase is independently buildable; no phase requires content from a later phase. Sequencing aligns with Codex section 26 phase recommendation. No phase is so large that it should be split further.

Self-review passes. Plan is ready.

---

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-15-tankoyomi-premium.md`. Two execution options:

**1. Subagent-Driven (recommended)** - dispatch a fresh subagent per phase with two-stage code review between phases. The author (Agent 1) reviews each phase's diff before approving the next dispatch. Catches drift early; each phase's context is clean.

**2. Inline Execution** - execute tasks in this session using `/superpowers:executing-plans`. Batch execution within each phase, RTC checkpoint at phase close.

Phase 11 (catalog curation + smoke matrix) is mostly wall-clock-heavy and Hemanth-touching by nature; both execution modes converge on the same human-in-the-loop pacing for that phase regardless of choice.

Which approach?

