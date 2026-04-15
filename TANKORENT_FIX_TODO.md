# Tankorent Fix TODO — middle-ground search + audit-P0-only downloader

**Owner:** Agent 4B (Sources — Tankorent + Tankoyomi). Agent 0 coordinates phase gates and commit sweeps. Agent 6 reviews each phase against this doc + `agents/audits/tankorent_2026-04-16.md` as co-objective. Cross-agent touches flagged per phase.

**Created:** 2026-04-16 by Agent 0 after Agent 7's Tankorent audit + Agent 4B's observation-only validation pass (chat.md:16935-16999).

## Context

Tankorent's 7-indexer search engine + libtorrent-backed downloader shipped during the original Tankoban 2 build-out but never had a settled domain agent until Agent 4B was established 2026-04-16. Hemanth flagged "world-class" ambition; Agent 7 delivered a comparative audit against Jackett + Prowlarr (search) and qBittorrent (downloader) at `agents/audits/tankorent_2026-04-16.md`.

Audit verdict was blunt: **Tankorent is not better than Jackett/Prowlarr at search or qBittorrent at downloading.** It's a useful embedded search-to-add tool with a reasonable libtorrent starter surface, but references are substantially ahead on configurability, metadata normalization, health surfacing, tracker/peer/file management, and persistence semantics.

Agent 4B validated every audit P0 with on-disk evidence (8/8 CONFIRMED BROKEN, with SEARCH-P0-1 correctly split into SCOPE-BOUNDED + PARTIAL per identity direction). No correctness bugs beyond audit scope surfaced. One P1 (SEARCH-P1-1 canonical info-hash schema) confirmed foundational for dedup downstream. Two P1s (DOWNLOADER-P1-1 seeding variants, DOWNLOADER-P1-3 protocol toggles) correctly flagged SCOPE-BOUNDED per identity.

**Identity direction locked by Hemanth 2026-04-16** via AskUserQuestion (saved to memory `project_tankorent_identity.md`):

1. **Middle-ground search.** Compile-time indexers kept. No Cardigann YAML, no runtime tracker-adding, no Torznab endpoint. But runtime config UI (per-indexer enable/disable, credentials for cookie-gated sources), per-indexer health/failure surfacing (last error + auth state + response time), 1337x Cloudflare fix (targeted, not general), typed category routing (search-type combo actually routes to relevant indexers per category) ARE in scope.
2. **All 7 indexers first-class.** 1337x's Cloudflare JavaScript challenge MUST be solved. Disabled-with-comment is not acceptable. Approach: embedded QWebEngineView auto-harvest (QtWebEngine already linked for book reader — no new dep).
3. **Downloader parity scope = audit P0s only.** Add flows expansion + tracker mgmt UI + peer mgmt UI + active file priority editing. Skip RSS automation, bandwidth scheduler, share-action variants, protocol toggles UI, alternative speed limits. Detail surface = **unified tabbed properties widget** (qBittorrent-style General/Trackers/Peers/Files), NOT three separate dialogs.
4. **Internal-only API.** No Torznab/Newznab endpoint.

**Scope:** 7 phases, ~15-17 batches. Phase 1 is a 10-line bug fix (typed category routing — foundation for everything). Phases 2-4 complete the search side. Phases 5-6 complete the downloader side. Phase 7 closes inherited open debt from Agent 4's Congress 4 observer position.

## Objective

After this plan ships, a Tankorent user can:
1. Type a query + pick "Books" → search only fires book-relevant indexers (PirateBay, ExtraTorrents, TorrentsCSV, 1337x), not YTS/EZTV.
2. See every indexer's health in a dedicated Sources panel: green/yellow/red status, last success, last error, response time, authentication state. Enable/disable indexers at runtime. Enter credentials for cookie-gated sources.
3. Use 1337x as a first-class indexer — Cloudflare challenge solved transparently via embedded QWebEngineView cookie harvest.
4. Add torrents from multiple entry points: search result (existing), magnet URL paste dialog, `.torrent` file picker, drag-drop from Explorer, clipboard auto-preload, multi-URL paste.
5. Double-click a torrent → unified properties widget with 4 tabs:
   - **General** (name, size, pieces, hash, save path, availability, ratio)
   - **Trackers** (list with status/tier/announce/peers; add/remove/edit/reannounce)
   - **Peers** (list with country/client/progress/speed; ban peer, add peer)
   - **Files** (editable priority column; open/open-folder/rename per file; supersedes read-only `TorrentFilesDialog`)
6. Completed torrents downloading into tracked library folders auto-rescan so Agent 5's VideosPage / ComicsPage pick up new files without manual rescan. Download-column data contract in place for Agent 5's future list-view work.

Dedup and ranking improvements happen quietly under the hood — same torrent from 2 indexers consolidates via canonical infoHash regardless of magnet-string differences.

## Non-Goals (explicitly out of scope for this plan)

- **Cardigann YAML runtime-tracker-adding pipeline** — out per identity #1. If a new tracker emerges as critical, add as a new compile-time `TorrentIndexer` subclass.
- **Torznab/Newznab API endpoint** — out per identity #4. Tankorent integrates with Tankoban only.
- **RSS automation with downloader rules** — out per identity #3 scope ceiling. qBittorrent-level feature, not essential for embedded media-app use.
- **Bandwidth scheduler with day/time alternative speed limits** — out per identity #3.
- **Share-action variants** (remove, remove-with-content, super-seed) beyond the current pause-on-threshold — SCOPE-BOUNDED per Agent 4B on DOWNLOADER-P1-1.
- **Protocol toggles UI** (DHT/LSD/PEX/NAT-PMP/UPnP/encryption) — SCOPE-BOUNDED per Agent 4B on DOWNLOADER-P1-3. Engine defaults stay as-is.
- **Full qBittorrent properties features** — pieces-downloaded visualization bar, availability bar, download speed graph over time, web seeds panel, reannounce-data watcher. If Hemanth flags gaps post-ship, can revisit.
- **YACReader-style broad archive format expansion** — not Tankorent's surface at all.
- **Tankoyomi audit/fix work** — separate TODO when Hemanth requests.
- **Any work outside `src/core/indexers/*`, `src/core/torrent/*`, `src/core/TorrentIndexer.h`, `src/core/TorrentResult.h`, `src/core/manga/*`, `src/ui/pages/TankorentPage.*`, `src/ui/pages/tankorent/*` (new subdir), `src/ui/dialogs/AddTorrentDialog.*`, `src/ui/dialogs/AddFromUrlDialog.*` (new), `src/ui/dialogs/TorrentFilesDialog.*` (to be deleted), plus targeted touches to `src/core/CoreBridge.*` for Phase 7**.

## Agent Ownership

All batches are **Agent 4B's domain** (Sources). Primary files: `src/core/indexers/*`, `src/core/torrent/*`, `src/core/TorrentIndexer.h`, `src/core/TorrentResult.h`, `src/ui/pages/TankorentPage.*`, `src/ui/pages/tankorent/*`, `src/ui/dialogs/AddTorrentDialog.*`, `src/ui/dialogs/TorrentFilesDialog.*`, new `src/ui/dialogs/AddFromUrlDialog.*`.

