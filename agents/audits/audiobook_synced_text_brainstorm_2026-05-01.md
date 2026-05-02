# Audiobook + Synced-Text Player — Feasibility Brainstorm

**Author:** Agent 2 (Book Reader)
**Date:** 2026-05-01
**Driver:** Hemanth's brief — *"a new feature, an audiobook player with synced text from the paired epub … I want to implement as something separate from the epub reader itself."*
**Output type:** Brainstorm memo (NOT a fix-TODO yet). Gates downstream fix-TODO authoring.
**Skill discipline:** `/superpowers:brainstorming` primary; `/superpowers:verification-before-completion` on cited file:line claims (foliate `MediaOverlay` class verified at `resources/book_reader/vendor/foliate/epub.js:394`, dormancy verified by zero non-vendor consumers, existing audiobook player verified at `reader_audiobook.js:580 LOC`).

---

## 0. Three load-bearing facts that frame the rest

These are verified against the working tree, not assumed:

1. **foliate-js (already vendored) ships full EPUB 3 Media Overlays support.** `resources/book_reader/vendor/foliate/epub.js:394` defines `class MediaOverlay extends EventTarget` with `start(sectionIndex)`, internal SMIL parser at `#loadSMIL()`, per-`<par>` cue table (`{src, begin, end, text}[]`), and dispatches `highlight` / `unhighlight` `CustomEvent`s as the `<audio>` element's `timeupdate` crosses cue boundaries. `getMediaOverlay()` at line 1041 hands back an instance. The capability is a free byproduct of using foliate as our reader engine — and it is **currently dormant** (zero non-vendor `MediaOverlay` references anywhere in `resources/book_reader/` or `src/`).

2. **The existing in-reader audiobook player (`reader_audiobook.js`, 580 LOC) is plain transport with chapter-level manual pairing.** No text-cue table, no SMIL, no per-phrase highlight. It addresses a different workflow ("I want to read a book while a separate audiobook narrates, with manual transport control"). The companion `reader_audiobook_pairing.js` (444 LOC) maps book TOC chapter href → audiobook chapter index, and that is the deepest sync resolution. Phrase-level / sentence-level / word-level sync was never on the table for that arc.

3. **Hemanth's "separate feature" framing is technically and architecturally accurate.** The synced-text player is a different artifact, different data model, different UI surface from the in-reader audio sidebar. Conflating them would force the EPUB reader's runtime to host two different timing models.

---

## 1. Relationship to existing AUDIOBOOK_PAIRED_READING work

| | AUDIOBOOK_PAIRED_READING (existing) | Synced-Text Player (new) |
|---|---|---|
| **Workflow** | User opens an EPUB in BookReader; sidebar Audio tab plays a separately-acquired audiobook; user manually pages + transports | User opens a synced-text artifact; reader and audio advance together automatically; user reads OR listens; the other follows |
| **Sync resolution** | Chapter-level (book TOC ↔ audiobook chapter index), manual user transport | Phrase-level (often sub-sentence — clipBegin/clipEnd on `<par>` elements, typical 2–8 sec resolution) |
| **Source of timings** | None. User aligns visually by reading along. | EPUB 3 Media Overlays (SMIL) embedded in the EPUB itself, OR a sidecar timing artifact produced by import-time alignment |
| **Surface** | Sidebar tab inside the existing EPUB reader | New, dedicated player surface — not inside the EPUB reader |
| **State of work today** | Phase 1 shipped + smoked, Phase 2 compile-only, Phase 3+4 not started — see `AUDIOBOOK_PAIRED_READING_FIX_TODO.md` (root) | Brainstorm only |
| **What user supplies** | Two unrelated files (EPUB + audiobook folder), pairs them per book | A single synced artifact (EPUB 3 with Media Overlays), or eventually an aligned (EPUB, audio) pair produced at import |

**Honest assessment:** these are **parallel features**, not alternatives. Both can ship and coexist:

- Paired-reading covers the long-tail case of "I have any EPUB and any audiobook, I want them in the same window with manual sync."
- Synced-text covers the short-tail case of "I have a media-overlay-aware artifact (or one we produced at import), I want hands-off lockstep playback."

**Tradeoff to name explicitly:** there is moderate UI-IA cost in having two audiobook-shaped surfaces inside Books. Hemanth's minimalism stance (`feedback_player_minimalism_pattern.md`) asks us to be careful about adding surfaces. Mitigation options surfaced in §4.

