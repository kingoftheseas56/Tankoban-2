# Multi-Engine Brother Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a shared `scripts/engines/` helper that lets any brother's Claude brain self-route work to DeepSeek (grunt), Codex (review), and Gemini (reader) as bare command-line tools — with a hard cost-cap, key hygiene, and per-call logging written exactly once — then prove it on a real Agent 4 Theatre task.

**Architecture:** A single Python CLI (`engine.py`) exposes three subcommands — `grunt` / `review` / `read` — each one-shot and stateless. Every call passes through a shared gate: packet-size guard (small-context rule) → cap check (per-wake hard / per-task soft) → engine dispatch → ledger log. DeepSeek runs via the existing `claude` CLI pointed at DeepSeek's endpoint; Codex via `codex exec`; Gemini via REST. Keys come from environment only, never from tracked files. A `wake-start` marker resets the per-wake counter. Gemini ships disabled behind a reliability gate.

**Tech Stack:** Python 3 (stdlib only — `subprocess`, `urllib`, `json`, `argparse`, `unittest`); the existing `claude` and `codex` CLIs; Gemini REST. Consistent with existing `scripts/office/*.py` tooling. No third-party deps.

**Spec:** `docs/superpowers/specs/2026-06-01-multi-engine-brother-design.md` (all 4 open questions resolved in §10).

**Ground rules:**
- Flat-on-master, no worktrees (gov-v13). Commit per task.
- Secrets in env only. Keys: `DEEPSEEK_API_KEY`, `GEMINI_API_KEY`. Never write a key into a tracked file or a log line.
- Run Python as `python` (fallback `py -3`). Run tests with `python -m unittest discover -s scripts/engines -p "test_*.py"`.

---

## File Structure

| File | Responsibility |
|---|---|
| `scripts/engines/engine.py` | The CLI + all logic: gate (packet guard, cap check), dispatch, ledger, parsers. Single source of truth. |
| `scripts/engines/engines.config.json` | Tunables: endpoints, model names, cap numbers, timeouts, `gemini.enabled` flag. No secrets. |
| `scripts/engines/test_engine.py` | Unit tests (stdlib `unittest`), all runnable offline via `ENGINE_DRY_RUN=1` + fixtures. |
| `scripts/engines/gemini_gate.py` | Standalone reliability probe that flips `gemini.enabled` true/false. |
| `scripts/engines/README.md` | How a brother sources the helper; the routing rule; the cost-cap contract. |
| `scripts/engines/.ledger.jsonl` | Per-call log (gitignored). Created at runtime. |
| `.gitignore` | Add the ledger path. |

---

### Task 1: Scaffold directory, config, gitignore

**Files:**
- Create: `scripts/engines/engines.config.json`
- Create: `scripts/engines/README.md`
- Modify: `.gitignore` (append one line)

- [ ] **Step 1: Write the config file**

Create `scripts/engines/engines.config.json`:

```json
{
  "deepseek": {
    "base_url": "https://api.deepseek.com/anthropic",
    "model": "deepseek-v4-pro"
  },
  "codex": {
    "model": "gpt-5.5"
  },
  "gemini": {
    "url": "https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent",
    "model": "gemini-2.5-flash",
    "enabled": false
  },
  "caps": {
    "per_wake_hard": 25,
    "per_task_soft": 8,
    "max_packet_chars": 8000
  },
  "timeouts": {
    "deepseek": 300,
    "codex": 300,
    "gemini": 120
  }
}
```

- [ ] **Step 2: Write the README**

Create `scripts/engines/README.md`:

```markdown
# scripts/engines — the multi-engine brother helper

One Claude brain (the brother), three engines on tap. Spec:
`docs/superpowers/specs/2026-06-01-multi-engine-brother-design.md`.

## Use it
At wake start (once):       python scripts/engines/engine.py wake-start
Grunt (DeepSeek):           python scripts/engines/engine.py grunt  --task T1 --purpose "write fn" "<tiny packet>"
Review (Codex):             python scripts/engines/engine.py review --task T1 --purpose "sign off" "<diff + ask>"
Read  (Gemini, if enabled): python scripts/engines/engine.py read   --task T1 --purpose "extract"  "<blob + ask>"
Status / spend:             python scripts/engines/engine.py status

## The routing rule (the brother decides, per task)
- grunt  -> mechanical edit / bulk parse / boilerplate            -> DeepSeek
- review -> a load-bearing decision/diff needs second eyes (rare) -> Codex
- read   -> chew a big log/file, hand back the slice              -> Gemini (if enabled)
- think  -> design / judgment / identity / important code         -> keep on Claude

## Contract
- Keys come from env only: DEEPSEEK_API_KEY, GEMINI_API_KEY. Never commit them.
- Packets must be tiny (small-context rule); oversized packets are refused.
- Hard cap ~25 calls/wake, soft ~8/task. Hitting the hard cap STOPS and asks.
```

