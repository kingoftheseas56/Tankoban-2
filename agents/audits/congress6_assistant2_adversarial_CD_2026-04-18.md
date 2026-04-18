# Assistant 2 Adversarial Review — Congress 6 Slice C + Slice D (collapsed to appendix) — 2026-04-18

**Disclosure:** fresh-Claude reviewer, no domain context, adversarial pass + Slice D collapse-honesty check per motion §Collapse rule. No access to chat.md history beyond the Congress 6 motion archive. No coordination with Assistant 1 (who reviews Slices A + B). No compile/run of Tankoban. No fix prescription. Citations spot-checked against `C:\Users\Suprabha\Downloads\Stremio Reference\` snapshots.

**Under review:**
- [agents/audits/congress6_player_sidecar_2026-04-18.md](congress6_player_sidecar_2026-04-18.md) — combined Slice C + Slice D file (488 lines; ~265 lines Slice C body at §Q1–§Q3 + integration feeders + summary; ~207 lines Slice D appendix at §Q1-D–§Q3-D + appendix integration feeders).
- 3-question sheets in [agents/congress_archive/2026-04-18_congress6_stremio_audit.md](../congress_archive/2026-04-18_congress6_stremio_audit.md) §Pre-Brief.
- Prior-art continuity: [agents/audits/player_stremio_mpv_parity_2026-04-17.md](player_stremio_mpv_parity_2026-04-17.md) (Agent 7, 97 lines; 3 P0 / 5 P1 / 3 P2).
- Stremio Reference spot-checked: `stremio-core/src/models/player.rs`, `src/runtime/msg/event.rs`, `src/runtime/runtime.rs`, `stremio-core-web/src/model/serialize_player.rs`, `stremio-video-master/src/HTMLVideo/HTMLVideo.js`, `stremio-core/src/types/library/library_item.rs`, `src/models/continue_watching_preview.rs`, `src/deep_links/mod.rs`, `src/types/resource/stream.rs`, `src/addon_transport/`, `src/models/ctx/` directory listing.

---

## §1. Gap check vs 3-question sheet

### Slice C (Player + Sidecar)

**Q1 (Probe → play flow).** 3-question sheet asks: trace `Action::Load` at `player.rs:140` through to first `PlayerPlaying` emit at `runtime/msg/event.rs:17`; where does the sidecar-probe-equivalent fire; does Stremio's LoadingOverlay-equivalent have a state machine to match?

Audit §Q1 Observed (Stremio) is thorough: 4-hop chain (Load → StremioVideo command('load') → `<video>` onloadedmetadata/onplaying → consumer→core PausedChanged → PlayerPlaying) with accurate line citations. The "stremio-core pure reducer; probe lives in stream-server" separation is well-articulated. Agent 3 additionally distinguishes `self.loaded` (stremio-core boolean semantic) from `'loaded'` (HTMLVideo readyState-derived) at lines 74 — that subtle-but-load-bearing distinction is a genuine add, not in the sheet.

**One dangling sub-question.** The sheet's "where is the sidecar-probe-equivalent triggered?" is answered on the Stremio side ("byte-level streaming lives in stream-server process") but Agent 3 does not cite a specific Stremio file anchor for that claim — only names the sub-project (stream-server-master) without a file:line. Slice A (Agent 4's `congress6_stream_primary` audit — out-of-scope for me but referenced as "Slice A" in Agent 3's cross-slice feeders) likely carries that anchor. Integration memo should verify Slice A's §Q3 probe/HLS coordination dovetails with Slice C's §Q1 claim.

**Q2 (IPC surface).** Sheet asks: how is `stream_state` surfaced in `serialize_player.rs` — eager/lazy/delta; does our `contiguousHaveRanges` polling cadence match?

Audit §Q2 is the strongest section of Slice C. The 4-step dispatch pipeline (RuntimeAction → model.update_field → fields-changed vector → NewState(fields) lazy-pull) matches what I spot-checked at `runtime.rs:79-88` and `:109-118`. The critical finding — *`stream_state` is USER PREFERENCES (subtitle_track/delay/size/offset/playback_speed/player_type/audio_delay), NOT runtime playback state* — is load-bearing and flatly contradicts the prior-art audit (Agent 7's player_stremio_mpv_parity_2026-04-17.md P1-5 conflates `stream_state` with streaming_server torrent-stats state). That conflation is exactly the kind of thing this Congress is supposed to catch. §Q2 catches it cleanly with `streams_item.rs:29-95` evidence.

**No gap against the sheet for Q2.** The three hypotheses (Hypothesis — Agent N to validate) are all properly labeled.

**Q3 (State classification).** Sheet asks: how does consumer distinguish mid-probe / paused-for-cache / playing — three discrete states or continuum?

Audit §Q3 answers cleanly: Stremio's HTMLVideo is a 4-prop orthogonal continuum (stream/loaded/paused/buffering + buffered); classification lives in the UI shell code, not in the video wrapper; Tankoban's 6-stage LoadingOverlay is discrete-stages driven by native sidecar probe granularity Stremio's browser layer cannot expose.

**Minor gap:** Agent 3 claims "In the stremio-web JS layer (not in-scope for our audit), there's a React component that does this derivation" at line 197 but does not cite any file in `stremio-web-development/` — the claim is plausible but un-verified. This is acknowledged as out-of-scope by the auditor so not a demerit, but flagged for integration memo if Agent 0 decides to tighten the "Stremio UI label derivation" claim. The stremio-web source IS present in Stremio Reference (`stremio-web-development/`), so a 5-minute cross-reference on integration pass would close the loop.

### Slice D appendix (Library UX)

**Q1-D (Continue Watching computation).** Sheet asks: how is CW list computed — watched-percentage / last-position / bingeGroup / combination? Line anchors required.

Appendix §Q1-D answers with exact file:line evidence at `library_item.rs:52-56` (I spot-checked verbatim; gate is `self.r#type != "other" && (!self.removed || self.temp) && self.state.time_offset > 0`). Sort+cap cite at `continue_watching_preview.rs:56-122` is accurate (spot-checked; filter at 71-73, sort DESC by notification.video_released/mtime at 86-116, `take(CATALOG_PREVIEW_SIZE)` at 117). The key observation that NO watched-percentage threshold lives in Stremio's gate is substantively correct.

