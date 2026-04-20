# Edge TTS Fix TODO — Qt-side WSS backend behind the existing `booksTtsEdge` JS bridge

**Owner: Agent 2 (Book Reader domain master). Coordination: Agent 0. Review gate: Hemanth smoke per phase exit (Agent 6 review protocol is suspended; Rule 11 READY TO COMMIT lines remain mandatory).**

Created: 2026-04-16.
Provenance:
- **Audit:** `agents/audits/edge_tts_2026-04-16.md` (Agent 7, Codex — 338-line comparative audit vs Readest + rany2/edge-tts + msedge ecosystem + MDN Web Speech + Microsoft Azure docs + Qt WebEngine docs + ~10 web sources cited).
- **Validation:** Agent 2 in `agents/chat.md:3204` (10/10 audit findings validated; CONFIRMED on all P0 findings + 2 ghost references flagged for cleanup).
- **Identity direction:** Edge TTS only (per `project_tts_kokoro` standing direction; Kokoro non-revival). Reference architecture is Readest's `EdgeTTSClient.ts` + `edgeTTS.ts` (MIT-licensed). Protocol mirrors `rany2/edge-tts` (Python, GPLv3 — behavior reference only, no code copied).
- **License posture:** reimplement in C++ from behavior + protocol references. Do NOT copy code from Readest (MIT) or rany2/edge-tts (GPLv3). Keeps internal IP clean + avoids GPL contamination.

---

## Context

The Book Reader's Listen feature is currently **broken-by-construction**, not "unreliable / inconsistent" as Hemanth originally characterized. Agent 7's audit + Agent 2's validation traced the architecture: the JS engine layer (`tts_engine_edge.js`) calls `window.Tanko.api.booksTtsEdge` expecting a real Qt/C++ WSS backend that never got implemented after the 2026-04-15 Kokoro removal. `BookReader.cpp:169-176` returns `{ok:false}` stubs for all 7 TTS methods. Listen → probe fails → `state.lastDiag = {code:'edge_probe_fail'}` → no engine selected → Listen button does nothing visible. Past characterization of "intermittent" was correct symptom, wrong root cause; today it is deterministic non-functionality.

Reference player: Readest's Edge TTS implementation. Direct WebSocket to `wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud/edge/v1` with `Sec-MS-GEC` token (5-min-rounded SHA256 of `timestamp + trustedClientToken`), Edge-like User-Agent + Origin headers, MUID cookie. Static voice table (~70 neural voices). LRU cache 200 blobs/URLs. Sentence-level marks (skips word-boundary even though protocol supports it — reliability trade-off). HTMLAudio playback via Blob object URLs. Tauri WebSocket plugin or Cloudflare Workers fetch-upgrade or Node isomorphic-ws to bypass browser-WebSocket-cannot-set-headers limitation.

Strategy locked: **Option B (Qt-side WSS backend behind the existing `booksTtsEdge` JS bridge).** Both Agent 2 and Agent 0 picked Option B independently per audit's strategy options. Reasoning trail:
- A (Web Speech) rejected — UA-opaque voice inventory, no Edge neural guarantee, conditional boundary events.
- **B chosen** — JS engine layer already shaped for the contract; smallest delta to working state; matches Readest's MIT-licensed reference architecture at a different language layer.
- C (renderer-only direct WSS) rejected — three structural blockers in QtWebEngine (`LocalContentCanAccessRemoteUrls=false` at `BookReader.cpp:75-77`, browser WebSocket cannot set arbitrary headers, browser security posture).
- D (staged hybrid) folded into B's phasing as ordering, not architecture.

Agent 2 also flagged two ghost references for Phase 5 cleanup:
- `window.booksTTSEngines.edgeDirect` factory at `tts_core.js:1175-1186` (and 4 other call sites) — never registered anywhere; was speculative entry point for Option C, which is rejected. Recommend deletion.
- `window.__ttsEdgeStream` injection at `tts_engine_edge.js:700-706` — never created. Streaming structurally unreachable until Phase 4 wires it.

Plus a third drift: `reader_state.js:251-255 ttsSupported()` returns `true` when `engines.edge` factory is registered — but factory registration ≠ working bridge. Capability signal is structurally weak; needs gating on `state.engineUsable.edge` post-init.

**Empirical deferred:** MP3 codec support in QtWebEngine. Agent 2 flagged this as the one Hemanth-side empirical needed (`MediaSource.isTypeSupported('audio/mpeg')` console check). Qt 6.10.2 MSVC official SDK ships proprietary codecs by default; assume MP3 path works. Phase 2 has Opus fallback design noted if codec test later returns false.

## Objective

After this plan ships, a user can:

1. Press Listen on any book → hear the first sentence within ~1-2 seconds.
2. Hear continuous sentence-by-sentence playback through a full chapter without interruption (when network is healthy).
3. See the voice picker populated with real Edge neural voices grouped by locale (en-US / en-GB / en-AU / en-IN).
4. Change voice mid-paragraph → playback restarts in the new voice.
5. Change rate (0.5x – 2x) mid-paragraph → playback restarts at the new rate.
6. Re-listen to a paragraph already played → instant playback (cache hit, no re-synth round-trip).
7. See clear diagnostic text when something fails (`Edge TTS network blocked`, `Voice unavailable`, etc.) — not a phantom Listen button or `Kokoro TTS removed` breadcrumb from the migration era.
8. Use sentence-level highlight in the page synced to audio playback — no word-level highlight (Readest pattern; reliability over granularity).
9. Pause / resume / skip-sentence / skip-paragraph via the existing HUD transport controls.
10. Persist progress across page navigation + close/reopen (existing HUD persistence infrastructure already wired; just needs working backend).

## Non-goals

