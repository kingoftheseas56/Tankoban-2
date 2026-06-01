# STREAM_HTTP_PREFER_FIX TODO

**Owner:** Agent 4 (Stream mode primary). Some batches touch addon / sources UI — Agent 4B coordinates for the Sources-tab surface; Agent 0 sweeps commits at phase boundaries.

**Created:** 2026-04-20 by Agent 4 after Hemanth observed same-torrent cold-open at ~2 minutes on 181 peers + 15 MB/s throughput with zero head pieces completing, vs Stremio-on-same-torrent at ~3 seconds. Telemetry confirmed bandwidth is flowing into the torrent but none of it lands in the priority=7 head pieces we request — a long-running libtorrent-scheduler starvation pattern with two previously falsified tweak attempts on record (`request_queue_time=10→3` regressed cold-open 11.5 s → 109 s; cold-open `setSequentialDownload(false)` regressed 11.5 s → 32 s).

**SCOPE CORRECTION 2026-04-20 same-wake:** Initial framing of this TODO claimed HTTP-prefer was the resolution path for that cold-open observation. Hemanth corrected: his comparison was Stremio on BASE Torrentio with no active debrid subscription — i.e. same libtorrent primitives, same swarm, same magnet codepath, still 40× faster. HTTP-prefer cannot explain that differential because HTTP-prefer is an orthogonal codepath from libtorrent. **This TODO is therefore NOT the cold-open fix**; it is a parallel long-term UX track for the case when debrid/HTTP sources ARE present. The actual cold-open bug needs evidence-first libtorrent-scheduler investigation — see the cold-open diagnostic work planned for the next Agent 4 summon. Phase 0.1 (stream-picker ranking) is still correct policy and stays shipped. Phases 1-3 of this TODO retain product value but drop in priority relative to the scheduler investigation.

