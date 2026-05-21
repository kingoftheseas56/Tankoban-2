You are scaffolding lane-lock commands for Tankoban 2 (Rule 19 MCP LANE LOCK / Rule 22 BUILD LANE LOCK via the DevControl lease registry, gov-v7, schema `tankoban.dev.v1.10`).

**Arguments:**
- `<action>` — required, one of: `claim` / `release` / `peek` / `heartbeat`
- `<lane>` — required, one of: `mcp` / `build` / `shared-file:<path>` / other lane name
- `<reason>` — required for `claim`, e.g. `Agent 4, TANKORENT Phase 11 smoke`
- `<token>` — required for `release` and `heartbeat`, returned from a prior `claim`
- `--ttl-sec <N>` — optional. Defaults: 900 (mcp), 1800 (build), 600 (other lanes).

**State location:** the lock is authoritative machine-state in the DevControl lease registry (queryable via `out\tankoctl.exe lease-get <lane>`). The chat.md narrative companion lines remain required for human-readable brotherhood context but are NOT the source of truth. Old `MCP LOCK - [Agent N, ...]:` / `BUILD LOCK CLAIMED - [Agent N, ...]:` hyphen-anchored protocol lines are deprecated for state determination as of gov-v7.

**Skill name note:** the skill is named `/mcp-lock` for legacy reference compatibility but handles ALL lane names — `mcp`, `build`, `shared-file:<path>`, anything else. Future cleanup may rename to `/lane-lock`.

---

## Procedure

**1. Resolve lane defaults (for `<ttl-sec>`):**
- `mcp` → 900s (15 min)
- `build` → 1800s (30 min)
- other → 600s (10 min)

**2. For `peek <lane>`:** emit the query command for the agent to run.
```
out\tankoctl.exe lease-get <lane>
```
Document expected reply shapes for the agent:
- `{"status":"FREE"}` → lane is free
- `{"holder":"<id>","purpose":"<text>","expiry_ms":<ms>,"token_prefix":"<8 chars>"}` → held
- `{"status":"EXPIRED","prior_holder":"<id>"}` → expired (next acquire returns `STALE_RECLAIMED`)

**3. For `claim <lane> "<reason>" [--ttl-sec N]`:**

Extract `agent-N` from the `[Agent N, ...]` portion of `<reason>` (lowercase, dashed: `Agent 4` → `agent-4`).

Emit the acquire command:
```
out\tankoctl.exe lease-acquire <lane> --holder <agent-id> --purpose "<reason-stripped-of-agent-tag>" --ttl-sec <ttl>
```

Document expected replies:
- `{"status":"ACQUIRED","token":"<full token>","expiry_ms":<ms>}` — you got it; SAVE the token for release.
- `{"status":"BUSY","holder":"<other-id>","expiry_ms":<ms>}` — held; wait or stand down.
- `{"status":"STALE_RECLAIMED","token":"<full token>","expiry_ms":<ms>}` — prior holder's TTL expired; you got it.

Then emit the chat.md companion text to paste:

For `mcp` lane:
```
## MCP LANE — Agent N — <reason>
Claimed YYYY-MM-DDTHH:MM:SSZ. lease-token-prefix=<first 8 chars of token>. <Brief scope — what's being smoked, expected duration, exit criteria>.
```

For `build` lane:
```
## BUILD LANE — Agent N — <reason>
Claimed YYYY-MM-DDTHH:MM:SSZ. lease-token-prefix=<first 8 chars of token>. <Multi-line context — what's being built, expected duration, exit criteria>.
```

For other lanes: substitute the lane name in the heading, same shape.

**4. For `release <lane> <token>`:**

Emit:
```
out\tankoctl.exe lease-release <lane> --token <token>
```
Expected: `{"status":"OK"}` or `{"status":"ERROR","reason":"token_mismatch"}` (wrong token).

