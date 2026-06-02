# TORRENT_DB_DURABILITY — plan (2026-06-02)

**Owner:** Agent 4 (TorrentRepository / torrents.db, TORRENT_PERSISTENCE_COLLAPSE lineage)
**Trigger:** Agent 0 root-caused the app-wide "Not Responding"/crash to a **corrupted `torrents.db`**
(SQLite "database disk image is malformed"). On startup the download reconcile hammers the malformed
DB — dozens of `upsertStreamDownload` failures in milliseconds — churning/blocking the UI thread →
Windows Event 1002 "stopped interacting" hang. Agent 0 applied the immediate mitigation (timestamped
backup + reset of `torrents.db` in both the dev and `Tankoban-personal` data dirs); the app is responsive
again. This plan is the **durable** fix so it cannot recur.

**Likely corruption mechanism:** `build_and_run.bat` runs `taskkill /F /IM Tankoban.exe` before every
rebuild. A hung app gets force-killed; if the kill lands during a WAL **checkpoint** (WAL frames being
written back into the main DB file), the main DB b-tree is left inconsistent → next start hangs worse
(vicious cycle). WAL+synchronous tuning reduces the odds but a force-kill is inherently hostile, so the
real safety net is **self-heal on detect** + **degrade-don't-hang**.

**Golden fixture (TDD):** `tests/fixtures/torrents_db_malformed_2026-06-02.db` (staged reset-proof by
Agent 0; 864256 bytes). Signature: `PRAGMA quick_check` raises / returns non-`ok`; `integrity_check`
reports "Tree 8 page 24: btreeInitPage() returns error code 1 …". Commit this fixture with Part 1's test.

---

## Part 1 — Startup integrity check + auto-recover  *(load-bearing; self-heal)*

**Where:** `TorrentRepository::open()` (src/core/torrent/TorrentRepository.cpp:140), after `m_db.open()`
succeeds and **before** `initSchema()`.

**What:**
1. After a successful `m_db.open()`, run `PRAGMA quick_check` (faster than `integrity_check`; first row
   `"ok"` == healthy). Treat a raised error, a query failure, or any non-`ok` first row as **malformed**.