**Cross-agent touches flagged per phase:**
- **Phase 7.1 (rootFoldersChanged wiring)** touches `src/core/CoreBridge.*` — shared file, Agent 0 coordination territory per Rule 7. Agent 4B posts heads-up in chat.md before the edit.
- **Phase 7.2 (downloadProgress contract)** is downstream-consumed by Agent 5's VideosPage / ComicsPage. No direct Agent 5 file touches from Agent 4B; Agent 5 wires the consumer side on their own timeline.
- **CMakeLists.txt** shared file per Rule 7. All new `.cpp`/`.h` additions flagged in chat with exact lines added.

Agent 0 coordinates phase gates and commit sweeps. Agent 6 reviews each phase against this plan + the audit as co-objective.

---

## Phase 1 — Search-type typed category routing (SEARCH-P0-3)

**Why:** Agent 4B validation confirmed BROKEN at chat.md:16945. `m_searchTypeCombo` populated at [TankorentPage.cpp:326-333](src/ui/pages/TankorentPage.cpp) with Videos/Books/Audiobooks/Comics data values. In [startSearch at :701-749](src/ui/pages/TankorentPage.cpp), lines :715-716 read `m_sourceCombo->currentData()` and `m_categoryCombo->currentData()` — `m_searchTypeCombo->currentData()` is never read, anywhere. Dispatch list :727-733 hard-codes every indexer regardless of search-type selection. Consequence: a "Books" query fires YTS (movies-only) and EZTV (TV-only); "Audiobooks" fires Nyaa (anime); etc.

Foundation phase: straightforward bug fix + allowlist decision. Must ship before Phase 3's Sources panel so the panel shows correctly-filtered dispatch behavior.

### Batch 1.1 — Media-type → indexer allowlist

- Read `m_searchTypeCombo->currentData().toString()` at the top of `startSearch` (inject before the existing source/category reads at :715).
- Define `static const QHash<QString, QSet<QString>> kMediaTypeIndexers = { ... }` at file scope in `TankorentPage.cpp`:
  - `"Videos"` → `{"yts", "eztv", "piratebay", "1337x", "exttorrents"}`
  - `"Books"` → `{"piratebay", "exttorrents", "torrentscsv", "1337x"}`
  - `"Audiobooks"` → `{"piratebay", "exttorrents", "torrentscsv", "1337x"}`
  - `"Comics"` → `{"nyaa", "piratebay", "1337x"}`
- Extract the `addIf` calls at :727-733 into a helper `dispatchIndexers(mediaType, sourceFilter, query, limit, categoryId)`. Helper iterates the allowlist for `mediaType`, ANDs with the `sourceCombo` selection (if not "All Sources"), then instantiates + dispatches only those indexer ids.
- 1337x stays commented-out in this batch — Phase 4 un-disables it. Keep the `// 1337x disabled` comment for now; Phase 4.3 removes it.

**Files:** [src/ui/pages/TankorentPage.cpp](src/ui/pages/TankorentPage.cpp) only. ~25 LOC.

**Success:** Videos query dispatches YTS/EZTV/PirateBay/ExtraTorrents (1337x still commented); Books query dispatches PirateBay/ExtraTorrents/TorrentsCSV; Audiobooks same as Books; Comics dispatches Nyaa/PirateBay. No indexer fires outside its allowlist. Regression: "All Sources" combo + any media type = intersection of allowlist and compiled indexers (currently 6 until Phase 4).

### Phase 1 exit criteria
- Typed category routing live.
- Default allowlist applied; Hemanth can tune post-ship.
- Agent 6 review against audit SEARCH-P0-3 citation chain.
- `READY FOR REVIEW — [Agent 4B, TANKORENT_FIX Phase 1]: Search-type typed category routing | Objective: Phase 1 per TANKORENT_FIX_TODO.md + agents/audits/tankorent_2026-04-16.md. Files: src/ui/pages/TankorentPage.cpp.`

---

## Phase 2 — TorrentResult canonical info-hash + schema expansion (SEARCH-P1-1)

**Why:** Agent 4B flag #5: foundational for every downstream phase. Current [TorrentResult.h:8-18](src/core/TorrentResult.h) has 9 fields; no canonical infoHash. Dedup at [TankorentPage.cpp:824-836](src/ui/pages/TankorentPage.cpp) runs regex over `magnetUri` to extract `btih:...`, falls back to whole-magnet-string. When indexers emit subtly-different magnets for the same infohash (different tracker list order, added `&dn=...` display-name, etc.), dedup fragments and the user sees the same torrent multiple times.