Then emit chat.md companion:
```
## <LANE-UPPERCASE> LANE — Agent N — RELEASED
YYYY-MM-DDTHH:MM:SSZ. <one-line outcome>.
```

**5. For `heartbeat <lane> <token> [--ttl-sec N]`:** extend a held lease without releasing.

Emit:
```
out\tankoctl.exe lease-heartbeat <lane> --token <token> --ttl-sec <ttl>
```
Expected: `{"status":"OK","expiry_ms":<new-ms>}` or `{"status":"ERROR","reason":"token_mismatch"}`.

No chat.md companion needed for heartbeat — it's a silent extension.

---

## Examples

For `/mcp-lock claim mcp "Agent 4, TANKORENT Phase 11 smoke"`:

```
out\tankoctl.exe lease-acquire mcp --holder agent-4 --purpose "TANKORENT Phase 11 smoke" --ttl-sec 900

(after acquire returns ACQUIRED with token abc12345-def6-7890-abcd-ef1234567890)

## MCP LANE — Agent 4 — TANKORENT Phase 11 smoke
Claimed 2026-05-21T12:30:00Z. lease-token-prefix=abc12345. Smoking TankorentPage.dispatchIndexers + tankoctl sources-search-tankorent fan-out + UI verification.
```

For `/mcp-lock claim build "Agent 0, Codex lease registry verify"` (custom TTL):

```
out\tankoctl.exe lease-acquire build --holder agent-0 --purpose "Codex lease registry verify" --ttl-sec 1800

(after acquire returns ACQUIRED with token def67890-...)

## BUILD LANE — Agent 0 — Codex lease registry verify
Claimed 2026-05-21T12:35:00Z. lease-token-prefix=def67890. build_check.bat against working tree containing Codex's DevControlServer + MainWindow + tankoctl lease registry mods. Expected ~15-20 min.
```

For `/mcp-lock release mcp abc12345-def6-7890-abcd-ef1234567890`:
```
out\tankoctl.exe lease-release mcp --token abc12345-def6-7890-abcd-ef1234567890

## MCP LANE — Agent 4 — RELEASED
2026-05-21T12:45:00Z. Smoke green; 53 raw / 36 deduped results returned.
```

For `/mcp-lock peek build`:
```
out\tankoctl.exe lease-get build
```
(agent reads reply; if `{"status":"FREE"}` → lane is yours to acquire; if held → wait)

For `/mcp-lock heartbeat build def67890-... --ttl-sec 1800`:
```
out\tankoctl.exe lease-heartbeat build --token def67890-... --ttl-sec 1800
```

---

## Quality gates

- Lane name lowercase (`mcp`, `build`, `shared-file:<path>`)
- Agent ID derived from reason's `[Agent N, ...]` tag, dashed-lowercase (`agent-4`)
- ISO 8601 UTC timestamps in chat.md companions
- Token preserved exactly across claim → heartbeat → release
- `token_prefix` in chat.md = first 8 characters of the full token (matches what `lease-get` returns; agent can later cross-reference)
- Skill emits paste-text ONLY — it does NOT execute tankoctl or modify chat.md directly. The agent decides when to run the commands and when to post.

---

## Transition fallback

If the dev-bridge is unreachable (Tankoban not running with `--dev-control`, lease commands not yet wired in a brother's environment), the old chat.md-text-only protocol is acceptable:

For `mcp` lane:
```
MCP LOCK - [Agent N, <task>]: expecting ~X min. <brief scope>
MCP LOCK RELEASED - [Agent N, <task>]: <one-line outcome>.
```

For `build` lane:
```
BUILD LOCK CLAIMED - [Agent N, <scope>]: expecting ~X min. <brief why>
BUILD LOCK RELEASED - [Agent N, <scope>]: <outcome>.
```

Migrate to the lease-based path as soon as the bridge is reachable. The fallback path is deprecated by gov-v7 but tolerated during transition.