- [ ] **Step 3: Gitignore the ledger**

Append to `.gitignore`:

```
# multi-engine brother runtime ledger (per-call spend log; never committed)
scripts/engines/.ledger.jsonl
```

- [ ] **Step 4: Verify the config parses**

Run: `python -c "import json; json.load(open('scripts/engines/engines.config.json')); print('config OK')"`
Expected: `config OK`

- [ ] **Step 5: Commit**

```bash
git add scripts/engines/engines.config.json scripts/engines/README.md .gitignore
git commit -m "feat(engines): scaffold multi-engine brother helper (config + README + gitignore)"
```

---

### Task 2: Ledger, wake marker, and cap enforcement

**Files:**
- Create: `scripts/engines/engine.py`
- Test: `scripts/engines/test_engine.py`

- [ ] **Step 1: Write the failing test**

Create `scripts/engines/test_engine.py`:

```python
import os, json, tempfile, unittest, importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("engine", os.path.join(HERE, "engine.py"))
engine = importlib.util.module_from_spec(spec)
spec.loader.exec_module(engine)

CFG = {"caps": {"per_wake_hard": 3, "per_task_soft": 2, "max_packet_chars": 100}}

class CapTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".jsonl")
        self.tmp.close()
        engine.LEDGER_PATH = self.tmp.name
        open(self.tmp.name, "w").close()  # empty ledger

    def tearDown(self):
        os.unlink(self.tmp.name)

    def test_wake_marker_resets_count(self):
        for _ in range(2):
            engine.log_call("deepseek", 10, "x", "T1")
        engine.mark_wake()
        rows = engine.ledger_rows_since_wake()
        calls = [r for r in rows if r.get("event") == "call"]
        self.assertEqual(len(calls), 0)

    def test_hard_cap_blocks(self):
        for _ in range(3):
            engine.log_call("deepseek", 10, "x", "T1")
        ok, msg = engine.check_cap("T1", CFG)
        self.assertFalse(ok)
        self.assertIn("hard cap", msg)

    def test_soft_cap_warns_but_allows(self):
        for _ in range(2):
            engine.log_call("deepseek", 10, "x", "T1")
        ok, msg = engine.check_cap("T1", CFG)
        self.assertTrue(ok)
        self.assertIn("soft cap", msg)

    def test_under_cap_clean(self):
        engine.log_call("deepseek", 10, "x", "T1")
        ok, msg = engine.check_cap("T1", CFG)
        self.assertTrue(ok)
        self.assertIsNone(msg)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest scripts/engines/test_engine.py -v`
Expected: FAIL — `engine.py` does not exist / `log_call` not defined.

- [ ] **Step 3: Write minimal implementation**

Create `scripts/engines/engine.py`:

```python
#!/usr/bin/env python3
"""Multi-engine brother helper: bare-tool access to DeepSeek/Codex/Gemini.

Spec: docs/superpowers/specs/2026-06-01-multi-engine-brother-design.md
Keys come from environment only (DEEPSEEK_API_KEY, GEMINI_API_KEY). Never logged.
"""
import os, sys, json, time

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(HERE, "engines.config.json")
LEDGER_PATH = os.path.join(HERE, ".ledger.jsonl")


def load_config():
    with open(CONFIG_PATH) as f:
        return json.load(f)


def _read_ledger():
    if not os.path.exists(LEDGER_PATH):
        return []
    rows = []
    with open(LEDGER_PATH) as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def ledger_rows_since_wake():
    rows = _read_ledger()
    last = 0
    for i, r in enumerate(rows):
        if r.get("event") == "wake-start":
            last = i + 1
    return rows[last:]


def log_call(engine_name, tokens, purpose, task):
    row = {"event": "call", "ts": int(time.time()),
           "engine": engine_name, "tokens": tokens,
           "purpose": purpose, "task": task}
    with open(LEDGER_PATH, "a") as f:
        f.write(json.dumps(row) + "\n")


def mark_wake():
    with open(LEDGER_PATH, "a") as f:
        f.write(json.dumps({"event": "wake-start", "ts": int(time.time())}) + "\n")


def check_cap(task, cfg):
    """Return (allowed: bool, message: str|None)."""
    calls = [r for r in ledger_rows_since_wake() if r.get("event") == "call"]
    wake_count = len(calls)
    task_count = len([r for r in calls if r.get("task") == task])
    hard = cfg["caps"]["per_wake_hard"]
    soft = cfg["caps"]["per_task_soft"]
    if wake_count >= hard:
        return (False, f"STOP: per-wake hard cap {hard} reached ({wake_count} calls). "
                       f"Reconsider before spending more.")
    if task_count >= soft:
        return (True, f"WARN: per-task soft cap {soft} reached for task '{task}' "
                      f"({task_count}). Continuing, but check scope.")
    return (True, None)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest scripts/engines/test_engine.py -v`
