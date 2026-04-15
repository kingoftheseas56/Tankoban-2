# Audit — Tankorent — 2026-04-16

By Agent 7 (Codex). Reference only.

Target: Agent 4B (Sources — Tankorent + Tankoyomi)

Scope: Tankorent search engine and Tankorent downloader. Out of scope: stream mode, video player, book reader, comic reader, Tankoyomi, libtorrent internals, packaging.

Primary references:
- Jackett at commit `6b4d3c1d3778d43e9170b4c946304ba8cbc43f2c`
- Prowlarr at commit `46ce8e270138e757b14cc1b42b259419a2fac979`
- qBittorrent local copy at `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master`

Verdict: Tankorent is not better than Jackett or Prowlarr at search, and it is not better than qBittorrent at downloading. Tankorent has a useful embedded search-to-add path and a reasonable libtorrent-backed starter surface, but the reference apps are substantially ahead in configurability, metadata normalization, operational health, tracker/peer/file management, persistence semantics, and user-visible controls.

## Observed behavior

### Search engine observations

- Tankorent's indexer interface is intentionally narrow: `TorrentIndexer` exposes `id()`, `displayName()`, `search(query, limit, category)`, `cancel()`, `searchFinished`, and `searchError`, but no status, capabilities, login configuration, category capabilities, request delay, rate limit, health, or auth/cookie contract. Citation: `src/core/TorrentIndexer.h:14-20`.

- Tankorent's normalized result shape is small: title, magnet URI, size, seeders, leechers, source name/key, category id/label. It does not carry info-hash as a canonical field, publish date, files, grabs, details URL, tracker status, external IDs, poster, download factors, upload factors, freeleech flags, language, or media identifiers. Citation: `src/core/TorrentResult.h:8-18`.

- The search UI presents a search-type combo with Videos, Books, Audiobooks, and Comics, but the dispatch path does not read that control. Citations: `src/ui/pages/TankorentPage.cpp:326-333`, `src/ui/pages/TankorentPage.cpp:701-749`.

- The source combo is compile-time fixed in the page and lists All Sources, Nyaa, PirateBay, YTS, EZTV, ExtraTorrents, and Torrents-CSV. It does not expose a runtime add/remove/configure flow. Citation: `src/ui/pages/TankorentPage.cpp:652-663`.

- The source list and the stated subsystem list disagree: a `X1337xIndexer` exists, but the UI dispatch disables 1337x with an inline comment that it is blocked by a Cloudflare JavaScript challenge. Citations: `src/core/indexers/X1337xIndexer.cpp:201-202`, `src/ui/pages/TankorentPage.cpp:729`.

- Search dispatch fans out over all selected hard-coded indexers, passes the same query, limit `80`, and category id to each, and collects asynchronous completions. There is partial-result behavior: each successful indexer result is appended and rendered while other indexers may still be pending. Citations: `src/ui/pages/TankorentPage.cpp:701-749`, `src/ui/pages/TankorentPage.cpp:765-786`.

- Per-indexer failures do not block all results, but the user-visible error surface is generic: "Search Failed" or "Some Sources Failed". There is no table of source status, last error, last success, response time, auth failure, rate-limit state, or disabled reason. Citation: `src/ui/pages/TankorentPage.cpp:787-804`.

- Result deduplication is local to the rendered result set. It deduplicates by extracting `btih:` from the magnet URI, with a fallback to the whole magnet string. There is no filename heuristic, details URL heuristic, canonical result GUID, external ID matching, or source priority tie-break. Citation: `src/ui/pages/TankorentPage.cpp:811-836`.

- Ranking is mostly presentation-side sorting and filtering. The page has seed filters for all results, hide dead, and high seed only, and then caps output to 100 rows. Citations: `src/ui/pages/TankorentPage.cpp:838-865`.

- Normalization is hand-written per indexer. Examples: Nyaa maps RSS fields into title, magnet, size, seeders, leechers, source, category; YTS maps torrent JSON fields and movie genres into the same thin result shape; TorrentsCSV builds a magnet and lacks category enrichment; PirateBay maps source fields and category label. Citations: `src/core/indexers/NyaaIndexer.cpp:182-188`, `src/core/indexers/YtsIndexer.cpp:94-101`, `src/core/indexers/TorrentsCsvIndexer.cpp:69-74`, `src/core/indexers/PirateBayIndexer.cpp:69-76`.

