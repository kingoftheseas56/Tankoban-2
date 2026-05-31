# THE OFFICE — Slice 1.5a: Status Honesty — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the roster tell the *whole truth* about each brother — replace the binary present/idle dot with a graded, source-labeled status ("active · said 2m ago" / "quiet · commit 45m ago · wake DOWN" / "cold · no signal") so the rail never looks confident when it's actually guessing.

**Architecture:** A new pure function `derive_status()` in `office_status.py` grades each brother from the *freshest* signal across {last message, last commit} into a tier (active/recent/quiet/cold), builds an honest human label that names the signal's *source + age* and folds in wake-reachability, and `compute_roster` exposes `status` + `status_label` per entry. The roster UI colors the presence dot by tier and shows the honest label. Pure-logic, TDD'd; no new infrastructure.

**Tech Stack:** Python 3.12 stdlib (extends the existing `office_status.py` derived-status engine), HTML/CSS/JS in `office_web.py`'s inline `PAGE`, the standalone-script test harness (`tests/test_status.py`).

**Spec:** `docs/superpowers/specs/2026-05-31-the-office-app-design.md` §2.1 (status honesty — "derived status must show its SOURCE and FRESHNESS, never a bare confident claim").

**Scope (YAGNI — deliberately narrow):** This is Slice 1.5**a**, the status-honesty piece only. Deferred to their own future slices, NOT built here: the Room State Store (earns its place only with high-frequency foreman events that don't exist yet), UI asset extraction (speculative until a second surface), discussion-mode evidence capture, MCP-lease readout (needs the Tankoban app up), and per-agent file attribution (impossible under flat-on-master's single shared working tree).

**Governance:** Flat-on-master, no worktree (gov-v13). Python-only; run tests with `python`, not `ctest`.

---

## File Structure

**Modify:**
- `scripts/office/office_status.py` — add `STATUS_TIERS` + `_ago()` + `freshest_signal()` + `derive_status()` pure functions; wire `status` + `status_label` into `compute_roster`'s output dict.
- `scripts/office/office_web.py` — color the presence dot by tier (4 states, not binary) + render the honest `status_label`.
- `scripts/office/tests/test_status.py` — tests for `derive_status` + the two new canonical keys.

**Canonical additions to the status dict** (keep these exact keys everywhere):
```python
"status": "active" | "recent" | "quiet" | "cold",   # graded tier
"status_label": "active · said 2m ago · wake DOWN", # honest human summary (source + age + reachability)
```

---

## Task 1: `derive_status` — the graded, honest pure logic

**Files:**
- Modify: `scripts/office/office_status.py`
- Test: `scripts/office/tests/test_status.py`

- [ ] **Step 1: Write the failing test.** Add to `scripts/office/tests/test_status.py` and call `test_derive_status()` from `main()` (before the `print` line):

```python
def test_derive_status():
    # tiers: active <=300s, recent <=1800s, quiet <=7200s, else cold.
    # freshest signal across (last_said_sec, last_commit_sec) drives the grade.
    tier, label = office_status.derive_status(120, 900, True)
    check(tier == "active", "derive: freshest signal (said 120s) -> active")
    check(label == "active · said 2m ago", "derive: label names source+age, said wins")
    tier, label = office_status.derive_status(5000, 600, True)
    check(tier == "recent", "derive: commit 600s is freshest -> recent")
    check(label == "recent · commit 10m ago", "derive: commit label")
    tier, label = office_status.derive_status(4000, None, True)
    check(tier == "quiet", "derive: said 4000s -> quiet")
    tier, label = office_status.derive_status(99999, 99999, True)
    check(tier == "cold", "derive: both >2h -> cold")
    tier, label = office_status.derive_status(None, None, True)
    check(tier == "cold" and label == "cold · no signal", "derive: no signal -> cold/no-signal")
    # wake-reachability folds into the label as an honest warning
    tier, label = office_status.derive_status(120, None, False)
    check(label == "active · said 2m ago · wake DOWN",
          "derive: wake-dead appends ' · wake DOWN' even when active")
    check(office_status.derive_status(120, None, True)[1].endswith("ago"),
          "derive: wake-live label carries NO wake warning")
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python scripts/office/tests/test_status.py`
Expected: FAIL — `AttributeError: module 'office_status' has no attribute 'derive_status'`.

