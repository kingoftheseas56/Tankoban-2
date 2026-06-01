"""Build Western-comics base records from RCO series pages.

Per series: fetch the RCO /Comic/<Series> page -> parse_rco the item list ->
classify each item -> keep only collected editions (compendium > omnibus > TPB >
deluxe > vol), tier-sorted -> write data/western_catalogue/<slug>.json.

The base record mirrors the manga catalogue shape closely enough that the C++
ComicsSeriesView tile path can render it (a collected edition becomes a
"volume"). Downloads are resolved separately at click-time via getcomics_resolve
(magnet -> our libtorrent). This module does NOT download.

Scope: blockbuster-first (project_catalog_scope_top500 discipline). Edition
typing is the accepted-blockbuster-clean label heuristic (RECON_FINDINGS.md).
"""
import json
import pathlib
import sys
import time
import urllib.request
import ssl

from parse_rco import parse_series, parse_series_cover
from edition_classify import is_collected, edition_tier

_UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
       "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36")
_BASE = "https://rcostation.xyz"
# Anchor output at repo-root/data/western_catalogue regardless of CWD
# (harvest.py lives at <repo>/scripts/comics_catalogue/). The C++ loader reads
# from repo-root; a CWD-relative path silently wrote to scripts/.../data once.
_OUT_DIR = pathlib.Path(__file__).resolve().parents[2] / "data" / "western_catalogue"


def build_record(series_id: str, series_title: str, items: list[dict],
                 series_cover: str = "") -> dict:
    """Pure assembly: parsed RCO items -> a base record holding only the
    collected editions, tier-sorted (compendium first). Single issues are
    excluded from the primary editions list. `series_cover` is the one
    series-hero cover RCO exposes; the C++ loader maps it to each edition's
    cover (RCO has no per-edition covers — see RECON_FINDINGS.md)."""
    collected = [it for it in items if is_collected(it["label"])]
    collected.sort(key=lambda it: edition_tier(it["label"]))  # stable
    editions = [
        {"label": it["label"], "href": it["href"], "formatTier": edition_tier(it["label"])}
        for it in collected
    ]
    return {
        "seriesId": series_id,
        "seriesTitle": series_title,
        "source": "rco",
        "seriesCover": series_cover,
        "editions": editions,
    }


def _fetch(url: str) -> str:
    req = urllib.request.Request(url, headers={"User-Agent": _UA, "Referer": _BASE})
    ctx = ssl.create_default_context()
    with urllib.request.urlopen(req, timeout=40, context=ctx) as r:
        return r.read().decode("utf-8", "replace")


def harvest_series(series_id: str, series_title: str, rco_name: str) -> dict:
    """Fetch one RCO series page and build its record. `rco_name` is the
    /Comic/<rco_name> path segment (capital-C scheme)."""
    html = _fetch(f"{_BASE}/Comic/{rco_name}")
    items = parse_series(html)
    cover = parse_series_cover(html)
    return build_record(series_id, series_title, items, series_cover=cover)


def write_record(record: dict, out_dir: pathlib.Path = _OUT_DIR) -> pathlib.Path:
    """Write a base record. Empty-record guard: never overwrite an existing
    non-empty record with one that has zero editions (mirrors stitch.py's
    don't-clobber-good-finals guard)."""
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"{record['seriesId']}.json"
    if not record["editions"] and path.exists():
        try:
            existing = json.loads(path.read_text(encoding="utf-8"))
            if existing.get("editions"):
                return path  # keep the good existing record
        except Exception:
            pass
    path.write_text(json.dumps(record, indent=2, ensure_ascii=False), encoding="utf-8")
    return path


# Blockbuster-first seed set (project_catalog_scope_top500): (seriesId,
# displayTitle, rco /Comic/<name> segment). Each slug was recon-verified
# (recon_slugs.py) to resolve AND yield >=2 collected editions.
# NOTE: several marquee titles (Walking Dead, Saga, Sandman, Hellboy, Fables,
# The Boys) list ONLY single issues on their primary /Comic/<slug> page; their
# collected editions live under non-obvious sibling slugs that can't be guessed
# reliably -> deferred to a proper RCO catalogue-discovery pass.
_SEED = [
    ("invincible", "Invincible", "Invincible"),
    ("spawn", "Spawn", "Spawn"),
    ("preacher", "Preacher", "Preacher"),
    ("watchmen", "Watchmen", "Watchmen"),
    ("deadly-class", "Deadly Class", "Deadly-Class"),
    ("chew", "Chew", "Chew"),
    ("sweet-tooth", "Sweet Tooth", "Sweet-Tooth"),
    ("descender", "Descender", "Descender"),
    ("the-wicked-the-divine", "The Wicked + The Divine", "The-Wicked-The-Divine"),
    ("nailbiter", "Nailbiter", "Nailbiter"),
    ("southern-bastards", "Southern Bastards", "Southern-Bastards"),
    ("seven-to-eternity", "Seven to Eternity", "Seven-to-Eternity"),
    ("gideon-falls", "Gideon Falls", "Gideon-Falls"),
]


def main():
    for series_id, title, rco_name in _SEED:
        try:
            rec = harvest_series(series_id, title, rco_name)
            path = write_record(rec)
            print(f"{series_id}: {len(rec['editions'])} editions -> {path}")
        except Exception as e:
            print(f"{series_id}: FAILED {type(e).__name__}: {e}", file=sys.stderr)
        time.sleep(1.0)  # polite


if __name__ == "__main__":
    main()