- The indexer implementations reviewed do not show explicit transfer timeouts, request-delay scheduling, runtime throttle state, CAPTCHA handling, or generalized login handling. EZTV sets a raw cookie header, but this is not a general auth model. Citation: `src/core/indexers/EztvIndexer.cpp:65`.

### Downloader observations

- The transfers list exposes core status columns: Name, Size, Progress, Status, Seeds, Peers, Down Speed, Up Speed, ETA, Category, Queue, and Info. Citation: `src/ui/pages/TankorentPage.cpp:584-620`.

- The "More" menu exposes global speed limits, global seeding rules, queue limits, pause all, resume all, and history. Citation: `src/ui/pages/TankorentPage.cpp:392-423`.

- The search-result context menu supports Download, Copy Magnet URI, and Copy Title. The main add path shown in Tankorent starts from a search result magnet, resolves metadata for up to 30 seconds, opens `AddTorrentDialog`, and then starts or discards a draft torrent. Citations: `src/ui/pages/TankorentPage.cpp:1127-1153`, `src/ui/pages/TankorentPage.cpp:1160-1208`.

- The add dialog supports category, destination path, content layout, sequential download, start torrent, and initial file priority selection. Citations: `src/ui/dialogs/AddTorrentDialog.cpp:144-172`, `src/ui/dialogs/AddTorrentDialog.cpp:193-218`, `src/ui/dialogs/AddTorrentDialog.cpp:632-654`.

- Initial file priority controls include Skip, Normal, High, and Maximum. Select All and Deselect All map files to Normal and Skip. Citations: `src/ui/dialogs/AddTorrentDialog.cpp:391-413`, `src/ui/dialogs/AddTorrentDialog.cpp:504-588`.

- The add dialog's status calculation counts selected files but does not accumulate selected bytes before displaying total selected size; it displays the torrent total size. Citation: `src/ui/dialogs/AddTorrentDialog.cpp:602-628`.

- The active torrent file dialog is read-only: it shows Name, Size, and Progress with a Close button. It does not expose priority edits, rename, open, file filtering, or per-file actions while the torrent is active. Citation: `src/ui/dialogs/TorrentFilesDialog.cpp:39-68`.

- The engine starts libtorrent with several hard-coded protocol and session settings: DHT, LSD, NAT-PMP, UPnP, and encryption are enabled in code. There is no observed user-facing protocol toggle surface in Tankorent UI. Citation: `src/core/torrent/TorrentEngine.cpp:169-220`.

- Engine lifecycle includes resume/DHT state handling on start and stop. Magnets can be added, fast-resume entries can be restored, file priorities can be applied, sequential download can be toggled, torrents can be paused/removed, queue positions can move up/down, queue limits can be set, per-torrent/global speed limits can be set, and per/global seeding rules can be stored. Citations: `src/core/torrent/TorrentEngine.cpp:357-388`, `src/core/torrent/TorrentEngine.cpp:393-431`, `src/core/torrent/TorrentEngine.cpp:434-482`, `src/core/torrent/TorrentEngine.cpp:485-509`, `src/core/torrent/TorrentEngine.cpp:563-589`, `src/core/torrent/TorrentEngine.cpp:616-670`.

- Seeding rule enforcement currently pauses torrents when a ratio or seed-time limit is reached. No observed action choice exists for remove, remove with data, super-seeding, or alternate target behavior. Citation: `src/core/torrent/TorrentEngine.cpp:674-699`.

- Torrent status exposes progress, rates, peers, seeds, queue position, and speed limits. Torrent file status exposes file name, size, and progress only. Citations: `src/core/torrent/TorrentEngine.cpp:701-760`.

- `TorrentClient` persists active records in JSON, restores active torrents from fast-resume files, deduplicates active magnets by info-hash, and records completion/error state in memory/history. Citations: `src/core/torrent/TorrentClient.cpp:27-69`, `src/core/torrent/TorrentClient.cpp:91-129`, `src/core/torrent/TorrentClient.cpp:376-410`.

## Reference behavior

### Search references

- Jackett normalizes tracker results into a much richer `ReleaseInfo` model, including title, links, publish date, category, size, files, grabs, description, multiple media IDs, genre/language/subtitle metadata, seeders/peers, poster, info-hash, magnet URI, minimum ratio/seed time, and upload/download factors. Citation: https://github.com/Jackett/Jackett/blob/6b4d3c1d3778d43e9170b4c946304ba8cbc43f2c/src/Jackett.Common/Models/ReleaseInfo.cs#L11

