# scripts/agents/smoke/visual_workorder.py
"""Visual lane = a directed work-order over CLIPS of the session MP4, couriered by
Hemanth to the Gemini LLM (native video understanding — NOT the Gemini API). The
harness calls no vision API. It (1) clips each to-be-seen window from the full MP4
(ffmpeg -ss -t), (2) emits a work-order listing each clip + its question, flagging
SELF-fail/watchdog windows as LOOK HARD; Hemanth uploads the clips to Gemini and
pastes back {id: description} JSON, folded by fold_visual.py."""
import json
import os
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
FFMPEG = r"C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin\ffmpeg.exe"


def select_flagged_windows(self_results, visual_ids, watchdog_ids):
    """The 'LOOK HARD' set: SELF fails + SELF blocked-by-crash + watchdog hits.
    VISUAL assertions are clipped+described regardless; this is the extra-attention
    set where something already looked wrong on the data side."""
    flagged = set(watchdog_ids)
    for r in self_results:
        if r.get("verdict") in ("fail", "blocked"):
            flagged.add(r["id"])
    return flagged


def _clip(mp4, off, dur, out_path):
    """Clip [off, off+dur] from mp4 -> out_path. Returns out_path or None."""
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    subprocess.run([FFMPEG, "-hide_banner", "-loglevel", "error", "-y",
                    "-ss", off, "-t", str(dur), "-i", mp4,
                    "-c", "copy", out_path], capture_output=True)
    if not os.path.exists(out_path) or os.path.getsize(out_path) == 0:
        # stream-copy can fail on a non-keyframe boundary; re-encode fallback.
        subprocess.run([FFMPEG, "-hide_banner", "-loglevel", "error", "-y",
                        "-ss", off, "-t", str(dur), "-i", mp4,
                        "-c:v", "libx264", "-preset", "veryfast",
                        "-pix_fmt", "yuv420p", out_path], capture_output=True)
    return out_path if os.path.exists(out_path) and os.path.getsize(out_path) > 0 else None


def build_workorder(session, mp4, visual_asserts, marks, self_results,
                    watchdog_ids, session_start, offset_fn, dur=8):
    """Clip flagged+visual windows and write the courier work-order.
    Returns (workorder_path, clips_dir). visual_asserts: {id: gemini_prompt}."""
    clips_dir = os.path.join(REPO, "out", f"smoke_{session}_clips")
    wo_path = os.path.join(REPO, "out", f"smoke_{session}_visual_workorder.md")
    flagged = select_flagged_windows(self_results, list(visual_asserts), watchdog_ids)
    # Clip every VISUAL assertion window + every flagged (look-hard) window.
    targets = set(visual_asserts) | flagged
    rows = []
    recorder_ok = os.path.exists(mp4) and os.path.getsize(mp4) > 0
    for aid in sorted(targets):
        win = marks.get(aid)
        if not win or not win.get("start"):
            continue
        off = offset_fn(session_start, win["start"])
        clip = _clip(mp4, off, dur, os.path.join(clips_dir, f"{aid}.mp4")) if recorder_ok else None
        rows.append((aid, off, clip, aid in flagged,
                     visual_asserts.get(aid, "A data check failed/blocked here — "
                                             "describe exactly what is on screen.")))
    lines = [f"# Visual work-order — {session}", "",
             "Courier each CLIP below to the Gemini LLM. For each, answer the "
             "question in one sentence. Reply as ONE JSON object: "
             "{\"<id>\": \"<answer>\", ...} and save it to "
             f"`out/smoke_{session}_visual_answers.json`.",
             "Checkpoints marked ** LOOK HARD ** already looked wrong on the data "
             "side — describe them most carefully.", ""]
    if not recorder_ok:
        lines.append("> WARN recorder produced no usable MP4 — clips unavailable. "
                     "Visual lane degraded (flagged, not faked).\n")
    for aid, off, clip, hard, q in rows:
        tag = " ** LOOK HARD **" if hard else ""
        cliptxt = os.path.relpath(clip, REPO) if clip else "(no clip — recorder failed)"
        lines.append(f"- [{aid}] @ {off}{tag}\n      clip: {cliptxt}\n      Q: {q}")
    with open(wo_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return wo_path, clips_dir


def load_visual_answers(path):
    """Parse Gemini's returned {id: description} JSON (or {} if absent/bad)."""
    if not path or not os.path.exists(path):
        return {}
    with open(path, "r", encoding="utf-8") as f:
        try:
            return json.load(f)
        except ValueError:
            return {}