- **No Kokoro TTS or any non-Edge engine.** Per `project_tts_kokoro` standing direction.
- **No Web Speech API fallback.** `tts_core.js:1173` already declares "Edge-only — no webspeech fallback"; Phase 5 deletes the stale Web Speech check in `reader_state.js:251-255`.
- **No word-level highlight in this TODO.** Readest deliberately ships sentence-only because Edge word-offset metadata drifts on long passages. Word-level can be a future polish track if Hemanth asks; not in scope here.
- **No flipping `LocalContentCanAccessRemoteUrls`.** That setting at `BookReader.cpp:75-77` stays `false`. Renderer-direct WSS (Option C) is structurally rejected.
- **No PCM bridge unless MP3 codec test fails.** MP3 base64 path is the default; PCM-via-ffmpeg is contingency design.
- **No GPL code copied.** Behavior reference from rany2/edge-tts (GPLv3) is fine; copying source is not.
- **No new TTS UI surface.** HUD + voice picker + transport controls already exist in `tts_hud.js`. This TODO ships the backend that makes them functional. UI polish is its own future track if needed.
- **No streaming if sentence batching is fast enough.** Phase 4 is conditional — only ships if Phases 1-3 are stable AND sentence-by-sentence latency is observably bad. The infrastructure scaffolds exist (`tts_engine_edge.js` MediaSource + bound-event scheduler at lines 700-785, 913-945) so future enablement is straightforward.
- **No agent-side smoke for the `MediaSource.isTypeSupported('audio/mpeg')` empirical.** Hemanth runs this once via DevTools console after Phase 1 ships. If `false`, Phase 2 design switches to Opus fallback.
- **No book reader navigation / Foliate engine changes.** TTS work integrates with existing Foliate marks + existing JS audio playback path.
- **No changes to PLAYER_UX_FIX, PLAYER_LIFECYCLE_FIX, PLAYER_PERF_FIX, or any video-player work.** Different domain, parallel tracks.

## Agent Ownership

**Primary (Agent 2, Book Reader domain):**
- `src/core/tts/EdgeTtsClient.{h,cpp}` (NEW — Phase 1)
- `src/core/tts/EdgeTtsWorker.{h,cpp}` (NEW — Phase 1)
- `src/ui/readers/BookReader.{h,cpp}` (existing — Phase 1.3, 4.1, 5.3)
- `resources/book_reader/domains/books/reader/tts_core.js` (existing — Phase 5.1)
- `resources/book_reader/domains/books/reader/reader_state.js` (existing — Phase 5.2)
- `resources/book_reader/services/api_gateway.js` (existing — Phase 5.3)
- `resources/book_reader/domains/books/reader/tts_engine_edge.js` (existing — minor compat changes only if bridge contract drifts)

**Secondary (announce-before-touch per Rule 10):**
- `CMakeLists.txt` — Phase 1.1 adds `src/core/tts/*` sources + `Qt6::WebSockets` find_package + link. Rule 7 chat.md heads-up required with exact lines added.

**Per contracts-v2:** Agent 2 does NOT run main-app builds (`cmake --build out`, MSVC, Ninja from bash — all unreliable). Hemanth runs `build_and_run.bat` natively for verification. Sidecar build is not touched in this TODO. Per Rule 15, Agent 2 reads logs / greps / traces references themselves; Hemanth observes UI.

---

## Phase 1 — Backend foundation (P0, ~3 batches)

**Why:** Audit Symptom 1 + Validation 1 CONFIRMED. The Qt bridge is stubbed; Edge TTS is deterministically non-functional. Foundation phase — nothing else in this TODO lands meaningfully without a real Qt-side WSS backend. Ship in three batches: client class, threading wrapper, bridge wiring.

### Batch 1.1 — `EdgeTtsClient` Qt class + WSS protocol foundation

**Scope:** New class `EdgeTtsClient` in `src/core/tts/EdgeTtsClient.{h,cpp}`. QtWebSockets dependency. Components:

