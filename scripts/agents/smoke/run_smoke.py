# scripts/agents/smoke/run_smoke.py
"""Entry point (phase A): catalogue -> drive (SELF) -> record -> clip work-order.
The visual lane is a manual courier of clips to the Gemini LLM; fold_visual.py
(phase B) merges the pasted-back answers into the final findings.

Usage:
  python scripts/agents/smoke/run_smoke.py --session op-proof \
      --catalogue scripts/agents/smoke/catalogue_stream.json --only J1,J2
  (add --no-visual to skip clipping; SELF lane still runs fully)
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, ".."))
from drive_journal import tankoctl, _events_since, _events_offset  # noqa: E402
import catalogue as cat_mod                                         # noqa: E402
import runner as runner_mod                                         # noqa: E402
import marks as marks_mod                                           # noqa: E402
import findings as findings_mod                                     # noqa: E402
import visual_workorder as wo_mod                                   # noqa: E402
from recording import SessionRecording, wallclock_to_offset         # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))


def collect_logs(marks):
    """For each assert id, grep the four log streams for one matching line."""
    logs = {}
    for aid in marks:
        ok, reply = tankoctl(["log-grep", aid, "3"])
        if ok and isinstance(reply, dict):
            hits = []
            for comp in (reply.get("perComponent") or {}).values():
                for m in (comp.get("matches") or [])[:1]:
                    hits.append(m.get("text", "")[:160])
            logs[aid] = " | ".join(hits)
    return logs


def write_report(session, found):
    """Write the traceable findings report (md + jsonl). blocked != fail."""
    md = os.path.join(REPO, "out", f"smoke_{session}_findings.md")
    js = os.path.join(REPO, "out", f"smoke_{session}_findings.jsonl")
    with open(js, "w", encoding="utf-8") as f:
        for x in found:
            f.write(json.dumps(x, ensure_ascii=False) + "\n")
    with open(md, "w", encoding="utf-8") as f:
        npass = sum(1 for x in found if x["verdict"] == "pass")
        nfail = sum(1 for x in found if x["verdict"] == "fail")
        nblk = sum(1 for x in found if x["verdict"] == "blocked")
        f.write(f"# Smoke findings — {session}\n\n{npass} pass / {nfail} fail / "
                f"{nblk} blocked-by-crash / {len(found)} traceable.\n\n")
        for x in found:
            f.write(f"## [{x['verdict'].upper()}] {x['id']} ({x['journey']}/{x['lane']})\n")
            f.write(f"- action: {x['action']}\n- ts: {x['ts']}  video: {x['video_offset']}\n")
            f.write(f"- reason: {x['reason']}\n")
            if x.get("gemini"):
                f.write(f"- gemini: {x['gemini']}\n")
            if x.get("log"):
                f.write(f"- log: {x['log']}\n")
            if x.get("note"):
                f.write(f"- note: {x['note']}\n")
            f.write("\n")
    print(f"findings -> {md}\n         {js}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--session", default="smoke")
    ap.add_argument("--catalogue", default=os.path.join(HERE, "catalogue_stream.json"))
    ap.add_argument("--only", default="")
    ap.add_argument("--no-visual", action="store_true")
    args = ap.parse_args()

    if not tankoctl(["ping"])[0]:
        print("ERROR: app not reachable — launch scripts/agents/run_drive_mode.bat first")
        return 1

    cat = cat_mod.load(args.catalogue)
    only = [s for s in args.only.split(",") if s] or None

    rec = SessionRecording(args.session)
    started = rec.start()
    print(f"recording: {'on -> ' + rec.path if started else 'FAILED (continuing SELF-only)'}")

    ev_off = _events_offset()
    self_results = runner_mod.run_catalogue(cat, only=only)
    events = _events_since(ev_off)
    rec.stop()

    marks = marks_mod.parse_marks(events)

    # Persist phase-A state so fold_visual.py can merge Gemini answers later.
    state_path = os.path.join(REPO, "out", f"smoke_{args.session}_state.json")
    with open(state_path, "w", encoding="utf-8") as f:
        json.dump({"self_results": self_results, "marks": marks,
                   "session_start": rec.start_ts}, f, ensure_ascii=False)

    # Build the visual work-order: clip flagged windows (NO vision API here).
    if not args.no_visual:
        visual_asserts = {a.id: a.gemini_prompt
                          for j in cat.journeys for s in j.steps for a in s.asserts
                          if a.lane == "VISUAL" and (not only or j.id in only)}
        watchdog_ids = [r["id"] for r in self_results if r.get("verdict") == "blocked"]
        wo, clips = wo_mod.build_workorder(
            args.session, rec.path, visual_asserts, marks, self_results,
            watchdog_ids, rec.start_ts, wallclock_to_offset)
        print(f"visual work-order -> {wo}\nclips dir       -> {clips}")

    logs = collect_logs(marks)
    found = findings_mod.build_findings(self_results, marks, {}, logs, rec.start_ts)
    write_report(args.session, found)  # SELF-only; visual folded by fold_visual.py
    print(f"\nSELF findings -> out/smoke_{args.session}_findings.md")
    if not args.no_visual:
        print("\nNEXT — visual lane (manual courier to the Gemini LLM):"
              f"\n  1. (optional, cheap) speed clips: screen_record.py speedup <clip> <out> 4"
              f"\n  2. upload out/smoke_{args.session}_clips/*.mp4 + the work-order to Gemini"
              f"\n  3. save its JSON reply -> out/smoke_{args.session}_visual_answers.json"
              f"\n  4. run: python scripts/agents/smoke/fold_visual.py --session {args.session}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
