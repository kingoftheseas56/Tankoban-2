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
