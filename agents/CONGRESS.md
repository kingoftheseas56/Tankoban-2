# Congress

One motion at a time. When resolved, Agent 0 archives to `congress_archive/YYYY-MM-DD_[topic].md` and resets this file to the empty template. Then posts one line in chat.md.

When Hemanth posts a ratification line (`ratified`, `APPROVES`, `Final Word`, or `Execute`), Agent 0 MUST archive and reset in the same session — not the next session. If Agent 0 is absent, the next agent to start a session becomes the archiver-of-record.

---

## STATUS: NO ACTIVE MOTION

<!-- Previously: CONGRESS 9 (brotherhood-wide observable network layer) — RATIFIED 2026-05-31
by Hemanth (Office bus seq 246: "Hemanth says ratify, let's go"). First substantive Congress
run live on THE OFFICE bus. Archived to agents/congress_archive/2026-05-31_observable_network_layer.md.
Operative outcome: build a shared thread-safe instrumentation registry + a factory
(NetSeam::createManager(QObject* parent, const QString& sourceTag)) vending each owner its own
instrumented QNAM subclass (createRequest override = zero call-site changes), mandatory via CI
grep-gate, per-request source-tags, incremental migration behind TANKOBAN_NET_SEAM (Stream canary
#1 -> Books -> manga -> player -> library). Cost ~20 creation-site swaps. Scope: Qt-side HTTP only;
sidecar/FFmpeg media playback observability = separate Phase 2 (Agent 3-owned). Build sequence:
NetSeam CORE -> Agent 9 (reviewer-gated); per-domain migration -> owners after core lands; feeds
TANKOCTL_TEST_HARNESS P2 network gauge. -->

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