- [ ] **Step 3: Write the implementation.** In `scripts/office/office_status.py`, immediately after the `HEARTBEAT_WINDOW_SEC` constant block, add:

```python
# Graded status tiers (seconds): the freshest signal's age picks the tier.
STATUS_TIERS = (("active", 300), ("recent", 1800), ("quiet", 7200))  # else "cold"


def _ago(sec):
    """Seconds -> compact human age ('2m', '3h', '4d'). Mirrors the GUI's ago()."""
    if sec is None:
        return ""
    if sec < 60:
        return "{0}s".format(int(sec))
    if sec < 3600:
        return "{0}m".format(int(sec // 60))
    if sec < 86400:
        return "{0}h".format(int(sec // 3600))
    return "{0}d".format(int(sec // 86400))


def freshest_signal(last_said_sec, last_commit_sec):
    """Return (age_sec, source) for the most-recent signal, or (None, None)."""
    cands = []
    if last_said_sec is not None:
        cands.append((last_said_sec, "said"))
    if last_commit_sec is not None:
        cands.append((last_commit_sec, "commit"))
    if not cands:
        return (None, None)
    return min(cands, key=lambda c: c[0])


def derive_status(last_said_sec, last_commit_sec, wake_alive):
    """Grade a brother into a tier + an HONEST label that names the signal's
    source + age and folds in wake-reachability. Never claims certainty the
    signals don't support (spec §2.1)."""
    age, source = freshest_signal(last_said_sec, last_commit_sec)
    if age is None:
        tier, label = "cold", "cold · no signal"
    else:
        tier = "cold"
        for name, bound in STATUS_TIERS:
            if age <= bound:
                tier = name
                break
        label = "{0} · {1} {2} ago".format(tier, source, _ago(age))
    if not wake_alive:
        label += " · wake DOWN"
    return tier, label
```

- [ ] **Step 4: Run the tests and make sure they pass**

Run: `python scripts/office/tests/test_status.py`
Expected: all `ok:`, ends `0 failure(s)`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add scripts/office/office_status.py scripts/office/tests/test_status.py
git commit -m "feat(office): derive_status — graded, source-labeled honest status (pure logic)"
```

---

## Task 2: Wire `status` + `status_label` into `compute_roster`

**Files:**
- Modify: `scripts/office/office_status.py:101-138` (the `compute_roster` body)
- Test: `scripts/office/tests/test_status.py`

- [ ] **Step 1: Write the failing test.** In `test_status.py`'s existing `test_compute_roster()`, after the existing `wake_age_sec` assertions, add:

```python
    check(by["agent4"]["status"] == "active", "roster: agent4 status=active (said 120s)")
    check(by["agent4"]["status_label"] == "active · said 2m ago",
          "roster: agent4 honest label (wake live -> no warning)")
    check(by["agent1"]["status"] == "cold", "roster: agent1 status=cold (said 99999s)")
    check("wake DOWN" in by["agent1"]["status_label"],
          "roster: agent1 label warns wake DOWN (heartbeat 900s > window)")
    check(by["agent7"]["status"] == "cold" and by["agent7"]["status_label"] == "cold · no signal · wake DOWN",
          "roster: agent7 no signal + no heartbeat -> cold/no-signal/wake-down")
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python scripts/office/tests/test_status.py`
Expected: FAIL — `KeyError: 'status'` in `test_compute_roster`.

- [ ] **Step 3: Write the implementation.** In `scripts/office/office_status.py`, inside `compute_roster`'s loop, after the `wake_alive = ...` line and before `last_commit = None`, add:

```python
        status, status_label = derive_status(bus_sec, com_sec, wake_alive)
