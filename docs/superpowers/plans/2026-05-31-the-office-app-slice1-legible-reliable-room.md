# THE OFFICE — Slice 1: The Legible, Reliable Room — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Office a room Hemanth can SEE and TRUST — every brother's real status derived from ground truth (git + bus + roster), surfaced in a live presence panel in its own standalone app-window, with an honesty lane and auto-mirrored commits, so off-channel work is never invisible.

**Architecture:** A new pure-Python derived-status engine (`office_status.py`) computes each brother's real status from git commits + bus activity + a static roster — never from self-reporting. The existing zero-dep web GUI (`office_web.py`) grows a `/roster` endpoint + presence panel and renders two new message kinds (`blocked`, `activity`). A tracked git `post-commit` hook mirrors every commit into the room. `open_office.bat` launches the page in its own standalone Chromium app-window. No Electron, no owned workers, no foreman — that's Slice 2+.

**Tech Stack:** Python 3.12 stdlib only (no deps — matches existing Office), HTML/CSS/JS in `office_web.py`'s inline `PAGE`, Windows `.bat` + git hooks, standalone-script test harness (matches `tests/test_office.py`).

**Spec:** `docs/superpowers/specs/2026-05-31-the-office-app-design.md` (Slice 1, §1/§2/§4/§8/§9).

**Governance:** Flat-on-master, no worktree (gov-v13). Python-only changes don't touch the C++ build; run tests with `python`, not `ctest`.

---

## File Structure

**Create:**
- `scripts/office/office_status.py` — the derived-status clarity engine (pure functions + thin git/bus IO + `roster` CLI). The heart of Slice 1.
- `scripts/office/tests/test_status.py` — standalone test script (mirrors `test_office.py` style) for the status engine + the new bus subcommands.
- `scripts/office/git-post-commit-mirror.sh` — the git `post-commit` hook body (calls `office_bus.py mirror-commit`).
- `scripts/office/install-office-hooks.sh` — installs the hook into `.git/hooks/post-commit` (tracked + reproducible).
- `scripts/office/office_flag.sh` — wrapper to post a blocker (`kind="blocked"`), sibling of `chat_send.sh`.
- `scripts/office/office.webmanifest` — small PWA manifest (optional app-window polish).

**Modify:**
- `scripts/office/office_bus.py` — add `cmd_flag` (blocked post) + `cmd_mirror_commit` (commit→activity) + dispatch entries.
- `scripts/office/office_web.py` — add `/roster` endpoint + roster panel HTML/CSS/JS + render `blocked`/`activity` kinds + serve the manifest.
- `open_office.bat` — launch a standalone Chromium app-window (`--app=`), Edge→Chrome→default fallback, server-first.

**Canonical status-dict shape** (returned by `compute_roster`, consumed by `/roster`, the panel, and tests — keep these exact keys everywhere):
```python
{
  "agent": "agent4",           # roster key
  "role": "Stream + Tankorent",
  "engine": "claude",          # claude | codex | deepseek | gemini
  "wakeable": True,            # auto-wake reliable on this engine?
  "present": True,             # any bus msg OR commit within PRESENCE_WINDOW_SEC
  "current_arc": "NETSEAM",    # arc of last chat/blocked bus msg, or None
  "last_said": "throttle ready",   # text of last chat/blocked msg (<=80 chars), or None
  "last_said_sec": 120,        # seconds since that msg, or None
  "last_commit": "NetSeam control half (4b1f82d)",  # subject (<=60c) + short-sha, or None
  "last_commit_sec": 300,      # seconds since that commit, or None
  "blocked": False             # True if this agent's latest chat/blocked msg is kind=blocked
}
```

---

## Task 1: Status engine — pure logic (the heart)

**Files:**
- Create: `scripts/office/office_status.py`
- Test: `scripts/office/tests/test_status.py`

- [ ] **Step 1: Write the failing test** (create `scripts/office/tests/test_status.py`)