**Recommendation:** keep both. Do not retire AUDIOBOOK_PAIRED_READING — it covers a real user workflow that the synced-text player cannot, because most pirated/free-tier audiobooks come without timings.

---

## 2. Where alignment timings come from — three structural shapes

### Tier A — BYO pre-synced files (EPUB 3 Media Overlays)

**The format.** EPUB 3.x defines [Media Overlays](https://www.w3.org/TR/epub-33/#sec-media-overlays-overview) as a SMIL 3.0 subset. Each `<par>` element pairs a text fragment (CSS-selector or fragment ID inside the XHTML content) with an audio clip and `clipBegin`/`clipEnd` timings. One audio file can host many `<par>`s; one EPUB can have many audio files (typically one per chapter section). It is the official, durable, portable standard for synced text↔audio in the EPUB ecosystem.

**Tankoban work to make this play.** Near-trivial. foliate-js parses the SMIL, builds the cue table, manages playback timing, dispatches `highlight` events on cue entry. We need:
- A new player surface that mounts foliate against the synced EPUB.
- A listener for `highlight`/`unhighlight` to apply visual treatment (CSS class on the cued element) + auto-scroll the cued element into view.
- A transport bar (play/pause/seek/speed/volume — same controls as `reader_audiobook.js`) that proxies to the foliate-managed `<audio>`.
- Persistence (book id, last cue, last position).

Estimated effort: **1 wake for a working v1, 2 wakes for polish.**

**User work to make this play.** User must supply EPUB 3 files with Media Overlays. These exist in the wild but are **rare**:

- **Where they exist:** Voyager & specialized EPUB 3 distributors (e.g. some kids' read-along books); Daisy/EPUB-Audio-Book accessibility distributors (Bookshare, Learning Ally — paywalled); some audiobook+ebook bundle SKUs from Audible/Apple Books re-authored as synced EPUBs by power users; hand-authored Standard Ebooks (no, they don't currently embed Media Overlays — checked).
- **Where they don't:** essentially all mainstream commercial audiobooks (Audible, Libby, etc.) ship audio-only or audio + ebook-as-separate-file. The two products are not aligned.

So Tier A only is **a "feature for power users who already have the artifact"**, not a feature for "any audiobook + any EPUB."

### Tier B — Sync at import (run alignment once on an EPUB+audio pair)

**The shape.** User supplies an EPUB and a folder of audio files claimed to narrate it. Tankoban runs once, produces timings, persists them. Subsequent playback is cheap (plays back the cached cues exactly like Tier A).

**The work involved.** Two technical pieces, both nontrivial under the offline-only / no-Python constraint of `project_backend.md`:

1. **Speech-to-text on the audiobook audio.** Produces a transcript with per-word timings. Needs an on-device ASR engine. Concrete options that ship as native libraries (no Python):
    - `whisper.cpp` — C++ port of OpenAI Whisper. Models from `tiny.en` (~75MB, ~5x realtime on CPU) up to `medium.en` (~1.5GB, ~0.5x realtime). License: MIT. **Most plausible candidate.**
    - `vosk` — small offline ASR (~50MB). Word-level timings. Lower accuracy than Whisper. License: Apache 2.0.
    - `sherpa-onnx` — already linked into Tankoban (per the `MakeMpvSolo` build cost line citing it). License: Apache 2.0. Lower-quality models than Whisper but already a dep we have.
    - The existing `ffmpeg_sidecar` cannot do this — it's decode/probe only.

2. **Forced alignment between transcript and ground-truth EPUB text.** Algorithms in this space: dynamic-time-warping over word sequences, CTC-segmentation, Needleman-Wunsch over fuzzy-matched tokens. Open-source C/C++ packages: `aeneas` (Python, ruled out), [`kaldi-aligner`](https://github.com/kaldi-asr/kaldi) (heavy), or **a hand-rolled aligner is maybe ~500 LOC of C++ for our use** because we don't need state-of-the-art — we need "snap to nearest sentence boundary in the EPUB content." Once we have ASR with word timings + EPUB text serialized in reading order, the alignment is a fuzzy diff problem at sentence granularity.

**Costs honestly.**

- **Binary size:** +75–500MB depending on model size. ~75MB if we accept `tiny.en` quality; ~150MB for `base.en` (the practical sweet spot); >500MB for `small.en+`. The Tankoban shipped binary today is ~120MB; adding 75–150MB roughly doubles installer size. NSIS installer (REPO_HYGIENE Phase 6) cares about this.
- **CPU on import:** Whisper.cpp `base.en` at ~1x realtime on a 2-core laptop. A 15-hour audiobook = ~15 hours of import alignment, run once. Background-thread, progress-overlay, resumable. Acceptable if it's a one-time cost per book.
- **RAM during import:** ~1GB for `base.en` model loaded.
- **Failure modes:** abridged audiobooks vs unabridged EPUB will produce huge fuzzy-alignment gaps. Multiple-narrator audiobooks (full-cast productions) confuse word-level ASR. Heavy accents / foreign-language passages drop accuracy locally.

**Estimated effort if pursued.** ~5–8 wakes for a working Tier B. Mostly: ASR engine integration + alignment algorithm + import UI + cache invalidation + edge-case handling for abridgement.

### Tier C — Runtime alignment

Ruled out. Live ASR + forced alignment during playback would mean: spinning up Whisper on every audio chunk, doing a fuzzy diff against the next ~paragraph of EPUB text, all while feeding `<audio>` smoothly. The CPU cost during playback (1x realtime ASR running while audio also plays) eats the laptop's thermals. The architecture is wrong — Tier B does the same work once at import, then plays back cheap forever.

---

## 3. Storage shape — Rule 14 calls

- **Tier A.** No new persistence. Timings live inside the EPUB 3 file itself (SMIL files referenced from the OPF manifest, audio files inside the package or as remote URLs). foliate-js reads them at section-load time. The only Tankoban-side persistence is "user's last cue + position in this synced EPUB" — same shape as `books_progress.json` extended with `lastCueId`, kept in **the existing `books_progress.json`** under the same SHA1-keyed contract. No new file.

- **Tier B.** Two viable shapes, picked by Rule 14:
    1. **Sidecar JSON next to the audio folder** — `<audiobook_folder>/.synctext.json` with `{schemaVersion, epubPath, cues: [{audioFile, startMs, endMs, textCfi}, ...], updatedAt}`. Symmetric with `AudiobookMetaCache`'s `.audiobook_meta.json` pattern. Easy to delete-and-redo. Bound to one audiobook+epub pair; portable across machines as long as both files travel together.
    2. **Re-author an EPUB 3 with Media Overlays** — produce a new `.epub` file at import time with embedded SMIL referencing the audio. **More portable** (single artifact ships everywhere; reads on any EPUB 3 reader), but **destructive** (changes the user's EPUB) and complex to author (we'd be writing valid EPUB OPF + SMIL on import).

  **My Rule-14 call:** start with shape (1) sidecar JSON. Re-authoring an EPUB at import is a v2 nice-to-have if Hemanth ever wants to share synced packs across machines.

- **What I rejected.** SQLite blob. We'd be inventing a new persistence channel for one feature. JsonStore is the brotherhood convention (`feedback_quality_standard.md` + `project_audit_fix_flow.md` flow inherits it). No SQLite.

---

## 4. UI shape

**The "separate feature, separate surface" framing locks out:** putting this as a flag inside `reader_core.js`. So the question is *where* the surface lives. Three options:

### Option 4-α — Top-level tile in BooksPage's "AUDIOBOOKS" row, distinct icon-badge

Synced-text books appear in the same audiobook row as plain audiobooks, but with a small badge ("synced text" or a sound-wave-over-page icon) on the tile. Click → opens the new player (NOT the EPUB reader, NOT the AudiobookDetailView). Rest of BooksPage layout unchanged.

Pros: zero new IA. User sees the artifact in the place they expect.
Cons: if a user has both a synced-text version AND a plain-EPUB version of the same book, they get two tiles for the same content. Minor.

### Option 4-β — A new "Read-along" row on BooksPage, separate from "AUDIOBOOKS" and "BOOKS"

Promotes synced-text content visually; user can see at a glance which artifacts support it.

Pros: clear discoverability.
Cons: another row on Books, mild violation of `feedback_player_minimalism_pattern.md`.

### Option 4-γ — Treat as a play-mode of the EPUB tile

The EPUB tile gets a "Read along" entry in its right-click menu / detail view, available only when the EPUB has Media Overlays. Clicking it opens the synced-text player; clicking the tile itself still opens the regular EPUB reader.

Pros: zero new top-level surface. Discoverability via context menu / detail.
Cons: the feature is only discoverable if the user knows to right-click. Less prominent.

**My Rule-14 call:** **Option 4-α** for v1. Lowest IA cost; most natural for a power-user feature; aligns with how `AudiobookDetailView` already routes from a tile click.

### Player chrome itself

Independent of where it's launched from, the player surface looks like:

- **Top:** book metadata (title / cover / total runtime / progress %), back-to-library button.
- **Center / dominant area:** the EPUB content rendered via foliate, with the active cue's `<span>` highlighted (CSS class injected by us in response to foliate's `highlight` event). Auto-scroll to keep the cue in view.
- **Bottom transport:** Play/Pause • -15s • +15s • Prev sec • Next sec • seek slider (per-section or global) • speed (0.75 / 1.0 / 1.25 / 1.5 / 2.0) • volume slider. Mirrors `reader_audiobook.js`'s transport bar verbatim — same pixels, same icons, same timing — to reuse muscle memory.
- **Optional left-rail or bottom-tab:** chapter list / TOC. Click jumps audio + text to the start of that section.

### Visual sync model — three modes (pick one for v1)

- **Phrase highlight in place** (foliate's default behavior). The cued span gets a CSS background/border-bottom; the rest of the page is ordinary. User reads naturally, eyes drawn to the highlight.
- **Karaoke-style auto-scroll** — the cued span is centered vertically and held there; text scrolls under it. More "kinetic"; can feel jumpy at sentence boundaries.
- **Both** — toggleable. More complexity for a v1.

**My Rule-14 call:** **phrase highlight in place** for v1. It's foliate's default, it's the EPUB 3 Media Overlays standard's intended UX, and it doesn't fight the user's eye if they want to read ahead or look back. Karaoke-mode is v2 if Hemanth misses it.

### Reading-position auto-recall on pause / resume

On pause: persist `{cueId, audioPosMs, sectionIndex}`. On resume: foliate's `start(sectionIndex)` + a manual `seekToCue(cueId)` reconstruct exactly. Standard. Same shape as `reader_audiobook.js` lines 89–94 already do for the in-reader player.

---

## 5. Pairing UX

**Tier A.** Pairing is implicit in the artifact — the EPUB 3 file *contains* both the text and the audio (or audio URLs). Nothing to pair manually. User opens the file, it Just Works.

**Tier B.** Explicit pair at import. Three plausible flows:

- **A. "Sync now" button on AudiobookDetailView,** when the audiobook is paired with a book via the existing AUDIOBOOK_PAIRED_READING flow → triggers the import-time aligner → produces sidecar timings → adds a "Read along" entry next to "In-reader only".
- **B. New "Add synced read-along…" import dialog** from Books library → user picks EPUB + audio folder → align → produce artifact.
- **C. Filename heuristic** — if `Book Title.epub` and `Book Title/` (audio folder) share a stem, auto-suggest pairing on scan.

**My Rule-14 call:** if/when we ship Tier B, start with **A** because it reuses the AUDIOBOOK_PAIRED_READING infra. C is a v2 polish.

---

## 6. Format and edge cases

| Edge case | Tier A behavior | Tier B behavior |
|---|---|---|
| **Multi-file audiobook (1 EPUB, 30 mp3s)** | Native EPUB 3 Media Overlays handles this. Each `<par>` references its own audio src. Works. | Aligner runs per-file, concatenates results. |
| **EPUB without clean chapter anchors** | Tier A's SMIL points at fragment IDs / CSS selectors regardless of TOC. Works. | Aligner's job. Mostly OK if EPUB's reading order is contiguous prose. |
| **Mostly-image EPUB (graphic novels)** | **Out of scope.** Image-only EPUBs have no text spans to highlight. Use the regular comic reader. | Same. |
| **External SRT / SBV transcript files (separate from EPUB)** | **Out of scope** for v1. Fundamentally a different artifact (timestamped subtitle file aligned to audio, not text-fragment aligned). Could be a future Tier D — load SRT, render as karaoke captions over audio, no EPUB at all. **Defer.** |
| **Abridged audiobook + unabridged EPUB** | Tier A: source publisher made the choice; user gets what's authored. | Tier B: alignment will gap-out where text is missing. Need graceful "no cue here" rendering. |
| **Multi-language EPUB (e.g. footnotes in another language)** | Tier A: per-`<par>` works the same regardless. | Tier B: ASR accuracy drops on language switch; handle as gap. |
| **Audio-only audiobooks with no EPUB at all** | Out of scope; this is the existing AUDIOBOOK_PAIRED_READING domain — paired-reading mode without text-sync, manual transport. |

---

## 7. Failure modes and recovery

- **Drift between audio and highlighted text.** Tier A: drift only on bad source data — `clipBegin`/`clipEnd` are exact. Tier B: drift if alignment was sloppy (especially abridged content). Recovery: a small "nudge" UI control (`< 250ms` / `> 250ms`) that adjusts a per-book offset, persisted with progress. **No auto-correction loop** — too easy to over-engineer and confuse users.
- **User scrubs audio mid-playback.** `<audio>.timeupdate` fires; foliate's MediaOverlay listener walks the cue table to find the active span and re-binds. Already handled inside foliate's MediaOverlay class — verified at `epub.js:458-472`.
- **User clicks a different paragraph.** v1 behavior: ignore (no text-driven seek-to-audio). v2 polish: clicking a span seeks audio to that span's `clipBegin`. Reasonable v2 nice-to-have. **Defer.**
- **No cue at the user's current text position (gap region).** Render the page normally, no highlight. Audio plays through. When a cue arrives again, highlight resumes. **No "trying to recover" overlay** — silent gap is the correct UX.
- **Audio file fails to load (file moved, network blob 404).** Player shows a clean error state, a Retry button, falls back to text-only reading mode. No death-spiral of retries.
- **Speed > 2x and cue boundaries blur visually.** Live limitation. Cue rendering caps at a redraw rate the eye can follow (~10 Hz). At 3x speed, highlight lag is visible. v1 caps speed dropdown at 2x to avoid this.

---

## 8. Scope ladder for v1

In wake-count order, smallest shippable slice → full vision:

| Tier | Scope | Wakes | Ships? |
|---|---|---|---|
| **A0** | Wire foliate's MediaOverlay → render highlight in the existing EPUB reader as a behind-flag mode (no new surface). Just verifies the technology end-to-end on a real EPUB 3 Media Overlay file. | 0.5 | Internal smoke only |
| **A1** | Separate Synced-Text player surface (per §4 Option 4-α). Phrase highlight in place. Transport bar matching `reader_audiobook.js`. Persistence (cueId + position). Tile badge on BooksPage. | 1–2 | **Recommended v1.** |
| **A1+** | A1 + per-book speed/volume defaults. Karaoke-mode toggle. Click-span-to-seek-audio (reverse direction). | +1 | v1.1 polish. |
| **B1** | A1 + Tier B import-time alignment for arbitrary EPUB+audio pairs via Whisper.cpp / sherpa-onnx + hand-rolled fuzzy aligner. Sidecar `.synctext.json`. Import dialog. | +5–8 | v2. |
| **B1+** | B1 + re-author-as-EPUB-3-with-Media-Overlays at export time (portable artifact). | +2–3 | v3. |
| **C** | Runtime alignment without precompute. | — | **NOT pursued.** |
| **D** | SRT / SBV / VTT support without EPUB at all (audio + standalone transcript). | — | Future, separate brainstorm. |

---

## 9. Recommended path (Rule 14, my call, defended)

**v1 = Tier A1.** Build the new Synced-Text player surface against foliate's already-vendored MediaOverlay class. One artifact format (EPUB 3 with Media Overlays). One UI surface. Phrase-level highlight in place. Standards-compliant. ~1–2 wakes.

**Reasoning.**

- The capability is *already present* in our toolchain — using it is engineering hygiene, not new architecture. We are turning on a foliate feature that's been sitting there since the day we vendored it.
- The artifact format is the **published EPUB 3 standard**. No proprietary file shape, no future migration debt.
- Risk is bounded — every failure mode (drift, audio-load-fail, gaps, speed) has a known straightforward recovery.
- Ships fast. Hemanth gets to use the feature, sees what content he can actually source, and decides whether Tier B (which is order-of-magnitude more work) is worth doing.

**Honest tradeoff.** Tier A's user-facing utility depends on Hemanth being able to get hands on EPUB 3 Media Overlay files. If he can't source any in practice, the feature ships and never gets used. He should know this before we start.

**Mitigation.** As part of v1 ship, include a 1-page "where to find synced EPUBs" doc (Voyager titles, accessibility distributors, manual authoring with [`pandoc-epub-mo`](https://github.com/jccr/pandoc-epub-mo) / similar tooling). If after using v1 he finds himself sourcing EPUB+audio pairs separately and wishing they synced, we revisit Tier B.

**v2 trigger condition (not committed).** If after using v1 for a few weeks Hemanth has at least 3 (EPUB, audiobook) pairs he wishes were synced and isn't satisfied with the existing AUDIOBOOK_PAIRED_READING manual-sync workflow, **then** Tier B (sync at import, Whisper.cpp + alignment) becomes a brainstorm of its own.

---

## 10. Ratification questions for Hemanth

Strict product/strategic only — no technical questions. (Per Rule 14 / `feedback_decision_authority.md`.)

1. **v1 = Tier A only?** Confirm we ship the EPUB 3 Media Overlays player as v1 and treat Tier B (sync-at-import) as a future-conditional. Lets us ship in 1–2 wakes; commits us to a "user supplies the synced artifact" model for v1.

2. **UI placement (§4).** Top-level tile under BooksPage with synced-text badge (Option 4-α) vs. promoting to its own row (4-β) vs. context-menu entry on EPUB tile (4-γ). My pick: 4-α.

3. **Visual sync model.** Phrase highlight in place (the standard) vs. karaoke-mode auto-scroll vs. toggleable. My pick: phrase highlight only for v1.

4. **Coexistence with AUDIOBOOK_PAIRED_READING.** Confirm both features ship and live side-by-side (the existing arc continues; this is additive), vs. retire AUDIOBOOK_PAIRED_READING in favor of synced-text. My pick: keep both — they cover different content shapes.

5. **Out-of-scope confirmation.** Confirm SRT-only / VTT-only / standalone-transcript modes (Tier D) are **not in this brainstorm's scope** and are a separate future brainstorm if ever needed.

6. **AUDIOBOOK_PAIRED_READING resume status.** Should I (Agent 2) resume the existing arc (Phase 2 MCP smoke + Phase 3 + Phase 4 — see `AUDIOBOOK_PAIRED_READING_FIX_TODO.md`) **before** starting the Synced-Text fix-TODO authoring, or **defer** the existing arc until Synced-Text v1 ships? My pick: synced-text first, since it's smaller and may inform UI patterns that AUDIOBOOK_PAIRED_READING can borrow.

7. **Tier B trigger.** Set a written trigger condition for revisiting Tier B (e.g. "Hemanth has ≥3 unsynced pairs after using v1 for two weeks"), or leave open-ended? My pick: leave open-ended; Tier B brainstorm happens when Hemanth says "I want this for arbitrary pairs."

---

## What this brainstorm does NOT do

- **No fix-TODO authored.** Per the brief: brainstorm gates fix-TODO. Once questions 1–7 are ratified, I author `AUDIOBOOK_SYNCED_TEXT_FIX_TODO.md` per `feedback_fix_todo_authoring_shape.md`'s 14-section template.
- **No code touched.** Zero `src/` changes. Zero `resources/book_reader/` changes. No build run.
- **No MCP smoke.** Pure text-and-files brainstorm.
- **No technical menu surfaced to Hemanth.** Library picks (Whisper.cpp vs Vosk vs sherpa-onnx, model size, alignment algorithm shape) are Agent 2 calls if/when Tier B is ever ratified.

## What's next, conditional on Hemanth ratification

1. Hemanth answers questions 1–7 (or some subset; missing answers default to the recommended pick).
2. Agent 2 authors `AUDIOBOOK_SYNCED_TEXT_FIX_TODO.md` per the 14-section template. Estimated 4 phases, ~6–8 batches. Phase 1: foliate MediaOverlay wiring + transport bar shell. Phase 2: tile routing + persistence. Phase 3: highlight-in-place visual + auto-scroll. Phase 4: edge cases (drift nudge, gap regions, error states) + sourcing doc.
3. Routine fix-TODO execution + smoke flow follows.

---

**End of brainstorm.**
