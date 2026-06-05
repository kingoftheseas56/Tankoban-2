# scripts/agents/smoke/fold_visual.py
"""Phase B: read persisted phase-A state + Gemini's pasted-back answers and
rewrite the findings with the visual descriptions folded in."""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import findings as findings_mod                       # noqa: E402
from visual_workorder import load_visual_answers      # noqa: E402
from run_smoke import collect_logs, write_report, REPO  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--session", required=True)
    ap.add_argument("--answers", default=None,
                    help="defaults to out/smoke_<session>_visual_answers.json")
    args = ap.parse_args()
    state_path = os.path.join(REPO, "out", f"smoke_{args.session}_state.json")
    with open(state_path, "r", encoding="utf-8") as f:
        st = json.load(f)
    answers_path = args.answers or os.path.join(
        REPO, "out", f"smoke_{args.session}_visual_answers.json")
    visual = load_visual_answers(answers_path)
    if not visual:
        print(f"no answers at {answers_path} — nothing to fold")
        return 1
    logs = collect_logs(st["marks"])  # app may be down; grep is best-effort
    found = findings_mod.build_findings(
        st["self_results"], st["marks"], visual, logs, st["session_start"])
    write_report(args.session, found)
    print(f"folded {len(visual)} visual answers into out/smoke_{args.session}_findings.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
