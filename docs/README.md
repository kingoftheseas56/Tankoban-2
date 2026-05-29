# Tankoban Documentation — Start Here

This is the map. Pick the row that matches you.

## I'm a user
- **What is this?** → [`/README.md`](../README.md) — product overview, the three modes (Comics, Books, Theatre), platform status.
- **How do I install / build it?** → [`/BUILD.md`](../BUILD.md) — Windows build guide. (No public installer yet; macOS not available yet.)

## I'm a contributor
- **How is the app put together?** → [`/ARCHITECTURE.md`](../ARCHITECTURE.md) — component map, process model, the sidecar/stream-server boundaries.
- **How do I contribute?** → [`/CONTRIBUTING.md`](../CONTRIBUTING.md) — code style, PR conventions, and why `agents/` can be ignored.
- **How is the repo laid out?** → [`developer/repo-structure.md`](developer/repo-structure.md) — directory-by-directory tour + the planned cleanup.

## I'm an agent (internal LLM-driven workflow)
- **Roles & ownership** → [`agents/`](agents/) — one file per brotherhood agent (overseer, comic reader, book reader, video player, stream/sources, library/UX).
- **Live coordination** → [`/agents/`](../agents/) (repo root, distinct from `docs/agents/`) — `GOVERNANCE.md` (rules), `STATUS.md` (per-agent state), `chat.md` (shared log), `CONTRACTS.md`, `ONBOARDING.md`. This is operational state, not public documentation.

## I'm reading project history / design
- **Specs, plans, mockups, audits** → [`superpowers/`](superpowers/) — dated design artifacts per arc. *(Being reorganized into active / archived / raw — see `REPO_STRUCTURE_CLEANUP_FIX_TODO.md` at repo root.)*
- **Decision audits** → [`/agents/audits/`](../agents/audits/) (repo root) — comparative audits and findings.

---

*This map reflects the current layout. A documentation cleanup is in progress (`REPO_STRUCTURE_CLEANUP_FIX_TODO.md`); this file is updated as the structure firms up.*
