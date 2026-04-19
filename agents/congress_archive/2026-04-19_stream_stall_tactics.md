## CONGRESS 7 — STATUS: RATIFIED + ARCHIVED
Opened by: Agent 0 (Coordinator)
Date: 2026-04-19
Convened by: Hemanth (direct call — "I'm just going to call the congress and have each agent look at one file and produce a report")
Archived: 2026-04-19 by Agent 0 (same-session per Rule 13 / auto-close clause)

## Motion

Reference-sweep comparative audits, one codebase per agent, to exhaust Stremio-adjacent escape routes for the Tankoban stream-mode stall before we commit to an implementation tactic (gate-pass sequential toggle / `read_piece` force / nuclear piece-priority reset / debrid-first pivot). Each of 5 agents (1, 2, 3, 4, 4B) audits ONE assigned reference in `C:\Users\Suprabha\Downloads\stremio reference 2\` (Agent 4B's assignment also includes a libtorrent-source re-dive), surfaces any piece-scheduling mechanism / session tuning / architectural pattern that Tankoban has NOT already tried, and posts a position in this Congress. Agent 0 synthesizes after all 5 positions land. Agents 5 and 7 explicitly excluded per Hemanth 2026-04-19.

## Scope

**IN scope:**
- Torrent piece scheduling tricks (priority, deadline, selection, prefetch, window sizing, peer dispatch).
- Session-setting bundles we haven't applied to our libtorrent session.
- Architectural patterns for routing around in-engine stalls (debrid, streaming-server sidecar, external player handoff).
- Seek-handling call sequences that reset libtorrent's internal state.
- Any evidence the reference hit our exact stall pattern and how it resolved.

**OUT of scope:**
- UI/theming/catalog parsing/addon manifest details.
- Auth flows, account management, Trakt sync.
- Subtitle handling, audio track logic (unless it reveals a streaming-server call shape).
- Re-surfacing already-audited material: Stremio Reference (Rust — priorities.rs / handle.rs / stream.rs already exhaustively audited in Congresses 5 + 6), Nuvio top-level finding (skips BT via debrid — confirmed).
- Re-surfacing dead Tankoban hypotheses: `request_queue_time` tuning, global `setSequentialDownload(false)` during playback, deadline-gradient tuning inside 0-500 ms band, re-assert cadence tuning above 5 Hz, Prioritizer output-cap value tuning.

## Pre-Brief

Required reading before positions:

1. **The smoking gun** — libtorrent's [torrent.cpp:11100-11135](C:/tools/libtorrent-source/src/torrent.cpp#L11100) gate in `request_time_critical_pieces`: for a fresh piece (`pi.requested == 0`), the time-critical dispatcher `continue`s past it regardless of our deadline/priority. Our `set_piece_deadline` is silently a no-op until the normal picker seeds a first request on the piece. Under `sequential_download=true`, pieces past the playhead cursor are invisible to the normal picker until the cursor advances to them — hence the 32 s stall.

   **NB (post-synthesis correction):** this pre-brief framing was FALSIFIED by Agent 4B B3 — see §Agent 0 Synthesis "Critical re-framing" below. The `pi.requested == 0` continue lives inside the finished-piece flush fast-path, not the fresh-piece path. Real gate stack is 6 layers deep.

2. **Telemetry captured** — `piece_diag` on stalled piece 5: `in_dl_queue=0, blocks=0, finished=0, writing=0, requested=0, peers_with=149, peers_dl=0, avg_peer_q_ms=163, peer_count=350`. 10+ MB/s bandwidth available. See chat.md:2018-2078 for full context.

3. **Already-tried + failed** (do not re-propose): `request_queue_time=3` (regressed 11.5 s → 109 s), `setSequentialDownload(false)` globally during playback (regressed 11.5 s → 32 s cold-open), deadline gradient tweaks inside 0-500 ms band. See chat.md:1958-2016.

4. **Three implementation tactics on the table** (Agent 4 synthesis, chat.md this-session): (a) gate-pass sequential toggle — flip off after cold-open gate passes; (b) `read_piece` force-pull on stall — bypass time-critical gate by injecting must-download-to-serve-read flag; (c) nuclear piece-priority reset on seek — flixerr-style `setPiecePriority(ALL, 0)` → re-apply file priorities → new deadline window. Each audit should FALSIFY or CONFIRM evidence for at least one of these, or propose a (d) nobody has named yet.

## How This Congress Works

- Agent 0 posts this motion + 5 summon briefs (one per agent) to chat.md in a single turn.
- Agents 1-4 + 4B post positions in parallel — no blocking order. Each agent's position = 250-500 words, structured per that agent's brief template.
- Agent 0 writes synthesis under the Positions section once all 5 positions are in — maps findings → tactics (a/b/c/d), recommends ratification shape.
- Hemanth ratifies with one of: **Execute** (greenlight top tactic for Agent 4 implementation next wake) / **Debrid-first pivot** (commit to Real-Debrid / AllDebrid as primary stream path, P2P as fallback) / **Different direction** (Hemanth's call, new scope).

## Assignments

1. **Agent 1** — `Stremio-Kai-main/` (docs/ + portable_config/ + AIOstreams JSON templates) — architectural patterns for HTTP+P2P / P2P-Only / DEBRID flows.
2. **Agent 2** — `stremio-enhanced-main/` (src/ + examples/) — enhancer hooks, any streaming-server shim or piece-scheduling intercept.
3. **Agent 3** — `flixerr-master/` (libs/ + assets/js/app.jsx + ffmpeg wiring) — webtorrent piece-scheduling internals + seek-handler patterns.
4. **Agent 4** — `stremio-community-v5-webview-windows/deps/ + src/node/` — suspected-vendored streaming-server piece-scheduling code.
5. **Agent 4B** — `stremio-web-neo-development/` (core-web WASM bindings + http_server.js) + deeper `C:\tools\libtorrent-source\` trace on `read_piece` / `pick_time_critical_block` gate bypass paths.

Agent 5 + Agent 7 excluded from this Congress per Hemanth direction 2026-04-19 (Agent 5's NuvioMobile debrid-flow UX assignment dropped; Agent 7 synthesizer role replaced by Agent 0 synthesis).

---

## Positions

### Agent 1 (Comic Reader)

**Verdict (1 sentence):** Stremio-Kai carries **zero** piece-scheduling or streaming-server tuning code (addon-list JSON + a pretty landing page + a player config bundle), but its `portable_config/mpv.conf` is a smoking gun for a **consumer-side read-ahead tactic (f)** that Tankoban's sidecar has not applied, and the 4-template naming convention is the strongest in-reference product-level signal yet that **HTTP is primary + P2P is fallback + debrid is "fast results"** — directly endorses tactic (d).

**Top findings:**

- **(f, new) — `portable_config/mpv.conf:27-37`, verbatim:**
  ```
  # OPTIMIZED FOR STREAMING
  cache=yes
  cache-secs=900
  demuxer-readahead-secs=180
  demuxer-max-back-bytes=150MiB   # Increased for better seeking
  demuxer-max-bytes=300MiB        # Increased for SVP buffering
  stream-buffer-size=64MiB        # Increased for smoother streaming
  demuxer-seekable-cache=yes      # Enable seeking in cached data
  stream-lavf-o=reconnect=1,reconnect_streamed=1,reconnect_delay_max=5
  ```
  Stremio-Kai's PLAYER eats **300 MB forward + 150 MB back + 15 min total cache + 3 min readahead** of decoded container bytes. The player is pulling from Stremio's streaming-server **far** ahead of the playhead continuously — that is what keeps libtorrent's time-critical window saturated from the consumer side. Tankoban's ffmpeg sidecar has no equivalent directive: `demuxer.cpp::probe_file` sets a probesize budget (5 MB Tier-3 per Agent 3 P4) but zero ongoing `demuxer-readahead-secs` equivalent. Under our current shape, any scrub beyond the probe window forces a cold piece request because the sidecar hasn't pre-pulled bytes. **Mirror these six directives into the sidecar's avformat open chain** (or expose them as sidecar-process CLI flags our `SidecarProcess` passes on launch) and: (i) 150 MB of seek-within-cache becomes zero-roundtrip; (ii) piece-dispatch pressure tracks the 300 MB forward-cache read-head rather than the current first-frame-and-stall cadence — libtorrent's `request_time_critical_pieces` always has pieces to chew on because the sidecar is always reading ahead; (iii) Tankoban's existing `StreamHttpServer::waitForPieces` path is the exact surface this pressure flows through — NO new IPC, NO piece-priority API change.
- **(d) — `AIOstreams-TEMPLATE-HTTP_Only.json:5` + `P2P_Only.json:5` + `DEBRID.json:5`, verbatim:**
  ```
  HTTP_Only  : "Main AIO for HTTP priority. To be used with \"P2P Only Template\"."
  P2P_Only   : "To be used as a fallback for the \"HTTP Only Template\"."
  DEBRID     : "Full config with all the addons needed and groups sorted for fast results."
  ```
  This is product-level taxonomy shipped in the config vocabulary itself: **P2P is not the default path — it is the backup to HTTP**. The DEBRID template's `stremthruStore` + `torrentio` chain (`instanceId: "d7e"` + `"4ef"` both `enabled: true`) and the P2P_Only template's dropping of StremThru entirely make the hierarchy explicit: debrid → HTTP → P2P fallback. Strongest in-reference evidence yet that the upstream community treats P2P as tier-3. Aligns with `project_nuvio_reference.md` (HTTP-only by design, zero BT) and `project_stream_path_pivot_pending.md` Path 3.
- **Null-result on piece-scheduling (reporting what is NOT here so Agent 0 doesn't re-dispatch Kai):** grep of `docs/` + `portable_config/` for `piece|deadline|prefetch|peer|swarm|magnet|torrent` returned only subtitle-track scoring hits (`smart_track_selector.conf`, `profile-manager.lua`) and SVP framerate buckets. `docs/` is a 7-file static landing page — zero streaming-server documentation. `portable_config/stremio-settings.ini` is 17 lines of window chrome + `InitialVolume=100`. The 4 AIOstreams JSONs are addon-list-and-order only (~46 KB each — all content is service enabledness + instanceId + `timeout:7500` addon-HTTP timeouts, never a piece parameter). **Stremio-Kai is a player-side distribution; the streaming-server is untouched stock Stremio** (confirming what Agent 2 also found from a different angle — stremio-enhanced fetches stock `server.js v4.20.17`; neither reference patches the streaming-server).

**Tactic mapping:**
- **(a) gate-pass sequential toggle** — SILENT. Kai doesn't configure libtorrent internals.
- **(b) read_piece force-pull on stall** — SILENT. Out of Kai's scope.
- **(c) nuclear piece-priority reset on seek** — SILENT. Out of Kai's scope.
- **(d) debrid-first pivot** — **SUPPORTS STRONGLY.** Kai's template taxonomy treats P2P as a fallback tier and debrid as "fast results." Strongest in-reference product-level evidence to date — matches the Nuvio topline Hemanth already has on file.
- **(e) Agent 2's session-level reload-on-stall** — SILENT (no overlap; orthogonal recovery primitive).
- **(f, new) consumer-side read-ahead pressure in Tankoban's ffmpeg sidecar** — Mirror mpv's 300 MB forward + 150 MB back + 180 s readahead into our demuxer/avformat open chain. Orthogonal to libtorrent scheduler; pulls pieces via the same `StreamHttpServer::waitForPieces` path Tankoban already owns, but does so continuously 3 minutes ahead of playhead instead of reactively per-probe. Converts scrub-within-150 MB to zero-network, and makes `request_time_critical_pieces` perpetually non-empty **without** touching libtorrent internals or any of the 12 frozen methods. Pairs with (d) rather than competing: (f) makes our P2P path less stall-prone on the 1575eafa piece-40/piece-9 shape; (d) reduces how often P2P is the chosen resolver in the first place.

**Recommendation:** Ship (f) as the next tactic before committing to (d)'s debrid pivot. (f) is additive, lands inside one sidecar avformat_open chain edit + at most a few `SidecarProcess` launch flags, costs Tankoban nothing on the HTTP-resolver path either (same directives help HTTP streams), is reversible in one commit, and — if it closes the post-cold-open mid-stream stall by keeping the time-critical queue saturated — then (d)'s debrid pivot becomes UX polish instead of an architecture rewrite.

### Agent 2 (Book Reader)

**Verdict (1 sentence):** Dry hole for cold-open scheduling tactics — stremio-enhanced-main is a theme/plugin/Discord-RPC wrapper with **zero** libtorrent or piece-level surface — but it surfaces ONE architectural signal: the Stremio desktop core exposes exactly one streaming-server control verb to JS land (`Reload`), which is in-reference precedent for a **session-level reload-on-stall** recovery primitive (tactic e).

**Top findings:**
- **`src/utils/StreamingServer.ts:283-331` — `start()` is a pure child-process forker.** It spawns user-downloaded `server.js` with `fork(serverScriptPath, [], { stdio: ..., env: { ...process.env, FFMPEG_BIN, FFPROBE_BIN } })`. No session settings, no tuning flags, no CLI args — the enhancer never passes `request_queue_time`, `max_out_request_queue`, `piece_timeout`, or anything scheduler-adjacent. Just two env vars pointing at ffmpeg/ffprobe binaries. Confirms stremio-enhanced has NO piece-scheduling tuning surface whatsoever.
- **`src/constants/index.ts:115` — `SERVER_JS_URL = "https://dl.strem.io/server/v4.20.17/desktop/server.js"`.** Stock Stremio streaming-server v4.20.17 used as-is, unpatched, downloaded manually by the user. Rules out any vendored/forked streaming-server variant. If we want streaming-server internals, that v4.20.17 binary is the artifact (Agent 4's vendored-deps dig is the route — not this repo).
- **`src/preload/setup/initialization.ts:24-29` — THE ONLY streaming-server control verb the Stremio core exposes to JS land:**
  ```
  export function reloadServer(): void {
      setTimeout(() => {
          Helpers._eval(`core.transport.dispatch({ action: 'StreamingServer', args: { action: 'Reload' } });`);
  ```
  The Stremio core surfaces exactly ONE action on the `StreamingServer` transport: `Reload`. Not `SetPriority`, not `SetBuffer`, not `Prefetch`, not `SetBitrate`. Just "restart the whole server." That's **architecturally significant** — it tells us Stremio-land considers session-level reset the sanctioned recovery primitive. No piece-level hooks exist because Stremio explicitly doesn't expose them.
- **No HTTP interceptor / proxy / request rewriter anywhere.** `externalPlayerInterceptor.ts` only intercepts at the ROUTE level (`location.href.includes('#/player')`) and yanks the pre-built stream URL out of `core.transport.getState("player").stream.content.url` to hand to VLC/mpv. `PlaybackState.ts` is identical shape — polls `core.transport.getState`, never mutates. Falsifies any "inject piece-priority hints at HTTP boundary" hypothesis for this reference.
- **No stall detection / retry / watchdog anywhere in the streaming path.** Grep for `stall|retry|watchdog|recover|timeout` returns only the FFmpeg-binary download retry loop + UI service-check interval. Enhancer entirely trusts the streaming-server. Falsifies any "stall-recovery pattern" we could lift.

**Tactic mapping:**
- **(a) gate-pass sequential toggle:** *silent* — no sequential flag in enhancer layer.
- **(b) `read_piece` force-pull:** *silent* — no libtorrent access from JS at all.
- **(c) nuclear piece-priority reset on seek:** *silent at piece level* — but the `StreamingServer.Reload` verb is the **session-level equivalent** and is Stremio-sanctioned. If piece-priority-reset turns out too invasive, full session reload has reference precedent.
- **(d) debrid-first pivot:** *silent* — no debrid code in enhancer.
- **(e) NEW — "session-level reload on stall":** *supported, as reference precedent.* `handle.pause() → handle.resume()` or full `session.remove_torrent(h) → session.add_torrent(p)` with resume data + cursor preserved = last-resort P5 recovery when `piece_diag` shows stall despite good peer count. Stremio's `Reload` verb is the architectural precedent: "I don't know what's wrong, just restart the torrent session." Cheapest tactic on the menu.

**Recommendation to Agent 0 / Hemanth:** Reference falsifies itself as a source of cold-open scheduling mechanics — do NOT block P2/P3/P4 on further digs here. DO bolt **tactic (e) session-level reload** onto the P5 stall-recovery path regardless of which of (a)(b)(c) wins for cold-open; the Stremio reference sanctions it. Agent 4 lead continues with (a)/(b)/(c) for cold-open; (e) becomes a P5 substrate ask whoever owns P5 (4 or 4B).

### Agent 3 (Video Player)

**Verdict (1 sentence — REVISED post Agent 4B B1 finding):** Flixerr moves the Tankoban needle **via tactic (f) only — the mechanism that keeps WebTorrent prefetching is consumer-side continuous demand (ffmpeg drain / HTTP Range reads), which maps cleanly to Agent 4's (f) sidecar readahead; my initial (e') `read_piece()` forward scout is SOURCE-FALSIFIED by Agent 4B's B1 finding at [libtorrent torrent.cpp:799](C:/tools/libtorrent-source/src/torrent.cpp#L799) (`read_piece` short-circuits with `invalid_piece_index` unless `user_have_piece(piece)` is true — independently verified by me this turn); flixerr also falsifies tactic (c) nuclear-reset-on-seek as reference behaviour.**

**Revision note (append):** initial post argued for (e') as the flixerr-modelled "how (b) lands in our architecture" move. Agent 4B's same-session libtorrent-source deep dive identified that `torrent::read_piece` is a passive "after we have it" primitive, not an active force-download primitive. I independently re-read [torrent.cpp:788-808](C:/tools/libtorrent-source/src/torrent.cpp#L788) — confirmed: the `else if (!user_have_piece(piece)) { ec.assign(errors::invalid_piece_index, ...) }` branch at line 799 IS load-bearing. You cannot `read_piece` a piece you don't already have. That collapses both (b) and my (e') at the source level. Leaving original finding body intact below for provenance, but the tactic mapping + recommendation at the end are the revised version.

**Top findings:**

1. **`libs/` contains ONLY `materialdesignicons/`** — no vendored torrent library, no modified webtorrent, no embedded ffmpeg, no Node helpers. Ffmpeg comes from `@ffmpeg-installer/ffmpeg` (prebuilt binary wrapper at a dep path; no custom tuning). `appx/` is just Windows Store logo PNGs. Flixerr is unmodified-upstream WebTorrent 0.107.17 ([`package.json:128`](C:/Users/Suprabha/Downloads/stremio%20reference%202/flixerr-master/package.json#L128)) — no torrent-layer tricks hiding.

2. **FFmpeg transcode is single-shot + stdio-piped, no seek support.** At [`video-stream.js:77-99`](C:/Users/Suprabha/Downloads/stremio%20reference%202/flixerr-master/assets/js/video-stream.js#L77): `ffmpeg -i pipe:0 -vcodec h264 -preset ultrafast -movflags frag_keyframe+empty_moov -f mp4 pipe:1`. Then at [`video-stream.js:106-110`](C:/Users/Suprabha/Downloads/stremio%20reference%202/flixerr-master/assets/js/video-stream.js#L106): `file.createReadStream() → ffmpeg.stdin; ffmpeg.stdout → res`. A browser `<video>` seek on this path cannot restart ffmpeg — **mkv-seek is fundamentally broken in flixerr's mkv/avi branch**. For mp4 it's `torrent.createServer()` (WebTorrent's native Range server), which handles seek via HTTP Range headers. Key implication: the transcode branch's continuous sequential `createReadStream` drain IS what keeps WebTorrent prefetching forward — consumer demand → WebTorrent piece-fetch, no separate urgency mechanism needed.

3. **Seek handler does NOTHING at the torrent layer.** [`player.jsx:217-222`](C:/Users/Suprabha/Downloads/stremio%20reference%202/flixerr-master/assets/js/player.jsx#L217) `setVideoTime`: `videoElement.current.currentTime = time;` + optional cast-device `.seek(time)`, end of story. No `torrent.critical()` re-call, no `file.deselect()/select()` churn, no priority reset. Slider drag ([`player.jsx:250-258`](C:/Users/Suprabha/Downloads/stremio%20reference%202/flixerr-master/assets/js/player.jsx#L250)) + keyboard arrows ([`player.jsx:239-247`](C:/Users/Suprabha/Downloads/stremio%20reference%202/flixerr-master/assets/js/player.jsx#L239)) both route through `setVideoTime` only. Scrub survives because WebTorrent's HTTP Range handler re-schedules implicitly on the new byte-range request — not because flixerr does any gymnastics.

4. **Cold-open flow is `deselect-all → file.select() → critical(start, end)` with numerically bogus math.** [`app.jsx:1023-1027`](C:/Users/Suprabha/Downloads/stremio%20reference%202/flixerr-master/assets/js/app.jsx#L1023) on `torrent.on("ready")`: bulk `torrent.deselect(0, length-1, false)` + per-file `fileToDeselect.deselect()`, THEN `file.select()` on target, THEN [`app.jsx:822-827`](C:/Users/Suprabha/Downloads/stremio%20reference%202/flixerr-master/assets/js/app.jsx#L822) `fetchFirstPieces` computes `startPiece = torrentLength / fileOffset` (a **ratio**, not an index) and `lastPieceToDownload = endPiece * (5MB / fileSize)` (also wrong shape). Ships that garbage range to `torrent.critical(startPiece, lastPieceToDownload)`. It works anyway because WebTorrent's scheduler tolerates absurd inputs by clamping + falling through to sequential `file.select()` behaviour. Critical-window target `1000*5000 = 5 MB` — within striking distance of our post-P4 gate at 1 MB.

5. **WebTorrent `critical()` semantic (per WebTorrent 0.107 `lib/torrent.js`):** sets per-piece `this._critical[]` range markers. Scheduler's `_updateSelections()` picks pieces inside any critical range first, then rarest-first within `select()`ed files. **NO deadline. NO priority level beyond binary "is critical". NO re-assert timer. NO equivalent of libtorrent's `pi.requested==0` time-critical-skip gate.** Flixerr sets critical once on cold-open and never touches it again — consumer reads drive everything downstream.

**Tactic mapping:**

- **(a) gate-pass sequential toggle:** SUPPORTS WEAKLY — WebTorrent has no time-critical gate at all, and its scheduler mixes sequential + priority without a fresh-piece-skip. Flixerr ships with WebTorrent's default mode and it works. Vindicates the DIRECTION of relaxing the mechanism rather than tuning harder inside it, but tells us nothing concrete about libtorrent's sequential_download flip being safe post-gate-pass (apples/oranges across engines).
- **(b) `read_piece` force-pull on stall:** **SUPPORTS** — this is flixerr's whole pipeline in skeleton. WebTorrent's `createReadStream` IS a demand-driven primitive: the HTTP consumer's Range-read (or ffmpeg's stdin drain) physically pulls bytes, and WebTorrent's scheduler observes the backlog and prioritizes the piece needed to satisfy the read. Libtorrent's `handle.read_piece(idx)` is the semantic equivalent — "external code demands this piece NOW", a signal that does NOT route through `request_time_critical_pieces` and therefore DOES NOT hit the `pi.requested==0` skip gate (`read_piece` transitions blocks to downloading directly via the picker's `mark_as_downloading` path). Flixerr's successful pipeline IS evidence that demand-pull is the right primitive; tactic (b) ports it to our stall path.
- **(c) nuclear piece-priority reset on seek:** **FALSIFIED as reference pattern.** Flixerr explicitly does NOT do this — seek writes `currentTime`, period. Range header hitting WebTorrent's HTTP server triggers implicit re-prioritization. **Caveat:** falsification is load-bearing only for a symmetric engine; libtorrent's stickier `m_time_critical_pieces` + skip gate means the "no-op seek handler" pattern will NOT work for us (we already proved `setSequentialDownload + deadline ≠ enough`). So (c) may still be correct for US, but flixerr is NOT evidence for it — should at minimum discourage adopting (c) as the FIRST tactic.
- **(d) debrid-first pivot:** SILENT — flixerr is pure P2P WebTorrent, 2020-era, no debrid integration. (Agent 1's Stremio-Kai + Nuvio reference carry the debrid signal; flixerr neither corroborates nor contradicts.)
- **(e) session-level reload-on-stall (Agent 2's proposal):** SILENT — flixerr has no stall-recovery layer at all; no watchdog, no `Reload` verb, no retry. Orthogonal to the cold-open scheduling question, and flixerr doesn't comment on it.
- **(e', NEW — demand-pull prefetch via `read_piece()` scout):** **PROPOSED.** Tankoban's `StreamHttpServer` currently waits for consumer (ffmpeg/sidecar) to request byte ranges, then `StreamPieceWaiter::awaitRange` passively alert-waits. A flixerr-modelled alternative: spawn a **forward-read scout inside StreamHttpServer** that actively issues `handle.read_piece(idx)` for pieces N..N+k just past the current consumer playhead — NOT a deadline hint, but a hard "read this piece" call. Libtorrent's `read_piece` path triggers `piece_picker::mark_as_downloading` on every block, bypassing the `pi.requested==0` gate that `request_time_critical_pieces` skips at [torrent.cpp:11100-11135](C:/tools/libtorrent-source/src/torrent.cpp#L11100). This is the concrete "how (b) lands in our architecture" answer: not an ambient deadline with re-assert cadence, but a synchronous `read_piece()` call driven by the HTTP server's own read-ahead pump.
- **(f) consumer-side readahead pressure in sidecar (Agent 4's proposal) vs (e') engine-side forward scout:** COMPLEMENTARY, not competing. (f) makes ffmpeg drain bytes continuously 3 minutes ahead of playhead, which naturally keeps `StreamHttpServer::waitForPieces` demand non-empty — this is EXACTLY the signal flixerr's transcode pipeline generates via `createReadStream`'s continuous drain. (e') is the engine-side belt-and-suspenders: even if sidecar's readahead rate temporarily stalls, `read_piece()` scout keeps libtorrent's piece-picker in "external code wants this" state. Suggest both, with (f) shipping first (simpler avformat edit + pure sidecar-side change, doesn't touch frozen Engine API) and (e') only if (f) alone doesn't close the post-cold-open mid-stream stall on the 1575eafa piece-40/piece-9 shape.

**Secondary observation — we may be over-engineering urgency:** our current stack (5-piece staircase + priority=7 + re-assert at 5 Hz + deadline gradient 0-40 ms) is dramatically more mechanism than flixerr's "one `critical()` call, bogus math, never re-touched". Flixerr ships and works. Not proof we should simplify, but signal we may be *adding* entropy to the scheduler rather than steering it. If (f) + (e') land clean, consider trimming the re-assert + staircase back to a single priority-7 + single deadline per piece.

**Recommendation to Agent 0 / Hemanth (REVISED post 4B B1 source-verification):** Ship Agent 4's **(f) consumer-side readahead pressure in the ffmpeg sidecar FIRST** — reversible, additive, doesn't touch frozen Engine API, and is flixerr's actual mechanism re-expressed at our sidecar boundary. **DROP (e') — source-falsified by Agent 4B's B1 finding, independently re-verified by me at [torrent.cpp:799](C:/tools/libtorrent-source/src/torrent.cpp#L799) this turn: `read_piece` early-returns with `invalid_piece_index` when `!user_have_piece(piece)`, so a forward scout can't force-pull pieces we don't yet have.** Same gate kills **(b) read_piece force-pull on stall**. If (f) alone doesn't close the real-swarm stall, next moves are Agent 4B's **(a) gate-pass sequential toggle** (Agent 4B's B3 source-rewrite has it SUPPORTS STRONGLY — sequential=false returns pieces past cursor to `pick_pieces` running on every peer-event, seeding `pi.requested>0` for subsequent time-critical escalation) and Agent 4B's **(e) session-settings bundle** (`max_out_request_queue` 1500-2000 + `whole_pieces_threshold` verify) — cheap orthogonals with independent rollback. DO NOT ship (c) first; flixerr doesn't do it, and Agent 4B flags it as destructive of mid-flight requests + `pi.writing` progress.

### Agent 4 (Stream)

**Verdict (1 sentence):** NO — architecturally hollow for Tankoban's stall; no vendored streaming-server, the native shell is a pure subprocess launcher, and every piece-scheduling answer this Congress needs lives in the externally-installed `stremio-runtime.exe` + `server.js` pair that this repo does NOT carry.

**Top findings:**

- **`deps/` has no vendored server.** `deps/libmpv/` is the only subfolder and it is empty (`ls -la` returns only `.` and `..`). No `stremio-runtime/`, `stremio-server/`, `streaming-server-torrent-client/`, or `server.js` anywhere. The pre-brief hypothesis ("streaming-server potentially vendored in deps/") is FALSIFIED.
- **`src/node/server.cpp` is a launcher, not a server.** [server.cpp:26-128](file:///C:/Users/Suprabha/Downloads/stremio%20reference%202/stremio-community-v5-webview-windows/src/node/server.cpp#L26) `CreateProcessW`s `cmdLine = L"\"stremio-runtime.exe\" \"server.js\""` against binaries resolved from exe dir or `%LOCALAPPDATA%\Programs\StremioService\` (lines 28-52). Anonymous stdin/stdout pipes (69-82). `NodeOutputThreadProc` (13-24) does `std::cout << "[node] " << buf` — pure log pass-through, no IPC parsing, no piece-level hooks. Only outbound message to frontend: `j["type"] = "ServerStarted"` (121-125). `g_nodeInPipe` stored but never written anywhere in `src/` — grep confirms no `WriteFile` call on it.
- **Runtime is out-of-tree.** [main.cpp:71-73](file:///C:/Users/Suprabha/Downloads/stremio%20reference%202/stremio-community-v5-webview-windows/src/main.cpp#L71) duplicate-check treats `"stremio-runtime.exe"` as a sibling process. No CMake target, no `package.json`, no `node_modules/`, no `binding.gyp`, no libtorrent-node artifact anywhere. The shell is a webview+mpv+subprocess orchestrator; the piece-scheduler is a separately-installed binary this project merely launches.
- **Torrent-file drop is a JSON forwarder.** [main.cpp:480-524](file:///C:/Users/Suprabha/Downloads/stremio%20reference%202/stremio-community-v5-webview-windows/src/main.cpp#L480) wraps `.torrent` drops into `j["type"] = "OpenTorrent"` + `SendToJS`. No libtorrent touch from C++ — JS frontend + external streaming-server do it all over HTTP-on-localhost.
- **Session-init sequence unanswerable here.** The summon brief's deepest question (`alert_mask` / `settings_pack` / `set_piece_deadline` + `set_piece_priority` call-order) is answered only in Stremio's Rust `priorities.rs` / `handle.rs` — already exhausted in C5 + C6. Corroborates Agent 2's stremio-enhanced-main finding: stock `server.js` v4.20.17 downloaded unmodified. Both Stremio distributions share the same out-of-tree binary.

**Tactic mapping:**
- (a) **gate-pass sequential toggle:** silent — no scheduler code in reach.
- (b) **`read_piece` force-pull:** silent — same reason.
- (c) **nuclear piece-priority reset on seek:** silent — same reason.
- (d) **debrid-first pivot:** weakly SUPPORTS — Stremio's flagship Windows shell decouples piece-scheduling into a separately-deployed binary it doesn't own the source of; shell-vs-engine split is reference precedent that routing stream sourcing out of our in-process libtorrent (debrid or external sidecar) is legitimate architecture, not a compromise.
- (e) **session-level reload on stall (Agent 2's new tactic):** weakly SUPPORTS — session-level is the only streaming-server control verb exposed across both Stremio codebases audited this Congress.

**Recommendation to Agent 0 / Hemanth:** Discount this reference — false-lead audit-surface, no scheduler code. Weight synthesis on Agent 4B's libtorrent-source gate-bypass deep dive + Agent 3's flixerr webtorrent `critical()` semantic for (b) read_piece evidence. (d) stays on the table as realistic non-P2P fallback; (e) bolts onto P5 stall-recovery cleanly. Cold-open ranking: (b) read_piece force-pull first — directly targets the `pi.requested == 0` gate at torrent.cpp:11100-11135 — then (c) as scrub-only mitigation, then (a) if (b)/(c) alone don't close it.

**Gate-bypass evidence:** NONE in this tree. No `read_piece`, no `pi.requested`, no piece-state transitions — native shell never calls libtorrent. The architectural difference that dodges our stall: Stremio's shell doesn't hit the time-critical gate at all because it doesn't own the scheduler; the out-of-tree streaming-server does. Agent 4B's libtorrent-source deep dive is the load-bearing gate-bypass audit for this Congress; this one is not.

### Agent 4B (Sources)

**Verdict (1 sentence):** Part A is a dead end (stremio-web-neo is observational-only, `http_server.js` is a dev static server); Part B reshapes the smoking gun — the real gate stack is deeper than the pre-brief narrative, **falsifies tactic (b) at the source level** and **falsifies Agent 3's new (e') `read_piece()` forward scout by the same hard gate** (cross-cuts flagged for Agent 0 synthesis), **strongly supports (a)**, and proposes a session-settings bundle (e) as cheap parallel experiment.

---

**Part A — stremio-web-neo (~150 words).**

- [`http_server.js`](../../../../Users/Suprabha/Downloads/stremio%20reference%202/stremio-web-neo-development/http_server.js):1-23 — entire file is `express().use(express.static(build_path))` on port 8080 with cache-control headers. No streaming-server proxy, no HTTP interceptors, no piece-scheduling surface. **FALSIFIED** as any route to a Stremio tuning surface.
- [`src/services/Core/CoreTransport.js`](../../../../Users/Suprabha/Downloads/stremio%20reference%202/stremio-web-neo-development/src/services/Core/CoreTransport.js):46-54 — JS↔WASM bridge exposes exactly four call shapes: `dispatch(action, field, location.hash)`, `getState(field)`, `getDebugState()`, `decodeStream(stream)`. **No byte-offset, no piece-priority, no seek-with-hint crosses the boundary** — the Rust core holds all torrent-adjacent state internally. Confirms prior Explore finding. Silent on all tactics.

**Part B — libtorrent deep dive (~400 words).**

**B1 — `read_piece` hard gate (SMOKING FALSIFICATION of tactics b + e'):** `torrent::read_piece` at [torrent.cpp:788-808](../../../../tools/libtorrent-source/src/torrent.cpp#L788) short-circuits with `errors::invalid_piece_index` unless `user_have_piece(piece)` is true (line 799), then dispatches disk-read jobs only for blocks we already hold (lines 841-852). Verbatim:

```cpp
else if (!user_have_piece(piece))
{
    ec.assign(errors::invalid_piece_index, libtorrent_category());
}
if (ec)
{
    m_ses.alerts().emplace_alert<read_piece_alert>(get_handle(), piece, ec);
    return;
}
```

**It cannot force-pull, prefetch, or "scout" a piece we don't have — it returns an error alert synchronously with zero side effect on piece-picker state.** `set_piece_deadline`'s `alert_when_available` only invokes `read_piece` *after* the piece already completes ([torrent.cpp:5242-5246](../../../../tools/libtorrent-source/src/torrent.cpp#L5242): `if (is_seed() || (has_picker() && m_picker->have_piece(piece)))`). This falsifies:
- Agent 4's (b) **`read_piece` force-pull** (direct)
- Agent 3's (e') **`read_piece()` forward scout** (same gate — a scout on an un-downloaded piece is a synchronous error, not a signal to the picker). Escalating both cross-cuts to Agent 0.

**B2 — `set_file_priorities` side effects:** `update_piece_priorities` → `prioritize_pieces` → picker `set_piece_priority` per piece ([torrent.cpp:5803-5812](../../../../tools/libtorrent-source/src/torrent.cpp#L5803)). Updates `piece_pos.priority` only; **no transition to `piece_downloading` state**, no block-level requests seeded. Piece stays `piece_open` until the normal picker's `pick_pieces` fires. Silent on all tactics.

**B3 — the gate stack (sharper than pre-brief):** The `pi.requested == 0` continue at [torrent.cpp:11121](../../../../tools/libtorrent-source/src/torrent.cpp#L11121) is *inside* `if (free_to_request == 0)` — a finished-piece flush fast-path, NOT the fresh-piece gate. For piece 5 (`blocks=0/finished=0/writing=0/requested=0`), `free_to_request == blocks_in_piece` → control falls through to `pick_time_critical_block`. The REAL gate stack:

1. **1-Hz cadence** — `request_time_critical_pieces` fires only from `second_tick` ([torrent.cpp:10349](../../../../tools/libtorrent-source/src/torrent.cpp#L10349)). ONE pass per second for the entire time-critical list.
2. **Bottom-10% peer cull** ([torrent.cpp:11032-11036](../../../../tools/libtorrent-source/src/torrent.cpp#L11032)) — ~15 of our 149 peers-with dropped by `download_queue_time`.
3. **Deadline horizon** ([torrent.cpp:11074-11075](../../../../tools/libtorrent-source/src/torrent.cpp#L11074)) — non-first pieces skipped if `deadline > now + avg_piece_time + dev*4 + 1000ms`.
4. **Per-peer saturation** — `can_request_time_critical` ([peer_connection.cpp:3543-3558](../../../../tools/libtorrent-source/src/peer_connection.cpp#L3543)) returns false when `download_queue + request_queue > desired_queue_size * 2`. `avg_peer_q_ms=163` on our data means peers are mid-saturation.
5. **2 s hard ceiling** ([torrent.cpp:10832](../../../../tools/libtorrent-source/src/torrent.cpp#L10832)) — `if (peers[0]->download_queue_time() > 2000ms) break`. Un-tunable.
6. **`add_blocks` state check** ([piece_picker.cpp:2653-2656](../../../../tools/libtorrent-source/src/piece_picker.cpp#L2653)) — returns empty unless state is `piece_open` or `piece_downloading`. Fresh piece IS `piece_open` → passes.

**No settings tunable materially weakens this stack.** `strict_end_game_mode=false` only affects double-requesting of in-flight blocks. `piece_timeout`/`request_timeout` govern re-request cadence, not first-request dispatch. 1-Hz tick + 2 s ceiling are source-level constants.

**B4 — `force_recheck`: nuclear, non-viable.** [torrent.cpp:2380-2434](../../../../tools/libtorrent-source/src/torrent.cpp#L2380): `disconnect_all` + `stop_announcing` + `m_picker->resize(...)` (destroys ALL `downloading_piece` state incl. `pi.requested`) + `m_file_progress.clear()` + re-check all files. **Session wipe, not a scrub primitive.** Cannot be used mid-playback.

**Tactic mapping:**

- **(a) gate-pass sequential toggle:** **SUPPORTS STRONGLY.** Flipping `sequential=false` returns pieces past cursor to the normal picker's reach; `pick_pieces` runs on every peer-event AND every tick (not 1-Hz-gated like time-critical), calling `add_blocks` on prio-ranked pieces. Combined with our prioritizer's 2k-piece window at priority=7, fresh pieces 5+ get block-requests seeded within milliseconds — THEN the time-critical escalation has `pi.requested > 0` to work with.
- **(b) read_piece force-pull:** **FALSIFIED** at [torrent.cpp:799](../../../../tools/libtorrent-source/src/torrent.cpp#L799). Drop from table.
- **(c) nuclear piece-priority reset on seek:** **SUPPORTS CONDITIONALLY.** `setPiecePriority(ALL, 0)` → picker evicts `downloading_piece` entries → re-apply → `avg_piece_time` heuristic recomputes. Escape hatch — destroys mid-flight requests + loses `pi.writing` progress. Last-resort scrub.
- **(d) debrid-first pivot:** **SILENT** from my deep-dive scope; deferred to product-level Agents 1/2/3 framing.
- **(e) NEW — session-settings flattening bundle:** raise `max_out_request_queue` to ~1500-2000 (unclog saturated peers so `can_request_time_critical` passes); verify `whole_pieces_threshold` isn't forcing whole-piece mode. Cheap, orthogonal to (a), independent rollback.
- **Agent 3's (f) consumer-side readahead pressure in sidecar:** SUPPORTS — keeps `StreamHttpServer::waitForPieces` demand non-empty, which feeds the normal picker via the same path tactic (a) opens. (f) + (a) compound.
- **Agent 3's (e') `read_piece()` forward scout:** **FALSIFIED by same B1 hard gate as (b).** Drop.

**Recommendation to Agent 0 / Hemanth:** ratify the **(a) gate-pass sequential toggle + Agent 3's (f) sidecar readahead pressure** combination as primary implementation — they compound at the same picker-dispatch surface and neither touches the frozen Engine API in a breaking way. **Drop (b) and (e') from the table** — both source-falsified at [torrent.cpp:799](../../../../tools/libtorrent-source/src/torrent.cpp#L799). Hold **(c)** as documented last-resort scrub. Run **(e) session-bundle tuning** in parallel as cheap orthogonal experiment with independent rollback.

---

## Agent 0 Synthesis

5 positions landed. Strong cross-agent convergence + two source-level falsifications that reshape the menu. Below is the ranked call, with a necessary re-framing of the pre-brief first.

### Critical re-framing (pre-brief correction)

The pre-brief's "smoking gun" at [torrent.cpp:11100-11135](C:/tools/libtorrent-source/src/torrent.cpp#L11100) was the **wrong gate**. Agent 4B's Part B deep dive (B3) source-verified that the `pi.requested == 0` continue lives *inside* `if (free_to_request == 0)` — the finished-piece flush fast-path, NOT the fresh-piece path. For piece 5 (all-zero diag), `free_to_request == blocks_in_piece` and control *falls through* to `pick_time_critical_block`. The real dispatch failure is a **6-layer gate stack**: 1-Hz `second_tick` cadence → bottom-10% peer cull → deadline horizon → `can_request_time_critical` per-peer saturation → 2 s hard ceiling ([torrent.cpp:10832](C:/tools/libtorrent-source/src/torrent.cpp#L10832)) → `add_blocks` state check. `avg_peer_q_ms=163` in our piece_diag now reads as "peers mid-saturation" (gate 4), not as "queues are short." Prior-wake's deadline/priority retuning never touched any of those gates — it couldn't have worked.

### Unanimous source-level falsifications (DROP)

1. **(b) `read_piece` force-pull on stall** — Agent 4B B1 + independent Agent 3 re-verification: [torrent.cpp:799](C:/tools/libtorrent-source/src/torrent.cpp#L799) `else if (!user_have_piece(piece)) ec.assign(errors::invalid_piece_index, ...)`. You cannot `read_piece` a piece you don't yet have; the call is a synchronous error alert with zero picker side-effect. **Drop from the table.**
2. **(e') `read_piece` forward scout** (Agent 3's initial proposal) — same [torrent.cpp:799](C:/tools/libtorrent-source/src/torrent.cpp#L799) gate. Agent 3 revised their own position mid-turn and dropped it. **Drop.**

### Strong convergence (SHIP)

**(f) consumer-side readahead pressure in the ffmpeg sidecar** — proposed by Agent 1 (mirror Stremio-Kai's `mpv.conf`: 300 MB forward + 150 MB back + 180 s readahead + 64 MB stream-buffer), independently corroborated as the actual mechanism behind flixerr's working pipeline by Agent 3 (WebTorrent's continuous `createReadStream` drain == flixerr's whole cold-open strategy), and re-endorsed by Agent 4B (keeps `StreamHttpServer::waitForPieces` demand non-empty, which feeds the non-time-critical `pick_pieces` path). **Additive, reversible, touches only the sidecar avformat_open chain + `SidecarProcess` launch flags, zero impact on the 12-method frozen Engine API.**

**(a) gate-pass sequential toggle** — Agent 4B B3 source-verified: `sequential_download=false` returns pieces past the playhead cursor to the normal `pick_pieces` path, which runs on every peer-event rather than the 1-Hz `second_tick` time-critical tick. This seeds `pi.requested>0` on fresh pieces within milliseconds; subsequent time-critical escalation then has something to operate on. The earlier "global sequential off" test that regressed cold-open 11.5→32 s conflated two regimes — gating the flip on the cold-open gate-pass (GatePct crossing 100 for the first time per session) keeps sequential during head delivery where it helps, flips it off for playback where the normal picker needs access to pieces past cursor. Agent 3 weakly supports; Agent 4B strongly supports.

**(e-settings) session-settings flattening bundle** (Agent 4B) — raise `max_out_request_queue` to ~1500-2000 (unclog the per-peer `can_request_time_critical` gate at [peer_connection.cpp:3543-3558](C:/tools/libtorrent-source/src/peer_connection.cpp#L3543)); verify `whole_pieces_threshold` isn't forcing whole-piece mode. Cheap, orthogonal to (a)/(f), independent rollback.

### Moderate support (PARK for P5 / cold-open fallback)

**(e-reload) session-level reload on stall** (Agent 2) — Stremio's core exposes exactly ONE streaming-server control verb to JS: `StreamingServer.Reload`. Reference precedent for "when scheduling is wedged, reset the session." Bolt onto P5 stall-recovery path (`handle.pause() → handle.resume()` or `session.remove_torrent() → add_torrent()` with resume data + cursor preserved) as last-resort recovery regardless of cold-open choice. Not a primary tactic.

**(c) nuclear piece-priority reset on seek** — Agent 3 source-falsifies as a REFERENCE pattern (flixerr explicitly doesn't do this; seek handler only writes `currentTime`); Agent 4B flags destructive of mid-flight requests + `pi.writing` progress. **Hold as last-resort scrub-only primitive;** do not ship first.

### Product-level (open question for Hemanth)

**(d) debrid-first pivot** — Agent 1 supplies the strongest in-reference product evidence to date: Stremio-Kai's 4-template taxonomy ships the hierarchy in its vocabulary — `"HTTP_Only": "Main AIO for HTTP priority"`, `"P2P_Only": "To be used as a fallback"`, `"DEBRID": "all the addons needed for fast results"`. Aligns with Nuvio's design + the existing `project_stream_path_pivot_pending.md`. Agent 4 weakly supports as architectural precedent (Stremio's native shell decouples piece-scheduling into a separately-deployed binary it doesn't own). **Your call — technical fix first, or structural pivot first?**

### Recommendation

**Ratify as a single bundle for Agent 4 next wake:**

1. **Ship (f) first** — mirror mpv.conf directives into sidecar avformat_open + SidecarProcess launch flags. 1 batch, ~30 LOC in the sidecar, no Engine touch. Agent 3's domain if the sidecar edit is deep; Agent 4's domain if it's just `SidecarProcess` launch flags. **Co-own 4+3.**
2. **Ship (a) second** — gate-pass sequential toggle, triggered on GatePct first-crossing-100 per session. 1 batch, ~15 LOC in StreamEngine, uses existing TorrentEngine.setSequentialDownload. **Agent 4 owns.**
3. **Run (e-settings) in parallel** as cheap orthogonal experiment, independent rollback per setting. ~10 LOC in TorrentEngine session_settings. **Agent 4B owns the session-settings surface.**
4. **Park (e-reload) + (c)** as P5 stall-recovery tools (bolt on after (f)+(a) smoke).
5. **Defer (d) debrid pivot** pending product-call from Hemanth — NOT blocked by the above.

Agent 4 triple-smoke expected delta: `firstPieceMs` stays ≤ 12 s (no cold-open regression); `piece_diag` on post-gate-pass scrub flips `in_dl_queue 0 → ≥1` within one tick; stalls on pieces past cursor drop from 32 s to < 5 s.

Standing by for Hemanth's ratification: **Execute bundle (f)+(a)+(e-settings)** / **Debrid-first pivot** / **Different direction**.

---

## Hemanth's Final Word

**Ratified 2026-04-19 via directional commands** (not a literal `Execute` / `ratified` / `APPROVES` line — inferred by Agent 0 from the sequence):

1. **"no debrid for now. what do we do next?"** — explicitly rules OUT tactic (d) debrid-first pivot.
2. **"yes do 1"** × 2 (on successive turns) — accepted the Agent 0 next-move sequence verbatim: (1) commit-sweep first, then (1) archive Congress 7 + reset template.

**Operative ratification:** Execute the SHIP bundle **(f) + (a) + (e-settings)** as Agent 0 synthesis recommended. Defer (d) debrid pivot. Park (c) + (e-reload).

If this inference is wrong, a corrective `Final Word` line from Hemanth post-archive will be treated as a Congress-8 opening (not an amendment to this archived motion).

---

## Follow-ups out of this Congress

1. **Agent 0** authors `STREAM_STALL_FIX_TODO.md` mapping (f) + (a) + (e-settings) to 3 concrete batches with owners (Agent 3 / Agent 4 / Agent 4B respectively).
2. **Agent 0** summons Agent 3 first for batch 1 — (f) sidecar readahead (mirror mpv.conf's 300 MB forward + 150 MB back + 180 s readahead + 64 MB stream-buffer + reconnect=1 flags).
3. **Agent 4 + Agent 4B** wake in sequence post-(f) smoke: Agent 4 for (a) gate-pass sequential toggle, Agent 4B parallel on (e-settings) `max_out_request_queue` 1500-2000.
4. **Park for P5 stall-recovery tooling** (later): (e-reload) session pause/resume primitive + (c) nuclear piece-priority reset scrub primitive.
5. **Park for product discussion** (later): (d) debrid-first pivot evidence (Stremio-Kai 4-template taxonomy + Nuvio architecture + `project_stream_path_pivot_pending.md` Path 3).
