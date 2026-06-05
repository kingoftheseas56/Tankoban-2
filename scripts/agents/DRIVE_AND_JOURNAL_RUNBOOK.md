# Drive + Journal Runbook — agent drives the app, every action is logged

**Purpose:** an agent (e.g. Agent 4) drives the live Tankoban app — opens it, navigates,
clicks, downloads — while a journal automatically records *what the app did in response to
each action*. Built for: "look at Agent 1's masterpiece, find the parts that don't work,
and meticulously log everything the app does."

Nothing to install — `tankoctl` (app control) and `pywinauto-mcp` (laptop-wide UI control)
are already on the machine; `drive_journal.py` is the action→effect journaler.

---

## 1. Launch in drive mode

```
scripts\agents\run_drive_mode.bat
```

Same build+launch as `build_and_run.bat`, plus the two gates the journal needs
(`TANKOBAN_DEV_WRITE=1` for the cross-log markers, `TANKOBAN_DEV_UI_SIM=1` for `ui-*`
clicks). Wait for the window; confirm the bridge is live:

```
out\tankoctl.exe ping        # expect a JSON reply with schema tankoban.dev.v1.x
```

## 2. Drive + journal — two ways

**A. Direct app control (preferred — fast, reads real C++ state):** `out\tankoctl.exe <cmd>`.
Useful verbs (full live list: `tankoctl ping` → `commands[]`):
- navigate: `open-page stream` (Theatre), `comics-*`, `books-*`
- stream/theatre: `search "One Piece"`, `dispatch-episode ...`, `dispatch-season ...`, `get-torrents`, `get-downloads`
- literal UI: `ui-click <objectName>`, `ui-set-checkbox`, `ui-list-widgets`, `ui-query-widget`
- effect/log: `log-mark "<label>"`, `events-tail`, `get-state`

**B. Laptop-wide / visual (fallback):** `pywinauto-mcp` for any window, screenshots,
focus/keyboard the bridge can't reach, and eyes-on-screen visual confirmation.
(Rule 19: one agent drives the desktop at a time.)

## 3. Make it journal automatically

Wrap actions through `drive_journal.py` — each one is bracketed with `log-mark` and the
resulting `events.jsonl` delta is captured into a readable `did X → app did Y` log.

Single action from the shell:
```
python scripts\agents\drive_journal.py --label "open Theatre" -- open-page stream
python scripts\agents\drive_journal.py --label "search One Piece" --probe get-torrents -- search "One Piece"
```

A sequence (e.g. the One Piece ep-1164 flow) — write a tiny script:
```python
from drive_journal import Driver
d = Driver(session="onepiece-ep1164")
d.do("open Theatre",     ["open-page", "stream"])
d.do("search One Piece", ["search", "One Piece"],   probe=["get-torrents"])
# confirm the right title/episode from the probe output, then:
d.do("download ep 1164", ["dispatch-episode", "<showId>", "1164"], probe=["get-downloads"])
d.close()
```
*(Confirm exact args for `dispatch-episode` against the live `tankoctl ping` catalog — the
show id comes from the `search` / `get-torrents` result.)*

## 4. Read the journal

- **`out/agent_drive_journal.md`** — human-readable: every action → the events it caused.
- **`out/agent_drive_journal.jsonl`** — structured (action, reply, events, before/after state).

This is the self-writing manual of "what each click does" — gold for finding the parts that
don't work, and for debugging + onboarding.

## Notes / honesty
- The journal works even without the write gate (markers degrade to best-effort; the
  `events.jsonl` delta still carries the effect) — but `run_drive_mode.bat` turns it on fully.
- `tankoctl` proves a signal *fired*, not that the screen *looks* right — gate visual
  judgments with a screenshot or Hemanth's eyes (`dev_bridge_visual_blindspot`).
- A real download only fires if a source actually has the item.
