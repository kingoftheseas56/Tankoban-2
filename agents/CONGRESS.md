# Congress

One motion at a time. When resolved, Agent 0 archives to `congress_archive/YYYY-MM-DD_[topic].md` and resets this file to the empty template. Then posts one line in chat.md.

When Hemanth posts a ratification line (`ratified`, `APPROVES`, `Final Word`, or `Execute`), Agent 0 MUST archive and reset in the same session — not the next session. If Agent 0 is absent, the next agent to start a session becomes the archiver-of-record.

---

## CONGRESS 9 — STATUS: OPEN
Opened by: Agent 0 (Coordinator)
Date: 2026-05-31
Venue: **first substantive Congress run live on THE OFFICE bus** (agents/bus.jsonl). Positions land on the bus addressed to @all; Agent 0 mirrors the synthesis here.

## Motion

**Adopt a brotherhood-wide observable network layer:** a single shared, instrumentable channel that every domain's outbound HTTP flows through, replacing today's ~135 independent `QNetworkAccessManager` instances scattered across ~40 files. Adopting it unlocks live request visibility, per-request throttle/block (deterministic bad-connection testing), and firewall/offline detection — foundational infrastructure that pays off well beyond the test harness that motivated it. Motivated by TANKOCTL_TEST_HARNESS P2 (network throttle/block was the highest-value gauge but is un-buildable without this seam); owned by the whole brotherhood because the seam runs through every domain's code at once.

## Scope

**IN scope:**
- WHETHER to adopt a shared network seam at all (vs leave 135 independent QNAMs and abandon network-level testing).
- The SHAPE of the seam: a central factory/accessor every component draws its QNAM (or request) from, with observer + throttle + block hooks. Mandatory routing vs opt-in.
- The MIGRATION approach: big-bang vs incremental-by-domain (behind a flag), and ordering.
- Per-domain COST estimate: how invasive the cutover is in each owner's files.
- The dev-control commands it unlocks (`net-list-requests` / `net-get-active` / `net-throttle-set` / `net-block-host`) — confirm the surface, not implement.

**OUT of scope:**
- The other 5 P2 debt pieces (perf counters, sidecar queue depth, scanner pause/resume, signal tracer, cache-stats — cache-stats already shipped). Those are ordinary commissions, not Congress matter.
- Actual per-domain migration execution — that flows as commissions AFTER ratification.
- Any behavior change to scrapers/indexers beyond where they obtain their network manager.
- Transport-layer rewrite of the dev-control bridge (stays additive v1.x).

## Pre-Brief

Required reading before posting a position:
- `TANKOCTL_TEST_HARNESS_FIX_TODO.md` §2 (P2) — why the network gauge is blocked on this seam.
- The ground-truth finding: ~135 `QNetworkAccessManager` declarations across ~40 files (`src/core/book/*`, `src/core/indexers/*`, `src/core/manga/**`, plus stream/torrent). No shared instance today.
- `feedback_stream_server_firewall_gotcha` — silent-firewall network failures this seam would make detectable.
- Your own domain's network call sites (each owner knows theirs best).

## How This Congress Works

Reply-only bus v1. Each brother posts ONE position to `@all` on the Office bus answering the four questions below for THEIR domain. Order: Agent 4 first (owns the most call sites — indexers/torrent/stream), then Agent 2 (book scrapers), Agent 1 (manga catalog), Agent 3 (sidecar/player network surface), Agent 5 (library/cross-mode). Agent 9 posts a prototype-feasibility read on the seam shape. Agent 8 assists Agent 0 in wording the motion/summons (no domain vote). Agent 0 synthesizes once all positions are in and brings a single recommendation to Hemanth.

**Four questions every domain owner answers:**
1. **Adopt?** Shared seam yes/no, and if no, your alternative.
2. **Shape?** Central factory all components draw from — mandatory or opt-in routing? Wrap-the-QNAM vs route-every-request?
3. **Migration?** Big-bang vs incremental-by-domain behind a flag; if incremental, where does your domain sit in the order?
4. **Cost?** Rough blast radius in YOUR files (how many call sites, how mechanical the cutover).

---

## Positions

### Agent 1 (Comic Reader + Tankoyomi)
[awaiting bus position]

### Agent 2 (Book Reader + TankoLibrary)
[awaiting bus position]

### Agent 3 (Video Player + sidecar)
[awaiting bus position]

### Agent 4 (Stream + Tankorent)
[awaiting bus position]

### Agent 5 (Library UX + Theme)
[awaiting bus position]

### Agent 9 (DeepSeek — prototype feasibility)
[awaiting bus position]

---

## Agent 0 Synthesis

[Synthesis after all positions in. Recommendation to Hemanth — he ratifies the direction; technical content is delegated to Agent 0 per his 2026-05-31 instruction.]

---

## Hemanth's Final Word

[Hemanth ratifies, modifies, or rejects the direction.]

---

<!-- TEMPLATE — copy this block when opening a new motion, replace STATUS above with the open motion -->

<!--
## CONGRESS N — STATUS: OPEN
Opened by: Agent 0 (Coordinator)
Date: YYYY-MM-DD

## Motion

[One-paragraph statement of what is being decided.]

## Scope

**IN scope:** [...]

**OUT of scope:** [...]

## Pre-Brief

[Required reading before posting positions, e.g., agents/congress_prep_*.md.]

## How This Congress Works

[Order of positions, who posts when, what each agent must confirm.]

---

## Positions

### Agent 1 (Comic Reader)
[position]

### Agent 2 (Book Reader)
[position]

### Agent 3 (Video Player)
[position]

### Agent 4 (Stream)
[position]

### Agent 4B (Sources)
[position]

### Agent 5 (Library UX)
[position]

---

## Agent 0 Synthesis

[Synthesis after all positions in. Override justification if any. Final recommendation to Hemanth.]

---

## Hemanth's Final Word

[Hemanth ratifies, modifies, or rejects.]
-->
