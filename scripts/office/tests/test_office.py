#!/usr/bin/env python3
"""Unit tests for office_bus.py — sandboxed (no touching real bus)."""
import os
import sys
import json
import tempfile
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
BUS_PY = os.path.join(HERE, "..", "office_bus.py")

fails = 0


def check(cond, label):
    global fails
    if cond:
        print("ok:", label)
    else:
        print("FAIL:", label)
        fails += 1


def run(env, *args):
    return subprocess.run(
        [sys.executable, BUS_PY, *args],
        capture_output=True, text=True, env=env,
    ).stdout.strip()


def main():
    sand = tempfile.mkdtemp()
    env = dict(os.environ)
    env["OFFICE_DIR"] = sand
    env["OFFICE_BUS"] = os.path.join(sand, "bus.jsonl")
    env["OFFICE_CURSORS"] = os.path.join(sand, "cursors")
    env["OFFICE_SESSIONS"] = os.path.join(sand, "sessions.json")

    # --- append + seq ---
    s1 = run(env, "append", "agent1", "agent4", "chat", "null", "hello")
    s2 = run(env, "append", "agent4", "agent1", "chat", "null", "hi back")
    check(s1 == "1", "first append seq == 1")
    check(s2 == "2", "second append seq == 2")
    with open(env["OFFICE_BUS"], encoding="utf-8") as f:
        lines = [json.loads(x) for x in f if x.strip()]
    check(len(lines) == 2, "two appends -> two lines")
    check(lines[0]["from"] == "agent1", "first from agent1")
    check(lines[0]["arc"] is None, "arc null normalized to None")

    # --- identity ---
    run(env, "join", "sess-abc", "4")
    run(env, "join", "sess-xyz", "1")
    check(run(env, "whoami", "sess-abc") == "agent4", "session->agent4")
    check(run(env, "whoami", "sess-xyz") == "agent1", "session->agent1")
    check(run(env, "whoami", "nope") == "", "unknown session -> empty")

    # --- unseen + cursor (bus has seq1 a1->a4, seq2 a4->a1) ---
    run(env, "append", "agent2", "all", "chat", "null", "broadcast")  # seq3
    u4 = [json.loads(x) for x in run(env, "unseen", "agent4").splitlines() if x]
    check(len(u4) == 2, "agent4 sees seq1(direct)+seq3(all)")
    check(run(env, "cursor", "agent4") == "0", "agent4 cursor starts 0")
    run(env, "mark-seen", "agent4", "3")
    check(run(env, "cursor", "agent4") == "3", "agent4 cursor now 3")
    u4b = [x for x in run(env, "unseen", "agent4").splitlines() if x]
    check(len(u4b) == 0, "after mark-seen nothing unseen for agent4")
    u1 = [json.loads(x) for x in run(env, "unseen", "agent1").splitlines() if x]
    check(len(u1) == 2, "agent1 sees seq2(direct)+seq3(all), not own seq1")

    # --- comma-list addressing ---
    run(env, "append", "agent0", "agent1,agent2", "chat", "null", "two of you")
    u2 = [json.loads(x) for x in run(env, "unseen", "agent2").splitlines() if x]
    check(any(r["msg"] == "two of you" for r in u2), "comma-list reaches agent2")

    # --- send (resolves FROM from session) ---
    run(env, "join", "sess-snd", "0")
    run(env, "send", "sess-snd", "@agent4", "ping via send")
    with open(env["OFFICE_BUS"], encoding="utf-8") as f:
        last = json.loads([x for x in f if x.strip()][-1])
    check(last["from"] == "agent0" and last["to"] == "agent4" and last["msg"] == "ping via send",
          "send resolves FROM=agent0 -> agent4")

    # --- deliver (reads stdin session_id, emits additionalContext, advances cursor) ---
    proc = subprocess.run(
        [sys.executable, BUS_PY, "deliver"], input=json.dumps({"session_id": "sess-abc"}),
        capture_output=True, text=True, env=env,
    )  # sess-abc -> agent4
    out = proc.stdout.strip()
    payload = json.loads(out)
    ctx = payload["hookSpecificOutput"]["additionalContext"]
    check(payload["hookSpecificOutput"]["hookEventName"] == "UserPromptSubmit", "deliver: correct hookEventName")
    check("ping via send" in ctx and "THE OFFICE" in ctx, "deliver: injects unseen msg text")
    # second deliver -> nothing (cursor advanced)
    proc2 = subprocess.run(
        [sys.executable, BUS_PY, "deliver"], input=json.dumps({"session_id": "sess-abc"}),
        capture_output=True, text=True, env=env,
    )
    check(proc2.stdout.strip() == "", "deliver: nothing after cursor advanced")

    # --- deliver guards: deepseek endpoint stays silent ---
    env_ds = dict(env); env_ds["ANTHROPIC_BASE_URL"] = "https://api.deepseek.com"
    procd = subprocess.run(
        [sys.executable, BUS_PY, "deliver"], input=json.dumps({"session_id": "sess-xyz"}),
        capture_output=True, text=True, env=env_ds,
    )
    check(procd.stdout.strip() == "", "deliver: silent on deepseek endpoint")

    # --- deliver auto-binds identity from wake prompt (unregistered session) ---
    run(env, "append", "agent2", "agent3", "chat", "null", "hey player guy")
    proc_ab = subprocess.run(
        [sys.executable, BUS_PY, "deliver"],
        input=json.dumps({"session_id": "sess-new3", "prompt": "my brother, you're Agent 3 — wake up"}),
        capture_output=True, text=True, env=env,
    )
    check(run(env, "whoami", "sess-new3") == "agent3", "deliver auto-binds sess-new3 -> agent3 from prompt")
    check("hey player guy" in proc_ab.stdout, "deliver delivers to just-auto-bound agent3")

    # --- drain: shows pending for late-joiner + advances cursor (no double-show) ---
    run(env, "append", "agent2", "agent5", "chat", "null", "late joiner msg")
    d1 = run(env, "drain", "agent5")
    check("late joiner msg" in d1, "drain shows pending for agent5")
    d2 = run(env, "drain", "agent5")
    check("no messages waiting" in d2, "drain: nothing on second call (cursor advanced)")

    # --- close: archive + clear ---
    out_close = run(env, "close")
    check("archived" in out_close, "close: reports archive")
    check(not os.path.exists(env["OFFICE_BUS"]), "close: live bus removed")
    arch = os.path.join(sand, "bus_archive")
    check(os.path.isdir(arch) and any(f.endswith(".jsonl") for f in os.listdir(arch)),
          "close: archive file written")

    print("RESULT:", "PASS" if fails == 0 else f"{fails} FAIL")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