- Jackett exposes Torznab capabilities across raw search and typed TV, movie, music, and book modes, with supported parameters and category metadata. Citation: https://github.com/Jackett/Jackett/blob/6b4d3c1d3778d43e9170b4c946304ba8cbc43f2c/src/Jackett.Common/Models/TorznabCapabilities.cs#L56

- Jackett loads Cardigann YAML definitions and constructs Cardigann indexers from those definitions, separating many tracker details from compiled code. Citation: https://github.com/Jackett/Jackett/blob/6b4d3c1d3778d43e9170b4c946304ba8cbc43f2c/src/Jackett.Common/Services/IndexerManagerService.cs#L170

- Jackett creates per-indexer web clients and separate cookie stores, maintains an aggregate "all" indexer, and supports test/search flows that report result status per indexer in manual search. Citations: https://github.com/Jackett/Jackett/blob/6b4d3c1d3778d43e9170b4c946304ba8cbc43f2c/src/Jackett.Common/Services/IndexerManagerService.cs#L123, https://github.com/Jackett/Jackett/blob/6b4d3c1d3778d43e9170b4c946304ba8cbc43f2c/src/Jackett.Server/Controllers/ResultsController.cs#L190, https://github.com/Jackett/Jackett/blob/6b4d3c1d3778d43e9170b4c946304ba8cbc43f2c/src/Jackett.Server/Controllers/ResultsController.cs#L276

- Jackett's Cardigann path carries request delays, category mappings, default categories, cookie instructions, form/cookie login handling, and CAPTCHA branches. Citations: https://github.com/Jackett/Jackett/blob/6b4d3c1d3778d43e9170b4c946304ba8cbc43f2c/src/Jackett.Common/Indexers/CardigannIndexer.cs#L98, https://github.com/Jackett/Jackett/blob/6b4d3c1d3778d43e9170b4c946304ba8cbc43f2c/src/Jackett.Common/Indexers/CardigannIndexer.cs#L178, https://github.com/Jackett/Jackett/blob/6b4d3c1d3778d43e9170b4c946304ba8cbc43f2c/src/Jackett.Common/Indexers/CardigannIndexer.cs#L202, https://github.com/Jackett/Jackett/blob/6b4d3c1d3778d43e9170b4c946304ba8cbc43f2c/src/Jackett.Common/Indexers/CardigannIndexer.cs#L563

- Jackett's PirateBay definition shows the reference model in practice: definition identity and links, category mappings, search modes, search path, and field extraction rules including info-hash and seeders. Citation: https://github.com/Jackett/Jackett/blob/6b4d3c1d3778d43e9170b4c946304ba8cbc43f2c/src/Jackett.Common/Definitions/thepiratebay.yml#L2

- Prowlarr's `IndexerBase` exposes protocol, priority, redirect behavior, support flags for RSS/search/pagination, capabilities, typed fetch methods, download handling, and cleanup that stamps protocol/indexer priority/flags and deduplicates releases by GUID. Citation: https://github.com/Prowlarr/Prowlarr/blob/46ce8e270138e757b14cc1b42b259419a2fac979/src/NzbDrone.Core/Indexers/IndexerBase.cs#L17

- Prowlarr has per-indexer query/grab limit enforcement and retry-after support. Citation: https://github.com/Prowlarr/Prowlarr/blob/46ce8e270138e757b14cc1b42b259419a2fac979/src/NzbDrone.Core/Indexers/IndexerLimitService.cs#L29

- Prowlarr stores per-indexer status, including disabled-till, failure counts, and cookies/cookie expiration. Citation: https://github.com/Prowlarr/Prowlarr/blob/46ce8e270138e757b14cc1b42b259419a2fac979/src/NzbDrone.Core/Indexers/IndexerStatusService.cs#L13

- Prowlarr's release search dispatches by query type, enriches movie/TV/book/music requests with typed IDs and metadata fields, filters enabled indexers by protocol and supported categories, applies category/age/size filtering, records query events, catches per-indexer errors, and deduplicates by GUID while preferring higher indexer priority. Citation: https://github.com/Prowlarr/Prowlarr/blob/46ce8e270138e757b14cc1b42b259419a2fac979/src/NzbDrone.Core/IndexerSearch/ReleaseSearchService.cs#L40

