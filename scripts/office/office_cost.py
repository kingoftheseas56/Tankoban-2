#!/usr/bin/env python3
"""The Office — awake-ready token-cost meter.

Measures, from a Claude Code session transcript, the REAL token cost of a brother
being "on watch" (always-on). It separates:

  * WAKE turns  — the model was re-invoked by a background event (the Monitor
                  bus-watch firing on a new message, or another background task).
  * PROMPT turns — a human (Hemanth) typed a message.
  * (tool_result user lines are continuations of the same turn, not new triggers)

The headline question it answers: "what does it cost to keep an idle agent
awake-ready?" Answer, proven from data:
  - IDLE itself costs ZERO model tokens — the watch is a shell loop; with no
    message there is no turn, so nothing is billed. (Visible here as: gaps
    between turns produce no assistant rows.)
  - Each WAKE costs ~one turn. Its dominant cost is re-reading the conversation
    context. If the cache is warm (messages close together) that re-read is
    cheap (cache_read, ~0.1x). After a long quiet gap the 5-min cache expires and
    the wake pays a one-time context re-cache (cache_creation, ~1.25x) — the real
    "reload tax" of waking a long-idle agent.

Usage:
  python scripts/office/office_cost.py                 # auto-find current session
  python scripts/office/office_cost.py <session.jsonl> # explicit transcript
"""
import os
import re
import sys
import json
import glob

# Windows consoles default to cp1252 and choke on the em-dashes / arrows below.
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


def find_session_jsonl():
    sid = os.environ.get("CLAUDE_CODE_SESSION_ID") or os.environ.get("CLAUDE_SESSION_ID")
    cwd = os.getcwd()
    proj = re.sub(r"[^A-Za-z0-9]", "-", cwd)
    base = os.path.join(os.path.expanduser("~"), ".claude", "projects", proj)
    if sid:
        p = os.path.join(base, sid + ".jsonl")
        if os.path.exists(p):
            return p
    # fall back to the most recently modified transcript in the project
    cands = sorted(glob.glob(os.path.join(base, "*.jsonl")), key=os.path.getmtime, reverse=True)
    return cands[0] if cands else None


def text_of(msg):
    c = msg.get("content")
    if isinstance(c, str):
        return c, False
    if isinstance(c, list):
        out, only_tool_result = [], True
        for b in c:
            if isinstance(b, dict):
                bt = b.get("type")
                if bt == "text":
                    out.append(b.get("text", "")); only_tool_result = False
                elif bt == "tool_result":
                    out.append("[tool_result]")
                else:
                    out.append("[" + str(bt) + "]"); only_tool_result = False
            else:
                out.append(str(b)); only_tool_result = False
        return " ".join(out), only_tool_result
    return str(c), False


def classify(text):
    if "<task-notification>" in text or "Monitor event" in text:
        return "wake"
    return "prompt"


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else find_session_jsonl()
    if not path or not os.path.exists(path):
        print("no transcript found (set CLAUDE_CODE_SESSION_ID or pass a path)")
        return
    rows = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                continue

    # Walk turns: a non-tool_result user line opens a new "turn group"; assistant
    # usage blocks until the next opener are attributed to that group.
    groups = []  # {kind, ts, out, cread, ccreate, inp, n_assist}
    cur = None
    for o in rows:
        t = o.get("type")
        if t == "user":
            txt, only_tr = text_of(o.get("message", {}))
            if only_tr:
                continue  # continuation of current turn
            snip = re.sub(r"\s+", " ", txt).strip()
            m = re.search(r"\[seq \d+\][^<]*|Monitor event[^<\"]*", snip)
            snip = (m.group(0) if m else snip)[:54]
            cur = {"kind": classify(txt), "ts": o.get("timestamp", ""), "snip": snip,
                   "out": 0, "cread": 0, "ccreate": 0, "inp": 0, "n": 0}
            groups.append(cur)
        elif t == "assistant" and cur is not None:
            u = o.get("message", {}).get("usage") or {}
            cur["out"] += u.get("output_tokens", 0)
            cur["cread"] += u.get("cache_read_input_tokens", 0)
            cur["ccreate"] += u.get("cache_creation_input_tokens", 0)
            cur["inp"] += u.get("input_tokens", 0)
            cur["n"] += 1

    wakes = [g for g in groups if g["kind"] == "wake" and g["n"] > 0]
    prompts = [g for g in groups if g["kind"] == "prompt" and g["n"] > 0]

    def tally(gs):
        return {
            "count": len(gs),
            "out": sum(g["out"] for g in gs),
            "cread": sum(g["cread"] for g in gs),
            "ccreate": sum(g["ccreate"] for g in gs),
            "inp": sum(g["inp"] for g in gs),
        }

    w, p = tally(wakes), tally(prompts)

    def fresh(t):
        # tokens billed at full / write rate (the real "cost" — cheap cache_read excluded)
        return t["out"] + t["inp"] + t["ccreate"]

    print("=" * 64)
    print("THE OFFICE — awake-ready token cost")
    print("transcript:", os.path.basename(path))
    print("=" * 64)
    print()
    print("IDLE (on watch, no message arriving):")
    print("  model turns taken: 0  ->  0 tokens. The watch is a shell loop;")
    print("  with nothing on the bus the model never runs. Idle = free.")
    print()
    print("WAKE turns (the watch re-invoked the model on a bus/background event):")
    print("  per-wake breakdown (steps = model invocations inside the turn):")
    print("  {0:<3} {1:>5} {2:>8} {3:>9} {4:>10}  {5}".format(
        "#", "steps", "output", "reload", "warm-read", "trigger"))
    for i, g in enumerate(wakes, 1):
        print("  {0:<3} {1:>5} {2:>8} {3:>9} {4:>10}  {5}".format(
            i, g["n"], g["out"], g["ccreate"], g["cread"], g["snip"]))
    print()
    # The FLOOR: bare wakes = read a line + short reply, <=3 model steps, no real work.
    bare = [g for g in wakes if g["n"] <= 3]
    if bare:
        bo = sum(g["out"] for g in bare) // len(bare)
        br = sum(g["ccreate"] for g in bare) // len(bare)
        bw = sum(g["cread"] for g in bare) // len(bare)
        print("  AWAKE-READY FLOOR (bare wakes, <=3 steps, n={0}):".format(len(bare)))
        print("    ~{0} output + ~{1} context-reload + ~{2} warm-read tokens per wake"
              .format(bo, br, bw))
        print("    (reload is 0 when the cache is still warm from recent traffic)")
    print()
    print("  totals across all {0} wakes: {1} output | {2} reload | {3} warm-read | {4} input"
          .format(w["count"], w["out"], w["ccreate"], w["cread"], w["inp"]))
    print()
    print("For contrast — PROMPT turns (you typed, often heavy work):")
    print("  prompts measured: {0}".format(p["count"]))
    print("  output tokens   : {0:>8}".format(p["out"]))
    print("  billable-rate   : {0:>8}".format(fresh(p)))
    print()
    print("READ-OUT: a bare wake (just reading a bus line + a short reply) is the")
    print("floor cost of always-on. The number that grows with a long-idle agent")
    print("is 'context reload' — the 5-min cache expiring between sparse messages.")
    print("Busy room => cache stays warm => wakes are cheap. Quiet-then-poked =>")
    print("each poke pays one reload. Idle minutes themselves are always free.")


if __name__ == "__main__":
    main()