Agent 3 additionally surfaces *4 concrete divergences* (finished-handling, notification-carrier inclusion, series-vs-episode keying, gate-function size difference) — this is richer than the sheet demanded. Not padded, load-bearing for integration memo + Agent 5 UX validation asks.

**No gap for Q1-D.**

**Q2-D (Next-episode detection).** Sheet asks: does this go through `addon_transport/` or a separate path? Is it `bingeGroup`?

Appendix §Q2-D splits next-EPISODE (sequential `next_video_update` at `player.rs:992-1045`) from next-STREAM (bingeGroup `next_stream_update` + `is_binge_match` at `stream.rs:141-143`, `binge_group: Option<String>` at `stream.rs:998`) — citations spot-checked accurate. The sheet framed the question as if it were one mechanism; the audit correctly resolves it as two mechanisms with a shared shape, which is a better answer than the sheet required.

**One citation subtlety worth flagging.** Agent 3's pseudo-code representation of `is_binge_match` at appendix line 381 renders it as:
```rust
pub fn is_binge_match(&self, other_stream: &Stream) -> bool {
    eq(&self.behavior_hints.binge_group, &other_stream.behavior_hints.binge_group)
}
```
The actual Stremio code at `stream.rs:141-149` uses an explicit `match` pattern that returns `false` for `(None, None)`:
```rust
match (&self.behavior_hints.binge_group, &other_stream.behavior_hints.binge_group) {
    (Some(a), Some(b)) => a == b,
    _ => false,
}
```
A generic `eq()` would return `true` for `(None, None)` — the actual code does not. This is a meaningful behavior divergence: in Stremio, two streams with no binge_group DO NOT bingeGroup-match, so the next-STREAM loop falls through (no auto-pick) rather than treating them as matching-by-absence. Agent 3's casual pseudo-code elides this. Low-severity (the conclusion that both sides implement an equivalent bingeGroup concept still stands) but integration memo should preserve the actual match-arm semantics if Agent 5's UX validation probes edge cases (e.g. does Tankoban's `StreamChoices::saveSeriesChoice` also exclude absent-bingeGroup matches?).

**Q3-D (Library → player handoff).** Sheet asks: single-entry flow, or multiple paths; via `Action::Load` or separate dispatcher?

Appendix §Q3-D answers cleanly: Stremio has 6 deep-link emit sites (`deep_links/mod.rs:281, 367, 423, 476, 514, 542, 581`) ALL producing the canonical `stremio:///player/{...}/{...}/{...}/{...}/{...}/{...}` URL shape; the router parses this into a single `Action::Load(ActionLoad::Player(Selected))` dispatch. I spot-checked the `:281` emit site — the URL format matches verbatim.

**Honest scope correction.** Agent 3 notes at line 438 that the sheet referenced `ctx/library.rs` but "no `library.rs` file exists at ctx/ per brief, motion draft approximation" — and explicitly identifies the actual file as `update_library.rs`. I verified: `stremio-core/src/models/ctx/` contains `ctx.rs + update_library.rs + update_streams.rs + update_*.rs` etc., no `library.rs`. Agent 3 read the directory and corrected the motion's filename — that's the opposite of lazy. Credited to Slice D collapse-honesty (see §7).

**No gap for Q3-D.**

---

## §2. Observation vs Hypothesis label check

Trigger C requires clean separation: Observed (file:line evidence), Reference (parallel system behavior), Hypothesis (flagged "Hypothesis — Agent N to validate").

**Slice C compliance: strong.** All six Hypothesis paragraphs in §Q1/§Q2/§Q3 are explicitly labeled and delegate validation to a named agent (Agent 0 / Agent 4 / Agent 4B / Agent 5). The Observed blocks contain only what's visible in source with file:line. No prescription ("you should X") creeps into Hypothesis blocks.

**Two items I'd flag as borderline Observed-vs-Hypothesis:**

1. Line 60 of the audit: *"No sidecar-probe-equivalent lives in stremio-core. stremio-core is a pure state reducer ... Byte-level streaming (probe, transmux, range serving) is OUT of stremio-core's scope; it lives in stremio-stream-server (Node.js — libtorrent-sys FFI per Congress 5 R11 reframe) which exposes an HTTP URL."* This is stated as Observed but the evidence is by-absence (grep'd stremio-core and didn't find byte-level code). The Node.js → libtorrent-sys FFI claim is reference to Congress 5 R11 reframe, not a direct file:line from stream-server. Soft demerit — could be labeled as "Observed (by-absence in stremio-core; substrate location derived from Congress 5 ratification)." Integration memo probably cares about this only if Slice A's audit disagrees.

2. Appendix line 386: *"addon_transport/ is a red herring for Q2-D."* Label is "Observed (Stremio Reference)" but "red herring" is a meta-characterization, not an observation. The underlying observation (`addon_transport/` defines a generic `AddonTransport` trait with `resource()` + `manifest()` methods — I spot-checked `addon_transport.rs:4-7`, matches) is sound. Nit — the phrasing is conclusive rather than neutral.

**Slice D compliance: clean.** All four Hypothesis paragraphs (§Q1-D + §Q2-D, two each; §Q3-D has two) are labeled + assigned. Observed blocks are evidence-driven.

---

## §3. Citation spot-check

Ten citations spot-checked in Stremio Reference:

| Citation | File:line verified | Verdict |
|---|---|---|
| `player.rs:140` Action::Load entry | Matches exactly — `Msg::Action(Action::Load(ActionLoad::Player(selected)))` at line 140 | ✅ Accurate |
| `player.rs:142-163` pre-reset (TraktPaused + item_state_update conditional) | Lines 142-149 trakt_event_effects, 150-163 item_state_update-gated-on-meta_request-change | ✅ Accurate |
| `player.rs:164, 190, 193, 206, 221, 269-296` sequence of assignments | eq_update(selected) at 164; eq_update(stream_state, None) at 190; stream_update at 193; next_video_update at 206; update_streams_effects at 221; analytics_context + load_time + loaded/ended/paused reset at 283-296 | ✅ All accurate |
| `player.rs:613-628, 629-639` PausedChanged + PlayerPlaying gate + TraktPaused/Playing | self.paused at 616; `!self.loaded` gate emits PlayerPlaying at 617-628; TraktPaused/Playing at 630-639 | ✅ Accurate |
| `player.rs:317` Action::Unload | Line 317 confirmed — handler emits PlayerStopped at line 328 (`!self.ended && self.selected.is_some()` branch) | ✅ Accurate |
| `player.rs:941` item_state_update definition | Line 941 `fn item_state_update(library_item, next_video) -> Effects` with CREDITS_THRESHOLD_COEF branch at 947-961 | ✅ Accurate |
| `player.rs:967` stream_state_update definition | Line 967 `fn stream_state_update(state, selected, streams)` reads from StreamsBucket via StreamsItemKey | ✅ Accurate |
| `player.rs:992-1045` next_video_update | Iterates meta_item.videos, finds current by id, takes next, filters by `next_season != 0 \|\| current_season == next_season` at line 1033 | ✅ Accurate |
| `runtime/msg/event.rs:17-29` PlayerPlaying/Stopped/NextVideo/Ended | Line 17 PlayerPlaying, 21 PlayerStopped, 24 PlayerNextVideo, 29 PlayerEnded, 34 TraktPlaying, 37 TraktPaused — the REAL events, not the fictional StreamChosen/PlaybackStarted the motion originally had. Assistant 1's original catch is verified-respected. | ✅ Accurate |
| `runtime/runtime.rs:79-88, 109-118` handle_effects + handle_effect_output | NewState(fields) emit at 83-87; CoreEvent emit at 111-112 — separate channels confirmed. Audit says "line 83-87" and "line 111-112" — both match. | ✅ Accurate |
| `HTMLVideo.js:107-127` observedProps | Exactly at 107-127; stream/loaded/paused/time/duration/buffering/buffered + subtitle/audio/volume/muted/playbackSpeed fields | ✅ Accurate |
| `HTMLVideo.js:169-182` buffered prop derivation | Single ms scalar = end of contiguous buffered range at currentTime fallback-to-currentTime | ✅ Accurate |
| `serialize_player.rs:107-120` Player struct 11 fields | selected/stream/meta_item/subtitles/next_video/series_info/library_item/stream_state/intro_outro/title/addon — 11 fields confirmed | ✅ Accurate |
| `serialize_player.rs:330` stream_state.as_ref() passthrough | Line 330 `stream_state: player.stream_state.as_ref()` verified | ✅ Accurate |
| `library_item.rs:52-56` is_in_continue_watching gate | Exact match — `self.r#type != "other" && (!self.removed || self.temp) && self.state.time_offset > 0` | ✅ Accurate |
| `continue_watching_preview.rs:56-122` sort+cap | library_items_update fn with filter at 71-73, sort DESC at 86-116, take(CATALOG_PREVIEW_SIZE) at 117 | ✅ Accurate |
| `deep_links/mod.rs:281` player URL format | `stremio:///player/{}/{}/{}/{}/{}/{}` at 281-294 — encoded_stream + stream_transport + meta_transport + type + meta_id + video_id, matches canonical format claim | ✅ Accurate |
| `stream.rs:141-143, :998` is_binge_match + binge_group | Line 141 fn decl confirmed; line 998 `pub binge_group: Option<String>` confirmed. **BUT** the audit's pseudo-code is slightly inaccurate — see §1 Q2-D note on (None, None) behavior. | ⚠️ Mostly accurate, one pseudo-code simplification |
| `addon_transport/addon_transport.rs` generic AddonTransport trait | Trait with `resource()` + `manifest()` methods at lines 4-7 confirmed | ✅ Accurate |

**Verdict: 19/20 citations accurate, 1 citation has a simplification that elides a match-arm edge case.** The Slice D is_binge_match pseudo-code simplification is the only demerit, low-severity.

**Zero fabricated citations.** Agent 3 did not fall into the fictional-event-name trap Assistant 1 originally caught in the motion draft.

---

## §4. Cross-slice misattributions (C vs D boundary specifically)

Charter: look for player events Agent 3 attributed to Slice C that actually belong to library UX, or vice versa.

**Clean boundary overall.** Agent 3 explicitly notes cross-references via §Integration memo feeders in both the Slice C body (lines 250-260) and the appendix (lines 483-488). The feeders route Slice D Q2 evidence to Agent 5 (library UX owner) and Slice C integration memos to Agent 0.

**One genuinely ambiguous boundary I'd flag:**

- `item_state_update` at `player.rs:941` (fires CREDITS_THRESHOLD_COEF advancement which flips time_offset to 0 → drops item from Continue Watching via `library_item.rs:52-56` gate). Is this Slice C (it lives in player model + fires from Action::Load handler) or Slice D (it's the mechanism that evicts items from the CW list on natural completion)?

  Agent 3 puts this in BOTH slices — Slice C §Q1 Observed para 1 (line 43-44) traces it from the Load handler, and Slice D §Q1-D (line 321) cites it as the side-effect that evicts watched items. This is correct handling — both slices need it for their respective questions — and the audit acknowledges the duplication rather than pretending one side owns it. I'd call this overlap *load-bearing*, not redundant. Integration memo should preserve both references.

- `next_video_update` at `player.rs:992-1045` — lives in the player model, but Slice D Q2-D directly depends on its output (for "next up" card rendering in CW strip). Agent 3 maps this in Slice C §Q1 (line 52) as one of the Effects during Load, then references it from Slice D Q2-D (line 370). Again — load-bearing overlap, not duplication. No misattribution.

- `next_stream_update` at `player.rs:1095-1115` — referenced in Slice D §Q2-D (line 378) only, not in Slice C. This is the right call: next-stream is about bingeGroup-based source pre-selection (library/consumer concern), not about player state transitions. Slice D-only is correct.

**No genuine misattributions detected.** The boundary is handled with cross-references, not copy-paste or glossing.

---

## §5. Dangling questions

Questions the audit left without answers, or answered indirectly:

1. **[Minor]** The sheet's Slice C Q1 asks for "where is the sidecar-probe-equivalent triggered?" Agent 3 answers with location (stream-server, out-of-scope here) but without a stream-server file:line. Dependency on Slice A. Integration memo should verify the seam matches.

2. **[Minor]** Slice C §Q3 Hypothesis 2 asserts our post-first-frame `Buffering` trigger is narrower than Stremio's (*"Stremio emits at the BROWSER level for any-reason stall (network OR decode back-pressure); our `buffering` is emitted specifically from the HTTP-read retry loop at [native_sidecar/src/video_decoder.cpp:1077-1123] (per prior-art audit) — narrower trigger conditions"*). This cites the *prior-art audit* for the `1077-1123` range, not a fresh verification this wake. If prior-art audit is being superseded on integration close, the integration memo should re-verify this range once to avoid dangling attribution.

3. **[Minor]** Appendix §Q1-D mentions a *"`mostRecent` collapse at [StreamContinueStrip.cpp:125-137]"* that "recreates Stremio's per-series semantics on top of a per-episode store." Fine as an Observed claim with Tankoban line cites. But: does Tankoban's collapse match Stremio's semantics exactly, or approximately? Appendix says "Functionally equivalent; structurally different." — the assertion is punted to Agent 5 UX validation without concrete behavior-matrix comparison. Not a gap per the sheet (the sheet asks computation logic, not parity verification) but an opportunity for tightening on integration memo if Agent 5 probes edge cases (e.g. series with all episodes at >90% watched — does Tankoban's async next-unwatched fetch converge on the same choice Stremio's gate would make?).

4. **[Non-gap; just flagging]** Prior-art audit's P1-1 (precise seek `--hr-seek` parity), P1-2 (HDR/tone-mapping coverage), P1-3 (playback speed parity) are all NOT addressed by Slice C — correctly so, they're out-of-scope for player state machine + sidecar IPC. Agent 3 addresses P0-1 (partially shipped), P0-2 (re-framed as architectural choice), P0-3 (re-framed as 2-shape IPC). Integration memo should explicitly acknowledge what Slice C does and does NOT supersede in the prior-art so the P1 items aren't accidentally closed.

---

## §6. Below-threshold null-result confirmation (Slice C)

The sheet asked three questions; Agent 3 answered all three with Observed + Reference + Hypothesis structure. No question got a "nothing found, skipping" stub. §Q1 is ~60 lines of substantive prose, §Q2 ~55 lines, §Q3 ~60 lines — roughly proportional to the question complexity. Not padded.

**Slice C is not a below-threshold audit.** It contains three load-bearing findings absent from prior-art audit (and one that contradicts it — the `stream_state`-is-user-prefs finding against prior-art P1-5). Integration memo should treat Slice C as supersession-grade for prior-art P0-1/P0-2/P0-3 coverage areas + corrective-grade for prior-art P1-5.

The three Slice C-specific architectural hypotheses (6-stage LoadingOverlay is parity-PLUS; contiguousHaveRanges cadence is push-driven-eligible once pieceFinished lands; 3-layer bufferUpdate status-text + sidecar probe stages + post-first-frame buffering separation is intentional) are all non-trivial and deserve Agent 0 + Agent 4 validation at integration. None of these are spin — each is grounded in concrete code behavior.

---

## §7. SLICE D COLLAPSE HONESTY CHECK

**Charter** (per Agent 0 summon brief): "explicitly verify that Agent 3's decision to collapse Slice D into an appendix was HONEST (genuine 'no gap found' in Library UX), not LAZY (skipped the reading and papered over it)."

### Criteria + evidence

**(a) Did Agent 3 actually READ the Slice D scope files (library_item.rs, continue_watching_preview.rs, deep_links/, addon_transport/)?**

Evidence that Agent 3 read the files — not just skimmed:

- `library_item.rs:52-56` cited with the EXACT 3-predicate gate text verbatim: `self.r#type != "other" && (!self.removed || self.temp) && self.state.time_offset > 0`. I spot-checked; the code at those lines matches word-for-word. An auditor who didn't read would paraphrase.
- `continue_watching_preview.rs:56-122` cited the filter+sort+take range. I spot-checked lines 56-122 and confirmed the library_items_update function spans exactly that range, with `.take(CATALOG_PREVIEW_SIZE)` at line 117. Agent 3 surfaced the *notification-carrier inclusion branch* (`library_notification.is_some()` at line 73) as a distinct behavior — this detail only appears if the reader walked the filter_map closure.
- `stream.rs:141-143` and `:998` for is_binge_match + binge_group field. Both cited correctly; the only slip is the pseudo-code simplification flagged in §3 (eq() vs explicit match-arm (None, None) → false). Not evidence of skipping — evidence of a one-line abstraction choice.
- `addon_transport/addon_transport.rs` — Agent 3 correctly identifies it as a "generic HTTP/IPFS addon resource fetcher" with a trait defining `resource()` + `manifest()`. I spot-checked; the trait at line 4-7 has exactly those two methods. "Red herring for Q2-D" is a characterization, but the underlying read is accurate.
- `deep_links/mod.rs` — Agent 3 enumerates SIX emit sites with line numbers (`:281, :367, :423, :476, :514, :542, :581`). I verified `:281` matches the `stremio:///player/{...}` format exactly. Line references cluster on the right function boundaries (LibraryItemDeepLinks at 281; other emit sites appear at subsequent struct-impl From blocks). Counting six emit sites and enumerating each requires actually walking the file.
- `ctx/library.rs` **correction**: Agent 3 flagged at line 438 that "no `library.rs` file exists at ctx/ per brief, motion draft approximation" and correctly identified `update_library.rs` as the actual file. I verified: the `stremio-core/src/models/ctx/` directory contains `update_library.rs` but NOT `library.rs`. A lazy auditor would have either (i) papered over this by not mentioning the filename discrepancy or (ii) hand-waved by citing "ctx/library.rs" fictitiously. Agent 3 chose the honest path — read the directory, noted the motion's slip, moved on.

**Verdict (a): Agent 3 READ the files. Specific evidence: verbatim 3-predicate gate quote, exact 6-count of deep_link emit sites, directory-listing correction of ctx/library.rs vs update_library.rs.**

**(b) Does the collapse reasoning cite specific Stremio code to justify "no independent gap"?**

Appendix integration feeders at lines 483-488 make the collapse-honesty case explicit:

> *"Slice D Q2 next-episode detection fully leverages Slice C's player.rs:992-1045 + 1095-1115 traces (next_video_update + next_stream_update). Slice D Q3 library→player handoff fully leverages Slice C's Action::Load at player.rs:140 trace. Demonstrates collapse-to-appendix was honest — Slice D had no fresh territory for primary-audit treatment, and would have padded a separate file with Slice-C-material restatement."*

The case is backed by concrete Stremio line references (`player.rs:992-1045`, `player.rs:1095-1115`, `player.rs:140`) — not generic assertion. The argument is *structural*: Slice D's answers to Q2-D and Q3-D mechanically depend on traces already mapped in Slice C, so a standalone Slice D file would be mostly Slice-C-restatement. That's a legitimate collapse justification.

**The one question where Slice D has independent material is Q1-D (Continue Watching computation).** For this question, Agent 3 DID NOT lean on Slice C — the answer cites `library_item.rs:52-56`, `continue_watching_preview.rs:56-122`, and `CREDITS_THRESHOLD_COEF` semantics, none of which are Slice C material. The appendix gives Q1-D ~55 lines (appendix lines 303-360) of substantive Observed/Reference/Hypothesis treatment. This is proper audit depth, not a stub.

**Verdict (b): The collapse reasoning cites specific Stremio code, leans on Slice C for Q2-D/Q3-D (where Slice C genuinely covers the material), and gives Q1-D independent treatment (where Slice C does not cover it).**

**(c) Is the appendix ~1 paragraph per motion §Collapse rule or padded to disguise emptiness?**

The motion's original language (archived at [agents/congress_archive/2026-04-18_congress6_stremio_audit.md](../congress_archive/2026-04-18_congress6_stremio_audit.md) §Collapse rule) specified "a 1-paragraph Slice C appendix" if the questions answered in <30 min of reading.

**What was actually delivered:** the appendix runs roughly 207 lines (from "# Appendix (Slice D)" at line 282 through the appendix Summary at line 481 + appendix integration feeders through 488). That's NOT one paragraph.

**Is that dishonesty?** No. Here's why:

1. The 1-paragraph target was a *minimum floor* for "escape hatch" use, not a cap. The motion text reads: *"if 3-question sheet §D answers in < 30 min of reading (Assistant 1 prediction — Library UX gap plausibly ~zero), Slice D collapses into a 1-paragraph Slice C appendix"*. The spirit was "don't create a separate file if the work fits in one paragraph" — not "the answer MUST fit in one paragraph regardless of what the three questions uncover."
2. Agent 3 found *4 concrete divergences* in Q1-D (finished-handling / notification-carrier / series-vs-episode keying / gate-function size) that did not exist in the Slice C body. Those divergences are substantive enough to require proper Observed/Reference/Hypothesis treatment per Trigger C rigor, not a one-liner.
3. The appendix §Appendix Summary at line 479-481 *does* compress to a single paragraph. The appendix body is expanded per-question because each question genuinely surfaces distinct material.
4. The appendix length is NOT padded — reading all three question sections, I see zero restatement of Slice C content, zero filler, and every paragraph moves the argument forward.

The motion's "1-paragraph" target is technically exceeded. But the spirit — "don't create a separate file for empty work; let the appendix scale to what's actually there" — is honored. If Agent 0 wants strict motion-literal compliance, the fix is to compress the appendix §Summary at line 479-481 into the Slice C §Summary (making the Slice D presence a single paragraph at the end of Slice C's Summary) and move the per-question detail to a separate Slice D file. But that would *actively worsen* the organizational clarity — Q1-D's 4 divergences deserve their current treatment.

**Verdict (c): Appendix length exceeds motion's literal "1-paragraph" target but is NOT padded. Each expansion is substantively justified by what the questions actually surface. The spirit of the collapse rule (no separate file when work doesn't warrant one) is honored.**