- Prowlarr tracks query, grab, RSS, auth, failure, and response-time statistics from history. Citations: https://github.com/Prowlarr/Prowlarr/blob/46ce8e270138e757b14cc1b42b259419a2fac979/src/NzbDrone.Core/IndexerStats/IndexerStatisticsService.cs#L25, https://github.com/Prowlarr/Prowlarr/blob/46ce8e270138e757b14cc1b42b259419a2fac979/src/NzbDrone.Core/History/HistoryService.cs#L120

- Prowlarr's Cardigann definition model includes settings, language, request delay, links, login, capabilities, search paths, download fields, category mappings, and CAPTCHA fields. Citation: https://github.com/Prowlarr/Prowlarr/blob/46ce8e270138e757b14cc1b42b259419a2fac979/src/NzbDrone.Core/Indexers/Definitions/Cardigann/CardigannDefinition.cs#L30

- Prowlarr syncs indexers to downstream applications through application services and events, which makes it an indexer manager rather than only a query proxy. Citations: https://github.com/Prowlarr/Prowlarr/blob/46ce8e270138e757b14cc1b42b259419a2fac979/src/NzbDrone.Core/Applications/ApplicationBase.cs#L59, https://github.com/Prowlarr/Prowlarr/blob/46ce8e270138e757b14cc1b42b259419a2fac979/src/NzbDrone.Core/Applications/ApplicationService.cs#L17

- qBittorrent's search plugin manager exposes plugin name/version/URL/state/id, supports drag/drop installation from plugin files or URLs, toggles enabled state, uninstalls plugins, checks for updates, and reports plugin install/update failures. Citations: `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\search\pluginselectdialog.cpp:54`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\search\pluginselectdialog.cpp:101`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\search\pluginselectdialog.cpp:154`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\search\pluginselectdialog.cpp:193`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\search\pluginselectdialog.cpp:346`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\search\pluginselectdialog.cpp:443`.

### Downloader references

- qBittorrent's URL add dialog accepts magnet links and URLs, preloads downloadable clipboard URLs, accepts multiple URLs, deduplicates them, and emits the list for adding. Citations: `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\downloadfromurldialog.cpp:52`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\downloadfromurldialog.cpp:74`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\downloadfromurldialog.cpp:110`.

- qBittorrent's main window wires add-from-URL, global speed limits, alternative speed limits, RSS tab creation, and URL list submission to the torrent manager. Citations: `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\mainwindow.cpp:165`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\mainwindow.cpp:211`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\mainwindow.cpp:704`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\mainwindow.cpp:1498`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\mainwindow.cpp:1653`.

- qBittorrent has automated RSS downloader rules with import/export and add-torrent parameter configuration. Citations: `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\rss\automatedrssdownloader.cpp:64`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\rss\automatedrssdownloader.cpp:122`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\rss\automatedrssdownloader.cpp:292`.

- qBittorrent's add-new-torrent flow includes stop conditions, do-not-delete-torrent behavior, save path and download path, category, tags, queue-top, skip checking, sequential download, first-and-last piece first, content layout, and metadata wait/cancel behavior. Citations: `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\addnewtorrentdialog.cpp:317`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\addnewtorrentdialog.cpp:353`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\addnewtorrentdialog.cpp:449`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\addnewtorrentdialog.cpp:807`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\addnewtorrentdialog.cpp:846`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\addnewtorrentdialog.cpp:918`.

- qBittorrent's active torrent content widget supports file filtering, rename, applying priorities, open/open containing folder, and priority choices Ignore, Normal, High, Maximum, and By shown order. Citations: `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\torrentcontentwidget.cpp:82`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\torrentcontentwidget.cpp:191`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\torrentcontentwidget.cpp:274`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\torrentcontentwidget.cpp:408`.

- qBittorrent's properties view includes file/content behaviors, downloaded pieces and availability bars, tracker list, peer list, reannounce data, and refreshed peer/file status. Citations: `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\properties\propertieswidget.cpp:81`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\properties\propertieswidget.cpp:105`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\properties\propertieswidget.cpp:441`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\properties\propertieswidget.cpp:516`.