1. **Endpoint constants** (mirror rany2/edge-tts behavior): WSS URL `wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud/edge/v1`, `TRUSTED_CLIENT_TOKEN` (literal string, public knowledge from rany2 ecosystem), Edge-like User-Agent string, `Origin: chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold`, MUID cookie pattern.
2. **`Sec-MS-GEC` token generator.** Algorithm: get current Windows file-time ticks → round to 5-minute boundary → concatenate with `TRUSTED_CLIENT_TOKEN` → SHA256 hash → uppercase hex. Reference: rany2/edge-tts `drm.py` lines 716-757. Reimplement in Qt; Qt has `QCryptographicHash::Sha256`. Add `QDateTime::currentDateTimeUtc()` based clock + manual file-time conversion math (file-time epoch is 1601-01-01 UTC, 100ns ticks).
3. **WSS connection setup.** `QWebSocket` with custom request headers via `QNetworkRequest`. Handshake to the WSS URL with `?ConnectionId=<UUID>&TrustedClientToken=<token>&Sec-MS-GEC=<token>&Sec-MS-GEC-Version=<edge-version>` query params.
4. **Protocol message framing.** Edge protocol uses two message types — text (config + SSML) and binary (audio chunks + metadata). Text messages: `X-RequestId`, `Content-Type`, `Path` headers + body. First message = `speech.config` JSON requesting MP3 + sentence boundary metadata. Second = `ssml` with the wrapped text. Reference: Readest `edgeTTS.ts:325-403`, rany2 `communicate.py` lines 2220-2247.
5. **Static voice table.** Reimplement Edge voice list — locale → voice name mappings for en-US (Aria, Andrew, Christopher, Eric, Guy, Roger, Steffan, Ana, Jenny, Michelle), en-GB (Sonia, Ryan, Libby, Maisie, Thomas), en-AU (Natasha, William), en-IN (Neerja with NeerjaExpressive variant, Prabhat). Reference: Readest `edgeTTS.ts:38-113` for the canonical list shape; reimplement, do not copy. Default voice on first launch: `en-US-AndrewNeural` (Hemanth's product call).

**Files (NEW):**
- `src/core/tts/EdgeTtsClient.h`
- `src/core/tts/EdgeTtsClient.cpp`

**Files (MODIFIED):**
- `CMakeLists.txt` — add `Qt6::WebSockets` to `find_package(Qt6 ...)` + target link list; add `src/core/tts/EdgeTtsClient.cpp` to source list. Rule 7 chat.md post required with exact line additions.

**Success:** `EdgeTtsClient` instance can be constructed; calling `client->probe(voice = "en-US-AriaNeural")` performs a WSS handshake + sends a 3-5 word synthesis request + receives audio chunks + closes cleanly. No exceptions. Token generator unit-testable: passing a fixed timestamp produces a deterministic hex string (compare against rany2/edge-tts known output for same input).

**Isolate-commit: yes** — foundation, validate empirically before Phase 2 piles on.

### Batch 1.2 — `EdgeTtsWorker` QThread wrapper

**Scope:** New class `EdgeTtsWorker` in `src/core/tts/EdgeTtsWorker.{h,cpp}`. Owns an `EdgeTtsClient` instance on a worker thread to keep WSS network I/O off the Qt event loop. Async API via signals:

```
signals:
    void probeFinished(bool ok, const QString& reason);
    void voicesReady(const QJsonArray& voices);
    void synthFinished(quint64 requestId, bool ok, const QByteArray& mp3, const QJsonArray& boundaries, const QString& reason);
    void streamChunk(quint64 streamId, const QByteArray& mp3Chunk);
    void streamBound(quint64 streamId, qint64 audioOffsetMs, qint64 textOffset, qint64 textLength);
    void streamEnded(quint64 streamId);
    void streamError(quint64 streamId, const QString& reason);
```

Slots:

```
public slots:
    void probe(const QString& voice);
    void getVoices();
    void synth(quint64 requestId, const QString& text, const QString& voice, double rate, double pitch);
    void synthStream(quint64 streamId, const QString& text, const QString& voice, double rate, double pitch);
    void cancelStream(quint64 streamId);
    void warmup();
    void resetInstance();
```

`requestId` / `streamId` are caller-provided; the worker echoes them back in the corresponding `*Finished` / `*Ended` signal so the JS bridge can correlate. Mirrors PaintLayer threading pattern. `QThread::run` not overridden; use signal/slot dispatch with `Qt::QueuedConnection` so calls hop the thread boundary correctly.

**Files (NEW):**
- `src/core/tts/EdgeTtsWorker.h`
- `src/core/tts/EdgeTtsWorker.cpp`

**Files (MODIFIED):**
- `CMakeLists.txt` — add `src/core/tts/EdgeTtsWorker.cpp` to source list. (Same Rule 7 commit as Batch 1.1 — the two .cpp files land in one CMakeLists edit.)

**Success:** Spawn `EdgeTtsWorker` on a `QThread`; call `worker->probe("en-US-AriaNeural")` from the main thread; receive `probeFinished(true, "")` via signal on the main thread within ~1-2s. No deadlocks. Destruction is clean — calling `worker->deleteLater()` on the worker + `quit()` + `wait()` on the QThread terminates without hangs.

**Isolate-commit: no** — composes with 1.3.

### Batch 1.3 — Wire `BookReader.cpp` `booksTtsEdge` JS bridge

**Scope:** Replace 7 stub `Promise.resolve({ok:false})` returns at `BookReader.cpp:169-176` with real `EdgeTtsWorker` calls via `QWebChannel`. Each JS bridge method dispatches to the worker via signal/slot, awaits the result via a Promise that resolves when the worker emits the corresponding `*Finished` signal.

API contract (matches what `tts_engine_edge.js` already expects):
- `probe({text, voice, requireSynthesis}) → {ok:bool, voiceListAvailable:bool, reason?:string}` — Agent 2's Validation 9 refinement: accept the voice parameter, attempt 3-5 word synthesis, return structured result.
- `getVoices() → {ok:bool, voices:[{name, locale, gender, displayName}]}` — return the static table from Batch 1.1.
- `synth({text, voice, rate, pitch}) → {ok:bool, audioBase64?:string, boundaries?:[], reason?:string}` — Phase 2 implementation (Phase 1.3 stub returns `{ok:false, reason:'phase_2_pending'}`).
- `synthStream({text, voice, rate, pitch}) → {ok:bool, streamId?:string, reason?:string}` — Phase 4 implementation (Phase 1.3 stub returns `{ok:false, reason:'phase_4_pending'}`).
- `cancelStream({streamId}) → {ok:true}` — wire to worker `cancelStream` slot.
- `warmup() → {ok:bool, reason?:string}` — wire to worker `warmup` slot.
- `resetInstance() → {ok:true}` — wire to worker `resetInstance` slot.

`BookReader` owns a `QThread` + `EdgeTtsWorker` lifecycle. Spawn on first JS bridge call (lazy); destroy on `BookReader` destructor (book close).

Goal of Phase 1.3: `probe()` and `getVoices()` return real success. Listen button no longer surfaces `edge_probe_fail`. Voice picker populates with real voices (en-US, en-GB, en-AU, en-IN groupings — `tts_hud.js:241+257` already filters and groups). `synth()` + `synthStream()` deliberately stubbed-pending until Phase 2 / Phase 4.

**Files (MODIFIED):**
- `src/ui/readers/BookReader.h` (declare worker + thread members + helper methods)
- `src/ui/readers/BookReader.cpp` (replace stub block at :169-176 with real wiring; instantiate worker + thread; QWebChannel registration if needed for any new QObject)

**Success:** Hemanth opens any book → presses Listen. Console log shows `probe` succeeded. Voice picker populates. `state.lastDiag` does NOT contain `edge_probe_fail`. Listen button stays in initialization state pending Phase 2 (no audio yet — that's expected at this phase exit; the goal is the bridge being alive).

**Isolate-commit: no** — composes with 1.2 worker; Phase 1 ships as a single conceptual unit in 2 commits (1.1 isolate + [1.2+1.3] together).

**Phase 1 exit criteria:**
- WSS handshake completes against Edge endpoint (token gen + headers correct).
- `probe()` returns `{ok:true}`.
- `getVoices()` returns ~70 voices grouped by locale.
- Voice picker shows en-US / en-GB / en-AU / en-IN groups with named voices.
- No `edge_probe_fail` in diagnostics.
- Listen button not yet producing audio (Phase 2 territory) — expected.
- Hemanth smoke: open book, press Listen, observe voice picker populates with real voice names. Read DevTools console for any errors.

---

## Phase 2 — Synth + sentence-level playback (P0, ~3 batches)

**Why:** Audit P0 #1 (slow startup → blank screen) + Validation 1 chain. Phase 1 makes the bridge alive; Phase 2 makes audio actually play. Sentence-level (per Readest pattern, sentence-first reliability over word-level granularity).

### Batch 2.1 — `synth()` round-trip MP3 base64

**Scope:** Wire `EdgeTtsClient::synth(text, voice, rate, pitch)` end-to-end. Sends `speech.config` + `ssml` messages over WSS, accumulates binary audio chunks, parses metadata frames into boundary array (sentence-level only — Phase 4 streams word-level if pursued). Returns `{ok:true, audioBase64:base64(mp3), boundaries:[]}` on success. Boundaries empty on Phase 2 (sentence boundaries come from Foliate marks on the JS side; real word-boundary metadata is Phase 4 streaming concern).

JS engine plays the returned `audioBase64` via existing path at `tts_engine_edge.js:1086-1104` — no changes needed on JS side.

Failure modes return structured `{ok:false, reason:<taxonomy>}`. Initial taxonomy in Phase 2 (expanded in Phase 5):
- `wss_handshake_fail` — WSS connect rejected
- `no_audio_received` — protocol completes but zero audio frames (rany2/edge-tts known issue #440)
- `voice_unavailable` — Edge returns voice-not-found error frame
- `network_blocked` — WSS connect timeout / socket error
- `cancelled` — caller invoked cancelStream

**Files (MODIFIED):**
- `src/core/tts/EdgeTtsClient.{h,cpp}` (synth implementation)
- `src/core/tts/EdgeTtsWorker.{h,cpp}` (synth slot wiring)
- `src/ui/readers/BookReader.cpp` (Phase 1.3 stub for synth → real bridge call)

**Success:** Calling `synth("Edge probe", "en-US-AriaNeural", 1.0, 1.0)` returns `{ok:true, audioBase64:<non-empty>}` within ~2s on a healthy network.

**Isolate-commit: yes** — synth round-trip is the single highest-value batch in the TODO; isolate for clean smoke before sentence wiring.

### Batch 2.2 — Foliate SSML wrapping + Edge consumer subset sanitization

**Scope:** `EdgeTtsClient::buildSSMLRequest(text, voice, rate, pitch)` constructs the protocol-compliant SSML payload. Edge consumer protocol restricts SSML to a single `<voice>` tag with one `<prosody>` tag (per rany2/edge-tts README lines 320-322). Foliate's `tts.js` may emit richer SSML — sanitize down to the supported subset:

```xml
<speak xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='en-US'>
  <voice name='en-US-AndrewNeural'>
    <prosody rate='+0%' pitch='+0Hz'>
      <text content here, escaped>
    </prosody>
  </voice>
</speak>
```

Strip any `<bookmark>`, `<phoneme>`, `<sub>`, `<say-as>`, etc. tags Foliate may emit (Edge consumer endpoint silently ignores or errors on them). Convert rate/pitch from JS-side (1.0 = baseline) to Edge format (`+0%` / `+0Hz`). Escape XML entities in text.

**Files (MODIFIED):**
- `src/core/tts/EdgeTtsClient.cpp` (buildSSMLRequest + sanitization helper)

**Success:** Round-trip SSML round-trip works for plain sentences, sentences with quotes, sentences with HTML entities (`&amp;`, `&lt;`, etc.), and sentences with em-dashes. No protocol errors.

**Isolate-commit: no** — composes with 2.3.

### Batch 2.3 — Sentence-level highlight wiring + smoke

**Scope:** Foliate's vendored `tts.js` already produces sentence boundaries on the JS side via marks. JS engine at `tts_engine_edge.js` already iterates marks + plays each sentence's audio in sequence + advances the highlight. Phase 2.3 verifies this path works end-to-end with the real Edge backend now plumbed.

No new JS code; this batch is pure integration smoke + any minor fixups required to align the Phase 1.3 bridge contract with the existing JS engine's expectations. Per Agent 2's note ("minor compat changes only if Qt bridge contract drifts"), if the existing `tts_engine_edge.js:183-201` synth call shape needs adjustment for the new `audioBase64` payload format, fix here.

**Files (MODIFIED):**
- `resources/book_reader/domains/books/reader/tts_engine_edge.js` (only if compat shim needed — likely no edits)

**Success:** Hemanth opens a book → presses Listen → audio plays first sentence in `en-US-AndrewNeural` → advances to next sentence on audio end → continues through full chapter. Sentence highlight in the page advances in sync.

**Isolate-commit: yes** — milestone batch (Listen actually works); isolate for celebratory smoke + targeted regression detection.

**Phase 2 exit criteria:**
- Full chapter playback from Listen press to end-of-chapter without stalls (on healthy network).
- Sentence highlight advances correctly.
- Pause / resume / skip-sentence / skip-paragraph (existing HUD transport) all work — they're just calling `tts.play()` / `tts.pause()` which now have a real backend.
- No console errors on the happy path.
- Hemanth smoke: open a book, press Listen, listen to ~5 minutes of audio. Skip a sentence. Pause. Resume.

---

## Phase 3 — Cache + voice/rate switching (P1, ~2 batches)

**Why:** Audit Symptom 4 (HUD button polish — voice/rate change responsiveness) + Validation 7. Audit + Readest pattern flagged caching as a meaningful UX improvement (re-listen of same paragraph should be instant, not a full WSS round-trip). Voice/rate change mid-text needs to handle restart cleanly.

### Batch 3.1 — LRU cache in `EdgeTtsClient`

**Scope:** In-memory LRU cache. Key = SHA1(text + voice + rate + pitch + format). Value = MP3 byte buffer + boundaries JSON. Capacity ~200 entries (mirrors Readest's `edgeTTS.ts:286-293`). Lazy eviction (drop oldest on insert when at capacity). Cache lookup before WSS request in `synth()`. Cache hit returns instantly without network.

**Files (MODIFIED):**
- `src/core/tts/EdgeTtsClient.{h,cpp}` (LRU cache members + cache-lookup wrapper around synth)

**Success:** Call `synth(same text, same voice, same rate, same pitch)` twice — second call returns within ~10ms (cache hit, no network). Cache fills to 200 entries then starts evicting oldest. Memory footprint bounded.

**Isolate-commit: yes** — verifiable via timing log.

### Batch 3.2 — Voice + rate change mid-text restart

**Scope:** Existing JS engine path already restarts synthesis on voice/rate change (calls `tts.play()` again with new params). Verify this composes correctly with Phase 3.1 cache: new (voice, rate) tuple = different cache key = new WSS round-trip; subsequent re-listen with that tuple = cache hit.

If existing JS path doesn't cleanly handle mid-sentence restart, fix here (likely a `cancelStream()` + restart from current sentence boundary).

**Files (MODIFIED):**
- `resources/book_reader/domains/books/reader/tts_core.js` or `tts_engine_edge.js` (only if mid-sentence restart needs explicit cancel/replay)

**Success:** Hemanth presses Listen → mid-paragraph changes voice from Andrew to Aria → playback restarts at current sentence in Aria voice. Same for rate change.

**Isolate-commit: no** — bundled with 3.1 if no JS change needed; standalone if JS edit required.

**Phase 3 exit criteria:**
- Re-listen of same paragraph: instant (cache hit verified by latency).
- Voice change mid-paragraph: restarts in new voice within ~2s.
- Rate change mid-paragraph: restarts at new rate within ~2s.
- 200-entry cache cap working (verifiable via debug log on eviction).
- Hemanth smoke: paragraph A → switch voice → paragraph A again (instant in new voice, then cache hit on third pass).

---

## Phase 4 — Streaming (P2, optional, ~3 batches)

**Why:** Audit P2 (lower latency on first-sentence playback). Conditional — only ships if Phases 1-3 are stable AND batch sentence-by-sentence latency is observably bad. If sentence batching is fast enough on Hemanth's network, defer indefinitely. Architecture is ready (`tts_engine_edge.js` lines 700-785 + 913-945) just needs the infrastructure wires.

### Batch 4.1 — Inject `window.__ttsEdgeStream` from `BookReader.cpp`

**Scope:** Create a new `QObject` subclass `EdgeTtsStreamBridge` exposing signals (`chunk`, `bound`, `end`, `error`) + slot (`cancel`). Register via `QWebChannel`. Inject as `window.__ttsEdgeStream` in `BookReader.cpp` JS bridge initialization. Verify QWebChannel can flow binary `QByteArray` payloads (the chunks) without base64 inflation — if QWebChannel inflates, may need to ferry via `QString` base64 with note in comments.

Open technical detail Agent 2 decides: the `__ttsEdgeStream` shape per `tts_engine_edge.js:738-761` expects `.on('chunk'|'bound'|'end'|'error', cb)` + `.off(...)`. Either implement an EventEmitter shim layer in JS that wraps the QObject signals, OR adapt the JS code to use Qt signal connect syntax directly. EventEmitter shim is cleaner.

**Files (NEW):**
- `src/core/tts/EdgeTtsStreamBridge.{h,cpp}` (or fold into `EdgeTtsWorker.{h,cpp}` if cleaner)

**Files (MODIFIED):**
- `src/ui/readers/BookReader.cpp` (register + inject `__ttsEdgeStream`)
- `CMakeLists.txt` (if new .cpp added)

**Success:** Console: `typeof window.__ttsEdgeStream === 'object'` returns `true`. `tts_engine_edge.js:_checkStreamSupport()` returns `true`.

**Isolate-commit: yes** — pure infrastructure scaffolding, validate before wiring synthStream.

### Batch 4.2 — `synthStream()` chunked WSS frames

**Scope:** `EdgeTtsClient::synthStream(text, voice, rate, pitch, streamId)` opens WSS connection, sends config + SSML, but instead of accumulating audio into a single buffer (Phase 2 path), pumps chunks out via `EdgeTtsWorker::streamChunk` signal as each binary frame arrives. Word-boundary metadata frames pump out via `streamBound` signal. End frame triggers `streamEnded`. `cancelStream(streamId)` closes the WSS connection.

`tts_engine_edge.js` already has the consumer side wired at lines 738-761 (listens for chunk / bound / end / error and feeds chunks to MediaSource).

**Files (MODIFIED):**
- `src/core/tts/EdgeTtsClient.{h,cpp}` (synthStream implementation)
- `src/core/tts/EdgeTtsWorker.{h,cpp}` (synthStream slot wiring)
- `src/ui/readers/BookReader.cpp` (synthStream JS bridge — replace Phase 1.3 phase_4_pending stub with real worker call)

**Success:** Calling `synthStream(long text, voice, rate, pitch, streamId)` produces multiple `chunk` events on the JS side over the synth duration, with `bound` events interleaved at sentence (or word) boundaries. JS-side MediaSource buffers chunks and starts playback before full synth completes (lower first-sentence latency).

**Isolate-commit: yes** — substantial functional change; isolate for targeted regression detection.

### Batch 4.3 — Conditional ship gate

**Scope:** Not a code batch. Decision gate: after Phases 1-3 ship and Hemanth smokes, if first-sentence latency is acceptable (sentence-batch path produces audio in <1.5s), defer Phase 4 indefinitely as a future polish track. If first-sentence latency is bad (>3s, audible delay between Listen press and first audio), proceed with 4.1 + 4.2.

This batch exists as a checkpoint for Agent 2 to make the call. No file changes.

**Files:** none

**Success:** Decision logged in chat.md by Agent 2.

**Isolate-commit: N/A** — decision-only.

**Phase 4 exit criteria (if pursued):**
- Streaming reduces first-sentence latency by ≥30% vs Phase 3 baseline.
- No regressions on Phase 2/3 sentence-level playback.
- MediaSource doesn't throw codec errors on chunk feed.

---

## Phase 5 — Cleanup + diagnostics (P1, ~3 batches)

**Why:** Validation 2 (`edgeDirect` ghost), Validation 3 (`__ttsEdgeStream` ghost — handled in Phase 4), Validation 4 (stale `ttsSupported()`), Validation 8 (stale `api_gateway.js` comment), Audit Symptom 4 (HUD button polish — phantom controls). Code-hygiene phase that closes leftover migration debt + introduces real failure-reason taxonomy.

### Batch 5.1 — Delete dead `edgeDirect` branch

**Scope:** Remove all references to `edgeDirect` in `tts_core.js`:
- `:1175-1186` factories check + .create() call
- `:1201-1204` probe call
- `:1219-1224` engine selection
- `:1227` warmup skip
- `:1933` engine order list

Was the speculative entry point for Option C (renderer-direct WSS), which is rejected. Greppable verification post-deletion: `grep -r edgeDirect resources/book_reader/` returns zero hits.

**Files (MODIFIED):**
- `resources/book_reader/domains/books/reader/tts_core.js`

**Success:** `grep -r edgeDirect resources/book_reader/` returns nothing. JS engine init still works (Phase 1 + 2 path takes over for the only remaining engine selection).

**Isolate-commit: yes** — pure code-deletion sanitation; isolate for regression confidence.

### Batch 5.2 — Rewrite `ttsSupported()` to gate on actual engine state

**Scope:** Current `reader_state.js:251-255 ttsSupported()`:

```js
function ttsSupported() {
  var engines = window.booksTTSEngines || {};
  if (engines.edge) return true;
  return typeof window.SpeechSynthesisUtterance === 'function' && !!window.speechSynthesis;
}
```

Rewrite to gate on `state.engineUsable.edge` post-init success (set by `tts_core.js:init()` after probe). Delete the Web Speech fallback line per the "Edge-only — no webspeech fallback" comment at `tts_core.js:1173`.

```js
function ttsSupported() {
  // Per audit + validation 2026-04-16: factory registration ≠ working bridge.
  // Only report supported after init's probe has succeeded.
  return !!(window.booksTTSState && window.booksTTSState.engineUsable && window.booksTTSState.engineUsable.edge);
}
```

(Exact accessor path may vary based on existing `state` shape — Agent 2 picks the right reference.)

**Files (MODIFIED):**
- `resources/book_reader/domains/books/reader/reader_state.js`

**Success:** `ttsSupported()` returns `false` before init runs, `true` after successful probe, `false` if probe fails. Web Speech check gone.

**Isolate-commit: no** — composes with 5.3 HUD failure state collapse.

### Batch 5.3 — Real failure-reason taxonomy + HUD failure state collapse

**Scope:** Two-part:

(a) **Failure-reason taxonomy in `BookReader.cpp` + propagated through bridge.** Define an enum or string set:
- `wss_handshake_fail`
- `token_gen_fail`
- `voice_unavailable`
- `no_audio_received`
- `network_blocked`
- `decode_fail`
- `cancelled`
- `phase_4_pending` (transitional, removed when Phase 4 ships or is permanently deferred)

Replace the `Kokoro TTS removed` string in stub returns (most stubs are gone after Phase 1.3, but any leftover defensive returns get the new taxonomy). All Phase 2 + 4 failure paths populate `reason` from this set. Update `api_gateway.js:138` stale "Python edge-tts backend" comment to current architecture.

(b) **HUD failure state collapse.** When `ttsSupported()` returns `false` OR probe fails:
- Hide voice picker (don't show empty/stale list)
- Disable Listen button (don't allow click that does nothing)
- Show diagnostic text: `"Edge TTS unavailable: <reason>"` — readable failure not phantom controls

This addresses Audit Symptom 4 hypothesis "HUD may feel inconsistent because availability, voice list, and playback state are not collapsed into a single clear 'Edge TTS unavailable / diagnostics' state."

**Files (MODIFIED):**
- `src/ui/readers/BookReader.cpp` (failure-reason taxonomy in JS bridge returns)
- `resources/book_reader/services/api_gateway.js` (stale comment fix)
- `resources/book_reader/domains/books/reader/tts_hud.js` (failure state collapse — hide picker, disable Listen, show diagnostic)

**Success:** Disconnect network mid-session → next Listen press shows "Edge TTS unavailable: network_blocked" instead of silent freeze. Voice picker hidden during failure state.

**Isolate-commit: yes** — user-visible diagnostic surface, isolate for targeted smoke.

**Phase 5 exit criteria:**
- `grep -r edgeDirect resources/book_reader/` returns zero hits.
- `ttsSupported()` is honest (only true when engine actually works).
- Failure state shows real diagnostic, not phantom controls.
- `api_gateway.js` comment accurate.
- Hemanth smoke: kill network → press Listen → see clear failure diagnostic. Restore network → press Listen → works.

---

## Scope decisions locked in

- **Default voice: `en-US-AndrewNeural`** (Hemanth's call). Voice picker stays free for user override.
- **Sentence-first highlight** (technical decision per Rule 14). Word-level deferred to a future polish track if Hemanth asks.
- **Option B architecture** — Qt-side WSS backend behind existing `booksTtsEdge` JS bridge. Renderer JS layer stays as-is.
- **License posture** — reimplement, do NOT copy from Readest (MIT) or rany2/edge-tts (GPLv3). Behavior reference only.
- **Threading** — `QThread` worker (Agent 2's recommendation). Mirrors PaintLayer pattern.
- **MP3 codec** — assume MP3 path works (Qt 6.10.2 official MSVC SDK ships proprietary codecs). Hemanth's one DevTools console check (`MediaSource.isTypeSupported('audio/mpeg')`) deferred to post-Phase-1; Phase 2 has Opus fallback noted if codec test returns `false`.
- **Cache capacity 200 entries** (mirrors Readest).
- **Phase 4 streaming is conditional** — only ships if sentence-batch latency is observably bad after Phase 3.
- **No `LocalContentCanAccessRemoteUrls` flip** — security posture preserved.

## Isolate-commit candidates (per Rule 11)

- Batch 1.1 (EdgeTtsClient + WSS foundation)
- Batch 2.1 (synth round-trip)
- Batch 2.3 (sentence playback milestone)
- Batch 3.1 (LRU cache)
- Batch 4.1 (`__ttsEdgeStream` injection — if Phase 4 pursued)
- Batch 4.2 (synthStream — if Phase 4 pursued)
- Batch 5.1 (edgeDirect deletion)
- Batch 5.3 (failure taxonomy + HUD collapse)

Phase boundaries: 1.2 + 1.3 land together as Phase 1 close. 3.2 lands with 3.1 if no JS change needed. 5.2 lands with 5.3.

## Existing functions/utilities to reuse

- **Foliate's vendored `tts.js`** at `resources/book_reader/vendor/foliate/tts.js:47-118` — SSML mark generation. Do NOT rewrite, just consume Foliate's marks in the Edge synth payload. Sanitize down to Edge consumer subset in Batch 2.2.
- **JS engine's existing audio playback path** at `tts_engine_edge.js:261-266` (`new Audio()` + object URLs) and `tts_engine_edge.js:1086-1104` (`audioUrl` / `audioBase64` decode + play). Phase 2.1 / 2.3 plug into this without modification.
- **JS engine's existing boundary scheduler** at `tts_engine_edge.js:913-945` — Phase 4.2 wires real `bound` events from the worker into this.
- **Existing HUD + transport controls** at `tts_hud.js` — no UI rebuild; Phase 5.3 just collapses failure-state visibility.
- **`BookReader.cpp` existing `QWebChannel` shim infrastructure** at lines ~110-180 — Phase 1.3 + 4.1 wire new `QObject`s through this.
- **`QCryptographicHash::Sha256`** (Qt builtin) — for `Sec-MS-GEC` token generation in Batch 1.1.
- **`QtWebSockets::QWebSocket`** — WSS client primitive in Batch 1.1.
- **Existing `LoggingWebEnginePage` infrastructure** in `BookReader` — DevTools wiring already exists; Hemanth's `MediaSource.isTypeSupported` check uses this.
- **`tts_engine_edge.js:_checkStreamSupport()`** at lines 700-706 — Phase 4.1 satisfies its requirements (MediaSource MP3 + `__ttsEdgeStream` object).

## Review gates

Agent 6 review protocol is SUSPENDED. Per phase exit:

- Agent 2 posts `READY TO COMMIT — [Agent 2, EDGE_TTS_FIX Phase <N> Batch <X.Y>]: <one-line> | files: a.cpp, b.h`.
- For batches that touch `CMakeLists.txt`, post Rule 7 chat.md heads-up with exact lines added BEFORE the edit.
- Per contracts-v2: Agent 2 does NOT run main-app builds. Hemanth runs `build_and_run.bat` natively per phase exit smoke.
- Hemanth smokes per phase exit criteria (listed in each Phase section above).
- Agent 0 sweeps commits + bumps CLAUDE.md dashboard.

(Preserved for Agent 6 reactivation: `READY FOR REVIEW — [Agent 2, EDGE_TTS_FIX Phase <N> Batch <X.Y>]: ...` template — do not post unless Agent 6 is revived.)

## Open design questions Agent 2 decides as domain master

1. **Cache eviction policy detail.** Strict LRU (touch on read), or LRU-with-frequency? Strict LRU is the Readest pattern + simpler. Your call.
2. **Retry/backoff timing.** On `wss_handshake_fail` or `network_blocked`, do we retry once with exponential backoff, or fail-fast and let the user retry via UI? Readest fails-fast. Your call.
3. **Exact failure-reason taxonomy text.** The set in Phase 5.3 is the proposed shape; specific user-visible strings ("Edge TTS network blocked" vs "Network connection failed" vs "Cannot reach Edge service") are yours.
4. **MP3 fallback to Opus** if Hemanth's empirical returns `false` on `MediaSource.isTypeSupported('audio/mpeg')`. Edge supports `audio-24khz-48kbitrate-mono-opus` per rany2/edge-tts constants. Phase 2 design: assume MP3, branch to Opus only if Hemanth's check confirms MP3 unsupported. Your call on the exact `outputFormat` string passed in `speech.config`.
5. **`QThread` lifecycle on book close.** Spawn-on-first-use + destroy-on-BookReader-destructor is the proposed pattern. Alternative: persistent worker thread for the whole BookReader page (less spawn cost, more idle resource). Your call.
6. **`__ttsEdgeStream` EventEmitter shim placement** (Phase 4.1). Pure-JS shim in a new `tts_stream_bridge.js` file, or inline in `BookReader.cpp` injected JS string? Pure-JS file is more readable + easier to test. Your call.
7. **Sentence-batch vs paragraph-batch synth requests** in Phase 2. Synth one sentence at a time (lower latency to first audio, more WSS round-trips), or one paragraph at a time (fewer round-trips, higher latency to first audio)? Readest does sentence-at-a-time. Your call based on observed latency.

## What NOT to include (explicit deferrals)

- **Word-level highlight in this TODO.** Sentence-only ships in Phase 2; word-level can be a future polish track per Hemanth direction.
- **Streaming if sentence batching is fast enough.** Phase 4 is conditional per Phase 3 latency observation.
- **Kokoro re-introduction.** Standing direction. Don't propose.
- **Web Speech API fallback.** Edge-only per `tts_core.js:1173` + Phase 5.2.
- **`LocalContentCanAccessRemoteUrls` flip.** Security posture preserved; Option C structurally rejected.
- **Browser-side WSS implementation.** Renderer can't set Edge headers; not a viable path.
- **GPL code paste from rany2/edge-tts.** Behavior reference only; reimplement.
- **MIT code paste from Readest.** Same — reference only, reimplement to keep contributor-license clean.
- **PCM bridge** unless MP3 codec test fails empirically.
- **Opus implementation effort** unless MP3 codec test fails (have the design noted, don't ship until needed).
- **`HTMLAudioElement` output device selection** (Hemanth's specific audio output device) — Qt 6 doesn't expose this through QtWebEngine cleanly; defer until/unless Hemanth reports needing it.
- **Voice list refresh from Edge endpoint** — static table is sufficient for first ship; live endpoint fetch is a future enhancement if voices drift.
- **SSML support beyond `<voice>` + `<prosody>`.** Edge consumer endpoint constrained per rany2 README. Don't try to enable bookmarks / phonemes / lexicons.
- **Multi-language detection per book** — assume English for now; book reader lang detection is a separate concern.
- **TTS HUD redesign or new controls** — backend work only; HUD already exists.

## Rule 6 + Rule 11 application

Standard. Every batch:

- Agent 2 builds — but per contracts-v2 + Rule 15, agents do NOT run main-app builds from bash. Hemanth runs `build_and_run.bat` natively for verification.
- Per Rule 7: any `CMakeLists.txt` edit (Phase 1.1 + 1.2 + possibly 4.1) requires chat.md heads-up post with exact lines added BEFORE the edit.
- Per Rule 10: any `BookReader.{h,cpp}` edit doesn't require Rule 10 (Agent 2's primary domain). Other shared files (`MainWindow.cpp` etc.) not touched in this TODO.
- Per Rule 11: post `READY TO COMMIT` line per batch with accurate file list + one-line message.
- Per Rule 12: bump `Last agent-section touch` in `STATUS.md` when overwriting your block.
- Per `feedback_one_fix_per_rebuild`: one batch, one rebuild, one smoke. No batching of code into pre-commit rollup.
- Per `feedback_credit_prototype_source`: no Agent 7 prototype exists for this TODO (Trigger B suspended for new TODOs by default per `feedback_prototype_agent_pacing`); credit Readest + rany2/edge-tts as behavior references in commit messages where they shaped the implementation.

## Verification procedure (Hemanth smoke end-to-end)

1. **Phase 1 smoke (bridge alive):** open a book → press Listen. Voice picker populates with Edge voices in en-US / en-GB / en-AU / en-IN groupings. DevTools console shows no `edge_probe_fail`. Listen button stays in initialization state pending Phase 2 — that's expected.

2. **Phase 1 codec empirical (one-time, deferred from authoring):** in DevTools console, run `MediaSource.isTypeSupported('audio/mpeg')`. Report result to Agent 2. If `true`, Phase 2 ships MP3. If `false`, Phase 2 switches to Opus per the fallback design.

3. **Phase 2 smoke (Listen actually works):** open a book → press Listen → first sentence plays in `en-US-AndrewNeural` within ~2s → next sentence plays in sequence → full chapter plays through. Sentence highlight in the page advances in sync with audio.

4. **Phase 2 SSML edge cases:** open a book with text containing quotes, em-dashes, HTML entities, mixed punctuation. Listen. Audio renders correctly without SSML protocol errors.

5. **Phase 3 cache:** open a book → press Listen → listen to paragraph A → press skip-back → re-listen paragraph A. Second listen starts within ~10ms (cache hit, no perceptible network round-trip).

6. **Phase 3 voice change:** mid-paragraph, change voice from Andrew to Aria → playback restarts at current sentence in Aria voice within ~2s. Repeat for `en-IN-NeerjaNeural` (different locale, exercises different voice path).

7. **Phase 3 rate change:** mid-paragraph, change rate to 1.5x → playback restarts at current sentence at faster rate.

8. **Phase 4 smoke (only if pursued):** open a book → press Listen → first audio plays in <1s (vs Phase 2 baseline ~2s). MediaSource consumes streamed chunks without codec errors.

9. **Phase 5 cleanup verification:** in DevTools console, run `typeof window.booksTTSEngines.edgeDirect`. Returns `'undefined'`. `grep -r edgeDirect resources/book_reader/` (Hemanth runs the grep, or asks Agent 0): zero hits.

10. **Phase 5 failure state:** disconnect network → press Listen → see "Edge TTS unavailable: network_blocked" message. Voice picker hidden. Listen button disabled. Restore network → press Listen → works again.

11. **Phase 5 `ttsSupported()` honesty:** in DevTools console pre-Listen-press, run any Edge-related capability check (e.g., the function call directly if exposed). Returns `false` before init, `true` after successful probe, `false` after deliberate probe fail.

12. **Regression check — Book Reader navigation:** open a book → start TTS → navigate to next page → TTS continues across page boundary (existing HUD persistence). Close book → reopen → progress persisted (existing `tts_hud.js:493-543` save path — should still work).

13. **Regression check — other agent work:** PLAYER_UX_FIX shipped surfaces (Loading overlay, HUD reset, Tracks popover, EQ presets) unaffected by Book Reader changes. STREAM_LIFECYCLE, PLAYER_LIFECYCLE work unaffected.

14. **Long-session smoke:** open a book → press Listen → listen for 30 minutes. No memory leak (cache eviction working). No degradation in audio quality. No spurious WSS reconnects on healthy network.

15. **End-to-end (comprehensive):** 1-hour session exercising: book A chapter 1 full playback → switch to book B → start TTS → switch voice → switch rate → cache hit on re-listen → close book B → return to book A → resume TTS at saved position → end of chapter → continue to next chapter (existing chapter-advance logic) → no crashes, no audio gaps, no HUD inconsistencies.

---

## Next steps post-approval

- Agent 0 posts routing announcement in chat.md when Hemanth ratifies the plan.
- Agent 2 executes phased per Rule 6 + Rule 11 + `feedback_commit_cadence`.
- Agent 6 reviews per phase exit [dormant — Hemanth smokes directly, READY FOR REVIEW lines not posted].
- Agent 0 commits at phase boundaries per `feedback_commit_cadence` (not mid-batch accumulation).
- MEMORY.md "Active repo-root fix TODOs" line updated to include `EDGE_TTS_FIX_TODO.md`.
- `project_tts_kokoro` memory's "current state: bridge stubbed" note becomes stale once Phase 1 ships — Agent 0 updates the memory at Phase 1 close.
- Hemanth's MP3 codec console check (Verification step 2) is the one Hemanth-side empirical needed before Phase 2 starts — request it in the chat.md routing announcement.

---

**End of plan.**