Expected: PASS — 4 tests OK.

- [ ] **Step 5: Commit**

```bash
git add scripts/engines/engine.py scripts/engines/test_engine.py
git commit -m "feat(engines): ledger + wake marker + cap enforcement (TDD)"
```

---

### Task 3: Key hygiene and packet-size guard

**Files:**
- Modify: `scripts/engines/engine.py` (add `require_key`, `guard_packet`)
- Test: `scripts/engines/test_engine.py` (add cases)

- [ ] **Step 1: Write the failing test**

Append to `scripts/engines/test_engine.py` (before the `if __name__` line):

```python
class GuardTest(unittest.TestCase):
    def test_packet_too_big_raises(self):
        with self.assertRaises(ValueError):
            engine.guard_packet("x" * 101, CFG)

    def test_packet_ok(self):
        engine.guard_packet("small", CFG)  # no raise

    def test_missing_key_raises(self):
        os.environ.pop("FAKE_KEY_XYZ", None)
        with self.assertRaises(RuntimeError):
            engine.require_key("FAKE_KEY_XYZ")

    def test_present_key_returns(self):
        os.environ["FAKE_KEY_XYZ"] = "sk-test"
        try:
            self.assertEqual(engine.require_key("FAKE_KEY_XYZ"), "sk-test")
        finally:
            os.environ.pop("FAKE_KEY_XYZ", None)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest scripts/engines/test_engine.py -v`
Expected: FAIL — `guard_packet` / `require_key` not defined.

- [ ] **Step 3: Write minimal implementation**

Append to `scripts/engines/engine.py`:

```python
def require_key(env_name):
    key = os.environ.get(env_name, "").strip()
    if not key:
        raise RuntimeError(f"{env_name} not set in environment. "
                           f"Refusing to call (key hygiene).")
    return key


def guard_packet(packet, cfg):
    limit = cfg["caps"]["max_packet_chars"]
    if len(packet) > limit:
        raise ValueError(f"packet {len(packet)} chars exceeds limit {limit}. "
                         f"Hand a SMALLER slice (small-context rule).")
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest scripts/engines/test_engine.py -v`
Expected: PASS — 8 tests OK.

- [ ] **Step 5: Commit**

```bash
git add scripts/engines/engine.py scripts/engines/test_engine.py
git commit -m "feat(engines): key hygiene + packet-size guard (TDD)"
```

---

### Task 4: Output parsers (Gemini JSON + Codex stdout)

**Files:**
- Modify: `scripts/engines/engine.py` (add `parse_gemini`, `parse_codex`)
- Test: `scripts/engines/test_engine.py` (add cases with real fixtures from the probe)

- [ ] **Step 1: Write the failing test**

Append to `scripts/engines/test_engine.py` (before `if __name__`):