- qBittorrent's tracker model tracks URL/announce endpoint, tier, status, message, peers, seeds, leeches, downloaded count, next announce, min announce, and status text such as working, not working, tracker error, unreachable, and not contacted. Citations: `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\trackerlist\trackerlistmodel.cpp:185`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\trackerlist\trackerlistmodel.cpp:225`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\trackerlist\trackerlistmodel.cpp:502`.

- qBittorrent's peer list shows country, IP, port, flags, connection, client, progress, speeds, totals, relevance, and files, and supports add peers, copy IP:port, and permanent peer bans. Citations: `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\properties\peerlistwidget.cpp:113`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\properties\peerlistwidget.cpp:285`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\properties\peerlistwidget.cpp:337`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\gui\properties\peerlistwidget.cpp:406`.

- qBittorrent exposes session-level toggles/settings for DHT, LSD, PeX, queueing, active download/upload/torrent limits, encryption, global and alternative speed limits, and save-resume interval. Citations: `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\base\bittorrent\sessionimpl.cpp:769`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\base\bittorrent\sessionimpl.cpp:784`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\base\bittorrent\sessionimpl.cpp:800`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\base\bittorrent\sessionimpl.cpp:3484`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\base\bittorrent\sessionimpl.cpp:3654`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\base\bittorrent\sessionimpl.cpp:3791`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\base\bittorrent\sessionimpl.cpp:4747`.

- qBittorrent share-limit processing supports ratio, seeding time, inactive seeding time, and actions including remove, remove with content, stop, and super-seeding. Citation: `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\base\bittorrent\sessionimpl.cpp:2377`.