Low-risk phase; high-leverage. Ships before Phase 3 (health surfacing benefits from having infoHash as a stable identity) and before Phase 4 (1337x's cookie-handling logic benefits from infoHash-based result cache).

### Batch 2.1 — Add infoHash + publishDate + detailsUrl to TorrentResult

- Extend `TorrentResult` struct at [src/core/TorrentResult.h](src/core/TorrentResult.h):
  ```cpp
  QString infoHash;       // canonical 40-char lowercase hex BTIH
  QDateTime publishDate;  // when indexer listed the torrent; invalid if unknown
  QString detailsUrl;     // per-indexer details page URL; empty if none
  ```
- Populate per-indexer at parse time. Each `.cpp` file in [src/core/indexers/](src/core/indexers):
  - **Nyaa** ([NyaaIndexer.cpp:182-188](src/core/indexers/NyaaIndexer.cpp)) — RSS pubDate → `publishDate`; GUID → `detailsUrl`; extract btih from magnet as fallback for infoHash.
  - **PirateBay** ([PirateBayIndexer.cpp:69-76](src/core/indexers/PirateBayIndexer.cpp)) — API exposes `info_hash` directly + added timestamp; use these.
  - **YTS** ([YtsIndexer.cpp:94-101](src/core/indexers/YtsIndexer.cpp)) — YTS JSON has `hash` (movie.torrents[n].hash) + `date_uploaded`; use these.
  - **EZTV** ([EztvIndexer.cpp](src/core/indexers/EztvIndexer.cpp)) — RSS usually has `<enclosure>` with infoHash attribute; check + fallback to magnet regex.
  - **ExtraTorrents** ([ExtTorrentsIndexer.cpp](src/core/indexers/ExtTorrentsIndexer.cpp)) — check parse path for infoHash availability.
  - **TorrentsCSV** ([TorrentsCsvIndexer.cpp:69-74](src/core/indexers/TorrentsCsvIndexer.cpp)) — CSV has infohash column; use directly.
  - **1337x** ([X1337xIndexer.cpp](src/core/indexers/X1337xIndexer.cpp)) — parse from detail-page magnet after scrape.
- Lowercase + strip-to-40-hex normalization in a small `src/core/TorrentResult.h` inline helper `static QString canonicalizeInfoHash(QString raw)`. Call from every indexer at parse time to normalize.

**Files:** [src/core/TorrentResult.h](src/core/TorrentResult.h), all 7 indexer `.cpp` files in [src/core/indexers/](src/core/indexers).

**Success:** every indexer populates `infoHash` when available. Log a debug warning (no user-visible error) when an indexer can't extract infoHash — becomes a list of known-degraded sources. No UI change this batch.

**Isolate-commit:** yes. Cross-indexer change; isolate before 2.2 so any indexer-specific parse regressions surface in isolation.

### Batch 2.2 — Replace regex-dedup with canonical infoHash dedup

- Rewrite [TankorentPage.cpp:811-836](src/ui/pages/TankorentPage.cpp) dedup:
  - Primary key: `result.infoHash` (if non-empty).
  - Fallback key: current `btih:` regex extract from magnet (for sources where Batch 2.1's parse couldn't populate).
  - Last-resort key: whole magnet string (preserves current behavior for broken sources).
- Ranking stays presentation-side (seeders/size filters at :838-865 unchanged this batch).

**Files:** [src/ui/pages/TankorentPage.cpp](src/ui/pages/TankorentPage.cpp).

**Success:** query for a popular torrent listed on 3+ indexers → dedups cleanly; result list shows 1 entry with the best-seeder variant winning. Compare against pre-batch behavior where duplicates survive.

### Phase 2 exit criteria
- `TorrentResult` schema expanded with 3 new fields.
- All 7 indexers populate at parse time.
- Dedup switched to canonical infoHash.
- Agent 6 review against audit SEARCH-P1-1 citation chain.

---

## Phase 3 — TorrentIndexer health contract + runtime config UI (SEARCH-P0-1 in-scope + SEARCH-P0-4 merged)

**Why:** Agent 4B flag #4 — SEARCH-P0-1 and SEARCH-P0-4 overlap. One `TorrentIndexer` contract expansion + one `IndexerStatusPanel` widget solves both simultaneously.

- SEARCH-P0-1 PARTIAL-broken portion (chat.md:16941) = runtime config UI (per-indexer enable/disable + credential entry) + per-indexer health surfacing. Cardigann runtime tracker-adding is SCOPE-BOUNDED out.
- SEARCH-P0-4 CONFIRMED BROKEN (chat.md:16947) = `onSearchError` collapses to generic strings. No per-indexer health table, no auth/rate-limit/Cloudflare distinction, no response time, no disabled-reason.

Current `TorrentIndexer` base at [TorrentIndexer.h:14-20](src/core/TorrentIndexer.h) has no health/status/auth contract — only `id() / displayName() / search() / searchFinished / searchError`. This phase expands it.

### Batch 3.1 — Expand TorrentIndexer base contract

- Add to [src/core/TorrentIndexer.h](src/core/TorrentIndexer.h):
  ```cpp
  enum class IndexerHealth {
      Unknown,           // never queried
      Ok,                // last query succeeded within TTL
      Disabled,          // user-disabled via Sources panel
      AuthRequired,      // missing/invalid credentials
      CloudflareBlocked, // CF challenge unsolvable (Phase 4 fills 1337x)
      RateLimited,       // recent 429 / similar
      Unreachable        // network error / timeout / 5xx
  };

  virtual IndexerHealth health() const = 0;
  virtual QDateTime lastSuccess() const = 0;
  virtual QString lastError() const = 0;
  virtual qint64 lastResponseMs() const = 0;
  virtual bool requiresCredentials() const { return false; }
  virtual QStringList credentialKeys() const { return {}; }
  virtual void setCredential(const QString& /*key*/, const QString& /*value*/) {}
  virtual QString credential(const QString& /*key*/) const { return {}; }
  ```
- Each indexer implementation tracks `{lastSuccess_, lastError_, lastResponseMs_, health_}` state members updated on every `onReplyFinished` / `onReplyError` / `cancel()` path.
- Network-failure classification: `QNetworkReply::TimeoutError` → `Unreachable`; HTTP 429 → `RateLimited`; HTTP 403 with "cloudflare" header → `CloudflareBlocked`; HTTP 401/403 otherwise → `AuthRequired`; else → `Unreachable`.
- EZTV's existing raw cookie header at [EztvIndexer.cpp:65](src/core/indexers/EztvIndexer.cpp) becomes the first `requiresCredentials() = true` indexer with `credentialKeys() = { "cookie" }`. Credential stored via QSettings key `tankorent/indexers/eztv/cookie`; `setCredential` writes, indexer reads at request-build time.
- Persistence schema: QSettings `tankorent/indexers/<id>/health/{lastSuccess,lastError,lastResponseMs}` for resume after restart.

**Files:** [src/core/TorrentIndexer.h](src/core/TorrentIndexer.h), all 7 indexer `.cpp` files + `.h` files in [src/core/indexers/](src/core/indexers).

**Success:** each indexer reports current health state. EZTV's cookie moves from hardcoded to user-configurable via credentials. `health()` transitions correctly on failure/success events. No UI yet — that's Batch 3.2.

### Batch 3.2 — IndexerStatusPanel widget

- NEW `src/ui/pages/IndexerStatusPanel.h/.cpp`. `QWidget` with a `QTableWidget`:
  - Columns: Name (icon + displayName), Health (colored status pill — gray/green/amber/red per aesthetic), Last Success (relative: "2 min ago"), Last Error, Response Time, Enabled (checkbox), Credentials (expand row if `requiresCredentials()==true`).
  - Row per indexer. Rebuilds when `TorrentIndexer::searchFinished` or `searchError` fire (connect all 7 at panel construction).
  - Enable/disable checkbox persists to QSettings `tankorent/indexers/<id>/enabled` (default true). `TankorentPage::startSearch` reads this + ANDs with Phase 1's allowlist.
  - Credentials column: single-line entry per credentialKey; save button triggers `setCredential()`.
- Dedicated `QDialog` host so the panel opens as a modal from the Sources button (Batch 3.3 wires). Not tabbed into a main settings pane — Tankorent has no global preferences panel today, and adding one is out of scope.
- Grayscale palette per `feedback_no_color_no_emoji` — health status uses text label ("Ok" / "Unreachable") + subtle rim color accent only, not a full colored chip.

**Files:** NEW [src/ui/pages/IndexerStatusPanel.h](src/ui/pages/IndexerStatusPanel.h) + `.cpp`. [CMakeLists.txt](CMakeLists.txt) — add `src/ui/pages/IndexerStatusPanel.cpp` to SOURCES, `.h` to HEADERS (Q_OBJECT widget).

**Success:** panel constructs cleanly. Row per indexer with correct state. Enable/disable persists across restart. EZTV cookie entry works.

### Batch 3.3 — Wire Sources panel into TankorentPage + search dispatch

- Add "Sources" QPushButton to TankorentPage toolbar (near the existing search/sort/source combo row). Click → opens `IndexerStatusPanel` as modal.
- Modify Batch 1.1's `dispatchIndexers` helper: after allowlist filter, AND with `QSettings.value("tankorent/indexers/<id>/enabled", true).toBool()`.
- `populateSourceCombo` at [:652-663](src/ui/pages/TankorentPage.cpp) stays unchanged — it's a search-time filter (user picks "nyaa" vs "All Sources"). The enabled-state check is orthogonal (user temporarily filters to Nyaa but indexer is disabled → no results from Nyaa).
- Live-update: when Sources panel changes enabled state or credentials, broadcast a signal; TankorentPage ignores it during active search but reapplies on next `startSearch`.

**Files:** [src/ui/pages/TankorentPage.cpp](src/ui/pages/TankorentPage.cpp) + [.h](src/ui/pages/TankorentPage.h) (new `m_sourcesBtn`, `m_sourcesPanel` members + slot `onSourcesClicked`).

**Success:** Sources button visible. Opens panel. Disabling Nyaa → next "Videos" query that would include Nyaa (via allowlist intersection) skips it. Panel updates health state after every search.

### Phase 3 exit criteria
- Per-indexer health contract live across all 7.
- Sources panel functional with enable/disable + credentials.
- Search dispatch honors enabled state.
- Agent 6 review against audit SEARCH-P0-1 + SEARCH-P0-4 citation chain.

---

## Phase 4 — 1337x Cloudflare QWebEngineView auto-harvest (SEARCH-P0-2)

**Why:** Identity #2: all 7 indexers must be first-class. Agent 4B validation (chat.md:16943) confirmed [X1337xIndexer.cpp](src/core/indexers/X1337xIndexer.cpp) is a full scraper (Mozilla UA, HTML parsing, detail-page fetch, magnet extraction) but has zero challenge-solving capability. The `addIf` at [TankorentPage.cpp:729](src/ui/pages/TankorentPage.cpp) is commented out with `// 1337x disabled — Cloudflare JS challenge`.

Hemanth chose embedded QWebEngineView auto-harvest. QtWebEngineWidgets already linked for the book reader (check [CMakeLists.txt](CMakeLists.txt) — `Qt6::WebEngineWidgets`). No new module dep.

**Highest-technical-risk phase** per Agent 4B flag #3. Cloudflare iterates challenges; mitigation = Phase 3 credentials fallback (user paste cf_clearance manually).

### Batch 4.1 — CloudflareCookieHarvester class

- NEW `src/core/indexers/CloudflareCookieHarvester.h` + `.cpp`. `QObject` wrapping `QWebEngineView` + `QWebEngineProfile`.
- Singleton accessed via `CloudflareCookieHarvester::instance()`.
- API:
  ```cpp
  void harvest(const QUrl& target, const QString& indexerId);
  // Emits cookieHarvested(indexerId, QString cfClearance, QString userAgent) on success.
  // Emits harvestFailed(indexerId, QString reason) on 30s timeout or page error.
  ```
- Implementation:
  - Construct hidden `QWebEngineView` (never added to widget tree; run in offscreen mode).
  - Install a `QWebEngineUrlRequestInterceptor` logging user-agent; navigate to `target`.
  - Connect `loadFinished(bool ok)` → poll `profile->cookieStore()->loadAllCookies` for `cf_clearance` cookie + wait for additional page-ready signal.
  - Use a 30s `QTimer` for total-timeout fallback.
  - On success, persist cookie to QSettings `tankorent/indexers/<id>/cf_clearance` with `cf_clearance_expires` (7-day TTL).
  - On failure, emit `harvestFailed` with diagnostic reason.
- Stress path: if `cf_clearance` expires mid-session, caller (Batch 4.2's X1337xIndexer) invalidates cached cookie and calls `harvest()` again.

**Files:** NEW [src/core/indexers/CloudflareCookieHarvester.h](src/core/indexers/CloudflareCookieHarvester.h) + `.cpp`. [CMakeLists.txt](CMakeLists.txt) — add to SOURCES + HEADERS. Verify `Qt6::WebEngineWidgets` already in `target_link_libraries` (book reader should have pulled it).

**Success:** harvester instance constructs without crashing. `harvest(https://1337x.to, "1337x")` returns `cookieHarvested` with non-empty `cf_clearance` within 30s on a stable connection. `harvestFailed` fires cleanly on network error / timeout.

**Isolate-commit:** yes. First QtWebEngine usage in Tankorent's Sources subsystem; isolate so any runtime/link issues surface in isolation before Batch 4.2 consumes.

### Batch 4.2 — X1337xIndexer Cloudflare integration

- [src/core/indexers/X1337xIndexer.cpp](src/core/indexers/X1337xIndexer.cpp) + `.h` — add cookie-fetching logic to `search()`:
  1. Check QSettings `tankorent/indexers/1337x/cf_clearance` + TTL. If valid, proceed to HTTP request with `Cookie: cf_clearance=...; User-Agent: <harvested>` headers.
  2. If stale/missing, set `health_ = CloudflareBlocked`, invoke `CloudflareCookieHarvester::instance()->harvest(QUrl("https://1337x.to"), "1337x")`. Queue the search until `cookieHarvested` fires; on fire, retry with fresh cookie.
  3. On HTTP 503 / CF-specific response body during an actual search request (cookie expired server-side before TTL), invalidate cache + re-harvest + retry ONCE. Further 503 → fail the search with `CloudflareBlocked` health.
- `credentialKeys()` override returns `{"cf_clearance"}` as the manual-override path if harvester fails repeatedly. User can paste a manually-fetched cookie via Phase 3's Sources panel.
- During Phase 3 Sources panel's row rebuild, when 1337x row shows "Solving Cloudflare challenge..." (health == `CloudflareBlocked` AND harvester is running), label changes to active-state indicator (spinner or "Harvesting..." text).

**Files:** [src/core/indexers/X1337xIndexer.cpp](src/core/indexers/X1337xIndexer.cpp) + [.h](src/core/indexers/X1337xIndexer.h).

**Success:** fresh install + 1337x query → harvester fires silently → cookie cached → search succeeds. 7-day later → re-harvest silently. Manual cookie paste via Sources panel works as fallback when harvester fails.

### Batch 4.3 — Un-disable 1337x in dispatch

- [TankorentPage.cpp:652-663](src/ui/pages/TankorentPage.cpp) — add 1337x to source combo populate list.
- [TankorentPage.cpp:727-733](src/ui/pages/TankorentPage.cpp) — un-comment the 1337x `addIf` at :729. Remove the `// Cloudflare JS challenge` comment entirely.
- Verify Phase 1's `kMediaTypeIndexers` allowlist already includes 1337x in Videos/Books/Audiobooks/Comics — it does per Batch 1.1 design.

**Files:** [src/ui/pages/TankorentPage.cpp](src/ui/pages/TankorentPage.cpp) only.

**Success:** 1337x appears in Sources picker. Videos/Books/Audiobooks/Comics queries dispatch 1337x. Phase 3 health panel shows 1337x status live.

### Phase 4 exit criteria
- CloudflareCookieHarvester functional.
- 1337x first-class: searches work, auto-re-harvest works, manual fallback works.
- Sources panel shows 1337x health correctly across states (solving / ok / blocked / rate-limited).
- Agent 6 review against audit SEARCH-P0-2 citation chain.

---

## Phase 5 — Downloader add flows expansion (DOWNLOADER-P0-1)

**Why:** Agent 4B validation (chat.md:16951) confirmed BROKEN. Zero `AddTorrentDialog` construction sites outside the one at [TankorentPage.cpp:1175](src/ui/pages/TankorentPage.cpp) (search-result add path). No URL/magnet paste dialog. No `.torrent` file picker exposed to users in Tankorent. No clipboard preload on the user-facing add flow (the :963 clipboard use is outbound "Copy magnet"). No drag-drop on TankorentPage.

Identity #3 calls for URL/magnet dialog + drag-drop + clipboard preload + multi-URL.

### Batch 5.1 — AddFromUrlDialog

- NEW `src/ui/dialogs/AddFromUrlDialog.h` + `.cpp`. `QDialog` with:
  - `QTextEdit` for multi-URL input (one URL per line).
  - Clipboard preload on construction: if `QGuiApplication::clipboard()->text()` starts with `magnet:` or looks like a `.torrent` URL, pre-fill the first line.
  - Validation: per-line, accept `magnet:?...`, `http(s)://*.torrent`, `http(s)://` generic URL. Invalid lines get a subtle inline error.
  - Toggle: "Start each immediately" vs "Queue all (first starts)".
  - Accept button disabled until at least one valid URL.
- On accept: iterate URLs, call `TorrentClient::addMagnet(url)` per entry (existing API at [TorrentClient.cpp](src/core/torrent/TorrentClient.cpp)). For `.torrent` URLs, download the file to temp + invoke add-torrent-file path (TorrentClient likely already has this — verify during implementation).
- Entry points: "Add from URL..." in a new TankorentPage toolbar button + Ctrl+V keybind (Batch 5.3).

**Files:** NEW [src/ui/dialogs/AddFromUrlDialog.h](src/ui/dialogs/AddFromUrlDialog.h) + `.cpp`. [CMakeLists.txt](CMakeLists.txt) — add to SOURCES + HEADERS. [src/ui/pages/TankorentPage.h](src/ui/pages/TankorentPage.h) + [.cpp](src/ui/pages/TankorentPage.cpp) — new toolbar button + slot `onAddFromUrlClicked`.

**Success:** click "Add from URL..." → dialog opens with clipboard pre-fill if applicable. Paste 5 magnets → all 5 add. Invalid URLs flagged before accept. Existing single-URL add via AddTorrentDialog (search flow) unchanged.

### Batch 5.2 — Drag-drop on TankorentPage

- [TankorentPage.cpp](src/ui/pages/TankorentPage.cpp) constructor — `setAcceptDrops(true)`.
- Override `dragEnterEvent`: accept if mime has `text/uri-list` OR `text/plain` matching magnet shape.
- Override `dropEvent`:
  - Partition dropped items into: `.torrent` local files, `.torrent` HTTP URLs, magnet URIs, generic HTTP URLs, plain text matching magnet shape.
  - Single `.torrent` file → open existing AddTorrentDialog preloaded with file path.
  - Multiple items → open AddFromUrlDialog pre-filled with the URL/magnet list (for `.torrent` files, convert local paths to `file://` URLs).
  - Subtle "Added N torrents" toast on drop completion (reuse existing Toast helper in `src/ui/widgets/Toast.*`).

**Files:** [src/ui/pages/TankorentPage.h](src/ui/pages/TankorentPage.h) + [.cpp](src/ui/pages/TankorentPage.cpp).

**Success:** drag a `.torrent` from Explorer onto TankorentPage → AddTorrentDialog opens. Drop 3 `.torrent` files → AddFromUrlDialog opens with all 3. Drop a browser tab (URL) → AddFromUrlDialog pre-filled.

### Batch 5.3 — Clipboard-paste handler + File menu entry

- Override `keyPressEvent` on TankorentPage — intercept `Ctrl+V` if focus is NOT in the search field or another text input. Open AddFromUrlDialog with clipboard pre-filled.
- Check [src/ui/MainWindow.cpp](src/ui/MainWindow.cpp) for an existing menu bar. If present, add "Add Torrent → From URL...\tCtrl+V" entry routing to TankorentPage's slot (menu bar is shared Rule 7 territory — flag in chat if touched).
- If no menu bar exists (app is menu-less per `WIN32_EXECUTABLE TRUE` + no QMenuBar confirmed), skip the menu entry. Ctrl+V keybind inside TankorentPage suffices.

**Files:** [src/ui/pages/TankorentPage.cpp](src/ui/pages/TankorentPage.cpp) + possibly [src/ui/MainWindow.cpp](src/ui/MainWindow.cpp) (Rule 7 heads-up).

**Success:** on TankorentPage with no focus → Ctrl+V → dialog opens with clipboard pre-filled if clipboard has magnet/URL content. Regression: Ctrl+V in the search box still pastes into the search box (event passes through).

### Phase 5 exit criteria
- URL/magnet dialog functional with clipboard preload.
- Drag-drop routes `.torrent` files + URLs correctly.
- Ctrl+V keybind works on TankorentPage.
- Agent 6 review against audit DOWNLOADER-P0-1 citation chain.

---

## Phase 6 — Unified TorrentPropertiesWidget (DOWNLOADER-P0-2 + P0-3 + P0-4 merged)

**Why:** Agent 4B flag #6 + Hemanth AskUserQuestion ratification — three audit P0s (trackers, peers, active files) collapse into one qBittorrent-style tabbed properties widget. Replaces current read-only [TorrentFilesDialog.cpp:1-107](src/ui/dialogs/TorrentFilesDialog.cpp) and adds tracker/peer surfaces that don't exist today.

Agent 4B flag #1: DOWNLOADER-P0-4 is UI-only. Engine-side `TorrentEngine::setFilePriorities` at [TorrentEngine.cpp:485-496](src/core/torrent/TorrentEngine.cpp) already exists and runs on live torrents — Files tab reuses directly, no engine delta needed.

Trackers + Peers tabs need bounded engine API additions (wrap existing libtorrent `torrent_handle` methods).

### Batch 6.1 — TorrentPropertiesWidget shell

- NEW subdirectory `src/ui/pages/tankorent/`.
- NEW `src/ui/pages/tankorent/TorrentPropertiesWidget.h` + `.cpp`. `QDialog` (modal per Agent 4B open design question — start simple, can revisit).
- `QTabWidget` inside: General / Trackers / Peers / Files. Each tab is its own widget class (Batches 6.2-6.5).
- API: `void showTorrent(QString infoHash)`. Stashes infoHash + updates title bar with torrent name from `TorrentEngine::allStatuses()` lookup. 1 Hz `QTimer` drives refresh loop across tabs.
- Double-click on TankorentPage transfers table (existing cell at [:584-620](src/ui/pages/TankorentPage.cpp)) → opens `TorrentPropertiesWidget` for that torrent. Connect via `itemDoubleClicked` signal.
- Existing right-click context menu at [:1388-1528](src/ui/pages/TankorentPage.cpp) gets a new "Properties" entry opening the widget (gives keyboard-accessible path).

**Files:** NEW [src/ui/pages/tankorent/TorrentPropertiesWidget.h](src/ui/pages/tankorent/TorrentPropertiesWidget.h) + `.cpp`. [src/ui/pages/TankorentPage.cpp](src/ui/pages/TankorentPage.cpp) (double-click wiring + context-menu entry). [CMakeLists.txt](CMakeLists.txt) — new directory + source/header additions.

**Success:** double-click a torrent → properties widget opens. 4 empty tabs render. Close via ESC or close button. No crash on rapid double-click. Title bar shows torrent name.

### Batch 6.2 — Files tab (DOWNLOADER-P0-4)

**Isolate-commit:** yes. First engine-path change that edits file priorities on a LIVE running torrent; isolate to smoke on Hemanth's real torrents before Tracker/Peer batches pile on.

- NEW `src/ui/pages/tankorent/TorrentFilesTab.h` + `.cpp`. `QWidget` with `QTreeWidget`:
  - Columns: Name / Size (humanized) / Progress (% + bar) / Priority (combo per-cell: Skip / Low / Normal / High / Maximum).
  - Load from `TorrentEngine::torrentFiles(infoHash)` at [:701-760](src/core/torrent/TorrentEngine.cpp) + extend with priority per file.
  - Priority combo emits `priorityChanged(fileIndex, priority)` → calls `TorrentEngine::setFilePriorities(infoHash, newPriorityArray)`.
  - Context menu per row: Skip / Low / Normal / High / Maximum (shortcut for combo), Open file (via `QDesktopServices::openUrl(QUrl::fromLocalFile(...))`), Open containing folder, Rename.
  - Select-all / Deselect-all buttons below the tree (reusing [AddTorrentDialog.cpp:548-589](src/ui/dialogs/AddTorrentDialog.cpp) pattern) → bulk-apply priority.
- Rename path: modify local-file on disk via `QFile::rename`. Warn-dialog about implications (libtorrent may re-check the file after rename). If libtorrent's rename API is available via `torrent_handle::rename_file()`, prefer that — check at implementation time.
- Live progress updates via 1 Hz refresh: re-fetch `torrentFiles` + update Progress column. Priority column is user-driven, don't overwrite mid-edit.
- Delete [src/ui/dialogs/TorrentFilesDialog.h](src/ui/dialogs/TorrentFilesDialog.h) + `.cpp`. Old context-menu entry "View Files" on transfers table → redirect to `TorrentPropertiesWidget::showTorrent(infoHash)` with Files tab pre-selected.

**Files:** NEW [src/ui/pages/tankorent/TorrentFilesTab.h](src/ui/pages/tankorent/TorrentFilesTab.h) + `.cpp`. [src/ui/pages/tankorent/TorrentPropertiesWidget.cpp](src/ui/pages/tankorent/TorrentPropertiesWidget.cpp) (mount Files tab). [src/ui/pages/TankorentPage.cpp](src/ui/pages/TankorentPage.cpp) (redirect "View Files" context action). [CMakeLists.txt](CMakeLists.txt) (add new files + REMOVE TorrentFilesDialog entries). DELETED: [src/ui/dialogs/TorrentFilesDialog.h](src/ui/dialogs/TorrentFilesDialog.h) + `.cpp`.

**Success:** Files tab populates. Change a file's priority to Skip → download stops on that file (check progress stops incrementing). Set to Maximum → priority rises (check progress picks up). Rename works. Open file via context menu works. Legacy "View Files" menu entry redirects correctly.

### Batch 6.3 — Trackers tab (DOWNLOADER-P0-2)

- NEW `src/ui/pages/tankorent/TorrentTrackersTab.h` + `.cpp`. `QWidget` with `QTableWidget`:
  - Columns: URL / Tier / Status / Next Announce / Min Announce / Peers / Seeds / Leeches / Downloaded / Message.
  - Load from new `TorrentEngine::trackersFor(infoHash) → QList<TrackerInfo>` method. `TrackerInfo` struct at [src/core/torrent/TorrentEngine.h](src/core/torrent/TorrentEngine.h).
- `TrackerInfo` struct fields:
  ```cpp
  struct TrackerInfo {
      QString url;
      int tier;
      QString status;    // "Working", "Not contacted", "Error", "Updating"
      QDateTime nextAnnounce, minAnnounce;
      int peers, seeds, leeches, downloaded;
      QString message;   // tracker error/status message
  };
  ```
- New `TorrentEngine` API (small engine delta, bounded):
  - `QList<TrackerInfo> trackersFor(QString infoHash) const;` — wraps libtorrent `torrent_handle::trackers()`.
  - `void addTracker(QString infoHash, QString url, int tier);` — wraps `torrent_handle::add_tracker(announce_entry)`.
  - `void removeTracker(QString infoHash, QString url);` — reads current, rewrites without URL via `replace_trackers`.
  - `void editTrackerUrl(QString infoHash, QString oldUrl, QString newUrl);` — same `replace_trackers` rebuild.
- Context menu per row: Force reannounce (reuses existing [TorrentEngine::forceReannounce](src/core/torrent/TorrentEngine.cpp)), Copy URL, Edit URL, Remove.
- Add button above the table → opens a small `QInputDialog` for URL + tier. Validates URL-shaped string.
- Tier shown 1-indexed in UI (internal libtorrent is 0-indexed per Agent 4B open design question — convert at boundary).

**Files:** NEW [src/ui/pages/tankorent/TorrentTrackersTab.h](src/ui/pages/tankorent/TorrentTrackersTab.h) + `.cpp`. [src/ui/pages/tankorent/TorrentPropertiesWidget.cpp](src/ui/pages/tankorent/TorrentPropertiesWidget.cpp) (mount). [src/core/torrent/TorrentEngine.h](src/core/torrent/TorrentEngine.h) + [.cpp](src/core/torrent/TorrentEngine.cpp) (new API methods + `TrackerInfo` struct). [CMakeLists.txt](CMakeLists.txt).

**Success:** Trackers tab populates from libtorrent. Add tracker `udp://tracker.openbittorrent.com:80` → appears in list + reannounces. Remove selected → tracker gone. Force reannounce fires immediately.

### Batch 6.4 — Peers tab (DOWNLOADER-P0-3)

- NEW `src/ui/pages/tankorent/TorrentPeersTab.h` + `.cpp`. `QWidget` with `QTableWidget`:
  - Columns: Country / IP:Port / Client / Flags / Connection / Progress / Down Speed / Up Speed / Downloaded / Uploaded / Relevance.
  - Country column: show 2-letter code (A2) from libtorrent's `peer_info::country` field if available. GeoIP DB is out of scope per Agent 4B open design question — ship with "--" placeholder for peers where country isn't resolved; leave hook for a future GeoIP integration.
- New `TorrentEngine` API:
  - `QList<PeerInfo> peersFor(QString infoHash) const;` — wraps `torrent_handle::get_peer_info()`.
  - `void banPeer(QString ipAddr);` — updates session `ip_filter` to exclude IP. Persist ban list to QSettings `tankorent/bannedPeers` (QStringList).
  - `void addPeer(QString infoHash, QString ipPort);` — parses `ip:port`, calls `torrent_handle::connect_peer(tcp::endpoint)`.
- `PeerInfo` struct fields (mirror libtorrent shape):
  ```cpp
  struct PeerInfo {
      QString address, client, country;
      quint16 port;
      QString flags;         // e.g. "uTP", "I" for incoming
      QString connection;    // "µTP" / "TCP"
      float progress;
      qint64 downSpeed, upSpeed, downloaded, uploaded;
      float relevance;
  };
  ```
- Context menu per row: Copy IP:Port, Ban peer permanently, Add peer (opens `QInputDialog` for IP:Port).
- Ban list persists across restart: on TorrentEngine startup, load `tankorent/bannedPeers` and apply to session `ip_filter`.

**Files:** NEW [src/ui/pages/tankorent/TorrentPeersTab.h](src/ui/pages/tankorent/TorrentPeersTab.h) + `.cpp`. [src/ui/pages/tankorent/TorrentPropertiesWidget.cpp](src/ui/pages/tankorent/TorrentPropertiesWidget.cpp) (mount). [src/core/torrent/TorrentEngine.h](src/core/torrent/TorrentEngine.h) + [.cpp](src/core/torrent/TorrentEngine.cpp) (new API + `PeerInfo` + ban list). [CMakeLists.txt](CMakeLists.txt).

**Success:** Peers tab populates with live peers. Ban a peer → peer disconnects, reconnect blocked. Ban persists across app restart. Add peer with `127.0.0.1:6881` (local test) → shows in list if reachable.

### Batch 6.5 — General tab

- NEW `src/ui/pages/tankorent/TorrentGeneralTab.h` + `.cpp`. `QWidget` with read-only `QFormLayout`:
  - Name (from torrent_info), Size (total), Pieces count, Piece size, Created (date), Created by, Comment, InfoHash (monospace for copy), Save Path (with "Open folder" link), Current Tracker (first active), Availability (piece availability from libtorrent), Share Ratio, Reannounce Timer (countdown to next announce).
- All data from existing [TorrentEngine::allStatuses](src/core/torrent/TorrentEngine.cpp) path + libtorrent `torrent_handle::torrent_file()` / `torrent_handle::status()`. Minor engine wrapping: a `TorrentDetails torrentDetails(QString infoHash) const` method that combines status + torrent_info + availability into one struct.
- 1 Hz refresh but most fields are static (Name, Hash, Created) — selective-refresh Name/Hash ONCE then stop updating (Agent 4B open design optimization). Dynamic fields (share ratio, reannounce timer, availability) continue refresh.
- No new engine capability; just convenient wrapping.

**Files:** NEW [src/ui/pages/tankorent/TorrentGeneralTab.h](src/ui/pages/tankorent/TorrentGeneralTab.h) + `.cpp`. [src/ui/pages/tankorent/TorrentPropertiesWidget.cpp](src/ui/pages/tankorent/TorrentPropertiesWidget.cpp) (mount + set as default tab). [src/core/torrent/TorrentEngine.h](src/core/torrent/TorrentEngine.h) + [.cpp](src/core/torrent/TorrentEngine.cpp) (new `TorrentDetails` struct + method). [CMakeLists.txt](CMakeLists.txt).

**Success:** General tab populates with static + live-refreshing fields. Open folder link works. InfoHash is selectable/copyable.

### Phase 6 exit criteria
- Unified TorrentPropertiesWidget functional with all 4 tabs.
- Files tab replaces deleted TorrentFilesDialog. Priority editing on active torrents works.
- Trackers tab add/remove/edit works.
- Peers tab list + ban works, ban persists.
- General tab shows static + live data correctly.
- Agent 6 review against audit DOWNLOADER-P0-2 + P0-3 + P0-4 citation chain.

---

## Phase 7 — Inherited open debt (rootFoldersChanged + downloadProgress)

**Why:** Agent 4B inherited two open-debt items from Agent 4's Congress 4 observer position (chat.md STATUS.md:68):

1. `TorrentClient::torrentCompleted → CoreBridge::rootFoldersChanged` rescan signal wiring — finished downloads into tracked library folders should auto-trigger a library rescan.
2. `TorrentClient::downloadProgress(folderPath) → float` query contract for Agent 5's future VideosPage/ComicsPage list-view "Download" column.

Both pre-parity, non-blocking, but natural fit as closing phase.

### Batch 7.1 — torrentCompleted → rootFoldersChanged wiring

**Isolate-commit:** yes. Cross-subsystem signal path touching CoreBridge (Agent 0 territory per Rule 7). Heads-up in chat.md before the edit.

- Audit [src/core/torrent/TorrentClient.cpp](src/core/torrent/TorrentClient.cpp) for existing `torrentCompleted` signal emission. Agent 4B noted at chat.md:16971 that persistence path wasn't re-read in depth — so start with a fresh read to confirm signal exists.
- If `torrentCompleted(QString infoHash, QString savePath)` exists: hook it. If not: add it (emit in the completion-state handler).
- On emission: check `savePath` against `CoreBridge::rootFolders()` list. If `savePath` is a child of any tracked root, emit `CoreBridge::rootFoldersChanged()` (existence of this signal TBD — add to [src/core/CoreBridge.h](src/core/CoreBridge.h) + [.cpp](src/core/CoreBridge.cpp) if missing).
- Agent 5's scanner flow (per `feedback_agent5_scope`) already listens on `rootFoldersChanged` to re-run `LibraryScanner` / `BooksScanner` / `VideosScanner`. No Agent 5 code change needed — the signal triggers the existing scan.

**Files:** [src/core/torrent/TorrentClient.cpp](src/core/torrent/TorrentClient.cpp) + possibly [.h](src/core/torrent/TorrentClient.h). [src/core/CoreBridge.h](src/core/CoreBridge.h) + [.cpp](src/core/CoreBridge.cpp) if signal doesn't exist. Post heads-up in chat.md before editing CoreBridge.

**Success:** complete a torrent saving to a tracked library folder → within 5s, VideosPage/BooksPage/ComicsPage shows the new files. No rescan triggered for downloads outside tracked roots.

### Batch 7.2 — downloadProgress(folderPath) contract

- Add to [src/core/torrent/TorrentClient.h](src/core/torrent/TorrentClient.h):
  ```cpp
  float downloadProgress(const QString& folderPath) const;
  ```
- Implementation iterates `m_records`, filters records whose `savePath` prefix-matches `folderPath` (case-insensitive on Windows), aggregates progress weighted by torrent size:
  ```cpp
  // Total = sum of sizes; done = sum of (size * progress) per matching record.
  // Return done / total, or 0.0f if total == 0.
  ```
- Returns 0.0 if no matching active torrents. Returns 1.0 if all matches are complete. Returns float `[0, 1]` otherwise.
- Agent 5's future list-view "Download" column call site: will query per-folder progress for in-flight torrents. Not wired by Agent 4B — just make the API available. Agent 5 wires on their own timeline via HELP request.

**Files:** [src/core/torrent/TorrentClient.h](src/core/torrent/TorrentClient.h) + [.cpp](src/core/torrent/TorrentClient.cpp).

**Success:** call `downloadProgress("C:/Users/.../Videos/")` with a partial-downloaded torrent saving there → returns float matching libtorrent progress. Completion state returns 1.0. Folder with no torrents returns 0.0.

### Phase 7 exit criteria
- rootFoldersChanged wiring live.
- downloadProgress API contract available.
- Agent 6 review against Congress 4 observer debt items.

---

## Scope decisions locked in

- **Middle-ground search identity** — compile-time indexers + runtime config UI (enable/disable, credentials, health surfacing) + typed category routing. No Cardigann YAML, no Torznab, no runtime tracker-adding.
- **All 7 indexers first-class** — 1337x Cloudflare solved via QWebEngineView auto-harvest. Manual cookie-paste fallback available via Phase 3's credential UI.
- **Downloader audit P0s only** — add flows expansion + unified tabbed properties widget. RSS automation / bandwidth scheduler / share-action variants / protocol toggles UI explicitly deferred.
- **Internal-only API** — no Torznab/Newznab endpoint.
- **Detail surface = unified tabbed widget** — retires TorrentFilesDialog; properties widget has General / Trackers / Peers / Files tabs. Not three separate dialogs.
- **Modal vs dockable** — start modal for simplicity. Can revisit if Hemanth wants inline inspection.

## Isolate-commit candidates

Per the TODO's Rule 11 section:
- **Batch 2.1** (TorrentResult schema + 7-indexer population) — cross-indexer change; isolate before 2.2 to validate all 7 indexers parse correctly.
- **Batch 4.1** (CloudflareCookieHarvester infrastructure) — first QtWebEngine usage in Tankorent's Sources subsystem; isolate before 4.2 consumption.
- **Batch 6.2** (Files tab with priority editing on live torrents) — first engine-path change editing active torrent state; isolate-smoke on Hemanth's real torrents before Tracker/Peer batches pile on.
- **Batch 7.1** (rootFoldersChanged wiring) — cross-subsystem signal path touching CoreBridge (Agent 0 territory); isolate.

Other batches commit-batch at phase boundaries.

## Existing functions/utilities to reuse (not rebuild)

- [TorrentEngine::setFilePriorities](src/core/torrent/TorrentEngine.cpp):485-496 — already exists, edits live torrents. Phase 6.2 Files tab consumes directly.
- [TorrentEngine::forceReannounce](src/core/torrent/TorrentEngine.cpp) — already exists. Phase 6.3 Trackers tab reuses via "Force reannounce" context menu.
- [TorrentEngine::allStatuses / torrentFiles](src/core/torrent/TorrentEngine.cpp):701-760 — existing data source. Phase 6.2 + 6.5 consume.
- [QWebEngineView + QWebEngineProfile::cookieStore](C:/tools/qt6sdk/6.10.2/msvc2022_64) — Qt module already linked for book reader. Phase 4.1 CloudflareCookieHarvester reuses.
- [AddTorrentDialog file priority QTreeWidget pattern](src/ui/dialogs/AddTorrentDialog.cpp):548-589 — pattern to mirror in Phase 6.2 Files tab with priority-edit context menu.
- [TankorentPage transfers-table context menu](src/ui/pages/TankorentPage.cpp):1388-1528 — existing infrastructure; Phase 6.1 adds "Properties" entry.
- [src/ui/widgets/Toast.*](src/ui/widgets/Toast.cpp) — existing toast helper; Phase 5.2 drop-completion toast reuses.
- [Qt6::WebEngineWidgets link](CMakeLists.txt) — already linked. Verify before Phase 4.

## Review gates

Each phase exits with:
```
READY FOR REVIEW — [Agent 4B, TANKORENT_FIX Phase X]: <title> | Objective: Phase X per TANKORENT_FIX_TODO.md + agents/audits/tankorent_2026-04-16.md. Files: ...
```
Agent 6 reviews against audit + TODO as co-objective.

## Open design questions Agent 4B decides as domain master

- **Allowlist exactness for Phase 1.1.** Default allowlist is sensible but Hemanth may tune. Easy to adjust post-ship.
- **CloudflareCookieHarvester concurrency.** One global instance vs per-indexer. One global is enough for now (only 1337x is CF-gated).
- **TorrentPropertiesWidget modal vs dockable.** Start modal (simpler). Revisit if Hemanth wants inline inspection.
- **General tab refresh cadence.** 1 Hz for everything works but static fields (Name, Hash, Created) could skip refresh. Optional optimization.
- **Tracker tier display 0-indexed vs 1-indexed.** Agent 4B recommends 1-indexed in UI (user-friendly) + 0-indexed internally (libtorrent native).
- **Peer country flags.** Ship with 2-letter code or "--" placeholder. GeoIP DB integration out of this TODO scope.
- **Rename via libtorrent vs filesystem.** Prefer `torrent_handle::rename_file()` if available; fallback to `QFile::rename` with re-check warning.
- **`.torrent` file download flow for Phase 5.1.** When a user pastes `http://...*.torrent` URL, download to temp first then invoke AddTorrentDialog? Or stream to TorrentClient directly? TorrentClient likely already supports URL add — verify.

## What NOT to include (explicit deferrals)

- Cardigann YAML runtime-tracker-adding (out per identity #1).
- Torznab/Newznab API endpoint (out per identity #4).
- RSS automation + automated downloader rules (out per identity #3).
- Bandwidth scheduler with day/time alternative speed limits (out per identity #3).
- Share-action variants (remove / remove-with-content / super-seed) beyond current pause-on-threshold (DOWNLOADER-P1-1 SCOPE-BOUNDED per Agent 4B).
- Protocol toggles UI (DHT/LSD/PEX/NAT-PMP/UPnP/encryption) (DOWNLOADER-P1-3 SCOPE-BOUNDED per Agent 4B).
- Full qBittorrent properties features: pieces-downloaded visualization bar, availability bar, download speed graph, web seeds panel, reannounce-data watcher. Revisit post-ship if Hemanth flags gaps.
- GeoIP DB integration for Peer country flags. Can add as follow-up TODO.
- Tankoyomi (separate audit + TODO when Hemanth requests).

## Rule 6 + Rule 11 application

- **Rule 6:** every batch compiles + smokes on Hemanth's box before `READY TO COMMIT`. Agent 4B does not declare done without build verification.
- **Rule 11:** per-batch `READY TO COMMIT` lines; Agent 0 batches commits at phase boundaries (isolate-commit candidates above ship individually).
- **Single-rebuild-per-batch** per `feedback_one_fix_per_rebuild`.
- **CMakeLists.txt HEADERS discipline** — per the 2026-04-16 Agent 3 CMake incident: every new `.h` with Q_OBJECT must appear in HEADERS section for AUTOMOC. New files in this TODO affected: IndexerStatusPanel.h, CloudflareCookieHarvester.h, AddFromUrlDialog.h, TorrentPropertiesWidget.h, TorrentGeneralTab.h, TorrentTrackersTab.h, TorrentPeersTab.h, TorrentFilesTab.h. Agent 4B's shipping checklist flags this explicitly.

## Verification (end-to-end smoke once all 7 phases ship)

1. Open Tankorent → click Sources button → panel shows all 7 indexers with green/yellow/red health, last-success relative timestamps, response times.
2. "Books" search → YTS and EZTV don't fire. "Videos" search → YTS, EZTV, PirateBay, 1337x, ExtraTorrents fire. "Audiobooks" → no Nyaa/YTS/EZTV/ExtraTorrents (wait, per allowlist ExtraTorrents IS in Audiobooks — verify). "Comics" → Nyaa/PirateBay/1337x fire only.
3. Search returns results from multiple indexers → dedup consolidates same-infoHash entries, no duplicates shown.
4. 1337x first appearance: Sources panel shows 1337x with "Solving Cloudflare challenge..." → transitions to green after harvest → search returns results. QSettings `tankorent/indexers/1337x/cf_clearance` populated.
5. Open Sources panel → disable Nyaa → next "Comics" search → Nyaa doesn't fire. Re-enable Nyaa → next search fires Nyaa again.
6. EZTV row in Sources panel → enter cookie value → save → next EZTV search uses new cookie.
7. Paste a magnet into Tankorent with no focus (or focus outside search box) → Ctrl+V → AddFromUrlDialog opens pre-filled.
8. Drag a `.torrent` from Explorer onto TankorentPage → AddTorrentDialog opens.
9. Drop 3 magnets → AddFromUrlDialog opens with all 3 lines → accept → 3 torrents added.
10. Double-click a running torrent → TorrentPropertiesWidget opens modal. General tab shows name/hash/size/ratio. Live refresh updates ratio + reannounce timer.
11. Switch to Files tab → see files with progress bars and priority combos. Change a file to Skip → libtorrent stops downloading (verify via General tab refreshed download rate).
12. Switch to Trackers tab → see current trackers + status. Click Add → enter `udp://tracker.openbittorrent.com:80` → tracker appears + reannounces.
13. Switch to Peers tab → see live peers. Right-click → Ban → peer disconnects, ban persists across restart.
14. Complete a torrent downloading into a tracked library folder → VideosPage/BooksPage/ComicsPage auto-rescan within 5s → new files appear.
15. Regression: existing search-result add flow still works (AddTorrentDialog from TankorentPage context menu).

## Next steps post-approval

1. Agent 0 posts announcement in chat.md routing Agent 4B to Phase 1 Batch 1.1.
2. Agent 4B executes phased per Rule 6 + Rule 11.
3. Agent 6 gates each phase exit.
4. Agent 0 commits at phase boundaries (isolate-commit exceptions per Rule 11 section).
5. MEMORY.md `Active repo-root fix TODOs` line updated to include `TANKORENT_FIX_TODO.md (Agent 4B, middle-ground-search + audit-P0-downloader, 7 phases ~15-17 batches)`.

---

**End of plan.**
