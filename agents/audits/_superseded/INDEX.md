# Superseded Audits — Index

This directory holds audit files whose findings have been re-done fresh by a later audit pass (typically a Congress-commissioned multi-agent redo). Kept for historical reference only. **Do not cite files here as authority** — follow the supersession pointer at the top of each file to the current source of truth.

## Index

| File | Original author | Superseded on | Superseded by |
|---|---|---|---|
| [stream_a_engine_2026-04-16.md](stream_a_engine_2026-04-16.md) | Agent 7 (Codex) | 2026-04-18 (Congress 6) | [../congress6_stream_primary_2026-04-18.md](../congress6_stream_primary_2026-04-18.md) + [../congress6_sources_torrent_2026-04-18.md](../congress6_sources_torrent_2026-04-18.md) + [../congress6_integration_2026-04-18.md](../congress6_integration_2026-04-18.md) |
| [player_stremio_mpv_parity_2026-04-17.md](player_stremio_mpv_parity_2026-04-17.md) | Agent 7 (Codex) | 2026-04-18 (Congress 6) | [../congress6_player_sidecar_2026-04-18.md](../congress6_player_sidecar_2026-04-18.md) + [../congress6_integration_2026-04-18.md](../congress6_integration_2026-04-18.md); P1-5 CORRECTED; P0-1 carry-forward; P1-1/P1-2/P1-3 → PLAYER_STREMIO_PARITY_FIX_TODO Phase 2+ |

## Reactivation path

If a superseded audit's finding needs to be reactivated (e.g., a Congress audit missed something the prior audit caught), move the file back up to `agents/audits/` and add a note at the top explaining the reactivation rationale. Update this INDEX accordingly.
