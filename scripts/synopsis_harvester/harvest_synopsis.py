"""Harvest per-volume synopses for a series and write its enrichment overlay.

Usage (from repo root):
  python scripts/synopsis_harvester/harvest_synopsis.py --series grand-blue-dreaming
  python scripts/synopsis_harvester/harvest_synopsis.py --series grand-blue-dreaming --wiki "Grand Blue Dreaming"

Reads data/mangafire_catalog/<seriesId>.json for the title, anilistId, and the
volume-number list; resolves the Wikipedia article (default = seriesTitle, or
--wiki override); maps each volume to its English ISBN; fetches the synopsis;
writes data/manga_enrichment/<seriesId>.volumes.json; prints coverage.
"""
import argparse
import datetime
import json
import sys
from pathlib import Path

import requests

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from synopsis_harvester.wikipedia_volumes import parse_volume_table
from synopsis_harvester.synopsis_sources import resolve_synopsis
from synopsis_harvester.enrichment_writer import build_enrichment, write_enrichment
from synopsis_harvester.clean import normalize_text, strip_series_boilerplate

REPO_ROOT = Path(__file__).resolve().parents[2]
WIKI_HEADERS = {"User-Agent": "TankobanSynopsisHarvester/1.0"}


def load_catalog(series_id):
    path = REPO_ROOT / "data" / "mangafire_catalog" / f"{series_id}.json"
    if not path.exists():
        sys.exit(f"catalog not found: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def fetch_wikipedia_html(article):
    url = "https://en.wikipedia.org/wiki/" + article.replace(" ", "_")
    resp = requests.get(url, headers=WIKI_HEADERS, timeout=30)
    resp.raise_for_status()
    return resp.text


def harvest(series_id, wiki_article=None, delay=0.7):
    catalog = load_catalog(series_id)
    title = catalog.get("seriesTitle") or catalog.get("title") or series_id
    anilist_id = catalog.get("anilistId", 0)
    catalog_vol_nums = {int(v["number"]) for v in catalog.get("volumes", []) if "number" in v}

    html = fetch_wikipedia_html(wiki_article or title)
    wiki_vols = {v["volumeNumber"]: v for v in parse_volume_table(html)}
    if not wiki_vols:
        sys.exit(f"no volume table parsed from Wikipedia for '{wiki_article or title}'")

    session = requests.Session()
    target_nums = sorted(catalog_vol_nums or set(wiki_vols))
    rows = []
    for num in target_nums:
        wv = wiki_vols.get(num)
        if not wv or not wv.get("isbnEn"):
            rows.append({"volumeNumber": num, "englishTitle": "",
                         "englishReleaseDate": "", "synopsis": ""})
            continue
        synopsis = resolve_synopsis(wv["isbnEn"], session=session, delay=delay)
        rows.append({
            "volumeNumber": num,
            "englishTitle": wv.get("englishTitle", ""),
            "englishReleaseDate": wv.get("englishReleaseDate", ""),
            "synopsis": synopsis,
        })

    # Clean: normalize each blurb, then strip the franchise marketing intro that
    # the publisher prepends to every volume (so each row leads with real content).
    for r in rows:
        r["synopsis"] = normalize_text(r["synopsis"])
    stripped = strip_series_boilerplate([r["synopsis"] for r in rows])
    for r, s in zip(rows, stripped):
        r["synopsis"] = s

    today = datetime.date.today().isoformat()
    doc = build_enrichment(series_id, anilist_id, title, rows, today)
    path = write_enrichment(REPO_ROOT, doc)

    got = len(doc["volumes"])
    total = len(target_nums)
    print(f"[{series_id}] synopsis coverage: {got}/{total} volumes -> {path}")
    missing = [r["volumeNumber"] for r in rows if not (r["synopsis"] or "").strip()]
    if missing:
        print(f"[{series_id}] no synopsis for volumes: {missing}")
    return got, total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--series", required=True, help="seriesId (matches data/mangafire_catalog/<id>.json)")
    ap.add_argument("--wiki", default=None, help="Wikipedia article title override")
    ap.add_argument("--delay", type=float, default=0.7, help="seconds between B&N requests")
    args = ap.parse_args()
    harvest(args.series, wiki_article=args.wiki, delay=args.delay)


if __name__ == "__main__":
    main()