2. On malformed → `recoverCorruptDatabase(dbFilePath)`:
   - `close()` the connection (and remove it from `QSqlDatabase`).
   - Rename the corrupt file + its `-wal`/`-shm` sidecars to `<db>.corrupt-<yyyy-MM-dd-HHmmss>` (never
     delete — keep for forensics, mirrors Agent 0's manual recovery).
   - Re-open fresh at the same path; `initSchema()` recreates the empty v2 schema.
   - `qWarning()` loudly with the backup path; emit a new signal `databaseRecovered()` so the
     reconcile/index layer knows the DB is empty and can rebuild-from-disk (Part 3 consumes this).
3. If recovery itself fails (can't rename / can't reopen), return `false` from `open()` (callers already
   handle a closed repo by degrading — Part 3) rather than proceeding onto a bad handle.

**Pure-logic seam (unit-testable):** factor the decision as a free function
`bool databaseIsHealthy(QSqlDatabase&)` (runs quick_check) + `recoverCorruptDatabase(path)` that operates
on the filesystem. Test feeds the golden fixture:
- `test_db_health_detects_malformed`: open fixture → `databaseIsHealthy` == false.
- `test_db_recover_backs_up_and_recreates`: copy fixture to a temp path → `open()` → assert (a) a
  `*.corrupt-*` sibling now exists, (b) the live DB at the path passes quick_check, (c) schema_version == 2.
- `test_db_health_passes_on_fresh`: fresh temp DB → healthy == true, no backup created.

**Risk:** low/medium (touches the open path used by the whole app). Mitigation: behind quick_check, only
fires on actual malformation; fresh/healthy DBs are untouched (one extra cheap PRAGMA per launch).

---

## Part 2 — WAL durability hardening  *(reduces corruption odds)*

**Where:** `TorrentRepository::open()`, set programmatically right after open (connection-level PRAGMAs
must be set per-connection; do not rely on the schema blob for these).

**What:**
- `PRAGMA journal_mode=WAL` — already in the schema blob (line 39); make explicit + verify it returns
  `"wal"` (log a warning if not, e.g. the file is locked in another mode).
- `PRAGMA synchronous=NORMAL` — the WAL durability sweet spot: durable across an app crash/force-kill,
  only risks the *last* transaction on OS/power loss (never corrupts).
- `PRAGMA busy_timeout=5000` — wait instead of instantly failing on `SQLITE_BUSY` (defends against any
  transient second-connection overlap, e.g. dev + personal instances, leaked connections).
- Checkpoint discipline: `PRAGMA wal_autocheckpoint=1000` is the default; add an explicit
  `PRAGMA wal_checkpoint(TRUNCATE)` in `close()` so a clean shutdown leaves an empty WAL (shrinks the
  force-kill-during-checkpoint window). Fix the shutdown ordering Agent 0 saw ("removeDatabase … still in
  use"): ensure all `QSqlQuery` objects are destroyed before `close()`/`removeDatabase`.

**Honesty note:** Part 2 lowers the probability of corruption but cannot eliminate it for a `/F` force-kill;
Part 1 + Part 3 are what make corruption a non-event.

---

## Part 3 — Reconcile/upserts off the UI thread + degrade-don't-hang  *(load-bearing; no-hang)*

**Where:** the startup download-index reconcile that calls `upsertStreamDownload` in a tight loop
(StreamDownloadIndex.cpp reconcile/validateAll/backfill + the TorrentClient boot path). Confirm exact
sites at implementation time (`grep reconcile|upsertStreamDownload|validateAll`).

**What:**
1. **Off the UI thread:** run the boot reconcile on a worker (QtConcurrent — the index already uses it for
   `validateAll` per spec §10.4) so even a slow/failing DB never blocks Qt's event loop / paint thread.
2. **Circuit-breaker (degrade, don't tight-loop):** track consecutive write failures; after a small
   threshold (e.g. 5) abort the reconcile pass, mark the repo `degraded`, `qWarning()` **once**, and stop
   hammering. A degraded repo serves reads best-effort and the app stays responsive instead of churning
   thousands of failing upserts/second.
3. **Compose with Part 1:** on `databaseRecovered()` the index is empty → trigger the existing
   rebuild-from-disk scan (StreamRescueScanner / registerEpisode reconcile) so a self-healed DB
   re-populates from the files actually on disk, rather than losing the user's download state silently.

**Risk:** medium (threading + lifecycle). Mitigation: reconcile is already designed to run via
QtConcurrent; the circuit-breaker is additive and only changes behavior on the (now-rare) failure path.

---

## Sequencing & verification
- Part 1 first (TDD with the golden fixture) — it is the safety net and is unit-testable in isolation.
- Part 2 second (small, mostly PRAGMA additions; verify journal_mode/synchronous via a query in the test).
- Part 3 third (threading + circuit-breaker), composing with Part 1's `databaseRecovered()`.
- `ctest` (tankoban_tests: TorrentRepoCrud + new DbDurability cases) green per part.
- Live smoke: copy the golden malformed fixture into the data dir → launch → app must come up Responding
  with a fresh DB + a `*.corrupt-*` backup beside it, no hang. Then force-kill mid-session a few times and
  confirm no recurring corruption.

## Cross-domain flag
The same force-kill corruption risk applies to the other SQLite stores — `MangaDownloadIndex`
(Agent 1) and `BooksCatalogueLibraryStore` (Agent 2). The Part 1 `databaseIsHealthy` + `recoverCorrupt`
helper should be lifted into a shared utility they can adopt. Flag to A1/A2 after Part 1 lands.
