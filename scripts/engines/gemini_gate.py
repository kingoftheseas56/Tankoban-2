#!/usr/bin/env python3
"""Reliability gate for Gemini-as-reader. Runs N reader-extracts against known
answers; flips engines.config.json gemini.enabled true iff all pass."""
import os, sys, json, tempfile, importlib.util

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
    engine._load_dotenv()
    cfg = engine.load_config()
    cfg["gemini"]["enabled"] = True  # force-enable for the probe only
    engine.require_key("GEMINI_API_KEY")
    # Do NOT call mark_wake() — gate probes must NOT reset the caller's cap
    # counter. log_call per probe is sufficient for budget visibility.
    passed = 0
    for prompt, want in CASES:
        # Mirror dispatch()'s critical-section pattern: check cap + call +
        # log under one lock so concurrent agents can't both sneak past.
        with engine._ledger_lock():
            ok_flag, msg = engine.check_cap("GEMINI_GATE", cfg)
            if msg:
                print(msg, file=sys.stderr)
            if not ok_flag:
                print("GEMINI GATE STOPPED: cap exhausted — gate cannot proceed.", file=sys.stderr)
                return 2
            try:
                got = engine.call_gemini(prompt, cfg).strip()
            except Exception as e:
                print(f"FAIL (error): {e}"); continue
            engine.log_call("gemini", len(got), "gate-probe", "GEMINI_GATE")
        ok = got == want  # exact match — '1200' must not pass '200'
        print(f"{'PASS' if ok else 'FAIL'}: want '{want}' got '{got[:40]}'")
        passed += ok
    flip = passed == len(CASES)
    path = engine.CONFIG_PATH
    disk = json.load(open(path))
    disk["gemini"]["enabled"] = flip
    # Atomic write: temp file + os.replace to avoid truncation on crash
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), suffix=".tmp")
    try:
        with os.fdopen(fd, "w") as f:
            json.dump(disk, f, indent=2)
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)
    print(f"\nGemini reliability: {passed}/{len(CASES)} -> "
          f"gemini.enabled = {flip}")
    return 0 if flip else 1


if __name__ == "__main__":
    sys.exit(main())