```python
GEMINI_FIXTURE = {
    "candidates": [
        {"content": {"parts": [{"text": "3.2"}], "role": "model"},
         "finishReason": "STOP", "index": 0}
    ],
    "usageMetadata": {"totalTokenCount": 159},
    "modelVersion": "gemini-2.5-flash",
}

CODEX_FIXTURE = """Reading additional input from stdin...
OpenAI Codex v0.131.0
--------
workdir: C:\\Users\\Suprabha\\Desktop\\Tankoban 2
model: gpt-5.5
--------
user
Review this C++ function...
codex
APPROVE clamps values below 0 to 0, above 100 to 100, and returns in-range values unchanged.
tokens used
15,018
"""

class ParserTest(unittest.TestCase):
    def test_parse_gemini(self):
        self.assertEqual(engine.parse_gemini(GEMINI_FIXTURE), "3.2")

    def test_parse_codex_extracts_answer(self):
        out = engine.parse_codex(CODEX_FIXTURE)
        self.assertEqual(
            out,
            "APPROVE clamps values below 0 to 0, above 100 to 100, "
            "and returns in-range values unchanged.")

    def test_parse_codex_multiline_answer(self):
        sample = "banner\ncodex\nline one\nline two\ntokens used\n42\n"
        self.assertEqual(engine.parse_codex(sample), "line one\nline two")
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest scripts/engines/test_engine.py -v`
Expected: FAIL — `parse_gemini` / `parse_codex` not defined.

- [ ] **Step 3: Write minimal implementation**

Append to `scripts/engines/engine.py`:

```python
def parse_gemini(data):
    return data["candidates"][0]["content"]["parts"][0]["text"].strip()


def parse_codex(stdout):
    """codex exec prints a banner, then a lone 'codex' line, then the answer,
    then 'tokens used'. Extract the answer between them."""
    lines = stdout.splitlines()
    start = None
    for i, ln in enumerate(lines):
        if ln.strip() == "codex":
            start = i + 1  # last 'codex' marker wins
    if start is None:
        return stdout.strip()
    answer = []
    for ln in lines[start:]:
        if ln.strip() == "tokens used":
            break
        answer.append(ln)
    return "\n".join(answer).strip()
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest scripts/engines/test_engine.py -v`
Expected: PASS — 11 tests OK.

- [ ] **Step 5: Commit**

```bash
git add scripts/engines/engine.py scripts/engines/test_engine.py
git commit -m "feat(engines): output parsers for Gemini + Codex (TDD, probe fixtures)"
```

---

### Task 5: Engine callers + dispatch CLI (dry-run testable)

**Files:**
- Modify: `scripts/engines/engine.py` (add callers + `main`)
- Test: `scripts/engines/test_engine.py` (dry-run dispatch cases)

- [ ] **Step 1: Write the failing test**

Append to `scripts/engines/test_engine.py` (before `if __name__`):

```python
class DispatchTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".jsonl")
        self.tmp.close()
        engine.LEDGER_PATH = self.tmp.name
        open(self.tmp.name, "w").close()
        os.environ["ENGINE_DRY_RUN"] = "1"

    def tearDown(self):
        os.unlink(self.tmp.name)
        os.environ.pop("ENGINE_DRY_RUN", None)

    def test_grunt_dry_runs_and_logs(self):
        out = engine.dispatch("grunt", "tiny packet", "T1", "p", CFG)
        self.assertTrue(out.startswith("DRY:deepseek"))
        calls = [r for r in engine.ledger_rows_since_wake() if r.get("event") == "call"]
        self.assertEqual(calls[-1]["engine"], "deepseek")

    def test_review_dry_runs(self):
        out = engine.dispatch("review", "diff", "T1", "p", CFG)
        self.assertTrue(out.startswith("DRY:codex"))

    def test_read_blocked_when_gemini_disabled(self):
        cfg = dict(CFG)
        cfg["gemini"] = {"enabled": False}
        with self.assertRaises(RuntimeError):
            engine.dispatch("read", "blob", "T1", "p", cfg)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest scripts/engines/test_engine.py -v`
Expected: FAIL — `dispatch` not defined.

- [ ] **Step 3: Write minimal implementation**

Append to `scripts/engines/engine.py`:

