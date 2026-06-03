# THE OFFICE — RETIRED / DORMANT (gov-v14, 2026-06-03)

The Office (live inter-agent bus + Night Watch foreman + reachability/summons) is **stood down.**

**Why:** the Office existed to let brothers coordinate when no human was in the loop. With Hemanth
hands-on directing the agents, that need is gone. The overnight autonomous run (2026-06-02→03) was an
outlier, and its retro (`agents/night_ops/agent0_moderator_retro_2026-06-03.md`) showed autonomy +
a shared mutable tree is unsafe while brothers are live.

**This code is intentionally preserved, not deleted — it is RE-ARMABLE.** If a future unattended
autonomous run is wanted, this is the substrate to revive (after addressing the retro's avoid-list:
isolate each worker's lane, single-own the land step, don't clone heads-down brothers, keep the
moderator out of routine traffic).

**Do NOT, in normal hands-on operation:**
- run `open_office.bat` / `office_watch.sh` / the dispatcher / the Night Watch foreman
- start a persistent Office Monitor watch

The `office-deliver.sh` UserPromptSubmit hook was unregistered from `.claude/settings.json` in the
same change. Governance: see the gov-v14 amendment at the top of `agents/GOVERNANCE.md` and
`agents/THE_PASSING_2026-06-03.md`.

**What replaces it:** parallelism = Trigger E (Claude Jr fan-out). Cross-model review / audit / bulk =
summon Codex/DeepSeek/Gemini as tools via `scripts/engines/engine.py`. Coordination = Hemanth + the
async trail (chat.md, RTC, routes.yml, domain CLAUDE.md, backlogs, memory).
