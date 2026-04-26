# Governance Versions

Per-file version stamps for the **stable** governance documents. Each agent maintains a pin in their `STATUS.md` block (`Governance seen: gov-vN | Contracts seen: contracts-vN`); if the live version here differs from your pin, re-read that file and bump your pin in the same edit.

This file is the only governance file every agent reads every session. It is intentionally tiny — under 30 lines — so the read is free.

| File | Current version | Last bumped | Bumped by | One-line changelog |
|------|----------------|-------------|-----------|--------------------|
| `agents/GOVERNANCE.md` | gov-v2 | 2026-04-16 | Agent 0 | Slim reading order; add Rule 12 (STATUS touch), Rule 13 (CLAUDE.md dashboard), Maintenance section (chat rotation), Congress auto-close clause |
| `agents/GOVERNANCE.md` | gov-v3 | 2026-04-16 | Agent 0 | Add Rule 14 (decision authority — agents pick technical options, Hemanth picks product/UX) + Rule 15 (self-service execution — agents read logs/rebuild sidecar/grep themselves; Hemanth's role is UI smoke + visual confirmation only) |
| `agents/CONTRACTS.md` | contracts-v1 | 2026-04-16 | Agent 0 | Baseline (no semantic changes; version stamp introduced) |
| `agents/CONTRACTS.md` | contracts-v2 | 2026-04-16 | Agent 0 | Build Verification Rule amended: sidecar build (`native_sidecar/build.ps1`, `build_qrhi.bat`) is now agent-runnable from bash; main app build stays honor-system (MSVC/Ninja/cl.exe unreliable from bash) |
| `agents/CONTRACTS.md` | contracts-v3 | 2026-04-25 | Agent 0 | New § Skill Provenance in RTCs: non-trivial RTCs (≥1 src/ file or ≥30 LOC) include `Skills invoked: [/skill1, /skill2, ...]` field between message body and `\| files:`; documentation-only this phase, Phase 4 pre-RTC hook adds nag-only enforcement first 30 days. Bumps GOVERNANCE.md Rule 11 format string in lockstep (no separate gov bump). Per SKILL_DISCIPLINE_FIX_TODO §5 ratification |

## Bump Authority

- **GOVERNANCE.md** bumps require Hemanth's ratification. Agent 0 proposes, Hemanth signs off, then Agent 0 bumps the version + writes the changelog row in the same commit.
- **CONTRACTS.md** bumps belong to whichever agent owns the contract being edited. They bump in the same commit as the contract change. Cross-agent contracts get a heads-up in chat.md before the bump.
- Typo / formatting / non-semantic edits do NOT bump the version. Use judgment: would another agent need to re-read to understand the change? If yes, bump.

## Changelog Format

One row per bump. Append the new row as the most recent line in the table above. Older versions are inferable from `git log` on the corresponding file.
