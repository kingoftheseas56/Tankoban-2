"""Build Western catalogue records from GCD + Open Library (no signup).

Per series: GCD search -> pick TPB line -> fetch its issues (throttled) ->
forward-ordered deduped volumes -> OL cover by ISBN -> schema-v3 JSON in
data/western_catalogue/<seriesId>.json. Replaces the RCO harvest (harvest.py).
Throttled + idempotent; safe to re-run. Offline dev-time build, never at runtime.

Run: cd scripts/comics_catalogue && python gcd_harvest.py
"""
import json
import pathlib
import sys

import gcd_client
import ol_covers
from gcd_record import build_gcd_record

_OUT = pathlib.Path(__file__).resolve().parents[2] / "data" / "western_catalogue"

# (seriesId, canonical GCD name, display title)
_SEED = [
    ("saga", "Saga", "Saga"),
    ("invincible", "Invincible", "Invincible"),
]


def _gcd_sid(api_url: str) -> int:
    return int([p for p in api_url.split("/") if p.isdigit()][-1])


def harvest_one(series_id, gcd_name, title):
    rows = gcd_client.fetch_all_series(gcd_name)
    line = gcd_client.pick_tpb_line(rows, gcd_name)
    if not line:
        raise RuntimeError(f"no TPB line for {gcd_name}")
    issues = [gcd_client.fetch_issue(u) for u in (line.get("active_issues") or [])]
    vols = gcd_client.build_volumes_from_issues(issues)
    for v in vols:
        v["coverUrl"] = ol_covers.verified_cover_url(v["isbn"])
    year_start = min((v["year"] for v in vols if v["year"]), default=0)
    return build_gcd_record(series_id, title, vols, gcd_series_id=_gcd_sid(line["api_url"]),
                            year_start=year_start)


def main():
    _OUT.mkdir(parents=True, exist_ok=True)
    for series_id, gcd_name, title in _SEED:
        try:
            rec = harvest_one(series_id, gcd_name, title)
            (_OUT / f"{series_id}.json").write_text(
                json.dumps(rec, indent=2, ensure_ascii=False), encoding="utf-8")
            print(f"{series_id}: {len(rec['editions'])} TPB volumes -> {series_id}.json")
        except Exception as e:
            print(f"{series_id}: FAILED {type(e).__name__}: {e}", file=sys.stderr)


if __name__ == "__main__":
    main()
