#!/usr/bin/env python3
"""Lint Tankoban memory files for a freshness story.

Flags files missing status/last_verified, and facts older than STALE_DAYS not
re-verified. Read-only: reports, never edits. Run: python scripts/memory_lint.py [memory_dir]

Why: most agent-memory rot is old truths looking current (Codex, 2026-06-02). Every
fact should carry status (active/superseded) + last_verified (YYYY-MM-DD) so a recalled
memory announces its own age before an agent acts on it.
"""
import os, sys, re, datetime

STALE_DAYS = 120
DEFAULT_DIR = os.path.expanduser(
    "~/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory")


def parse_frontmatter(text):
    m = re.match(r"^---\n(.*?)\n---\n", text, re.DOTALL)
    if not m:
        return {}
    fm = {}
    for line in m.group(1).splitlines():
        if ":" in line and not line.strip().startswith("#"):
            k, _, v = line.partition(":")
            fm[k.strip()] = v.strip()
    return fm


def main(argv):
    d = argv[0] if argv else DEFAULT_DIR
    if not os.path.isdir(d):
        print(f"memory dir not found: {d}")
        return 1
    today = datetime.date.today()
    missing, stale, ok = [], [], 0
    for name in sorted(os.listdir(d)):
        if not name.endswith(".md") or name == "MEMORY.md":
            continue
        fm = parse_frontmatter(open(os.path.join(d, name), encoding="utf-8").read())
        lv = fm.get("last_verified", "")
        if "status" not in fm or not lv:
            missing.append(name)
            continue
        try:
            age = (today - datetime.date.fromisoformat(lv)).days
            if age > STALE_DAYS:
                stale.append((name, age))
            ok += 1
        except ValueError:
            missing.append(name)
    print(f"OK: {ok}  |  missing freshness fields: {len(missing)}  |  stale(>{STALE_DAYS}d): {len(stale)}")
    for n in missing[:40]:
        print("  MISSING:", n)
    for n, a in sorted(stale, key=lambda x: -x[1])[:40]:
        print(f"  STALE {a}d:", n)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