```

Then in the appended dict, add the two keys right after `"wake_age_sec": wake_age,`:

```python
            "status": status,
            "status_label": status_label,
```

- [ ] **Step 4: Run the tests and make sure they pass**

Run: `python scripts/office/tests/test_status.py`
Expected: all `ok:`, `0 failure(s)`.

- [ ] **Step 5: Update the CLI canonical-keys test.** In `test_status.py`'s `test_roster_cli()`, extend the `need` set to include the new keys:

```python
        need = {"agent", "role", "engine", "wakeable", "present", "wake_alive",
                "wake_age_sec", "status", "status_label", "current_arc", "last_said",
                "last_said_sec", "last_commit", "last_commit_sec", "blocked"}
```

- [ ] **Step 6: Run tests + real CLI smoke**

Run: `python scripts/office/tests/test_status.py`
Expected: `0 failure(s)`.

Run: `python scripts/office/office_status.py roster | python -c "import sys,json; d=json.load(sys.stdin); print(d[0]['agent'], '->', d[0]['status'], '|', d[0]['status_label'])"`
Expected: a real line like `agent0 -> cold | cold · no signal · wake DOWN` (exact tier depends on live state).

- [ ] **Step 7: Commit**

```bash
git add scripts/office/office_status.py scripts/office/tests/test_status.py
git commit -m "feat(office): compute_roster exposes graded status + honest status_label"
```

---

## Task 3: Roster UI — color the dot by tier + show the honest label

**Files:**
- Modify: `scripts/office/office_web.py` (`renderRoster` + the `.pdot` CSS)

- [ ] **Step 1: Add tier dot colors.** In `office_web.py`'s `<style>`, replace the existing two `.pdot` state rules:

```css
  .pdot.active{background:var(--green);}
  .pdot.blocked{background:var(--red);animation:pulse 2s ease-in-out infinite;}