```python
#!/usr/bin/env python3
"""Tests for office_status.py — pure functions tested directly (deterministic,
no real git/bus), plus the new office_bus subcommands via subprocess.
Mirrors tests/test_office.py: check()/main(), run with `python test_status.py`."""
import os
import sys
import json
import tempfile
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import office_status  # noqa: E402

fails = 0


def check(cond, label):
    global fails
    if cond:
        print("ok:", label)
    else:
        print("FAIL:", label)
        fails += 1


def test_parse_agent_commits():
    now = 1_000_000
    # git log lines: "<ctime>\t<short-sha>\t<subject>"
    text = "\n".join([
        "999700\t4b1f82d\t[Agent 9 (DeepSeek), NetSeam control half] block/throttle",
        "999000\tbb7e4ed\t[Agent 0, NETSEAM closing sweep]: 2 stray sites",
        "998000\tdeadbee\tmerge branch (no agent tag)",
        "997000\t1234abc\t[Agent 0, dashboard] older agent0 commit",
    ])
    out = office_status.parse_agent_commits(text, now)
    check(out["agent9"]["sha"] == "4b1f82d", "parse: agent9 latest commit sha")
    check(out["agent9"]["sec"] == 300, "parse: agent9 commit age = now-ctime")
    check(out["agent0"]["sha"] == "bb7e4ed", "parse: agent0 keeps NEWEST (not older 1234abc)")
    check("subject" in out["agent0"], "parse: subject captured")
    check(all(not k.startswith("agent") or k[5:].isdigit() for k in out), "parse: only agentN keys")


def test_bus_activity():
    now = 1_000_000
    recs = [
        {"seq": 1, "from": "agent4", "to": "all", "kind": "chat", "arc": "NETSEAM",
         "ts": "2026-05-31T00:00:00+00:00", "msg": "throttle ready"},
        {"seq": 2, "from": "agent4", "to": "all", "kind": "activity", "arc": None,
         "ts": "2026-05-31T00:01:00+00:00", "msg": "committed X (abc1234)"},
        {"seq": 3, "from": "agent1", "to": "all", "kind": "blocked", "arc": "COMICS",
         "ts": "2026-05-31T00:02:00+00:00", "msg": "stuck on cover leak"},
    ]
    # epoch_of returns the parsed unix time; feed a now far ahead so sec is positive
    out = office_status.bus_activity(recs, now)
    check(out["agent4"]["last_said"] == "throttle ready",
          "bus: activity line does NOT override last_said (chat only)")
    check(out["agent4"]["arc"] == "NETSEAM", "bus: arc from last chat msg")
    check(out["agent4"]["blocked"] is False, "bus: agent4 not blocked")
    check(out["agent1"]["blocked"] is True, "bus: agent1 blocked (kind=blocked)")
    check(out["agent1"]["last_said"] == "stuck on cover leak", "bus: blocked msg is last_said")


def test_compute_roster():
    now = 1_000_000
    commits = {"agent4": {"subject": "NetSeam control half", "sha": "4b1f82d", "sec": 300}}
    busby = {
        "agent4": {"last_said": "throttle ready", "arc": "NETSEAM", "sec": 120, "blocked": False},
        "agent1": {"last_said": "stuck", "arc": "COMICS", "sec": 99999, "blocked": True},
    }
    roster = office_status.compute_roster(commits, busby, now, presence_window=1800)
    by = {r["agent"]: r for r in roster}
    check(by["agent4"]["present"] is True, "roster: agent4 present (recent msg+commit)")
    check(by["agent4"]["current_arc"] == "NETSEAM", "roster: agent4 arc")
    check(by["agent4"]["last_commit"] == "NetSeam control half (4b1f82d)",
          "roster: last_commit = subject (sha)")
    check(by["agent4"]["last_commit_sec"] == 300, "roster: commit age passed through")
    check(by["agent1"]["present"] is False, "roster: agent1 stale (msg sec > window, no commit)")
    check(by["agent1"]["blocked"] is True, "roster: agent1 blocked surfaced")
    check(by["agent7"]["wakeable"] is False, "roster: agent7 (codex) not wakeable")
    check(by["agent0"]["wakeable"] is True, "roster: agent0 (claude) wakeable")
    check([r["agent"] for r in roster] == sorted([r["agent"] for r in roster]),
          "roster: stable sorted order")


def main():
    test_parse_agent_commits()
    test_bus_activity()
    test_compute_roster()
    print("\n%d failure(s)" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python scripts/office/tests/test_status.py`