STREAM_STALL_FIX closed PLAYBACK-stall detection in 2026-04-19 soak but explicitly left cold-open as "separate, pre-existing libtorrent fresh-swarm latency." The HTTP-prefer pivot IS a real product improvement for debrid users (many Stremio users are in that population even if Hemanth isn't currently), and Phase 0.1's ranking is a zero-regression zero-risk tier-1 change.

---

## Decisions (locked by Hemanth 2026-04-20)

- **HTTP-prefer is accepted as a core path.** Strategic pivot was parked in memory (`project_nuvio_reference.md` + `project_stream_path_pivot_pending.md` + `project_stream_stall_fix_closed.md` parked tactic `d` = debrid-first). Hemanth explicit ratification 2026-04-20: "i agree that going for path 2 is right."
- **Architecture: Stremio-style.** Don't rip libtorrent out. When an addon returns `Stream { source: Http / Url }` for a title, Tankoban plays that URL directly via `StreamEngine::streamFile()` (code path already exists at [StreamPlayerController.cpp:55](src/ui/pages/stream/StreamPlayerController.cpp#L55)). When an addon returns `Stream { source: Magnet / InfoHash }`, libtorrent still handles it. What changes is stream **ranking / default selection** + **user configuration of debrid services** so more addons return HTTP.
- **Reference slate:**
  - **Nuvio** (HTTP-only Kotlin Multiplatform reference) — `C:\Users\Suprabha\Downloads\NuvioMobile-cmp-rewrite\` (source) + companion addon repo `tapframe/NuvioStreamsAddon` per `project_nuvio_reference.md`. Establishes that HTTP-only is a viable product class.
  - **Stremio** — `C:\Users\Suprabha\Downloads\Stremio Reference\stremio-core-development\` — the `Stream` type discriminator at `types/resource/stream.rs` + how Stremio's player pipeline routes `Http` / `Url` past the streaming server.
  - **Torrentio** — already installed as a default addon; supports `|providers=realdebrid&apikey=XXX` query params to make streams return HTTP directly against Real-Debrid CDN. Known to work; Stremio desktop users primarily use this path.
- **No libtorrent removal.** Torrents still work for pure-P2P streams (user installs a pure-BT addon, or a debrid-backed addon returns a magnet when the title isn't debrid-cached). The goal is "HTTP when available, BT fallback," not "HTTP exclusively."

## Non-Goals (explicitly out of scope)

- **Removing the libtorrent path or any STREAM_ENGINE_REBUILD work** (P2 StreamPieceWaiter, P5 stall watchdog, prefetch thread — all stay). This TODO is ADDITIVE to the existing stream engine, not a replacement.
- **Re-authoring the addon protocol.** STREAM_PARITY Phase 1 already shipped a full Stremio-compatible addon protocol with the `StreamSource` tagged union (Url/Magnet/YouTube/Http). This TODO uses what's there.
- **Building a custom debrid aggregator.** Stremio does this via the Torrentio `providers=` param. We do the same.
- **Chromecast / casting** — explicit non-goal across every Stream TODO to date, unchanged here.
- **Real-time transcoding** (e.g. transmuxing over HTTP) — this TODO does not touch sidecar decode behavior. Input to sidecar is the same HTTP URL shape it already consumes for the libtorrent-internal `streamFile` output; just a different origin.
- **Phase 2 "native Real-Debrid OAuth client"** — deferred. Phase 1 uses the existing "paste API key into addon URL" pattern (which is what Stremio does too). OAuth is nicer UX but YAGNI until Hemanth explicitly asks. Noted in Phase 4 as a possible future wake.

## Objective (a user can, inside Stream mode)

1. Open a title that has both HTTP-source and BT-source streams — see the HTTP ones ranked first in the stream-picker with a visual "Instant" cue.
2. Pick a default preference: "Always prefer HTTP when available" / "Show BT and HTTP equally" / "Always pick the best-quality stream regardless of source" — stored per-profile.
3. Add a Real-Debrid / Premiumize / AllDebrid / TorBox API key to Tankoban's settings once; Tankoban auto-configures the Torrentio addon URL to include the right `providers=` query param so all future Torrentio responses include debrid-backed HTTP streams.
4. See a clear indicator on the stream picker + buffering overlay that distinguishes HTTP-path streams ("Instant") from BT-path streams ("Peer-to-peer, may buffer on cold-start").
5. On cold-open of a debrid-backed HTTP stream, first byte + first frame < 5 seconds wall-clock in the general case (CDN-bound, not scheduler-bound).

## Per-batch protocol

- Follow Build Rule 6: build clean + smoke before declaring done. `build_check.bat` for compile, `build_and_run.bat` + Windows-MCP for smoke.
- Post `READY TO COMMIT - [Agent 4, STREAM_HTTP_PREFER Batch X.Y]: <msg> | files: ...` at batch close. Agent 0 sweeps at phase boundaries.
- Smoke each batch on at least one real repro: Torrentio with a known-debrid-cached title (e.g. One Piece / Invincible / Sopranos). Compare wall-clock time-to-first-frame against the same title through the existing BT path as A/B.
- Rule 17 cleanup (`scripts/stop-tankoban.ps1`) at end of every smoked wake.
- 12-method StreamEngine API freeze stays — any unavoidable surface change escalates through HELP.md / Congress.

---

## Phase 0 — Stream ranking (HTTP-prefer) — zero config, immediate value

**Scope:** rank HTTP / Url streams above Magnet / InfoHash streams in `StreamDetailView`'s source list + default-select the first HTTP stream when the user double-clicks the title to play. Users whose installed addons ALREADY return HTTP (via Torrentio `providers=realdebrid` or similar pre-configured) see instant playback without any further work. This phase is the smallest piece that captures most of the value.

### Batch 0.1 — Source-kind ranking in stream picker
- [ ] Locate the stream-list population site (likely [StreamDetailView.cpp](src/ui/pages/stream/StreamDetailView.cpp) `populateStreams` / equivalent).
- [ ] Add a `streamSortKey(const Stream& s)` free function in `src/core/stream/addon/` that returns a sort tuple: `(httpRank, qualityRank, addonRank)`. httpRank = 0 for `Http`/`Url`, 1 for `Magnet`/`InfoHash`, 2 for `YouTube`. Quality parse from `Stream::name` (1080p > 720p > 480p, keyword match). Stable sort so addon-order ties are preserved.
- [ ] Wire the sort into the list populate step so HTTP rises to top.
- [ ] "Instant" badge on HTTP stream tiles (right-side pill, monochrome per `feedback_no_color_no_emoji`). "Peer-to-peer" badge on Magnet tiles.

**Smoke:** open a title that Torrentio returns both HTTP and BT streams for (any title that a common debrid provider has cached). Stream picker should list HTTP first with "Instant" badge. Clicking an HTTP stream → cold-open < 5 s. Clicking a Magnet stream → existing BT behavior (slow cold-open per this TODO's motivation, but still functional).

**Exit:** HTTP rises, BT still works, smoke shows first-frame on HTTP within 5 s. Gate to Phase 1.

### Batch 0.2 — Default selection prefers HTTP
- [ ] `StreamDetailView` / `StreamPage` "Play" quick-action (double-click title, Play button, resume via Continue Watching) picks the first HTTP stream if present, else falls back to the first BT stream. Keep the user's explicit pick (via stream picker) overriding the default.
- [ ] Continue Watching "resume" path also prefers HTTP re-selection (if the original stream kind was HTTP but the original URL has expired — common with debrid links that have TTL — it re-queries the addon for a fresh HTTP URL rather than falling back to magnet).

**Smoke:** "resume" a stream 24 h later (or force-expire the URL via addon restart). Should re-resolve to fresh HTTP, not fall back to the magnet silently.

**Exit:** resume path is HTTP-first. Phase 0 closes.

---

## Phase 1 — Debrid key settings UI + Torrentio URL auto-config

**Scope:** let the user paste a Real-Debrid / Premiumize / AllDebrid / TorBox API key once in Sources → Settings → Debrid. Tankoban rewrites the Torrentio addon install URL to include the right `providers=...` + `apikey=...` query params, re-installs Torrentio with the new URL, so all future catalog / stream resolutions from Torrentio include debrid-backed HTTP results. This brings the bulk of Stremio's "3 seconds cold-open" user experience without asking Hemanth or a user to understand addon URL syntax.

### Batch 1.1 — Debrid key storage + Settings UI
- [ ] New: `src/core/stream/addon/DebridKeys.h/.cpp`. Persists to `{AppData}/Tankoban/debrid_keys.json`. Per-provider record: `{ provider: "realdebrid"|"premiumize"|"alldebrid"|"torbox", apiKey: "...", addedAt: iso8601 }`.
- [ ] Sources tab → Settings → "Debrid services" section. Four rows (one per supported provider), each with an Enter-key-press-activated password field + "Verify" button + status indicator (Valid / Invalid / Untested). Verify button calls a minimal REST ping against each provider's user-info endpoint (`/rest/1.0/user` for RD, etc.) to sanity-check the key.
- [ ] Keys stored via Qt's `QSettings` with the standard Tankoban app store path. NO rotation / encryption added this batch — keys are local per-machine, same security posture as the user's browser Stremio instance.
- [ ] Agent 4B coordinates the Sources-tab UI layout (consistent with TANKORENT_FIX Phase 3 settings section).

**Smoke:** add a test key (or a real one if Hemanth has it); click Verify → status goes Valid. Restart Tankoban → key persists.

### Batch 1.2 — Torrentio addon URL auto-config
- [ ] When a debrid key is verified, Tankoban auto-rewrites the installed Torrentio addon descriptor's `transportUrl` to `https://torrentio.strem.fun/providers=realdebrid,premiumize|apikey=XXX|YYY|.../manifest.json` (exact query format per Torrentio docs — verify via `C:\Users\Suprabha\Downloads\Stremio Reference\` if the format is ambiguous). Persists via AddonRegistry.
- [ ] On startup: if a debrid key exists but Torrentio's transportUrl doesn't include the provider key, auto-upgrade.
- [ ] On key removal: auto-rewrite back to the plain `https://torrentio.strem.fun/manifest.json`.
- [ ] Debrid-aware addon list: if the user later installs MediaFusion / Comet / another debrid-aware addon via URL, offer (via one-time notification) to append the key to that addon's URL too if it's in a known-supported list. Out-of-scope this batch — just Torrentio.

**Smoke:** add RD key → open a title previously BT-only on Torrentio → now has HTTP streams at top → click one → cold-open < 5 s.

**Exit:** Torrentio responses include debrid HTTP streams for debrid-cached content. Phase 1 closes.

---

## Phase 2 — UX surfaces: stream-picker polish + buffering copy + first-run nudge

**Scope:** small UI surfaces that make the HTTP-prefer behavior legible to users.

### Batch 2.1 — Buffering overlay differentiation
- [ ] When stream is HTTP source: overlay shows "Connecting..." then "Starting playback..." then first frame.
- [ ] When stream is BT source: overlay shows "Connecting to peers..." then "Downloading first chunk..." with peer count + MB/s tied to the existing telemetry fields.
- [ ] Single Cancel button in both modes — same existing wiring.

### Batch 2.2 — First-run nudge
- [ ] If user has opened Stream mode ≥ 3 times + hit a BT cold-open (detected by `firstPieceMs > 30000`), show a one-time banner in the Sources tab: "Add a debrid key for near-instant streaming." Deep-link to Phase 1.1 settings.
- [ ] Single dismiss; do not re-nag.

### Batch 2.3 — Stream picker quality + provider info
- [ ] Each stream row shows: resolution + HDR flag + audio language + source badge (Instant / Peer-to-peer) + the addon name that provided it.
- [ ] Stremio reference shape — check `C:\Users\Suprabha\Downloads\stremio reference 2\stremio-web-neo-development\src\routes\Player\StreamsMenu\` for the visual pattern.

**Smoke:** fresh install → open Stream mode → BT a torrent → nudge should appear on session 3+. With key added → nudge never reappears.

**Exit:** user-facing surfaces make the "why is this instant" vs "why is this buffering" distinction obvious. Phase 2 closes.

---

## Phase 3 — Fallback + reliability hardening

**Scope:** what happens when HTTP doesn't work — debrid link expired, CDN 404, quota exceeded, addon timeout. Graceful fallback to BT without losing the user's place.

### Batch 3.1 — HTTP stream failure → offer BT alternative
- [ ] If HTTP stream returns 403 / 404 / connection error within the first 10 s of play attempt, pause, surface "This link has expired — try another source?" with the BT alternatives (same episode, same addon) listed inline.
- [ ] One-click selection retargets the player at the fallback without forcing a user re-navigation.
- [ ] Continue Watching updates to the new stream source if fallback is accepted.

### Batch 3.2 — Debrid quota handling
- [ ] Detect provider-specific quota-exceeded error shapes (RD: HTTP 509 + specific body, Premiumize: specific JSON field). On detection, mark the debrid key as "quota exhausted" in settings, fall back to BT, and surface a 1-day persistent notification in Sources.

### Batch 3.3 — Known-not-debrid-cached titles
- [ ] If a torrent was added via Torrentio and the HTTP resolution returns "not cached in RD" (torrent is pending on the debrid side), Torrentio returns just the magnet — the existing StreamEngine path handles this. Add a pre-roll toast: "This torrent isn't cached at your debrid provider yet. Streaming peer-to-peer (may be slow on start)."

**Smoke:** force an expired RD link → Tankoban falls back to BT with the inline picker → user clicks next HTTP candidate → plays.

**Exit:** HTTP failures don't dead-end the user; fallback is one click. Phase 3 closes.

---

## Phase 4 — Deferred / future wakes

These items are noted but not scheduled inside this TODO. Hemanth can summon them individually later.

- **Native Real-Debrid OAuth flow.** Instead of "paste API key," launch a browser window → RD OAuth → token storage. Nicer onboarding but adds web auth plumbing. Deferred unless Phase 1.1's key-paste UX proves friction.
- **Auto-install of popular debrid-resolver addons** (MediaFusion / Comet / etc.) on first-run-with-key, so the user doesn't need to know which addons to install for best coverage. Deferred.
- **Per-title provider preference** (e.g. "prefer Premiumize for this anime, RD for movies"). Low ROI; deferred.
- **Torrent caching persisted to disk** so even BT fallback gets warm-start behavior after first full play. Explicit deferral — `StreamGuard::Drop 5-min ENGINE_TIMEOUT` behavior per Congress 6 Slice A audit is the reference; non-trivial scope; wait for evidence it's needed.

---

## Cross-cutting notes

- **Telemetry additions.** Add `event=stream_source_selected { hash, sourceKind, addon, qualityRank, httpRank }` at the point the user / default-logic picks a stream — lets us measure HTTP-vs-BT selection rates in the wild.
- **Feature flag.** `TANKOBAN_HTTP_PREFER=0` env var disables the Phase 0 ranking change for rollback-without-rebuild. Off by default = HTTP-prefer enabled, the feature. Flag is emergency-off only.
- **Smoke comparisons.** Every batch that affects cold-open time-to-first-frame should record an A/B delta in its ship post: BT path cold-open ms vs HTTP path cold-open ms on the same title / same swarm. Accept that BT path is a known-slow baseline — the metric is "HTTP got us to first frame in X s" where X target is < 5 s on any debrid-cached title.

## Rollback strategy

- Phase 0 revert: `git revert HEAD` on the Batch 0.1 / 0.2 commits. Users' BT behavior is completely unchanged — only the ranking reverts.
- Phase 1 revert: delete `debrid_keys.json`, revert Batch 1.2 commit — Torrentio URL reverts to default, all debrid paths go dark, BT path still works. No data loss beyond the paste-your-key step needing a redo.
- Phase 2 + 3 reverts: cosmetic, independent.
- Full Phase-4 demolition: `git revert` the individual phase-close commits. `STREAM_ENGINE_REBUILD` + `STREAM_STALL_FIX` machinery is untouched; reverting this TODO leaves the BT path exactly as it stands 2026-04-20.