- qBittorrent per-torrent APIs include add/remove/replace trackers, add/remove URL seeds, sequential and first/last download flags, per-torrent share limits, share-limit action, and per-torrent speed limits. Citations: `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\base\bittorrent\torrentimpl.cpp:660`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\base\bittorrent\torrentimpl.cpp:747`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\base\bittorrent\torrentimpl.cpp:1216`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\base\bittorrent\torrentimpl.cpp:2634`, `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\base\bittorrent\torrentimpl.cpp:2688`.

- qBittorrent has a bandwidth scheduler that switches alternative speed limits by configured day/time. Citation: `C:\Users\Suprabha\Downloads\qBittorrent-master\qBittorrent-master\src\base\bittorrent\bandwidthscheduler.cpp:48`.

## Gaps

### Search engine gaps

#### P0

- **P0 — Tankorent is a fixed in-app scraper set, not an indexer manager.** Jackett and Prowlarr both model indexers as configured providers with capabilities, configuration, test flows, and runtime status. Tankorent's indexers are compiled into the page, and no runtime add/remove/configure path is observed. This blocks the Jackett/Prowlarr class of "world-class" search because tracker breakage, auth, cookies, and new indexers cannot be handled as user-visible provider state. Citations: `src/ui/pages/TankorentPage.cpp:652-663`, `src/ui/pages/TankorentPage.cpp:701-749`, Jackett `IndexerManagerService`, Prowlarr `IndexerBase`.

- **P0 — One of the seven stated indexers is not actually searchable from the UI dispatch.** The source list omits 1337x and the dispatch path explicitly disables it because of a Cloudflare JavaScript challenge. Jackett and Prowlarr both carry cookie/login/CAPTCHA/request-delay machinery in the indexer definition pipeline, while Tankorent leaves this as a disabled source. Citations: `src/ui/pages/TankorentPage.cpp:652-663`, `src/ui/pages/TankorentPage.cpp:729`, Jackett `CardigannIndexer`, Prowlarr `CardigannDefinition`.

- **P0 — Category/type routing is not at reference level.** The Tankorent search type selector is not used by dispatch, and category filtering is a single category id passed to all selected sources. Prowlarr dispatches by typed search mode and filters enabled indexers by protocol and supported categories; Jackett exposes typed Torznab capability modes and category mappings. This means Tankorent cannot reliably route movie/anime/book/comic-style searches to source-appropriate providers. Citations: `src/ui/pages/TankorentPage.cpp:326-333`, `src/ui/pages/TankorentPage.cpp:701-749`, Prowlarr `ReleaseSearchService`, Jackett `TorznabCapabilities`.

- **P0 — Indexer health and failure surfacing are below both references.** Tankorent supports partial results, which is good, but it collapses failures into generic status text. Prowlarr stores per-indexer disabled state, failure counts, cookies, limits, and history; Jackett reports manual search status per indexer and supports test flows. Tankorent does not surface enough information for a user to distinguish "no results" from "source broken", "auth expired", "rate-limited", or "Cloudflare-blocked". Citations: `src/ui/pages/TankorentPage.cpp:787-804`, Jackett `ResultsController`, Prowlarr `IndexerStatusService`, Prowlarr `IndexerStatisticsService`.

#### P1

- **P1 — Result normalization is too thin for high-quality aggregation.** Tankorent's result shape lacks canonical info-hash, publish date, files, details URL, grabs, language, media IDs, tracker factors, and flags. Jackett and Prowlarr both normalize richer metadata and use it for category routing, dedupe, client integration, statistics, and downstream matching. Citations: `src/core/TorrentResult.h:8-18`, Jackett `ReleaseInfo`, Prowlarr `IndexerBase`.

- **P1 — Deduplication and ranking are shallow.** Tankorent dedupes by magnet `btih` when present and otherwise by the entire magnet string. It has no GUID/details fallback, no filename/size heuristic, no trusted source priority, no age/size filters, no failure-aware ranking, and no user-configurable ranking policy. Prowlarr deduplicates by GUID and resolves ties with indexer priority after applying category/age/size filtering. Citations: `src/ui/pages/TankorentPage.cpp:811-865`, Prowlarr `ReleaseSearchService`.

- **P1 — Rate limiting and request scheduling are not visible in Tankorent's provider contract.** Jackett and Prowlarr both carry request-delay/limit concepts at the indexer level. Tankorent's interface has no such field and reviewed indexers do not show generalized delay/limit machinery. This increases tracker-ban and flaky-result risk. Citations: `src/core/TorrentIndexer.h:14-20`, Jackett `CardigannIndexer`, Prowlarr `IndexerLimitService`.

- **P1 — Search history, saved search, watchlist, and stats surfaces are missing.** Prowlarr records query/grab/RSS/auth history and derives indexer statistics. Tankorent's reviewed search UI renders transient results only; no durable search history or per-indexer statistics surface was observed. Citations: `src/ui/pages/TankorentPage.cpp:701-865`, Prowlarr `HistoryService`, Prowlarr `IndexerStatisticsService`.

- **P1 — Tankorent is consumer-only and exposes no Torznab/Newznab integration contract.** Jackett's core value is a Torznab proxy surface, and Prowlarr syncs indexers to downstream applications. Tankorent currently appears to integrate search only with its own downloader. This is acceptable for a media app, but it is not competitive with reference indexer managers as an ecosystem component. Citations: Jackett `ResultsController`, Prowlarr `ApplicationService`.

#### P2

- **P2 — qBittorrent-style search plugin lifecycle is absent.** qBittorrent is not as indexer-management-heavy as Jackett/Prowlarr, but it still lets users install, enable/disable, uninstall, and update search plugins from local files or URLs. Tankorent has no comparable plugin lifecycle. Citations: qBittorrent `pluginselectdialog.cpp`, `src/ui/pages/TankorentPage.cpp:652-663`.

- **P2 — UI-level result enrichment is minimal.** Tankorent adds source badges and quality suffixes, but no observed result row carries tracker health, publish age, details link quality, metadata IDs, freeleech flags, or failure-aware provenance. Citations: `src/ui/pages/TankorentPage.cpp:902-915`, Jackett `ReleaseInfo`, Prowlarr `IndexerBase`.

### Downloader gaps

#### P0

- **P0 — Add flows are far narrower than qBittorrent.** Tankorent's visible primary add flow starts from a search result magnet and then opens metadata/file selection. qBittorrent supports URL/magnet entry, multiple URL ingest, clipboard preload, `.torrent`/metadata workflows, RSS auto-add rules, and add-torrent stop conditions. A world-class downloader needs broad ingest; Tankorent currently reads more like a search-result downloader than a general torrent client. Citations: `src/ui/pages/TankorentPage.cpp:1127-1208`, qBittorrent `downloadfromurldialog.cpp`, qBittorrent `automatedrssdownloader.cpp`, qBittorrent `addnewtorrentdialog.cpp`.

- **P0 — Tracker management is effectively absent from Tankorent's user surface.** The Tankorent transfer context has force reannounce, but no observed add/remove/edit tracker UI, tracker tier editing, tracker status list, next/min announce, tracker messages, or tracker peer/seed/leecher details. qBittorrent exposes tracker model/status/tier columns and per-torrent add/remove/replace tracker APIs. Citations: `src/ui/pages/TankorentPage.cpp:1388-1528`, qBittorrent `trackerlistmodel.cpp`, qBittorrent `torrentimpl.cpp:660`.

- **P0 — Peer management is absent from Tankorent's user surface.** Tankorent transfers show seed/peer counts, but no observed peer list, peer client/country/progress/speed table, add-peer flow, copy peer address, or peer ban. qBittorrent has a full peer list and permanent ban action. Citations: `src/ui/pages/TankorentPage.cpp:584-620`, qBittorrent `peerlistwidget.cpp`.

- **P0 — Active file management is read-only.** Tankorent supports initial file priority in the add dialog, but the active torrent file dialog only lists name/size/progress. qBittorrent allows active file priority changes, open/open containing folder, rename, filtering, and priority by shown order. This is a core downloader capability gap because users often adjust file choices after metadata resolves or after download starts. Citations: `src/ui/dialogs/AddTorrentDialog.cpp:504-588`, `src/ui/dialogs/TorrentFilesDialog.cpp:39-68`, qBittorrent `torrentcontentwidget.cpp`.

#### P1

- **P1 — Seeding rules are materially simpler than qBittorrent.** Tankorent can pause when ratio or seed time is reached. qBittorrent supports ratio, seeding time, inactive seeding time, and actions including remove, remove with content, stop, and super-seeding. Tankorent's surface also needs validation for durable persistence of per-torrent/global seeding rules across restart. Citations: `src/core/torrent/TorrentEngine.cpp:658-699`, qBittorrent `sessionimpl.cpp:2377`, qBittorrent `torrentimpl.cpp:2634`.

- **P1 — Speed-limit support lacks qBittorrent's alternative and scheduled modes.** Tankorent exposes global and per-torrent speed limit forwarding. qBittorrent adds alternative global limits and a bandwidth scheduler that switches limits by day/time. Citations: `src/core/torrent/TorrentEngine.cpp:641-655`, qBittorrent `sessionimpl.cpp:3484`, qBittorrent `bandwidthscheduler.cpp:48`.

- **P1 — Protocol toggles are hard-coded rather than user-managed.** Tankorent enables DHT, LSD, NAT-PMP, UPnP, and encryption in engine setup. qBittorrent exposes settings for DHT, LSD, PeX, encryption, and related session behavior. Tankorent users cannot visibly tune privacy/connectivity behavior at client level. Citations: `src/core/torrent/TorrentEngine.cpp:169-220`, qBittorrent `sessionimpl.cpp:769`, qBittorrent `sessionimpl.cpp:3791`.

- **P1 — Queue semantics are basic.** Tankorent supports queue limits and move up/down. qBittorrent supports queueing system controls, max active downloads/uploads/torrents, add-to-top behavior, and richer add-state semantics. Citations: `src/core/torrent/TorrentEngine.cpp:616-638`, qBittorrent `sessionimpl.cpp:4747`, qBittorrent `addnewtorrentdialog.cpp:449`.

- **P1 — Sequential download exists, but first-and-last-first is missing.** Tankorent exposes sequential download on add and in transfer context. qBittorrent separately supports sequential and first/last piece first, which matters for early preview and streaming-adjacent use cases. Citations: `src/ui/dialogs/AddTorrentDialog.cpp:144-172`, `src/ui/pages/TankorentPage.cpp:1388-1528`, qBittorrent `torrentimpl.cpp:1216`, qBittorrent `torrentimpl.cpp:1704`.

- **P1 — Storage workflow is less complete.** Tankorent has destination and content layout choices, but no observed incomplete-vs-complete folder flow, add-dialog stop condition, skip-checking option, torrent-file retention option, or active storage policy surface comparable to qBittorrent's add dialog. Citations: `src/ui/dialogs/AddTorrentDialog.cpp:144-172`, qBittorrent `addnewtorrentdialog.cpp:317`, qBittorrent `addnewtorrentdialog.cpp:353`, qBittorrent `addnewtorrentdialog.cpp:563`.

#### P2

- **P2 — Detailed torrent diagnostics are thin.** Tankorent exposes basic progress/rates/peers/seeds/queue columns. qBittorrent exposes pieces downloaded, availability, tracker status, peer details, web seeds, relevance, client names, and more detailed transfer diagnostics. Citations: `src/core/torrent/TorrentEngine.cpp:701-760`, qBittorrent `propertieswidget.cpp`, qBittorrent `peerlistwidget.cpp`, qBittorrent `trackerlistmodel.cpp`.

- **P2 — RSS automation is not observed in Tankorent.** qBittorrent has RSS tab creation and automated downloader rules. Tankorent has search-to-download integration but no observed feed-driven auto-add path. Citations: `src/ui/pages/TankorentPage.cpp:1127-1208`, qBittorrent `mainwindow.cpp:704`, qBittorrent `automatedrssdownloader.cpp`.

- **P2 — Add-dialog selected-size display appears inaccurate.** The dialog counts selected files but displays total torrent size rather than accumulated selected size. This is smaller than the strategic gaps above, but it directly affects file-selection confidence. Citation: `src/ui/dialogs/AddTorrentDialog.cpp:602-628`.

- **P2 — Search/download integration is a strength, but still single-path.** Clicking a search result opens metadata resolution and file selection before start, which is a good embedded-app behavior. The same integration does not yet appear to cover external magnets, `.torrent` files, URLs, RSS, or drag/drop. Citations: `src/ui/pages/TankorentPage.cpp:1160-1208`, qBittorrent `downloadfromurldialog.cpp`, qBittorrent `automatedrssdownloader.cpp`.

## Hypothesized root causes

### Search engine hypotheses

- Hypothesis — Tankorent's search layer may have grown from a small set of hand-coded public-source scrapers, so the current abstraction stops at "run a query and return rows" instead of "model a provider with configuration, capabilities, health, auth, and limits"; Agent 4B to validate.

- Hypothesis — The search type/category controls may have been added before a capability model existed, leaving the UI ahead of the dispatch contract and causing category routing to degrade into a single optional string passed to every indexer; Agent 4B to validate.

- Hypothesis — 1337x may be disabled because Tankorent currently has no generalized challenge/login/cookie acquisition path, not because that specific source is uniquely impossible to support; Agent 4B to validate.

- Hypothesis — Deduplication and ranking may be limited by the result schema lacking canonical GUID/info-hash/details/date fields, forcing the page to infer identity from magnet URI strings; Agent 4B to validate.

- Hypothesis — Tankorent may intentionally be consumer-only, but that product decision means it should not be judged as a Jackett/Prowlarr-equivalent indexer manager unless an integration API surface is explicitly added to the product ambition; Agent 4B to validate.

### Downloader hypotheses

- Hypothesis — Tankorent's downloader may currently be optimized for "download what search found" rather than full torrent-client parity, which explains the narrow add flows and absence of tracker/peer management surfaces; Agent 4B to validate.

- Hypothesis — Several libtorrent-backed capabilities may already be technically reachable in `TorrentEngine`, but the UI and persistence contracts expose only the subset needed for first-pass transfers; Agent 4B to validate.

- Hypothesis — Active file management may have been deferred because the add dialog already writes initial priorities, leaving no later edit contract between `TorrentFilesDialog`, `TorrentClient`, and `TorrentEngine`; Agent 4B to validate.

- Hypothesis — Seeding rules, speed limits, and protocol settings may be volatile or partially persistent because the engine-level settings were implemented before a durable client-settings model was defined for Tankorent; Agent 4B to validate.

- Hypothesis — Tankorent may be using `category` as a media-root routing concept rather than a torrent-client category/tag system, which limits parity with qBittorrent's category/tag/add-state workflows; Agent 4B to validate.

## Recommended follow-ups

These are validation questions, not fix prescriptions, to resolve before Agent 0 authors a fix TODO:

- Is Tankorent intended to compete with Jackett/Prowlarr as an indexer manager, or only to provide embedded media-app search?
- Which of the seven stated indexers are expected to be first-class and reliable, including 1337x?
- Should search type/category routing be media-domain routing, Torznab-style category routing, or both?
- Should Tankorent expose a Torznab/Newznab-compatible API or remain internal-only?
- Are tracker management, peer management, active file priority editing, first/last piece download, and RSS automation in scope for "world-class" downloader parity?
- Which settings must persist across restart: indexer state, search history, seeding rules, speed limits, queue limits, protocol toggles, and per-torrent overrides?
