"""Assemble + persist data/manga_enrichment/<seriesId>.volumes.json.

Schema matches the consumer LocalMangaCatalogLoader.cpp:80-85 / :214-235
(merge by integer volumeNumber; empty englishTitle/englishReleaseDate are skipped
by the loader, so they are safe to omit/leave blank).
"""
import json
from pathlib import Path

ENRICHMENT_BASIS = "wikipedia_isbn_bn_harvest"


def build_enrichment(series_id, anilist_id, title, volumes, verified_date):
    """Build the enrichment document. Only volumes with a non-empty synopsis are emitted."""
    emitted = []
    for v in volumes:
        if not (v.get("synopsis") or "").strip():
            continue
        emitted.append({
            "volumeNumber": int(v["volumeNumber"]),
            "englishTitle": v.get("englishTitle", "") or "",
            "englishReleaseDate": v.get("englishReleaseDate", "") or "",
            "synopsis": v["synopsis"].strip(),
        })
    emitted.sort(key=lambda x: x["volumeNumber"])
    return {
        "seriesId": series_id,
        "anilistId": anilist_id,
        "title": title,
        "enrichmentBasis": ENRICHMENT_BASIS,
        "lastVerifiedDate": verified_date,
        "volumes": emitted,
    }


def enrichment_path(repo_root, series_id):
    return Path(repo_root) / "data" / "manga_enrichment" / f"{series_id}.volumes.json"


def write_enrichment(repo_root, doc):
    path = enrichment_path(repo_root, doc["seriesId"])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return path
