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

## 2026-04-16 Lifecycle Fix Prototypes

- `player_lifecycle/Batch1.1_SidecarProcess_sessionId_filter.cpp` - PLAYER_LIFECYCLE Batch 1.1. Scope: SidecarProcess drops stale session-scoped events whose incoming `sessionId` differs from current `m_sessionId`. Agent 3 should port the guard into the real `processLine()` dispatch. Concrete: current `sendOpen()` regenerates session IDs and current `processLine()` ignores incoming IDs. Guess: the process-global allowlist may need one or two additions if Agent 3 observes more non-session events.
- `stream_lifecycle/Batch1.1_PlaybackSession_struct_API.cpp` - STREAM_LIFECYCLE Batch 1.1. Scope: `PlaybackSession` struct plus `beginSession`, `resetSession`, `currentGeneration`, and `isCurrentGeneration` helpers. Agent 4 should add the API without migrating current fields in this batch. Concrete: current `PendingPlay`, `NextEpisodePrefetch`, and reset helper names. Guess: future `SeekRetryState` shape uses `std::shared_ptr`; Agent 4 may keep QObject identity until Batch 1.3.
- `stream_lifecycle/Batch2.1_StopReason_signal_signature.cpp` - STREAM_LIFECYCLE Batch 2.1. Scope: controller-side `StopReason` enum, defaulted `stopStream(StopReason)`, and `streamStopped(StopReason)` signal. Agent 4 should tag the defensive stop inside `startStream()` as `Replacement`. Concrete: current `startStream()` calls `stopStream()` first and `stopStream()` emits synchronously. Guess: `Failure` may stay vocabulary-only until failure cleanup is unified.
- `stream_lifecycle/Batch2.2_onStreamStopped_branch_logic.cpp` - STREAM_LIFECYCLE Batch 2.2 skeleton. Scope: StreamPage branch shape for `Replacement` versus true end-of-session. Agent 4 should preserve new session identity during replacement and keep existing teardown for UserEnd/Failure. Concrete: current `onStreamStopped()` clear/disconnect/showBrowse behavior. Guess: replacement overlay handling depends on Agent 4's final `onSourceActivated()` ordering.