```

with the graded set:

```css
  .pdot.active{background:var(--green);}
  .pdot.recent{background:#5FA86B;}
  .pdot.quiet{background:var(--warn);}
  .pdot.cold{background:#667781;}
  .pdot.blocked{background:var(--red);animation:pulse 2s ease-in-out infinite;}
  .rstatus{font-size:11px;color:var(--txt2);margin-top:3px;}
  .rstatus.active,.rstatus.recent{color:#8FcF9c;}
  .rstatus.quiet{color:var(--warn);}
  .rstatus.cold{color:#6B7B86;}
```

- [ ] **Step 2: Update `dotClass` to use the tier.** In `office_web.py`'s `<script>`, replace:

```javascript
function dotClass(r){ return r.blocked ? 'blocked' : (r.present ? 'active' : ''); }
```

with:

```javascript
function dotClass(r){ return r.blocked ? 'blocked' : (r.status || 'cold'); }
```

- [ ] **Step 3: Render the honest label.** In `renderRoster`, replace the `rsub` line of the card's `innerHTML`:

```javascript
        '<div class="rsub"><span class="rline">' + line + '</span>' + arc + nudge + blocked + wake + '</div>' +
```

with a version that adds the honest status label as its own line under the preview:

```javascript
        '<div class="rsub"><span class="rline">' + line + '</span>' + arc + nudge + blocked + wake + '</div>' +
        '<div class="rstatus ' + (r.status || 'cold') + '">' + esc(r.status_label || '') + '</div>' +
```

- [ ] **Step 4: Verify the page serves with the new fields.** Run the in-process verifier:

```bash
cd "c:/Users/Suprabha/Desktop/Tankoban 2" && python - <<'PY'
import threading, time, socket, urllib.request, json, sys, os
sys.path.insert(0, os.path.join("scripts","office"))
s=socket.socket(); s.bind(("127.0.0.1",0)); port=s.getsockname()[1]; s.close()
import office_web
from http.server import ThreadingHTTPServer
srv=ThreadingHTTPServer(("127.0.0.1",port), office_web.Handler)
threading.Thread(target=srv.serve_forever, daemon=True).start(); time.sleep(0.3)
g=lambda p: urllib.request.urlopen("http://127.0.0.1:%d%s"%(port,p),timeout=5).read().decode("utf-8")
page=g("/")
checks={
 "tier dot CSS": ".pdot.quiet" in page and ".pdot.cold" in page,
 "rstatus CSS": ".rstatus.quiet" in page,
 "dotClass uses status": "r.status || 'cold'" in page,
 "renders status_label": "status_label" in page,
 "/roster has status": "status_label" in json.dumps(json.loads(g("/roster"))["roster"][0]),
}
for k,v in checks.items(): print(("ok  " if v else "FAIL ")+k)
srv.shutdown()
print("ALL GREEN" if all(checks.values()) else "SOME FAILED")
PY
```
Expected: all `ok`, `ALL GREEN`.

- [ ] **Step 5: Commit**

```bash
git add scripts/office/office_web.py
git commit -m "feat(office): roster shows graded dot + honest status label (active/recent/quiet/cold)"
```

---

## Task 4: Closeout — suites green, restart live server, manual smoke

**Files:**
- Verify: both test scripts
- Restart: the live Office server on 8787

- [ ] **Step 1: Run BOTH office suites**

```bash
python scripts/office/tests/test_office.py && python scripts/office/tests/test_status.py
```
Expected: `test_office.py` prints its `ok:` lines with no `FAIL:`; `test_status.py` ends `0 failure(s)`; exit 0.

- [ ] **Step 2: Restart the live server with the new code**

```bash
cd "c:/Users/Suprabha/Desktop/Tankoban 2" && PIDS=$(netstat -ano 2>/dev/null | grep "127.0.0.1:8787" | grep LISTENING | awk '{print $5}' | sort -u); for p in $PIDS; do taskkill //F //PID $p >/dev/null 2>&1; done; sleep 1; python scripts/office/office_web.py 8787 >/tmp/office_sh.log 2>&1 &
sleep 2; curl -s -o /dev/null -w "live: HTTP %{http_code}\n" http://127.0.0.1:8787/
```
Expected: `live: HTTP 200`.

- [ ] **Step 3: Manual smoke (Hemanth).** Refresh the Office window (`Ctrl+R`). Confirm each brother's roster card now shows an honest status line under the preview ("active · said 2m ago", "quiet · commit 45m ago · wake DOWN", "cold · no signal"), and the presence dot is graded (green active → muted green recent → amber quiet → grey cold; red pulse if blocked). Verify a brother who is *present but wake-dead* reads clearly as reachable-no / "wake DOWN".

- [ ] **Step 4: No closeout commit needed** (Task 3 already committed the UI). The server restart is runtime-only.

---

## Self-Review

**Spec coverage (§2.1 status honesty):**
- "Derived status must show its SOURCE and FRESHNESS" → Task 1 `derive_status` label names source ("said"/"commit") + age ("2m ago"). ✓
- "Distinguish *known* from *inferred from absence*" → graded tiers + "cold · no signal" for the no-data case. ✓
- "Never imply certainty the signals don't support" → no bare "active" dot; every state carries its evidence + a wake-DOWN warning when unreachable. ✓
- Deferred §1.5 pieces (state store, UI extraction, discussion evidence, MCP lease) → explicitly out of scope, noted in header. ✓

**Placeholder scan:** no TBD/TODO; every code step shows complete code + expected output. ✓

**Type/name consistency:** `derive_status(last_said_sec, last_commit_sec, wake_alive)` signature identical in Task 1 (def), Task 2 (call), and tests. Keys `status` + `status_label` identical across Task 2 dict, Task 2 CLI test, Task 3 UI, and the verifier. Tier names (`active`/`recent`/`quiet`/`cold`) identical across `STATUS_TIERS`, the CSS classes, and `dotClass`. `_ago` mirrors the GUI `ago()` thresholds. ✓

**Deferred (NOT in this plan):** Room State Store, UI asset extraction, discussion-mode evidence capture, MCP-lease readout, repo-state strip, owned workers — each its own future slice when actually needed.