Expected: FAIL — `ModuleNotFoundError: No module named 'office_status'` (file doesn't exist yet).

- [ ] **Step 3: Write the minimal implementation** (create `scripts/office/office_status.py`)

```python
#!/usr/bin/env python3
"""The Office — derived-status clarity engine (Python, $0).

Computes each brother's REAL status from GROUND TRUTH — git commits + bus
activity + a static roster — NEVER from self-reporting (the thing that's
unreliable). Pure functions do the logic (deterministic, unit-tested); thin IO
wrappers read real git/bus; `roster` CLI prints JSON for the web GUI.

  python office_status.py roster      -> prints JSON list of per-brother status
"""
import os
import re
import sys
import json
import subprocess
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import office_bus  # noqa: E402  (shares BUS() path + repo root)

# Static roster: identity facts that ground truth can't derive (role, engine,
# whether this engine auto-wakes reliably). Order here = display order.
ROSTER = [
    ("agent0", "Coordinator",        "claude",   True),
    ("agent1", "Comics / Tankoyomi", "claude",   True),
    ("agent2", "Books / TankoLibrary", "claude", True),
    ("agent3", "Video Player",       "claude",   True),
    ("agent4", "Stream / Tankorent", "claude",   True),
    ("agent5", "Library UX / Theme", "claude",   True),
    ("agent7", "Codex (prototypes / audits)", "codex", False),
    ("agent8", "Prompt Architect",   "claude",   True),
    ("agent9", "DeepSeek (exec / audit)", "deepseek", False),
]

PRESENCE_WINDOW_SEC = 1800  # 30 min: active if a bus msg OR commit within this

_AGENT_TAG = re.compile(r"^\[Agent\s*#?\s*(\d+)", re.IGNORECASE)


def parse_agent_commits(git_log_text, now_epoch):
    """git log text ('<ctime>\\t<short-sha>\\t<subject>' per line, NEWEST first)
    -> {('agent'+N): {subject, sha, sec}} keeping the newest commit per agent."""
    out = {}
    for line in git_log_text.splitlines():
        line = line.rstrip("\n")
        if not line:
            continue
        parts = line.split("\t", 2)
        if len(parts) < 3:
            continue
        ctime_s, sha, subject = parts
        m = _AGENT_TAG.match(subject)
        if not m:
            continue
        key = "agent" + m.group(1)
        if key in out:  # newest-first: first hit wins
            continue
        try:
            ctime = int(ctime_s)
        except ValueError:
            continue
        out[key] = {"subject": subject, "sha": sha, "sec": max(0, now_epoch - ctime)}
    return out


def _epoch_of(ts):
    """ISO-8601 ('2026-05-31T00:00:00+00:00') -> unix seconds, or None."""
    if not ts:
        return None
    try:
        return int(datetime.fromisoformat(ts).timestamp())
    except (ValueError, TypeError):
        return None


def bus_activity(records, now_epoch):
    """Bus records -> {('agent'+N): {last_said, arc, sec, blocked}}.
    Only kind in (chat, blocked) feed last_said/arc/blocked; 'activity' lines
    (auto-mirrored commits) are for the message log, not the roster."""
    out = {}
    for rec in records:
        frm = rec.get("from", "")
        if not frm.startswith("agent"):
            continue
        if rec.get("kind") not in ("chat", "blocked"):
            continue
        ep = _epoch_of(rec.get("ts"))
        sec = max(0, now_epoch - ep) if ep is not None else None
        out[frm] = {
            "last_said": (rec.get("msg") or "")[:80],
            "arc": rec.get("arc"),
            "sec": sec,
            "blocked": rec.get("kind") == "blocked",
        }
    return out


def compute_roster(commits_by_agent, bus_by_agent, now_epoch,
                   roster=ROSTER, presence_window=PRESENCE_WINDOW_SEC):
    """Merge static roster + git + bus into the canonical status list."""
    result = []
    for agent, role, engine, wakeable in roster:
        c = commits_by_agent.get(agent)
        b = bus_by_agent.get(agent)
        bus_sec = b.get("sec") if b else None
        com_sec = c.get("sec") if c else None
        present = any(s is not None and s <= presence_window for s in (bus_sec, com_sec))
        last_commit = None
        if c:
            last_commit = "{0} ({1})".format(c["subject"][:60], c["sha"])
        result.append({
            "agent": agent,
            "role": role,
            "engine": engine,
            "wakeable": wakeable,
            "present": present,
            "current_arc": b.get("arc") if b else None,
            "last_said": b.get("last_said") if b else None,
            "last_said_sec": bus_sec,
            "last_commit": last_commit,
            "last_commit_sec": com_sec,
            "blocked": bool(b.get("blocked")) if b else False,
        })
    return sorted(result, key=lambda r: r["agent"])
```

- [ ] **Step 4: Run the tests and make sure they pass**

Run: `python scripts/office/tests/test_status.py`
Expected: all `ok:` lines, ends with `0 failure(s)`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add scripts/office/office_status.py scripts/office/tests/test_status.py
git commit -m "feat(office): derived-status engine — pure logic (git+bus+roster -> per-brother status)"
```

---

## Task 2: Status engine — real IO + `roster` CLI

**Files:**
- Modify: `scripts/office/office_status.py` (append IO wrappers + `main`)
- Test: `scripts/office/tests/test_status.py` (add a smoke that the CLI emits valid JSON)

- [ ] **Step 1: Write the failing test** (add to `test_status.py`, and call it from `main()`)

Add this function and add `test_roster_cli()` to `main()` (before the `print` line):

```python
def test_roster_cli():
    # The CLI must run against the REAL repo and emit a JSON list with the
    # canonical keys for every roster agent. (Integration smoke, not values.)
    BUS_PY_DIR = os.path.join(HERE, "..")
    proc = subprocess.run(
        [sys.executable, os.path.join(BUS_PY_DIR, "office_status.py"), "roster"],
        capture_output=True, text=True,
    )
    check(proc.returncode == 0, "cli: roster exits 0")
    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError:
        data = None
    check(isinstance(data, list) and len(data) >= 9, "cli: roster prints JSON list of >=9 brothers")
    if data:
        keys = set(data[0].keys())
        need = {"agent", "role", "engine", "wakeable", "present", "current_arc",
                "last_said", "last_said_sec", "last_commit", "last_commit_sec", "blocked"}
        check(need <= keys, "cli: each entry has all canonical keys")
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python scripts/office/tests/test_status.py`
Expected: FAIL — `cli: roster exits 0` fails (no `roster` subcommand / no `main` yet; nonzero exit or traceback).

- [ ] **Step 3: Write the implementation** (append to `scripts/office/office_status.py`)

```python
def _git_log_text(repo=None, limit=200):
    repo = repo or office_bus._repo_root()
    try:
        r = subprocess.run(
            ["git", "-C", repo, "log", "-n", str(limit),
             "--pretty=format:%ct%x09%h%x09%s"],
            capture_output=True, text=True, encoding="utf-8", errors="replace",
        )
        return r.stdout if r.returncode == 0 else ""
    except (OSError, ValueError):
        return ""


def _bus_records():
    bus = office_bus.BUS()
    out = []
    if os.path.exists(bus):
        with open(bus, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    out.append(json.loads(line))
                except json.JSONDecodeError:
                    continue
    return out


def roster_now(now_epoch=None):
    import time
    now_epoch = int(time.time()) if now_epoch is None else now_epoch
    commits = parse_agent_commits(_git_log_text(), now_epoch)
    busby = bus_activity(_bus_records(), now_epoch)
    return compute_roster(commits, busby, now_epoch)


def main(argv):
    if not argv or argv[0] != "roster":
        sys.exit("usage: office_status.py roster")
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    print(json.dumps(roster_now(), ensure_ascii=False))


if __name__ == "__main__":
    main(sys.argv[1:])
```

- [ ] **Step 4: Run the tests and make sure they pass**

Run: `python scripts/office/tests/test_status.py`
Expected: all `ok:`, `0 failure(s)`, exit 0. (`roster` CLI prints a real JSON list for this repo.)

- [ ] **Step 5: Commit**

```bash
git add scripts/office/office_status.py scripts/office/tests/test_status.py
git commit -m "feat(office): roster CLI — reads real git+bus, emits per-brother status JSON"
```

---

## Task 3: `/roster` endpoint + presence panel in the GUI

**Files:**
- Modify: `scripts/office/office_web.py` (import `office_status`; add `/roster` GET; add panel HTML/CSS/JS)

- [ ] **Step 1: Add the `/roster` endpoint.** In `scripts/office/office_web.py`, after the existing `import office_bus` line (line 27), add:

```python
import office_status  # noqa: E402  (derived-status engine; shares bus path)
```

Then in `do_GET`, immediately before the final `self._send(404, ...)` line, add:

```python
        if self.path.startswith("/roster"):
            try:
                data = office_status.roster_now()
            except Exception:
                data = []
            self._send(200, json.dumps({"roster": data}))
            return
```

- [ ] **Step 2: Add the panel markup.** In the `PAGE` string, change `<body>` layout so the chat sits beside a roster rail. Replace the `<body>` opening and the `<div id="log">` block:

Replace:
```html
<body>
  <header>
```
with:
```html
<body>
  <header>
```
(unchanged header) — then replace the single log line:
```html
  <div id="log"><div class="empty">No messages yet. Say something below.</div></div>
```
with:
```html
  <div id="main">
    <aside id="roster"><div class="rhead">BROTHERHOOD</div><div id="rlist"></div></aside>
    <div id="log"><div class="empty">No messages yet. Say something below.</div></div>
  </div>
```

- [ ] **Step 3: Add the panel CSS.** In the `<style>` block, before `</style>`, add:

```css
  #main{flex:1;display:flex;min-height:0;}
  #roster{width:236px;flex:0 0 236px;background:#0e1822;border-right:1px solid var(--line);
          overflow-y:auto;padding:8px 0;}
  #roster .rhead{color:var(--dim);font-size:10.5px;letter-spacing:1.4px;padding:4px 14px 8px;}
  .rcard{padding:8px 14px;border-bottom:1px solid #0c151c;display:flex;gap:9px;align-items:flex-start;}
  .rdot{width:9px;height:9px;border-radius:50%;margin-top:4px;flex:0 0 9px;background:#3a4a57;}
  .rdot.on{background:#6ec96e;box-shadow:0 0 6px #6ec96e88;}
  .rmeta{flex:1;min-width:0;}
  .rname{font-size:13px;font-weight:600;display:flex;align-items:center;gap:6px;}
  .rrole{color:var(--dim);font-size:11px;margin-top:1px;}
  .rline{color:#9fb0bd;font-size:11px;margin-top:3px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}
  .rarc{color:#f2c94c;font-size:10.5px;}
  .badge{font-size:9.5px;padding:1px 5px;border-radius:4px;font-weight:600;}
  .badge.nudge{background:#3a2c12;color:#e0a24c;}
  .badge.blocked{background:#3a1414;color:#e87676;}
  #log{flex:1;}
```

- [ ] **Step 4: Add the panel JS.** Before `poll();` near the end of the `<script>`, add:

```javascript
const rlist = document.getElementById('rlist');
function ago(s){
  if (s == null) return '';
  if (s < 60) return s + 's';
  if (s < 3600) return Math.floor(s/60) + 'm';
  if (s < 86400) return Math.floor(s/3600) + 'h';
  return Math.floor(s/86400) + 'd';
}
function renderRoster(list){
  rlist.innerHTML = '';
  for (const r of list){
    const card = document.createElement('div');
    card.className = 'rcard';
    const bits = [];
    if (r.last_said) bits.push('"' + esc(r.last_said) + '" ' + ago(r.last_said_sec));
    else if (r.last_commit) bits.push('committed ' + ago(r.last_commit_sec));
    const arc = r.current_arc ? '<span class="rarc">#' + esc(r.current_arc) + '</span>' : '';
    const nudge = r.wakeable ? '' : '<span class="badge nudge" title="not auto-wakeable">nudge</span>';
    const blocked = r.blocked ? '<span class="badge blocked">blocked</span>' : '';
    card.innerHTML =
      '<div class="rdot ' + (r.present ? 'on' : '') + '"></div>' +
      '<div class="rmeta">' +
        '<div class="rname">' + esc(labelFor(r.agent)) + ' ' + arc + ' ' + nudge + ' ' + blocked + '</div>' +
        '<div class="rrole">' + esc(r.role) + '</div>' +
        '<div class="rline">' + (bits.join(' · ') || '<span style="color:#5a6b78">idle</span>') + '</div>' +
      '</div>';
    rlist.appendChild(card);
  }
}
async function pollRoster(){
  try {
    const r = await fetch('/roster?_=' + Date.now(), {cache:'no-store'});
    const data = await r.json();
    if (data.roster) renderRoster(data.roster);
  } catch (e) {}
}
pollRoster();
setInterval(pollRoster, 4000);
```

- [ ] **Step 5: Verify it renders.** Start the server and confirm the endpoint + page work:

```bash
python scripts/office/office_web.py 8788 &
sleep 1
curl -s "http://127.0.0.1:8788/roster" | python -c "import sys,json; d=json.load(sys.stdin); print('roster entries:', len(d['roster'])); print('keys ok:', {'agent','present','role','wakeable'} <= set(d['roster'][0].keys()))"
curl -s "http://127.0.0.1:8788/" | grep -c "id=\"roster\""
kill %1 2>/dev/null
```
Expected: `roster entries: 9` (or more), `keys ok: True`, and grep prints `1` (panel present in HTML).

- [ ] **Step 6: Commit**

```bash
git add scripts/office/office_web.py
git commit -m "feat(office): /roster endpoint + live presence panel (derived status in the GUI)"
```

---

## Task 4: Standalone Chromium app-window

**Files:**
- Modify: `open_office.bat`
- Create: `scripts/office/office.webmanifest`
- Modify: `scripts/office/office_web.py` (serve the manifest + link it)

- [ ] **Step 1: Rewrite `open_office.bat`** to start the server first, then open a standalone Chromium app-window (Edge → Chrome → default browser fallback). Replace the whole file with:

```bat
@echo off
:: The Office — one-click launcher. Serves the local bus GUI and opens it in its
:: OWN standalone Chromium app-window (no tabs / no address bar), falling back to
:: the default browser if neither Edge nor Chrome is found.
cd /d "%~dp0"
set "OFFICE_URL=http://127.0.0.1:8787"

:: Start the server in its own console window (so this script can launch the UI).
start "The Office (server)" cmd /c "python scripts\office\office_web.py 8787"

:: Give the server a moment to bind the port before the window loads.
ping -n 2 127.0.0.1 >nul

:: Prefer Edge (ships on every Win11), then Chrome, then default browser.
set "EDGE=%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe"
set "CHROME=%ProgramFiles%\Google\Chrome\Application\chrome.exe"
if exist "%EDGE%" (
  start "" "%EDGE%" --app=%OFFICE_URL%
) else if exist "%CHROME%" (
  start "" "%CHROME%" --app=%OFFICE_URL%
) else (
  start "" %OFFICE_URL%
)
```

- [ ] **Step 2: Create the PWA manifest** `scripts/office/office.webmanifest`:

```json
{
  "name": "The Office",
  "short_name": "Office",
  "description": "The brotherhood's live coordination room.",
  "start_url": "/",
  "display": "standalone",
  "background_color": "#0b141a",
  "theme_color": "#17212b"
}
```

- [ ] **Step 3: Serve the manifest + link it.** In `office_web.py`, in `do_GET`, before the `/roster` block, add:

```python
        if self.path.startswith("/office.webmanifest"):
            path = os.path.join(HERE, "office.webmanifest")
            try:
                with open(path, "r", encoding="utf-8") as f:
                    self._send(200, f.read(), "application/manifest+json")
            except OSError:
                self._send(404, json.dumps({"error": "no manifest"}))
            return
```

And in the `PAGE` `<head>`, after the `<title>` line, add:

```html
<link rel="manifest" href="/office.webmanifest">
<meta name="theme-color" content="#17212b">
```

- [ ] **Step 4: Verify the manifest serves.**

```bash
python scripts/office/office_web.py 8788 &
sleep 1
curl -s "http://127.0.0.1:8788/office.webmanifest" | python -c "import sys,json; print('manifest name:', json.load(sys.stdin)['name'])"
kill %1 2>/dev/null
```
Expected: `manifest name: The Office`. (The `--app=` window itself is a manual Hemanth smoke — see the closeout.)

- [ ] **Step 5: Commit**

```bash
git add open_office.bat scripts/office/office.webmanifest scripts/office/office_web.py
git commit -m "feat(office): standalone Chromium app-window (--app= launch + PWA manifest)"
```

---

## Task 5: Honesty surface — the blocker / real-talk lane

**Files:**
- Modify: `scripts/office/office_bus.py` (add `cmd_flag` + dispatch)
- Create: `scripts/office/office_flag.sh`
- Modify: `scripts/office/office_web.py` (render `blocked` kind distinctly)
- Test: `scripts/office/tests/test_status.py` (add `test_flag_subcommand`)

- [ ] **Step 1: Write the failing test.** Add to `test_status.py` and call from `main()`:

```python
def test_flag_subcommand():
    sand = tempfile.mkdtemp()
    env = dict(os.environ)
    env["OFFICE_DIR"] = sand
    env["OFFICE_BUS"] = os.path.join(sand, "bus.jsonl")
    env["OFFICE_CURSORS"] = os.path.join(sand, "cursors")
    env["OFFICE_SESSIONS"] = os.path.join(sand, "sessions.json")
    BUS_PY = os.path.join(HERE, "..", "office_bus.py")
    subprocess.run([sys.executable, BUS_PY, "join", "sess-b", "4"], env=env)
    subprocess.run([sys.executable, BUS_PY, "flag", "sess-b", "blocked on HTTP preload"], env=env)
    with open(env["OFFICE_BUS"], encoding="utf-8") as f:
        rec = json.loads([x for x in f if x.strip()][-1])
    check(rec["kind"] == "blocked", "flag: posts kind=blocked")
    check(rec["from"] == "agent4" and rec["to"] == "all", "flag: from resolved, to=all")
    check(rec["msg"] == "blocked on HTTP preload", "flag: message body intact")
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python scripts/office/tests/test_status.py`
Expected: FAIL — `office_bus.py: unknown subcommand flag` (nonzero; record assertions fail).

- [ ] **Step 3: Implement `cmd_flag`.** In `office_bus.py`, after `cmd_send` (line ~217), add:

```python
def cmd_flag(sid, msg):
    """Post a BLOCKER to the room (the honesty / real-talk lane): kind='blocked',
    to='all', so it surfaces distinctly + marks the brother blocked in the roster."""
    frm = _agent_for(sid)
    if not frm:
        sys.exit("office flag: tab not registered — run office_join first")
    cmd_append(frm, "all", "blocked", "null", msg)  # prints seq
```

In `main`, add a dispatch branch (after the `send` branch):

```python
    elif cmd == "flag":
        cmd_flag(*rest)
```

And add `flag` to the usage string on line 403:
```python
    sys.exit("usage: office_bus.py <append|join|whoami|unseen|mark-seen|cursor|send|flag|deliver|drain|watch-peek|mirror-commit|close> ...")
```

- [ ] **Step 4: Create the wrapper** `scripts/office/office_flag.sh`:

```bash
#!/usr/bin/env bash
# The Office — raise a BLOCKER / be honest about being stuck (real-talk lane).
# Usage: office_flag.sh "what you're blocked on"
# FROM is resolved from this tab's registered identity.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SID="${CLAUDE_CODE_SESSION_ID:-${CLAUDE_SESSION_ID:-}}"
[ -z "$SID" ] && { echo "office_flag: no session id (CLAUDE_CODE_SESSION_ID unset)" >&2; exit 1; }
MSG="${1:-}"
[ -z "$MSG" ] && { echo "usage: office_flag.sh \"what you're blocked on\"" >&2; exit 1; }
python "$HERE/office_bus.py" flag "$SID" "$MSG"
```

- [ ] **Step 5: Render the blocked lane.** In `office_web.py`'s `render()`, the row currently ignores `kind`. After `const me = (m.from === 'hemanth');` add:

```javascript
    const kind = m.kind || 'chat';
```
Then change the `row.className` line to include a kind class:
```javascript
    row.className = 'row' + (me ? ' me' : '') + (grouped ? ' grouped' : '') + (kind !== 'chat' ? ' k-' + kind : '');
```
And add CSS in the `<style>` block before `</style>`:
```css
  .row.k-blocked .bubble{background:#2c1414;border-left:3px solid #e87676;}
  .row.k-blocked .bubble .name::after{content:" · BLOCKER";color:#e87676;font-size:10px;}
```

- [ ] **Step 6: Run the tests and make sure they pass**

Run: `python scripts/office/tests/test_status.py`
Expected: all `ok:`, `0 failure(s)`, exit 0.

- [ ] **Step 7: Commit**

```bash
git add scripts/office/office_bus.py scripts/office/office_flag.sh scripts/office/office_web.py scripts/office/tests/test_status.py
git commit -m "feat(office): honesty surface — flag/blocked lane (kind=blocked) + GUI styling"
```

---

## Task 6: Catch-and-mirror — auto-post every commit into the room

**Files:**
- Modify: `scripts/office/office_bus.py` (add `cmd_mirror_commit` + dispatch)
- Create: `scripts/office/git-post-commit-mirror.sh`
- Create: `scripts/office/install-office-hooks.sh`
- Modify: `scripts/office/office_web.py` (render `activity` kind muted)
- Test: `scripts/office/tests/test_status.py` (add `test_mirror_commit`)

- [ ] **Step 1: Write the failing test.** Add to `test_status.py` and call from `main()`:

```python
def test_mirror_commit():
    sand = tempfile.mkdtemp()
    env = dict(os.environ)
    env["OFFICE_DIR"] = sand
    env["OFFICE_BUS"] = os.path.join(sand, "bus.jsonl")
    env["OFFICE_CURSORS"] = os.path.join(sand, "cursors")
    env["OFFICE_SESSIONS"] = os.path.join(sand, "sessions.json")
    BUS_PY = os.path.join(HERE, "..", "office_bus.py")
    # mirror-commit takes (sha, subject) so it's testable without a real commit.
    subprocess.run([sys.executable, BUS_PY, "mirror-commit", "abc1234",
                    "[Agent 4, stream] fix preload race"], env=env)
    with open(env["OFFICE_BUS"], encoding="utf-8") as f:
        rec = json.loads([x for x in f if x.strip()][-1])
    check(rec["kind"] == "activity", "mirror: posts kind=activity")
    check(rec["from"] == "agent4", "mirror: from = agent parsed from [Agent N tag")
    check("abc1234" in rec["msg"], "mirror: msg includes short sha")
    # Untagged commit -> from 'system', still mirrored.
    subprocess.run([sys.executable, BUS_PY, "mirror-commit", "def5678",
                    "merge branch master"], env=env)
    with open(env["OFFICE_BUS"], encoding="utf-8") as f:
        rec2 = json.loads([x for x in f if x.strip()][-1])
    check(rec2["from"] == "system", "mirror: untagged commit -> from=system")
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python scripts/office/tests/test_status.py`
Expected: FAIL — `office_bus.py: unknown subcommand mirror-commit`.

- [ ] **Step 3: Implement `cmd_mirror_commit`.** In `office_bus.py`, after `cmd_flag`, add:

```python
_COMMIT_AGENT_RE = re.compile(r"^\[Agent\s*#?\s*(\d+)", re.IGNORECASE)


def cmd_mirror_commit(sha, *subject_parts):
    """Mirror a commit into the room as an 'activity' line so off-channel WORK is
    never invisible. FROM is the agent parsed from the '[Agent N' subject tag, or
    'system' if untagged. Called by the git post-commit hook with (sha, subject)."""
    subject = " ".join(subject_parts).strip()
    m = _COMMIT_AGENT_RE.match(subject)
    frm = ("agent" + m.group(1)) if m else "system"
    msg = "{0} ({1})".format(subject[:80], sha)
    cmd_append(frm, "all", "activity", "null", msg)  # prints seq
```

(`re` is already imported at line 219.) In `main`, add after the `flag` branch:

```python
    elif cmd == "mirror-commit":
        cmd_mirror_commit(*rest)
```

- [ ] **Step 4: Create the hook body** `scripts/office/git-post-commit-mirror.sh`:

```bash
#!/usr/bin/env bash
# Git post-commit hook -> mirror the new commit into the Office as an activity
# line. Best-effort + always exit 0 (a messaging failure must NEVER break a commit).
HERE="$(cd "$(dirname "$0")" && pwd)"
SHA="$(git rev-parse --short HEAD 2>/dev/null)"
SUBJ="$(git log -1 --pretty=%s 2>/dev/null)"
[ -n "$SHA" ] && python "$HERE/office_bus.py" mirror-commit "$SHA" "$SUBJ" >/dev/null 2>&1
exit 0
```

- [ ] **Step 5: Create the installer** `scripts/office/install-office-hooks.sh`:

```bash
#!/usr/bin/env bash
# Install the Office git hooks into .git/hooks (reproducible: the SOURCE lives in
# scripts/office/, tracked; this just wires it into the local repo's .git/hooks).
# If a post-commit hook already exists and isn't ours, we append a call (chain).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(git -C "$HERE" rev-parse --show-toplevel)"
HOOK="$REPO/.git/hooks/post-commit"
SRC="$HERE/git-post-commit-mirror.sh"
MARK="# >>> office mirror >>>"

if [ -f "$HOOK" ] && grep -q "$MARK" "$HOOK"; then
  echo "office hooks: already installed"
  exit 0
fi

if [ ! -f "$HOOK" ]; then
  printf '#!/usr/bin/env bash\n' > "$HOOK"
fi
{
  echo "$MARK"
  echo "bash \"$SRC\" \"\$@\""
  echo "# <<< office mirror <<<"
} >> "$HOOK"
chmod +x "$HOOK"
echo "office hooks: installed post-commit mirror -> $HOOK"
```

- [ ] **Step 6: Render `activity` lines muted.** Add CSS in `office_web.py`'s `<style>` before `</style>`:

```css
  .row.k-activity{margin-top:4px;}
  .row.k-activity .bubble{background:transparent;border:1px dashed #243240;box-shadow:none;opacity:.82;}
  .row.k-activity .bubble .name::after{content:" · committed";color:#6ec9cb;font-size:10px;}
```
(The `k-activity` class is already emitted by the Task 5 `row.className` change — no JS change needed.)

- [ ] **Step 7: Run the tests, then install + live-verify the hook**

Run: `python scripts/office/tests/test_status.py`
Expected: all `ok:`, `0 failure(s)`.

Then install and verify end-to-end against the real bus:
```bash
bash scripts/office/install-office-hooks.sh
echo "mirror probe $(date +%s)" > /tmp/office_probe.txt && git add docs/superpowers/plans/2026-05-31-the-office-app-slice1-legible-reliable-room.md
git commit -m "[Agent 0, office] mirror hook probe" --only docs/superpowers/plans/2026-05-31-the-office-app-slice1-legible-reliable-room.md
python -c "import json; recs=[json.loads(l) for l in open('agents/bus.jsonl',encoding='utf-8') if l.strip()] if __import__('os').path.exists('agents/bus.jsonl') else []; act=[r for r in recs if r.get('kind')=='activity']; print('activity lines:', len(act)); print('last:', act[-1]['from'], act[-1]['msg']) if act else print('none')"
```
Expected: at least one `activity` line, `from agent0`, msg containing "mirror hook probe". (If `agents/bus.jsonl` doesn't exist because the office isn't open, the mirror still appends it — that's fine.)

- [ ] **Step 8: Commit**

```bash
git add scripts/office/office_bus.py scripts/office/git-post-commit-mirror.sh scripts/office/install-office-hooks.sh scripts/office/office_web.py scripts/office/tests/test_status.py
git commit -m "feat(office): catch-and-mirror — git post-commit auto-posts activity into the room"
```

---

## Task 7: Closeout — full suite green, docs, memory

**Files:**
- Verify: both test scripts
- Modify: `docs/superpowers/specs/2026-05-31-the-office-app-design.md` (mark Slice 1 shipped)
- Create/Update: memory `project_the_office_live_agent_bus` (Slice 1 note) — via memory-write

- [ ] **Step 1: Run BOTH office test scripts; confirm green**

```bash
python scripts/office/tests/test_office.py && python scripts/office/tests/test_status.py
```
Expected: both end with no `FAIL:` lines (`test_office.py` prints its existing oks; `test_status.py` ends `0 failure(s)`), exit 0.

- [ ] **Step 2: Manual app-window smoke (Hemanth or agent-driven).** Launch and eyeball the standalone window:

```bash
cmd /c open_office.bat
```
Confirm: a borderless Chromium **app-window** (no tabs/address bar) opens; the **BROTHERHOOD** rail shows all brothers with green/grey presence dots, roles, current arc, last-said/last-commit; posting a normal message renders in the log; `bash scripts/office/office_flag.sh "test blocker"` shows a red BLOCKER row + flips that brother's roster badge to `blocked`; a commit shows a muted `· committed` activity row. Then close it (`scripts/office/stop-tankoban.ps1` is unrelated; just close the window + its server console).

- [ ] **Step 3: Mark Slice 1 shipped in the spec.** In `docs/superpowers/specs/2026-05-31-the-office-app-design.md` §8, change the `Slice 1` bullet's leading text from `**Slice 1 — The legible, reliable room.**` to `**Slice 1 — The legible, reliable room. ✅ SHIPPED 2026-05-31.**`

- [ ] **Step 4: Commit the closeout**

```bash
git add docs/superpowers/specs/2026-05-31-the-office-app-design.md
git commit -m "docs(office): Slice 1 (legible reliable room) shipped — mark in spec"
```

- [ ] **Step 5: Record the memory.** Use the memory-write skill to append a Slice-1-shipped note to `project_the_office_live_agent_bus` (new `office_status.py` engine, `/roster` panel, `flag`/blocked lane, commit-mirror hook, standalone app-window) and link `[[project_agentchattr_shelved_2026-05-28]]`. Off-git; no commit.

---

## Self-Review

**Spec coverage (§ → task):**
- §2 cheap-context rule (derive, $0) → Tasks 1–2 (pure Python over git+bus, zero tokens). ✓
- §4 status from ground truth → Tasks 1–3. ✓
- §4 channel discipline / catch-and-mirror → Task 6 (deterministic commit-mirror; *literal prose mirror is explicitly deferred — see Scope note below*). ✓ (scoped)
- §4 reachability honesty → Task 1 roster `wakeable` + Task 3 `nudge` badge. ✓
- §5 honesty culture surface → Task 5 (flag/blocked lane + GUI). ✓
- §9 standalone Chromium window now, Electron later → Task 4. ✓
- §9 reuse `office_web.py` HTML seed → Tasks 3–6 extend it in place. ✓
- Proof/testing discipline → Tasks 1/2/5/6 TDD + Task 7 suite + manual smoke. ✓

**Scope note (honest boundary, matches spec §4 intent):** Slice 1's "catch-and-mirror" mirrors **ground-truth activity** (every commit → an `activity` line) and surfaces off-channel work in the roster from git — deterministic and reliable. Mirroring the *literal VS Code prose* of an agent's reply requires transcript classification (fuzzy) and is deferred to a later slice. The room is never *blind* to off-channel work in Slice 1; it just summarizes work as commits/activity rather than quoting chat. Flagged for Hemanth at plan time.

**Placeholder scan:** no TBD/TODO; every code step shows complete code; expected outputs given. ✓

**Type/name consistency:** `compute_roster` dict keys identical across Task 1 (def), Task 2 (CLI), Task 3 (endpoint + panel JS), and tests. `kind` values (`chat`/`blocked`/`activity`) consistent across `cmd_flag`/`cmd_mirror_commit`/`bus_activity`/render CSS. Subcommands (`flag`, `mirror-commit`) added to dispatch + usage string. ✓

**Deferred to later slices (NOT in this plan):** owned SDK workers, foreman A/B, autonomous-mode lifecycle, MCP traffic-cop, holding-area/green-gate/kill-switch, cross-engine workers, Electron, discussion mode.