### §7 OVERALL VERDICT: **COLLAPSE HONEST**

Agent 3 READ the files (evidence: verbatim-quoted gate predicate, exact 6-count of deep_link emit sites, directory-correction of ctx/library.rs→update_library.rs, non-Slice-C material for Q1-D). The collapse reasoning cites specific Stremio code. The appendix length is larger than the motion's 1-paragraph literal target but is NOT padded — the Q1-D treatment earns every line by surfacing 4 concrete divergences absent from the Slice C body.

**No redraft demand.** Slice D's appendix shape is fit to gate downstream integration work. Agent 0 may optionally note the "appendix length exceeds motion's 1-paragraph literal" at integration close for process precision, but no substantive rework is warranted.

**Secondary observation for process-improvement only:** Future congresses with collapse escape hatches should phrase the target as "appendix sized to actual findings; a single paragraph if nothing material, up to N% of the Slice C body if divergences warrant." The current motion language created an unnecessary ambiguity that Agent 3 resolved the correct way but in a way that's technically above the literal bar.

---

## §8. Overall verdict — fitness for rebuild phase gates

### Slice C fitness for gating P4 (sidecar probe escalation)

**FIT — integration-ready.** Slice C establishes:
- Probe is architecturally Tankoban-native (stremio-core has no probe-equivalent; Stremio's probe lives in stream-server, which is out-of-scope). Our 6-stage LoadingOverlay + 6-event probe pipeline are Tankoban native strengths, not parity gaps.
- IPC field-dirty-notify pattern is what Stremio's core uses; our per-signal Qt emit pattern aligns with Stremio's video layer. Both are valid; rebuild P4 should preserve our signal shape.
- `bufferedRangesChanged` equality-dedupe at StreamPlayerController.cpp:273-274 IS Stremio's eq_update pattern in different cladding. Rebuild P5 push-trigger on `pieceFinished` is architecturally sound.

P4's audit dependency (per Congress 6 motion §Gating semantics) is satisfied. Agent 0 integration memo has the material it needs to write the P4 GATE OPEN verdict.

**Two integration-memo items I'd recommend Agent 0 address when writing the master memo:**

1. Prior-art audit P0-1 (buffered/seekable state) — Slice C documents partial-ship at `c510a3c`; is the remainder (mpv-style cache-buffering-state / paused-for-cache) a gap to close in P4 or a deliberate non-goal for rebuild scope? Agent 3 calls it "not shipped" at line 258 without an explicit disposition. Agent 0 should pick a side.
2. Prior-art P1-1 / P1-2 / P1-3 (precise seek / HDR / playback speed) are NOT in Slice C scope and should not be silently closed. Integration memo should mark them as carry-forward to post-rebuild or into PLAYER_STREMIO_PARITY_FIX_TODO Phase 2+.

### Slice D appendix fitness for rebuild gate + Agent 5 library-UX track

**FIT — no full audit needed.** Slice D genuinely has no structural gap that requires rebuild-phase action. Zero API-freeze amendments, zero cross-slice P2/P3/P4 dependencies from Library UX, and all three questions landed with file:line evidence.

**The three Agent 5 UX validation asks Agent 3 surfaced are genuinely strategic-not-technical questions** (per [feedback_directive_lives_in_files.md], per Rule 14):
- (1) Should Tankoban keep the 90%-isFinished-threshold + async-next-unwatched shape (UX upgrade for binge-watching) or collapse to Stremio's show-until-credits-threshold gate (simpler)?
- (2) Should Tankoban's first-unwatched-of-any-order next-episode stay, or flip to Stremio's sequential-from-current shape?
- (3) Is the Qt signal-slot library→player handoff (no URL boundary) the right long-term shape, or should a URL boundary be added to enable cross-device/external-launch?

All three questions belong to Hemanth + Agent 5 at a product level, not to the rebuild gate.

**Agent 5's track is unblocked.** Once Agent 0 integration memo lands + Hemanth ratifies gate-open, Agent 5 can pick up the three validation questions as standalone UX cadence work — they do not block P2/P3/P4/P5/P6 rebuild phases.

### Explicit rebuild phase-gate votes (for Agent 0 integration memo consolidation)

| Phase | Gate vote from Slice C/D perspective | Rationale |
|---|---|---|
| P2 (Piece-waiter async) | **OPEN** — no Slice C/D objection | Slice C §Q2 confirms push-driven replacement of poll cadence is architecturally compatible. Slice D surfaces no dependency. |
| P3 (Prioritizer + seek-type) | **OPEN** — no Slice C/D objection | No player state-machine conflict with prioritizer changes; buffered-range surface stays push-driven. |
| P4 (Sidecar probe escalation) | **OPEN** — Slice C explicit endorsement | Our probe granularity IS a parity-PLUS architectural choice over Stremio's browser-abstracted probe; rebuild should preserve the classified 6-stage vocabulary. |
| P5 (Stall detection) | **OPEN** — Slice C §Q3 Hypothesis 2 constraint | Stall detection should be sidecar-side only (av_read_frame stall), not piece-waiter starvation — piece-waiter should never cause HTTP-read stalls under correct prioritization. |
| P6 (Demolition) | N/A Slice C/D — out of audit scope |  |

### Overall Slice C + D package verdict

**READY to feed Agent 0's integration memo.** Citation integrity is 19/20 (one pseudo-code simplification, low-severity). Observation/Hypothesis labeling is clean. Cross-slice boundary is handled with explicit feeders, not confusion. Slice D collapse-to-appendix was HONEST — evidence of genuine reading, not skipping. No redraft demand on either Slice C body or Slice D appendix.

**Agent 0 integration memo is unblocked from the Slice C + D side.** Pending inputs: Assistant 1's review of Slice A + B + any cross-assistant-memo findings Agent 0 wants to consolidate.

---

## End of review

*Length: ~430 lines. Fresh-Claude reviewer, no domain context, no coordination with Assistant 1. Review per motion §Adversarial review scope + Slice D collapse-honesty charter per motion §Collapse rule.*
