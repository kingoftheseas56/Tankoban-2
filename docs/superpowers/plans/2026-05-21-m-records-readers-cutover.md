# m_records Readers Cutover Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (inline single-agent refactor — no Jr fan-out, no worktree per `feedback_no_worktrees`). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate `TorrentClient`'s ~72 in-process `m_records` (JSON cache) reader callsites to the SQLite-backed `TorrentRepository` substrate, completing the Phase 5 ("Caller Class Validation") deliverable of the TORRENT_PERSISTENCE_COLLAPSE arc. After this arc, `m_records` is no longer load-bearing and can be deleted entirely in a future cleanup commit.

**Architecture:** The four lying notebooks (torrents.json + stream_bulk_groups.json + stream_downloads.json + per-hash .fastresume) were collapsed into the SQLite-backed bulletproof notebook (`TorrentRepository`) during Phases 0–4 of the persistence-collapse arc. The repository is the durable source of truth; `m_records` survives only as a vestigial in-process cache. Phase 5 swaps reader sites to the repo until `m_records` is dead.

**Tech Stack:** Qt6 (QObject + QSqlDatabase + QString), C++20, existing `TorrentRepository` CRUD surface, GoogleTest via FetchContent.

---

## Categorical breakdown (from session-recap 2026-05-21 wake start)

| Category | Count | Shape | Phase |
|---|---|---|---|
| `m_records.contains(hash)` | 20 | boolean predicate | P5.1 (this commit) |
| `m_records.value(hash).toObject().value(field)` | 7 | vocabulary-translation field reads | P5.2 |
| `for (auto it = m_records.constBegin(); ...)` | 3 | bulk iterations → `listTorrents()` rewrites | P5.3 |
| `m_records.remove(hash)` | 1 | mutation; check downstream | P5.4 |
| `m_records.find(hash)` | 1 | iterator-based access | P5.4 |

After P5.4 lands, `m_records` is unused → delete the member declaration + the `saveRecords()` call sites that already gut-no-op'd in P4.4. That's the close-out commit.

---

## File Structure

**Modified files (P5.1, this commit):**
- `src/core/torrent/TorrentRepository.h` — add `bool hasTorrent(const QString& hash)` decl
- `src/core/torrent/TorrentRepository.cpp` — add `hasTorrent` impl (SELECT 1 query, cheaper than `getTorrent().has_value()`)
- `src/core/torrent/TorrentClient.cpp` — swap 20 `m_records.contains(hash)` → `m_repo.hasTorrent(hash)` callsites
- `tests/core/torrent/test_torrent_repository_crud.cpp` — 3 new GoogleTests (positive / negative / case-insensitive)

**Future-phase files (P5.2+, NOT this commit):**
- Same files (TorrentClient.cpp gets vocabulary-translation helper functions, TorrentRepository.h gets richer field-getter methods if needed).

---

## Invariants to preserve

1. **Hash case-insensitivity** — `m_records` keys are lowercase; `TorrentRepository::hasTorrent` calls `hash.toLower()` to match.
2. **No semantic shift on isDuplicate** — the const `isDuplicate(magnetUri)` method continues to work; `m_repo` is `mutable` (declared in TorrentClient.h:409) to permit non-const repo queries from const contexts.
3. **No new behavior** — `.contains` returned bool; `hasTorrent` returns bool. Identical semantics.

---

## Tasks

### Task 1 (P5.1): Add `hasTorrent` predicate to TorrentRepository ✅ DONE IN FLIGHT

**Files:** `src/core/torrent/TorrentRepository.h`, `src/core/torrent/TorrentRepository.cpp`

- [x] Header decl added (line ~70).
- [x] Impl added next to `getTorrent` (line ~501). SELECT 1 query, single-row probe. Lowercase-hash binding to match key case-insensitivity invariant.

### Task 2 (P5.1): Sweep 20 m_records.contains callsites in TorrentClient.cpp ✅ DONE IN FLIGHT

**Files:** `src/core/torrent/TorrentClient.cpp`

- [x] Replace_all from `m_records.contains(` to `m_repo.hasTorrent(`.
- [x] Verified 20 in / 20 out via grep — no false positives, no orphan refs.

### Task 3 (P5.1): Add 3 GoogleTests for hasTorrent ✅ DONE IN FLIGHT

**Files:** `tests/core/torrent/test_torrent_repository_crud.cpp`

- [x] `HasTorrentReturnsTrueAfterUpsert` — positive path.
- [x] `HasTorrentReturnsFalseForUnknownHash` — negative path.
- [x] `HasTorrentIsCaseInsensitive` — invariant 1 from above.

### Task 4 (P5.1): Build + test verify under BUILD LANE 🔄 IN PROGRESS

**Files:** None (verification only)

- [x] BUILD LANE claimed (chat.md ~12:10pm IST).
- [x] `build_check.bat` → BUILD OK.
- [ ] tankoban_tests target build + ctest filter run on TorrentRepoCrudTest.HasTorrent* — all 3 PASS.
- [ ] Adjacent-baseline sanity (existing tests still pass).

### Task 5 (P5.1): Commit + chat.md RTC + release BUILD LANE

- [ ] One bundled commit covering the 4-file delta (repo .h/.cpp + client .cpp + test .cpp).
- [ ] chat.md BUILD LANE release line.
- [ ] chat.md RTC documenting the cutover scope + remaining P5.2-P5.4 follow-on work.

### Phase 5.2+ (NOT THIS COMMIT — scoped pending future wake)

- **P5.2** — vocabulary-translation spec for the 7 `.value(...).toObject().value(field)` callsites. Each one reads a JSON field that has a `TorrentRow` peer; the translation table is what needs ratification. Hemanth-driven scope decision: ship the 7 as one commit, or split by field (state / savePath / name / category / etc).
- **P5.3** — 3 bulk-iteration rewrites against `listTorrents()`. Iterator-based; replaces JSON object iteration with `std::vector<TorrentRow>` for-range.
- **P5.4** — 1 `.remove` + 1 `.find` callsite. Probably collapses into P5.2 or P5.3 commit depending on context.
- **P5.5** — close-out: delete `m_records` member + `saveRecords()` stub. After P5.4, no readers remain → safe to delete the writer surface too.

Estimated total scope: 4–6 commits across 1–2 wakes. P5.1 (this commit) is the smallest mechanical opener.

---

## Self-Review Checklist

After plan landed:
1. ✅ **Spec coverage:** All 5 categorical buckets from the session-recap mapped to a phase. P5.1 fully spec'd; P5.2+ scoped pending.
2. ✅ **No placeholders:** P5.1 tasks have concrete code already in flight.
3. ✅ **Type consistency:** `bool hasTorrent(const QString& hash)` consistent across header, .cpp, test file, callsites.
4. ✅ **Invariants preserved:** hash case-insensitivity + const-context callability + no-semantic-shift all explicit.

## Risks

- **None for P5.1** — mechanical swap, identical semantics, repo invariant matches.
- **For P5.2:** vocabulary translation has edge cases (legacy "downloading" vs repo "active" state-equivalence checks) that need Hemanth-ratified spec before execution. Easy to ship a build-+-tests-green refactor that silently shifts UI state. Plan-first discipline mandatory.

---

## Execution Handoff

Plan complete. Inline execution (this session) for P5.1; P5.2+ deferred to future wakes with Hemanth-ratified vocabulary spec.

REQUIRED SUB-SKILL on execution: `superpowers:executing-plans`.