```python
import subprocess, tempfile, urllib.request


def call_deepseek(packet, cfg):
    if os.environ.get("ENGINE_DRY_RUN") == "1":
        return "DRY:deepseek:" + packet[:20]
    scratch = tempfile.mkdtemp(prefix="ds_engine_")
    env = dict(os.environ)
    env.update({
        "CLAUDE_CONFIG_DIR": os.path.join(scratch, ".cfg"),
        "ANTHROPIC_BASE_URL": cfg["deepseek"]["base_url"],
        "ANTHROPIC_AUTH_TOKEN": require_key("DEEPSEEK_API_KEY"),
        "ANTHROPIC_API_KEY": "",
        "ANTHROPIC_MODEL": cfg["deepseek"]["model"],
        "ANTHROPIC_DEFAULT_OPUS_MODEL": cfg["deepseek"]["model"],
    })
    proc = subprocess.run(
        ["claude", "-p", packet, "--model", cfg["deepseek"]["model"]],
        cwd=scratch, env=env, capture_output=True, text=True,
        timeout=cfg["timeouts"]["deepseek"])
    return proc.stdout.strip()


def call_codex(packet, cfg):
    if os.environ.get("ENGINE_DRY_RUN") == "1":
        return "DRY:codex:" + packet[:20]
    proc = subprocess.run(
        ["codex", "exec", packet],
        stdin=subprocess.DEVNULL, capture_output=True, text=True,
        timeout=cfg["timeouts"]["codex"])
    return parse_codex(proc.stdout)


def call_gemini(packet, cfg):
    if os.environ.get("ENGINE_DRY_RUN") == "1":
        return "DRY:gemini:" + packet[:20]
    key = require_key("GEMINI_API_KEY")
    url = cfg["gemini"]["url"].format(model=cfg["gemini"]["model"]) + "?key=" + key
    body = json.dumps({"contents": [{"parts": [{"text": packet}]}]}).encode()
    req = urllib.request.Request(url, data=body,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=cfg["timeouts"]["gemini"]) as resp:
        return parse_gemini(json.load(resp))


def dispatch(cmd, packet, task, purpose, cfg):
    guard_packet(packet, cfg)
    if cmd == "grunt":
        out = call_deepseek(packet, cfg); name = "deepseek"
    elif cmd == "review":
        out = call_codex(packet, cfg); name = "codex"
    elif cmd == "read":
        if not cfg["gemini"].get("enabled"):
            raise RuntimeError("Gemini disabled (reliability gate not passed). "
                               "Read directly on Claude, or run gemini_gate.py.")
        out = call_gemini(packet, cfg); name = "gemini"
    else:
        raise ValueError(f"unknown command: {cmd}")
    log_call(name, len(out), purpose, task)
    return out


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(description="Multi-engine brother helper")
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("grunt", "review", "read"):
        p = sub.add_parser(name)
        p.add_argument("--task", required=True)
        p.add_argument("--purpose", default="")
        p.add_argument("packet")
    sub.add_parser("wake-start")
    sub.add_parser("status")
    args = ap.parse_args(argv)
    cfg = load_config()

    if args.cmd == "wake-start":
        mark_wake(); print("wake marked"); return 0
    if args.cmd == "status":
        calls = [r for r in ledger_rows_since_wake() if r.get("event") == "call"]
        tot = sum(r.get("tokens", 0) for r in calls)
        print(f"calls this wake: {len(calls)}/{cfg['caps']['per_wake_hard']} "
              f"| out-chars: {tot}")
        for r in calls:
            print(f"  {r['engine']:9} task={r['task']:6} {r['purpose']}")
        return 0

    ok, msg = check_cap(args.task, cfg)
    if msg:
        print(msg, file=sys.stderr)
    if not ok:
        return 2
    out = dispatch(args.cmd, args.packet, args.task, args.purpose, cfg)
    print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest scripts/engines/test_engine.py -v`
Expected: PASS — 14 tests OK.

- [ ] **Step 5: Commit**

```bash
git add scripts/engines/engine.py scripts/engines/test_engine.py
git commit -m "feat(engines): engine callers + dispatch CLI (dry-run TDD)"
```

---

### Task 6: Live wiring smoke (DeepSeek + Codex, real calls)

This is an integration check, not a unit test — it makes real engine calls (the probe, formalized). It validates the two known gotchas stay beaten.

**Files:**
- (none created) — manual verification + a recorded result note

- [ ] **Step 1: Set keys in the environment (not tracked)**

```bash
export DEEPSEEK_API_KEY=sk-...     # from start_agent9.bat line 10
export GEMINI_API_KEY=AIza...      # Hemanth-provided, rotating
```

- [ ] **Step 2: Mark wake + run a real grunt call**

Run:
```bash
python scripts/engines/engine.py wake-start
python scripts/engines/engine.py grunt --task SMOKE --purpose "write clamp" \
  "Write a C++ function: int clampVolume(int v) clamped to [0,100]. Output ONLY the function in a code block."
```
Expected: a `cpp` code block with a correct `clampVolume`, exit 0. (DeepSeek, via clean-scratch `claude -p`.)

