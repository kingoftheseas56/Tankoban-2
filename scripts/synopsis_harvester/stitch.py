"""Congress routing-brain / stitcher (Leader = Agent 1).

Merges the three arm outputs into each final per-series record and runs the
quality gates, logging every failed field to the gaps-log.

Inputs (per series <slug>):
  data/manga_enrichment/_congress/batch1/<slug>.volumes.json
        -- Stage-1 skeleton (Agent 9): spine + volumes[{volumeNumber,
           englishTitle, englishReleaseDate, isbn, synopsis}].
           Agent 2 fills `synopsis` (+ optional `synopsisSource`) IN-PLACE here.
  data/manga_enrichment/_congress/covers/<slug>.json   (optional, Codex)
        -- {"<volNum>": {"coverUrl": "...", "coverSource": "bookwalker"}}

Output:
  data/manga_enrichment/<slug>.volumes.json  -- final record. The loader renders
        synopsis/title/date today; coverUrl + series spine are baked for the
        loader fast-follow.
  data/manga_enrichment/_congress/gaps.jsonl -- one line per unfilled field.

Quality gates:
  1. boilerplate  -- strip the majority-shared franchise intro (clean.py).
  2. per-volume-real -- flag a series whose stripped synopses are mostly identical.
  3. cover -- presence (BookWalker covers are already poster-grade).

Run:  python stitch.py [slug ...]   (default: every skeleton in batch1/)
Guard: never overwrites an existing final that HAS synopsis with an empty stitch.
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from clean import normalize_text, strip_shared_prefixes

REPO = Path(__file__).resolve().parents[2]
ENRICH = REPO / "data" / "manga_enrichment"
BATCH_DIR = ENRICH / "_congress" / "batch1"
COVERS_DIR = ENRICH / "_congress" / "covers"
GAPS = ENRICH / "_congress" / "gaps.jsonl"


def log_gap(series, volume, field, reason, sources_tried=None):
    GAPS.parent.mkdir(parents=True, exist_ok=True)
    rec = {"series": series, "volume": volume, "field": field, "reason": reason}
    if sources_tried:
        rec["sources_tried"] = [s for s in sources_tried if s]
    with GAPS.open("a", encoding="utf-8") as f:
        f.write(json.dumps(rec, ensure_ascii=False) + "\n")


def _load_covers(slug):
    p = COVERS_DIR / f"{slug}.json"
    if not p.exists():
        return {}
    try:
        return json.loads(p.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return {}


def _existing_synopsis_count(slug):
    p = ENRICH / f"{slug}.volumes.json"
    if not p.exists():
        return 0
    try:
        doc = json.loads(p.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return 0
    return sum(1 for v in doc.get("volumes", []) if (v.get("synopsis") or "").strip())


def stitch_series(slug):
    skel = BATCH_DIR / f"{slug}.volumes.json"
    if not skel.exists():
        return f"SKIP {slug}: no skeleton"
    doc = json.loads(skel.read_text(encoding="utf-8"))
    vols = doc.get("volumes", [])
    covers = _load_covers(slug)

    # Gate 1: boilerplate. Normalize then strip any long opening shared by >=3
    # volumes (catches majority AND sub-majority clusters like Bleach 48-74).
    raw = [normalize_text(v.get("synopsis", "") or "") for v in vols]
    stripped = strip_shared_prefixes(raw)

    # Gate 2: per-volume-real. If most stripped synopses are identical, flag.
    nonempty = [s for s in stripped if s]
    distinct = len(set(nonempty))
    low_fidelity = len(nonempty) >= 3 and distinct <= max(1, len(nonempty) // 3)

    syn_filled = sum(1 for s in stripped if s)

    # Stitch only once Agent 2 has filled synopsis for this series. Zero synopsis
    # = not-yet-delivered, NOT a gap -> skip cleanly (and never clobber an
    # existing good final with empties).
    if syn_filled == 0:
        return f"AWAIT {slug}: no synopsis filled yet (Agent 2 pending)"

    covers_delivered = bool(covers)
    cov_filled = 0
    for i, v in enumerate(vols):
        vn = v["volumeNumber"]
        v["synopsis"] = stripped[i]
        if not stripped[i]:
            log_gap(slug, vn, "synopsis", "empty after gate", [v.get("synopsisSource")])
        c = covers.get(str(vn)) or covers.get(vn)
        if c and (c.get("coverUrl") or "").strip():
            v["coverUrl"] = c["coverUrl"]
            v["coverSource"] = c.get("coverSource", "")
            cov_filled += 1
        else:
            v.setdefault("coverUrl", "")
            v.setdefault("coverSource", "")
            if covers_delivered:
                log_gap(slug, vn, "cover", "no cover resolved")

    doc["enrichmentBasis"] = "congress_catalog_engine_v1"
    out = ENRICH / f"{slug}.volumes.json"
    out.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    flag = "  [!] LOW-FIDELITY synopsis (mostly identical)" if low_fidelity else ""
    return f"{slug}: {syn_filled}/{len(vols)} synopsis, {cov_filled}/{len(vols)} covers -> {out.name}{flag}"


def main():
    targets = sys.argv[1:]
    if not targets:
        targets = sorted(p.name[: -len(".volumes.json")] for p in BATCH_DIR.glob("*.volumes.json"))
    for slug in targets:
        print(stitch_series(slug))


if __name__ == "__main__":
    main()
