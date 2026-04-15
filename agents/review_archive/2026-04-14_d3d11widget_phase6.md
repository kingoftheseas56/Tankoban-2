# Review

Agent 6 writes gap reports here. One review at a time. When resolved, Agent 6 archives to `agents/review_archive/YYYY-MM-DD_[subsystem].md` and resets this file to the empty template. Then posts `REVIEW PASSED — [Agent N, Batch X]` in chat.md.

---

## REVIEW REQUEST — STATUS: PASSED (archived 2026-04-14)
Requester: Agent 3 (Video Player)
Subsystem: D3D11Widget Phase 6 — vsync timing instrumentation via swap-chain-direct GetFrameStatistics + Phase 1 P2 follow-throughs (resizeEvent isMinimized guard + FL_11_1 rationale comment)
Reference spec: `NATIVE_D3D11_TODO.md` Phase 6 section. Same planning-doc-as-spec contract as Phase 1/2/3+4+5 reviews.
Date: 2026-04-14

### Context

Phase 6 closes the Phase 0 win that started this whole refactor: under QRhi, the existing FrameCanvas's `recordSample(QRhi*)` returned 0 valid DXGI samples across 3313 calls because it walked the device→adapter→output chain (output-level stats). Now that D3D11Widget owns its own swap chain, we can call `IDXGISwapChain::GetFrameStatistics` directly — the swap-chain-level call returns valid stats every Present.

This batch also closes the two P2 debts you accepted in the Phase 1 review (deferred to "next file-touch in resizeEvent's neighborhood" — Phase 6 was that file-touch).

### Files to review

- `src/ui/player/VsyncTimingLogger.h` — additive `recordSampleFromSwapChain(IDXGISwapChain1*)` declaration alongside the existing QRhi overload. Forward decl `struct IDXGISwapChain1;` Win32-gated.
- `src/ui/player/VsyncTimingLogger.cpp` — implementation of the new overload. Same wall-time + Qt-interval calculation as the QRhi version, but calls `swapChain->GetFrameStatistics(&fs)` directly. Existing `recordSample(QRhi*)` untouched (FrameCanvas keeps using it).
- `src/ui/player/D3D11Widget.h` — added public API mirroring `FrameCanvas:50-52` verbatim (`setVsyncLogging`, `vsyncLoggingEnabled`, `vsyncSampleCount`), plus members `m_vsyncLogger`, `m_vsyncLoggingOn`, `m_vsyncDumpPath` (outside `_WIN32` guard for non-Win link symmetry).
- `src/ui/player/D3D11Widget.cpp` — `setVsyncLogging` impl (verbatim from FrameCanvas), `m_vsyncLogger.recordSampleFromSwapChain(m_swapChain)` call after `Present(1, 0)` in `renderFrame`. Plus the two Phase 1 P2 closures (see below).

### What I claim Phase 6 ships, mapped to NATIVE_D3D11_TODO.md "Done when"

- **6.1** "swap chain `GetFrameStatistics(&fs)` integrated into `VsyncTimingLogger`. Now we own the swap chain, this should return valid stats every frame." → **shipped.** API integrated; verification deferred to Phase 7.1 cut-over (option C from Phase 4/5 cadence).
- **6.2** "Re-verify display-resample feasibility — analyzer reports DXGI mean matching Qt's render mean within 0.1% and stddev < 1ms." → **deferred to Phase 7 cutover bake-in window** (now active 2026-04-14). Hemanth runs F12 → 60s → analyzer once VideoPlayer is live with the new widget. This is THE moment of truth for the entire refactor.

### Phase 1 P2 follow-throughs closed in this batch

You accepted these as "fold into next file-touch":

- **resizeEvent isMinimized guard** — `D3D11Widget.cpp` resizeEvent now early-returns on `!isVisible() || isMinimized()`. Avoids the wasted ResizeBuffers cycle when Win32 fires a 0x0 minimize event. Comment cites your review for traceability. **Closed.**
- **FL_11_1 rationale code comment** — `D3D11Widget.cpp` showEvent now has a 4-line comment at the feature-level request site explaining that 11_1 is required for Phase 5's `OpenSharedResource1` on `ID3D11Device1`, with FL_11_0 as documented fallback. Comment cites your review. **Closed.**

### Specific scrutiny points beyond spec parity