- [ ] **Step 3: Run a real review call on that output**

Run:
```bash
python scripts/engines/engine.py review --task SMOKE --purpose "sign off" \
  "Reply one line APPROVE or REJECT with reason. int clampVolume(int v){if(v<0)return 0;if(v>100)return 100;return v;}"
```
Expected: `APPROVE ...` one line, exit 0. (Codex, stdin auto-closed by `subprocess.DEVNULL`.)

- [ ] **Step 4: Confirm the ledger logged both, with NO key leakage**

Run: `python scripts/engines/engine.py status` then `grep -i "sk-\|AIza" scripts/engines/.ledger.jsonl || echo "NO KEYS IN LEDGER"`
Expected: status shows 2 calls (deepseek, codex); grep prints `NO KEYS IN LEDGER`.

- [ ] **Step 5: Commit a result note**

Append a `## Live smoke 2026-..-..` section to `scripts/engines/README.md` recording the two calls succeeded + the no-key-leak confirmation, then:
```bash
git add scripts/engines/README.md
git commit -m "test(engines): live DeepSeek+Codex smoke recorded; no key leakage"
```

---

### Task 7: Gemini reliability gate

Gemini ships disabled. This standalone probe decides whether it flips on, per the resolved decision (§10.2).

**Files:**
- Create: `scripts/engines/gemini_gate.py`

- [ ] **Step 1: Write the gate script**

Create `scripts/engines/gemini_gate.py`:

