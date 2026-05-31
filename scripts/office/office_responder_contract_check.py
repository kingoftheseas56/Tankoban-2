#!/usr/bin/env python3
"""GATE: prove `claude -p` can drive a headless, read-only, structured-output
reply for the Office responder. Agent 7's live smoke (spec §7) showed --json-schema
returned a free-form result + a hook ran MCP despite --tools "". This verifies the
EXACT contract before we build the responder. PASS -> build. FAIL -> pivot to SDK.

Run: python scripts/office/office_responder_contract_check.py
"""
import os
import sys
import json
import shutil
import subprocess

sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def claude_bin():
    """Resolve the claude CLI. It's an npm global (claude.CMD on Windows) and is
    NOT found by bare subprocess(['claude']) — must use the resolved full path."""
    found = shutil.which("claude")
    if found:
        return found
    for cand in (os.path.join(os.environ.get("APPDATA", ""), "npm", "claude.cmd"),
                 os.path.join(os.path.expanduser("~"), "AppData", "Roaming", "npm", "claude.cmd")):
        if cand and os.path.exists(cand):
            return cand
    return "claude"

SCHEMA = {
    "type": "object", "additionalProperties": False,
    "properties": {"replies": {
        "type": "array", "maxItems": 2,
        "items": {"type": "object", "additionalProperties": False,
                  "properties": {"to": {"type": "string"},
                                 "class": {"type": "string"},
                                 "msg": {"type": "string"}},
                  "required": ["to", "class", "msg"]}}},
    "required": ["replies"],
}

PROMPT = ('You are a test harness. Output ONLY JSON matching the schema. '
          'Return exactly {"replies": []} (an empty replies array). '
          'Do not read files, do not call tools.')


def _json_candidates(out):
    yield out
    i, j = out.find("{"), out.rfind("}")
    if i != -1 and j > i:
        yield out[i:j + 1]


def extract_replies(out):
    """Be liberal: try whole-string JSON, then a 'result' wrapper, then first {...} blob."""
    for candidate in _json_candidates(out):
        try:
            obj = json.loads(candidate)
        except (json.JSONDecodeError, TypeError):
            continue
        if isinstance(obj, dict):
            if isinstance(obj.get("replies"), list):
                return obj["replies"]
            inner = obj.get("structured_output") or obj.get("result")
            if isinstance(inner, dict) and isinstance(inner.get("replies"), list):
                return inner["replies"]
            if isinstance(inner, str):
                try:
                    inner_obj = json.loads(inner)
                    if isinstance(inner_obj.get("replies"), list):
                        return inner_obj["replies"]
                except json.JSONDecodeError:
                    pass
    return None


def try_command(args, label, timeout=90):
    print("\n--- trying: %s ---" % label)
    try:
        proc = subprocess.run(args, input=PROMPT, capture_output=True, text=True,
                              encoding="utf-8", errors="replace", timeout=timeout)
    except subprocess.TimeoutExpired:
        print("  RESULT: TIMEOUT after %ss" % timeout)
        return False
    except FileNotFoundError:
        print("  RESULT: FAIL — `claude` not found on PATH")
        return False
    print("  exit:", proc.returncode)
    out = proc.stdout.strip()
    print("  stdout (first 600):", out[:600])
    if proc.stderr.strip():
        print("  stderr (first 300):", proc.stderr.strip()[:300])
    replies = extract_replies(out)
    if replies is None:
        print("  RESULT: FAIL — could not extract a valid {replies:[...]} object")
        return False
    print("  extracted replies:", replies)
    print("  RESULT: PASS — structured reply parseable")
    return True


def main():
    cb = claude_bin()
    print("claude bin:", cb)
    # PROVEN command (2026-05-31): plan-mode (read-only) + json output-format; the
    # model writes JSON into the result wrapper, which extract_replies parses. We do
    # NOT rely on --json-schema (Agent 7's smoke + ours: it returns free-form). MCP
    # blocked via --strict-mcp-config; no session persistence.
    variants = [
        ([cb, "-p", "--no-session-persistence", "--permission-mode", "plan",
          "--strict-mcp-config", "--output-format", "json"], "plan-mode json (result-string parse)"),
    ]
    passed = any(try_command(args, label) for args, label in variants)
    print("\n==================")
    print("CONTRACT TEST:", "PASS — claude -p is usable; proceed to Task 2." if passed
          else "FAIL — claude -p contract not clean. STOP. Escalate: pivot to Agent SDK (spec §7).")
    print("==================")
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
