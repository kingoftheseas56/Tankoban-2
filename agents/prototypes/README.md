# Agent 7 Prototypes

Reference-only code written by Agent 7 (Codex) when a domain agent posts a `REQUEST PROTOTYPE` line in `agents/chat.md`. Nothing in this directory is compiled, committed into `src/`, or treated as authoritative. Domain agents read these for perspective and implement their own versions.

## Rules

- **Not compiled.** Never included from `src/`. Never added to `CMakeLists.txt`.
- **Not authoritative.** The domain agent's implementation is the truth. Prototypes are one perspective among many (alongside reference codebases like Stremio, Mihon, groundwork).
- **Not edited after posting.** Prototypes are immutable snapshots. If one becomes stale, Agent 0 archives it to `prototypes/archive/` at session boundaries — no one edits in place.
- **Every file has a dated header** identifying the requester, the batch, the references consulted, and "THIS FILE IS NOT COMPILED." No exceptions.

## Naming

`<batch_id>_<subsystem>.(cpp|h|md)` — example: `1.2_addon_transport.cpp`, `4.1_stream_aggregator.h`, `5.2_sidecar_subtitle_protocol.md`.

Markdown files are for architecture sketches or protocol proposals where prose beats code.

## Workflow

1. Domain agent posts `REQUEST PROTOTYPE — [Agent N, Batch X.Y]: <need> | References: <paths>.` in `agents/chat.md`.
2. Hemanth starts a Codex session. Codex reads `AGENTS.md` at repo root, follows the required reading order, finds the request.
3. Codex writes a prototype file here.
4. Codex posts one line: `Agent 7 prototype ready — agents/prototypes/<filename>. For [Agent N, Batch X.Y]. Reference only.` in `agents/chat.md`.
5. Domain agent reads the prototype when implementing the batch. Takes what's useful. Writes their own version in `src/`.
6. Whether the prototype was used or ignored is not tracked — Agent 7 does not lobby for adoption.

## Archive

When a batch ships and its prototype is no longer relevant, Agent 0 moves the prototype file to `prototypes/archive/YYYY-MM_<filename>` during a session-end cleanup. Archived prototypes stay in git for historical reference.