```python
#!/usr/bin/env python3
"""Reliability gate for Gemini-as-reader. Runs N reader-extracts against known
answers; flips engines.config.json gemini.enabled true iff all pass."""
import os, sys, json, importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("engine", os.path.join(HERE, "engine.py"))
engine = importlib.util.module_from_spec(spec)
spec.loader.exec_module(engine)

CASES = [
    ("From this log, output ONLY the stall seconds as a number: "
     "[stream] underrun pts=842.5s, refill took 3.2s. Just the number.", "3.2"),
    ("Output ONLY the HTTP status code in this line as a number: "
     "GET /stream 200 OK 14ms. Just the number.", "200"),
    ("Output ONLY the episode number: 'One Piece S01E1089 unavailable'. "
     "Just the number.", "1089"),
]


def main():
    cfg = engine.load_config()
    cfg["gemini"]["enabled"] = True  # force-enable for the probe only
    engine.require_key("GEMINI_API_KEY")
    passed = 0
    for prompt, want in CASES:
        try:
            got = engine.call_gemini(prompt, cfg).strip()
        except Exception as e:
            print(f"FAIL (error): {e}"); continue
        ok = want in got
        print(f"{'PASS' if ok else 'FAIL'}: want '{want}' got '{got[:40]}'")
        passed += ok
    flip = passed == len(CASES)
    path = engine.CONFIG_PATH
    disk = json.load(open(path))
    disk["gemini"]["enabled"] = flip
    json.dump(disk, open(path, "w"), indent=2)
    print(f"\nGemini reliability: {passed}/{len(CASES)} -> "
          f"gemini.enabled = {flip}")
    return 0 if flip else 1


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run the gate (real Gemini calls)**

Run: `python scripts/engines/gemini_gate.py`
Expected: 3 PASS lines and `gemini.enabled = True` (or honest FAIL + stays `False`).

- [ ] **Step 3: Verify the config reflects the verdict**

Run: `python -c "import json; print('enabled =', json.load(open('scripts/engines/engines.config.json'))['gemini']['enabled'])"`
Expected: `enabled = True` if the gate passed.

- [ ] **Step 4: Commit**

```bash
git add scripts/engines/gemini_gate.py scripts/engines/engines.config.json
git commit -m "feat(engines): Gemini reliability gate; flips enabled per probe result"
```

---

### Task 8: Agent 4 pilot — one real Theatre task, self-routed

Proves the whole thing on a real own-lane task (acceptance criterion §11.3). Agent 4 picks a genuine small Theatre/Tankorent grunt task (e.g. a mechanical refactor or a parser tweak) and routes it through the helper.

**Files:**
- Depends on the task Agent 4 chooses; uses `scripts/engines/engine.py` only.

- [ ] **Step 1: Agent 4 plans the task on Claude**

Claude (Agent 4) decomposes a real small Theatre task into: one grunt unit (DeepSeek), one review unit (Codex). Write the two tiny packets. Confirm each packet < `max_packet_chars`.

- [ ] **Step 2: Run the grunt unit**

Run: `python scripts/engines/engine.py grunt --task THEATRE1 --purpose "<what>" "<packet>"`
Expected: usable output Claude folds into the edit.

- [ ] **Step 3: Claude lands the edit + builds**

Apply the DeepSeek output as the actual `src/` change; run `build_check.bat` (or `/build-verify`).
Expected: `BUILD OK`.

- [ ] **Step 4: Run the review unit on the diff**

Run: `python scripts/engines/engine.py review --task THEATRE1 --purpose "sign off" "<diff + ask>"`
Expected: `APPROVE` (or address `REJECT` and re-run).

- [ ] **Step 5: Confirm spend stayed under cap**

Run: `python scripts/engines/engine.py status`
Expected: calls this wake well under 25; THEATRE1 under 8.

- [ ] **Step 6: Commit the pilot result**

```bash
git add -A
git commit -m "feat(engines): Agent 4 pilot — first real Theatre task self-routed (DeepSeek grunt + Codex review)"
```

---

### Task 9: Reviewer pass + integration into brother bootstrap

**Files:**
- Modify: `CLAUDE.md` (Build Quick Reference — one line pointing brothers at the helper)
- Modify: `scripts/engines/README.md` (final)

- [ ] **Step 1: Cross-model reviewer pass**

Hand the full diff to Codex (Agent 7) or Agent 9 for a reviewer pass before master, per Trigger-D discipline. Address findings.

- [ ] **Step 2: Add the one-line pointer to CLAUDE.md Build Quick Reference**

Add under Build Quick Reference:
```
- Multi-engine brother helper (grunt/review/read via DeepSeek/Codex/Gemini): `python scripts/engines/engine.py <grunt|review|read> --task <id> "<tiny packet>"` — `wake-start` once at session start; `status` for spend. Spec/plan: `docs/superpowers/{specs,plans}/2026-06-01-multi-engine-brother*`.
```

- [ ] **Step 3: Final test run + commit**

Run: `python -m unittest discover -s scripts/engines -p "test_*.py" -v`
Expected: all tests PASS.
```bash
git add CLAUDE.md scripts/engines/README.md
git commit -m "docs(engines): wire multi-engine helper into brother bootstrap; reviewer pass closed"
```

---

## Self-Review (run by author after drafting)

**Spec coverage:**
- §4 engine roster + roles → Tasks 5 (callers), config (Task 1). ✓
- §4 governance line (engine vs brother) → README routing rule + CLAUDE.md pointer (Tasks 1, 9). ✓
- §5 self-routing rule → README + `dispatch()` (Tasks 1, 5). ✓
- §6 small-context handoff → `guard_packet` (Task 3) + tiny packets enforced in pilot (Task 8). ✓
- §7 proven recipes → callers replicate them exactly (Task 5); live smoke (Task 6). ✓
- §8 cost-cap (tiny context, fire limits, cheap-first, visible accounting) → `guard_packet` + `check_cap` + ledger + `status` (Tasks 2, 3, 5). ✓
- §9 key hygiene → `require_key`, env-only, no-key-in-ledger check (Tasks 3, 6). ✓
- §10.1 shared wrapper + A4 pilot → shared `scripts/engines/` (Task 1) + Task 8. ✓
- §10.2 Gemini reader behind reliability gate → `gemini_gate.py` + `enabled:false` default (Tasks 1, 7). ✓
- §10.3 shared helper home → all logic in one `engine.py` (Tasks 2–5). ✓
- §10.4 fire limits ~25/~8 → config caps + `check_cap` (Tasks 1, 2). ✓
- §11 acceptance criteria → Tasks 6, 8, 9 (live flow, pilot, reviewer pass). ✓

**Placeholder scan:** No TBD/TODO; every code + test step has complete content. ✓

**Type consistency:** `LEDGER_PATH`, `CONFIG_PATH`, `load_config`, `log_call`, `mark_wake`, `ledger_rows_since_wake`, `check_cap`, `require_key`, `guard_packet`, `parse_gemini`, `parse_codex`, `call_deepseek`, `call_codex`, `call_gemini`, `dispatch`, `main` — names used identically across all tasks and tests. ✓