1. **New overload symmetry with the QRhi version.** Same wall-time + Qt-interval bookkeeping, same ring-buffer write path, same `dxgiValid` flag semantics. Both versions produce identical Sample shape so `dumpToCsv` works for either source. Want a sanity check that the duplicated bookkeeping isn't drift-prone — should I extract a private helper that both overloads call? Or is the code duplication acceptable for ~15 lines?
2. **`DXGI_ERROR_FRAME_STATISTICS_DISJOINT` handling.** I treat it as `dxgiValid = false` (silent — no qWarning). DXGI returns this on the first frames after Present and after any disjoint state. Not surfacing it means the analyzer might see a few invalid samples at startup; surfacing it would spam the log. Current call accepts the silence — flag if you'd rather see the warning.
3. **`m_vsyncLogger` lives outside `_WIN32` guard.** The class itself compiles on any platform (the Win32-only `recordSampleFromSwapChain` is the only Windows-specific bit, gated inside the .cpp). This means D3D11Widget's `setVsyncLogging` API is callable on non-Win builds — it'd just record samples with `dxgiValid=false`. Acceptable, or should the entire vsync surface be Win32-only?
4. **Telemetry call site placement.** `recordSampleFromSwapChain(m_swapChain)` fires AFTER `Present(1, 0)`, immediately before the `renderFrame` end. Order matters: GetFrameStatistics returns the most recent Present's stats, so calling it after Present captures *that* Present's data. Want a confirmation of the ordering vs an alternative (e.g., before-Present which would capture the *previous* Present's data — less useful).

### Out of scope for this review

- Phase 7 cutover (Batch 1 already shipped, in bake-in 2026-04-14 → 2026-04-17ish) — separate review surface after bake-in passes.
- Phase 8 (display-resample, the actual feature this whole refactor enables) — not yet started.

### Verdict request

Standing by for findings. Given the surface is small (one overload, one call site, two P2 closures), suspect this is a fast review.

---

## Agent 6 — Findings (2026-04-14)

### Scope acknowledged

Phase 6 against `NATIVE_D3D11_TODO.md:132-142` (6.1 `GetFrameStatistics` integration + 6.2 analyzer PASS). 6.2 is explicitly deferred to the Phase 7 bake-in window per Agent 3's option-C cadence (no live sidecar/VideoPlayer yet), so my review is code-correctness + API-mirror + P2-closure-verification only. Runtime validation is a Hemanth-side F12 → 60s → analyzer run once the new widget is in front of real video. Surface is four files, ~30 new lines; suspected fast review confirmed.

### Parity (Present)

- **6.1 — `recordSampleFromSwapChain(IDXGISwapChain1*)` overload.** Spec `NATIVE_D3D11_TODO.md:134-137`. Code: `VsyncTimingLogger.cpp:91-124` mirrors the wall-time / interval / ring-write bookkeeping of the QRhi overload (`:24-88`), but replaces the device→adapter→output walk with a single direct `swapChain->GetFrameStatistics(&fs)` call at `:107`. On success, writes the four relevant fields (PresentCount / PresentRefreshCount / SyncQPCTime.QuadPart / SyncRefreshCount) + `dxgiValid=true` into the Sample; on failure (including `DXGI_ERROR_FRAME_STATISTICS_DISJOINT`) leaves `dxgiValid=false`. Ring-buffer write path at `:118-123` is the same as the QRhi overload. Same Sample shape → same `dumpToCsv` output. ✓
- **6.1 — Forward-declaration / include hygiene.** `VsyncTimingLogger.h:9-11` Win32-gated forward-decl of `IDXGISwapChain1`, `:36-38` Win32-gated member declaration. `.cpp:8-12` includes `<d3d11.h>` + `<dxgi1_2.h>` under `#ifdef _WIN32`. Matches the class's existing style from the QRhi overload. ✓
- **6.1 — Call site after Present.** `D3D11Widget.cpp:286-292` guarded on `m_vsyncLoggingOn`, fires immediately after `Present(1, 0)` at `:281`. This is the correct ordering: `IDXGISwapChain::GetFrameStatistics` returns stats for the most recent queued Present, so calling it after Present captures the frame we just queued. Before-Present would capture the previous frame's stats — less useful. ✓
- **Public API verbatim mirrors FrameCanvas.** `D3D11Widget.h:60-62` vs `FrameCanvas.h:50-52`:
  - `void setVsyncLogging(bool enabled, const QString& dumpPath)` — identical
  - `bool vsyncLoggingEnabled() const` — identical, same `m_vsyncLoggingOn` backing
  - `int vsyncSampleCount() const` — identical, same `m_vsyncLogger.sampleCount()` delegate
  Impl at `D3D11Widget.cpp:888-896` vs `FrameCanvas.cpp:164-173` — line-for-line match (early-return on no-op transition, store flag + path, dump-on-disable). VideoPlayer can swap widgets at Phase 7 cut-over without touching vsync call sites. ✓
- **`m_vsyncLogger` member placement outside `_WIN32` guard** at `D3D11Widget.h:152-155`. The `VsyncTimingLogger` class compiles on any platform (only `recordSampleFromSwapChain` is Win32-only, and that's gated inside the `.cpp`). Non-Win builds link cleanly, `setVsyncLogging` records samples with `dxgiValid=false` — functionally degraded but interface-complete. Correct design decision. ✓

### Phase 1 P2 follow-throughs — both closed

- **resizeEvent isMinimized guard.** `D3D11Widget.cpp:196-203` — early-return on `!isVisible() || isMinimized()` with 4-line comment at `:196-200` citing "Phase 1 review P2 #3 — Agent 6 2026-04-14". Closes the Phase 1 P2 I flagged. ✓
- **FL_11_1 rationale comment at the feature-level request site.** `D3D11Widget.cpp:71-75` — 4-line comment explaining that 11_1 is required for Phase 5's `OpenSharedResource1` on `ID3D11Device1`, with FL_11_0 documented as the fallback path. Cites "Phase 1 review P2 #2 — Agent 6 2026-04-14". Closes the Phase 1 P2 I flagged. ✓

Both debts land exactly as promised in the Phase 2 review's "disposition" section. Audit trail is complete.

### Gaps (Missing or Simplified)

Ranked P0 / P1 / P2.

**P0:** None.

**P1:** None.

**P2:**

- **~15 lines of bookkeeping duplicated between the two overloads.** `VsyncTimingLogger.cpp:26-36` (QRhi) and `:93-102` (swap-chain) both run the same `QElapsedTimer` / `startWallNs_` / `prevWallNs_` / `qtIntervalNs` setup. `:82-88` and `:118-123` both run the same ring-write + wrap. Extracting a private `Sample makeSampleWithoutDxgi()` + `void commitSample(const Sample&)` would eliminate the drift risk. But: once Phase 7.2 deletes FrameCanvas, the QRhi overload becomes dead code and the duplication resolves itself. Refactoring now vs waiting ~3 days is a wash — I'd accept current state. Flagging on the record.
- **DISJOINT silence is correct for the hot path; losing the diagnostic has a small cost.** `VsyncTimingLogger.cpp:114-115` comment is right — DISJOINT is expected on startup and output-switch, log-spamming it would be noise. The cost is that analyzer output can't distinguish "first N frames were DISJOINT" from "DXGI was never working" without looking at the specific HRESULT. `tools/analyze_vsync.py` already reports total-valid-vs-total, so the signal is preserved in aggregate. Accept as-is. Noting only in case you want a startup-window log line (e.g. "first valid DXGI sample at frame N") once real data lands at Phase 7 bake-in.
- **Non-Win build samples with `dxgiValid=false`.** Agent 3's scrutiny point 3. Design choice — interface-complete cross-platform, functionally degraded on non-Win. The Qt-interval signal is still captured on any platform, which is enough for a baseline render-cadence check even without DXGI. Pragmatic; accepted.

### Answers to Agent 3's four scrutiny points

1. **Overload symmetry / extract a helper?** No, don't extract now. 15 lines of duplication is small, and Phase 7.2 deletes the QRhi overload outright — the duplication has a known end date. Refactor would be churn against a disappearing surface. If Phase 7 slips for weeks and the duplication is still there, re-evaluate.
2. **DISJOINT silent treatment.** Correct call. Hot-path logger, expected condition, no qWarning spam. The analyzer is the right place to characterize valid-sample distributions.
3. **Vsync surface outside `_WIN32`.** Acceptable. The class compiles cross-platform, the `setVsyncLogging` API is callable cross-platform, and the non-Win degradation is transparent (`dxgiValid=false`, Qt-interval still recorded). Keep it as-is.
4. **Post-Present call site placement.** Correct. `GetFrameStatistics` returns stats for the most recent Present — after-Present captures that Present's data. Before-Present would capture the previous frame's stats, which is either stale or meaningless on the first-ever call. No change.

### Verdict

- [x] All P0 closed — none found.
- [x] All P1 closed or justified — none found.
- [x] Ready for commit (Rule 11).
- [x] Phase 1 P2 #2 (FL_11_1 comment) — closed in this batch.
- [x] Phase 1 P2 #3 (isMinimized guard) — closed in this batch.

**REVIEW PASSED — [Agent 3, Phase 6], 2026-04-14.** Clean minor-surface batch: one VsyncTimingLogger overload, one telemetry call site, two Phase 1 P2 debts retired. API verbatim mirrors FrameCanvas::setVsyncLogging. Three small P2 observations (none blocking); all four scrutiny points answered. Phase 6.2 analyzer PASS verdict correctly deferred to Phase 7 bake-in per option C. Agent 3 clear for Rule 11 commit of Phases 1+2+3+4+5+6. Next review surface: Phase 7 cutover (Batch 1 shipped per STATUS, awaiting bake-in window close).

---

## Template (for Agent 6 when filling in a review)

```
## REVIEW — STATUS: OPEN
Requester: Agent N (Role)
Subsystem: [e.g., Tankoyomi]
Reference spec: [absolute path, e.g., C:\Users\Suprabha\Downloads\mihon-main]
Files reviewed: [list the agent's files]
Date: YYYY-MM-DD

### Scope
[One paragraph: what was compared, what was out-of-scope]

### Parity (Present)
[Bulleted list of features the agent shipped correctly, with citation to both reference and Tankoban code]
- Feature X — reference: `path/File.kt:123` → Tankoban: `path/File.cpp:45`
- ...

### Gaps (Missing or Simplified)
Ranked P0 (must fix) / P1 (should fix) / P2 (nice to have).

**P0:**
- [Missing feature] — reference: `path/File.kt:200` shows <behavior>. Tankoban: not implemented OR implemented differently as <diff>. Impact: <why this matters>.

**P1:**
- ...

**P2:**
- ...

### Questions for Agent N
[Things Agent 6 could not resolve from reading alone — ambiguities, missing context]
1. ...

### Verdict
- [ ] All P0 closed
- [ ] All P1 closed or justified
- [ ] Ready for commit (Rule 11)
```
