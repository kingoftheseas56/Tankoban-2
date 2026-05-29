# Manga Per-Volume Synopsis Harvester — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an offline Python tool that harvests an official per-volume English synopsis for each volume of a manga series and writes it into the app's existing enrichment overlay, so volume rows show real plot blurbs.

**Architecture:** Pure offline harvester under `scripts/synopsis_harvester/`, mirroring the existing `scripts/mangafire_scraper/mangafire_ingest.py` pattern (Python 3 + `requests` + `BeautifulSoup` + a rate limiter). The pipeline is three proven steps: **(1)** parse the series' Wikipedia volume table to get each volume's *English* ISBN; **(2)** look that ISBN up on Barnes & Noble to get the official per-volume synopsis text; **(3)** write `data/manga_enrichment/<seriesId>.volumes.json` in the schema the C++ loader already consumes. **The app needs zero changes** — `LocalMangaCatalogLoader` already merges `volumes[].synopsis` into `MangaVolume.synopsis` by integer `volumeNumber`, and `VolumeTile` already renders it.

**Tech Stack:** Python 3, `requests`, `beautifulsoup4`, `pytest`. Output: JSON enrichment files. No C++ build, no app changes.

**Why this shape (locked decisions — see memory `project_manga_synopsis_source_decision`):**
- Back-cover *images* are mostly unavailable/OCR-hostile; the per-volume synopsis *text* is published as plain text on retailer/publisher pages. Proven live 2026-05-29.
- The synopsis is keyed to the **English-edition ISBN**. Wikipedia volume tables (standardized "Graphic novel list" template) list both JP and English ISBNs per volume — that is our keying resolver.
- Proven end-to-end on Grand Blue Dreaming v4: Wikipedia → EN ISBN `978-1-63236-740-2` → B&N → full synopsis. These verbatim outputs are the test oracles below.

---

## File Structure

**Create (all new, isolated under one folder):**
- `scripts/synopsis_harvester/wikipedia_volumes.py` — parse a Wikipedia article's volume table → per-volume `{volumeNumber, isbnEn, isbnJp, englishTitle, englishReleaseDate}`. Pure functions over HTML (testable with fixtures).
- `scripts/synopsis_harvester/synopsis_sources.py` — given an English ISBN, fetch the official synopsis text (Barnes & Noble primary; Google Books optional fallback behind an API key). HTML/JSON parse functions are pure + fixture-tested; network fetch is a thin wrapper.
- `scripts/synopsis_harvester/enrichment_writer.py` — assemble + write `data/manga_enrichment/<seriesId>.volumes.json` in the exact loader schema.
- `scripts/synopsis_harvester/harvest_synopsis.py` — CLI orchestrator: read the catalog for a series, run the pipeline per volume with rate limiting, write the enrichment file, print a coverage report.
- `scripts/synopsis_harvester/requirements.txt`
- `scripts/synopsis_harvester/README.md`
- `scripts/synopsis_harvester/tests/__init__.py`
- `scripts/synopsis_harvester/tests/conftest.py`
- `scripts/synopsis_harvester/tests/fixtures/` (real captured HTML — saved during the relevant tasks)
- `scripts/synopsis_harvester/tests/test_wikipedia_volumes.py`
- `scripts/synopsis_harvester/tests/test_synopsis_sources.py`
- `scripts/synopsis_harvester/tests/test_enrichment_writer.py`

**Output data (written by the tool, not hand-edited):**
- `data/manga_enrichment/<seriesId>.volumes.json`

**Reference only — DO NOT EDIT (read to confirm contracts):**
- `data/manga_enrichment/one-piece.volumes.json` — the exact enrichment schema to emit.
- `src/core/manga/LocalMangaCatalogLoader.cpp:48-88` (overlay loader) and `:214-235` (merge by `volumeNumber`) — the consumer contract.
- `src/core/manga/wikipedia/WikipediaParser.cpp:37-91` — the existing C++ `Special:BookSources` ISBN regex (reference for the Python port).
- `data/mangafire_catalog/<seriesId>.json` — source of `seriesTitle`, `anilistId`, and the volume-number list to fill.

---

## Schema contract (emit exactly this)

The loader (`LocalMangaCatalogLoader.cpp:80-85`) keys by integer `volumeNumber` and (`:214-235`) merges `englishTitle`, `synopsis`, `englishReleaseDate` only when non-empty. Emit:

```json
{
  "seriesId": "grand-blue-dreaming",
  "anilistId": 100568,
  "title": "Grand Blue Dreaming",
  "enrichmentBasis": "wikipedia_isbn_bn_harvest",
  "lastVerifiedDate": "2026-05-29",
  "volumes": [
    {
      "volumeNumber": 4,
      "englishTitle": "",
      "englishReleaseDate": "2019-02-19",
      "synopsis": "The hit comedy manga comes to print by popular demand! ..."
    }
  ]
}
```

`englishTitle` / `englishReleaseDate` may be empty strings when Wikipedia lacks them — the loader skips empty fields, so empty is safe. `synopsis` is the harvest target.

---

### Task 1: Wikipedia volume-table parser

**Files:**
- Create: `scripts/synopsis_harvester/wikipedia_volumes.py`
- Create: `scripts/synopsis_harvester/tests/test_wikipedia_volumes.py`
- Create: `scripts/synopsis_harvester/tests/fixtures/grand_blue_dreaming_wikipedia.html` (captured in Step 1)
- Create: `scripts/synopsis_harvester/requirements.txt`, `scripts/synopsis_harvester/tests/__init__.py`, `scripts/synopsis_harvester/tests/conftest.py`

- [ ] **Step 1: Scaffold + capture the real Wikipedia fixture**

Create `scripts/synopsis_harvester/requirements.txt`:
```
requests>=2.31
beautifulsoup4>=4.12
pytest>=8.0
```

Create empty `scripts/synopsis_harvester/tests/__init__.py` and a `conftest.py` that exposes the fixtures dir:
```python
# scripts/synopsis_harvester/tests/conftest.py
from pathlib import Path
import pytest

@pytest.fixture
def fixtures_dir():
    return Path(__file__).parent / "fixtures"
```

Capture the real page as a fixture (run from repo root):
```bash
python -c "import requests,pathlib; pathlib.Path('scripts/synopsis_harvester/tests/fixtures').mkdir(parents=True,exist_ok=True); open('scripts/synopsis_harvester/tests/fixtures/grand_blue_dreaming_wikipedia.html','w',encoding='utf-8').write(requests.get('https://en.wikipedia.org/wiki/Grand_Blue_Dreaming', headers={'User-Agent':'TankobanSynopsisHarvester/1.0'}).text)"
```
Expected: a `grand_blue_dreaming_wikipedia.html` file > 100 KB containing `Special:BookSources/978-1-63236-740-2`.

- [ ] **Step 2: Write the failing test**

```python
# scripts/synopsis_harvester/tests/test_wikipedia_volumes.py
from synopsis_harvester.wikipedia_volumes import parse_volume_table

def test_grand_blue_volume4_english_isbn(fixtures_dir):
    html = (fixtures_dir / "grand_blue_dreaming_wikipedia.html").read_text(encoding="utf-8")
    vols = parse_volume_table(html)
    by_num = {v["volumeNumber"]: v for v in vols}
    assert 4 in by_num
    # English ISBN must be selected over the Japanese one for the same volume.
    assert by_num[4]["isbnEn"] == "9781632367402"
    assert by_num[4]["isbnJp"] == "9784063880816"

def test_grand_blue_first_six_have_english_isbns(fixtures_dir):
    html = (fixtures_dir / "grand_blue_dreaming_wikipedia.html").read_text(encoding="utf-8")
    by_num = {v["volumeNumber"]: v for v in parse_volume_table(html)}
    expected_en = {
        1: "9781632366665", 2: "9781632366672", 3: "9781632366689",
        4: "9781632367402", 5: "9781632367242", 6: "9781632367259",
    }
    for num, isbn in expected_en.items():
        assert by_num[num]["isbnEn"] == isbn, f"vol {num}"
```

- [ ] **Step 3: Run it — verify it fails**

Run (from `scripts/`): `cd scripts && python -m pytest synopsis_harvester/tests/test_wikipedia_volumes.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'synopsis_harvester.wikipedia_volumes'`.

- [ ] **Step 4: Implement the parser**

```python
# scripts/synopsis_harvester/wikipedia_volumes.py
"""Parse a Wikipedia manga article's volume table into per-volume records.

Wikipedia renders the standardized "Graphic novel list" template as table rows;
each volume row carries one or two Special:BookSources/<isbn> links (Japanese
and/or English edition). We classify each ISBN by prefix and keep the English one.
"""
import re
from datetime import datetime
from bs4 import BeautifulSoup

# Mirrors src/core/manga/wikipedia/WikipediaParser.cpp:37 (Special:BookSources regex).
_ISBN_HREF_RE = re.compile(r"/wiki/Special:BookSources/([0-9Xx\-]+)")
_VOL_ID_RE = re.compile(r"^vol(\d+)$")
_DATE_RE = re.compile(r"([A-Z][a-z]+ \d{1,2}, \d{4})")


def _normalize_isbn(raw):
    digits = raw.replace("-", "").upper()
    return digits if len(digits) in (10, 13) else None


def _is_japanese(isbn13):
    # JP group: ISBN-13 starting 9784, or ISBN-10 starting 4.
    return isbn13.startswith("9784") or (len(isbn13) == 10 and isbn13.startswith("4"))


def _to_isbn13(isbn):
    # Best-effort: callers only need a stable key; B&N/Google accept ISBN-13.
    if len(isbn) == 13:
        return isbn
    if len(isbn) == 10:
        core = "978" + isbn[:-1]
        total = sum((1 if i % 2 == 0 else 3) * int(d) for i, d in enumerate(core))
        return core + str((10 - total % 10) % 10)
    return isbn


def _iso_date(text):
    m = _DATE_RE.search(text or "")
    if not m:
        return ""
    try:
        return datetime.strptime(m.group(1), "%B %d, %Y").date().isoformat()
    except ValueError:
        return ""


def _row_volume_number(row):
    th = row.find("th", id=_VOL_ID_RE)
    if th:
        return int(_VOL_ID_RE.match(th["id"]).group(1))
    first = row.find(["th", "td"])
    if first:
        m = re.match(r"\s*(\d+)\s*$", first.get_text())
        if m:
            return int(m.group(1))
    return None


def parse_volume_table(html):
    """Return a list of {volumeNumber, isbnEn, isbnJp, englishTitle, englishReleaseDate}."""
    soup = BeautifulSoup(html, "html.parser")
    out = {}
    for table in soup.find_all("table"):
        if "Special:BookSources" not in str(table):
            continue
        for row in table.find_all("tr"):
            num = _row_volume_number(row)
            if num is None:
                continue
            isbn_en = isbn_jp = None
            for a in row.find_all("a", href=_ISBN_HREF_RE):
                m = _ISBN_HREF_RE.search(a["href"])
                norm = _normalize_isbn(m.group(1)) if m else None
                if not norm:
                    continue
                isbn13 = _to_isbn13(norm)
                if _is_japanese(isbn13):
                    isbn_jp = isbn_jp or isbn13
                else:
                    isbn_en = isbn_en or isbn13
            if isbn_en is None and isbn_jp is None:
                continue
            out[num] = {
                "volumeNumber": num,
                "isbnEn": isbn_en,
                "isbnJp": isbn_jp,
                "englishTitle": "",
                "englishReleaseDate": _iso_date(row.get_text(" ")),
            }
    return [out[k] for k in sorted(out)]
```

- [ ] **Step 5: Run the tests — verify they pass**

Run: `cd scripts && python -m pytest synopsis_harvester/tests/test_wikipedia_volumes.py -v`
Expected: PASS (both tests). If a row yields the JP ISBN as `isbnEn`, the prefix classifier needs the captured fixture re-checked — confirm `_is_japanese` against the real markup.

- [ ] **Step 6: Commit**

```bash
git add scripts/synopsis_harvester/wikipedia_volumes.py scripts/synopsis_harvester/requirements.txt scripts/synopsis_harvester/tests/
git commit -m "feat(synopsis): Wikipedia volume-table -> per-volume English ISBN parser"
```

---

### Task 2: Synopsis source (Barnes & Noble by ISBN)

**Files:**
- Create: `scripts/synopsis_harvester/synopsis_sources.py`
- Create: `scripts/synopsis_harvester/tests/test_synopsis_sources.py`
- Create: `scripts/synopsis_harvester/tests/fixtures/bn_9781632367402.html` (captured in Step 1)

- [ ] **Step 1: Capture the real B&N fixture**

```bash
python -c "import requests,pathlib; pathlib.Path('scripts/synopsis_harvester/tests/fixtures').mkdir(parents=True,exist_ok=True); open('scripts/synopsis_harvester/tests/fixtures/bn_9781632367402.html','w',encoding='utf-8').write(requests.get('https://www.barnesandnoble.com/w/?ean=9781632367402', headers={'User-Agent':'Mozilla/5.0'}).text)"
```
Expected: an HTML file containing the phrase `Swimsuits! Ramen!`. (If B&N returns a bot wall, retry with the header block in Step 4; do not proceed until the fixture contains the synopsis text.)

- [ ] **Step 2: Write the failing test**

```python
# scripts/synopsis_harvester/tests/test_synopsis_sources.py
from synopsis_harvester.synopsis_sources import extract_bn_synopsis

def test_extract_bn_grand_blue_v4(fixtures_dir):
    html = (fixtures_dir / "bn_9781632367402.html").read_text(encoding="utf-8")
    text = extract_bn_synopsis(html)
    assert text  # non-empty
    assert "Swimsuits! Ramen!" in text
    # Must capture the FULL blurb, not just the truncated preview.
    assert "the bane of all morons: an exam" in text
```

- [ ] **Step 3: Run it — verify it fails**

Run: `cd scripts && python -m pytest synopsis_harvester/tests/test_synopsis_sources.py -v`
Expected: FAIL — `ModuleNotFoundError`.

- [ ] **Step 4: Implement the extractor + fetch wrapper**

```python
# scripts/synopsis_harvester/synopsis_sources.py
"""Resolve an English-ISBN to its official per-volume synopsis text.

Primary: Barnes & Noble product page (keyless). We read the JSON-LD `description`
(complete text) and fall back to the visible overview block / meta description.
Optional: Google Books (needs GOOGLE_BOOKS_API_KEY) returns a usually-complete
description and is used only when B&N yields nothing.
"""
import json
import os
import re
import time
import requests
from bs4 import BeautifulSoup

_BN_HEADERS = {
    "User-Agent": ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                   "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36"),
    "Accept-Language": "en-US,en;q=0.9",
}


def _clean(text):
    return re.sub(r"\s+", " ", (text or "").replace("\xa0", " ")).strip()


def extract_bn_synopsis(html):
    """Pure: pull the complete synopsis from a Barnes & Noble product page HTML."""
    soup = BeautifulSoup(html, "html.parser")
    # 1) JSON-LD description (most complete).
    for tag in soup.find_all("script", type="application/ld+json"):
        try:
            data = json.loads(tag.string or "")
        except (ValueError, TypeError):
            continue
        for obj in (data if isinstance(data, list) else [data]):
            if isinstance(obj, dict) and obj.get("description"):
                desc = _clean(BeautifulSoup(obj["description"], "html.parser").get_text(" "))
                if len(desc) > 40:
                    return desc
    # 2) Visible overview block.
    for sel in ("#productInfoOverview", "#overview", "div.overview-content"):
        node = soup.select_one(sel)
        if node:
            desc = _clean(node.get_text(" "))
            if len(desc) > 40:
                return desc
    # 3) Meta description (last resort, may be short).
    meta = soup.find("meta", attrs={"name": "description"})
    if meta and meta.get("content"):
        desc = _clean(meta["content"])
        if len(desc) > 40:
            return desc
    return ""


def fetch_bn_synopsis(isbn13, session=None, delay=0.0):
    """Network: fetch the B&N page for an ISBN-13 and extract its synopsis."""
    if delay:
        time.sleep(delay)
    sess = session or requests.Session()
    resp = sess.get(f"https://www.barnesandnoble.com/w/?ean={isbn13}",
                    headers=_BN_HEADERS, timeout=30, allow_redirects=True)
    if resp.status_code != 200:
        return ""
    return extract_bn_synopsis(resp.text)


def fetch_google_books_synopsis(isbn13, session=None):
    """Optional fallback. Requires env GOOGLE_BOOKS_API_KEY (keyless quota is 0)."""
    key = os.environ.get("GOOGLE_BOOKS_API_KEY")
    if not key:
        return ""
    sess = session or requests.Session()
    resp = sess.get("https://www.googleapis.com/books/v1/volumes",
                    params={"q": f"isbn:{isbn13}", "key": key}, timeout=30)
    if resp.status_code != 200:
        return ""
    items = resp.json().get("items") or []
    if not items:
        return ""
    return _clean(items[0].get("volumeInfo", {}).get("description", ""))


def resolve_synopsis(isbn13, session=None, delay=0.0):
    """B&N first; Google Books only if B&N is empty and a key is configured."""
    text = fetch_bn_synopsis(isbn13, session=session, delay=delay)
    return text or fetch_google_books_synopsis(isbn13, session=session)
```

- [ ] **Step 5: Run the tests — verify they pass**

Run: `cd scripts && python -m pytest synopsis_harvester/tests/test_synopsis_sources.py -v`
Expected: PASS. If the JSON-LD branch misses, inspect the captured fixture and adjust the selector list in `extract_bn_synopsis` to the real container — the oracle phrases above must appear in the returned text.

- [ ] **Step 6: Commit**

```bash
git add scripts/synopsis_harvester/synopsis_sources.py scripts/synopsis_harvester/tests/test_synopsis_sources.py scripts/synopsis_harvester/tests/fixtures/bn_9781632367402.html
git commit -m "feat(synopsis): Barnes & Noble ISBN -> synopsis extractor (+ optional Google Books)"
```

---

### Task 3: Enrichment-file writer

**Files:**
- Create: `scripts/synopsis_harvester/enrichment_writer.py`
- Create: `scripts/synopsis_harvester/tests/test_enrichment_writer.py`

- [ ] **Step 1: Write the failing test**

```python
# scripts/synopsis_harvester/tests/test_enrichment_writer.py
import json
from synopsis_harvester.enrichment_writer import build_enrichment

def test_build_enrichment_shape():
    vols = [
        {"volumeNumber": 4, "englishTitle": "", "englishReleaseDate": "2019-02-19",
         "synopsis": "The hit comedy manga ..."},
        {"volumeNumber": 5, "englishTitle": "", "englishReleaseDate": "",
         "synopsis": ""},
    ]
    doc = build_enrichment(series_id="grand-blue-dreaming", anilist_id=100568,
                           title="Grand Blue Dreaming", volumes=vols,
                           verified_date="2026-05-29")
    assert doc["seriesId"] == "grand-blue-dreaming"
    assert doc["anilistId"] == 100568
    assert doc["enrichmentBasis"] == "wikipedia_isbn_bn_harvest"
    assert doc["lastVerifiedDate"] == "2026-05-29"
    # Only volumes that actually got a synopsis are emitted.
    nums = [v["volumeNumber"] for v in doc["volumes"]]
    assert nums == [4]
    v = doc["volumes"][0]
    assert set(v.keys()) == {"volumeNumber", "englishTitle", "englishReleaseDate", "synopsis"}
    # round-trips as valid JSON
    json.loads(json.dumps(doc))
```

- [ ] **Step 2: Run it — verify it fails**

Run: `cd scripts && python -m pytest synopsis_harvester/tests/test_enrichment_writer.py -v`
Expected: FAIL — `ModuleNotFoundError`.

- [ ] **Step 3: Implement the writer**

```python
# scripts/synopsis_harvester/enrichment_writer.py
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
```

- [ ] **Step 4: Run the tests — verify they pass**

Run: `cd scripts && python -m pytest synopsis_harvester/tests/test_enrichment_writer.py -v`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add scripts/synopsis_harvester/enrichment_writer.py scripts/synopsis_harvester/tests/test_enrichment_writer.py
git commit -m "feat(synopsis): enrichment-overlay JSON writer (loader-schema exact)"
```

---

### Task 4: CLI orchestrator + end-to-end validation

**Files:**
- Create: `scripts/synopsis_harvester/harvest_synopsis.py`
- Create: `scripts/synopsis_harvester/README.md`
- Output: `data/manga_enrichment/grand-blue-dreaming.volumes.json`

- [ ] **Step 1: Implement the orchestrator**

```python
# scripts/synopsis_harvester/harvest_synopsis.py
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
```

- [ ] **Step 2: Write the README**

Create `scripts/synopsis_harvester/README.md` documenting: purpose, the 3-step pipeline, `pip install -r requirements.txt`, the run command, the output path, the optional `GOOGLE_BOOKS_API_KEY`, and that the app auto-merges the overlay on next load (no rebuild needed). Note the locked-decision memory `project_manga_synopsis_source_decision`.

- [ ] **Step 3: Full regression — all unit tests green**

Run: `cd scripts && python -m pytest synopsis_harvester/tests/ -v`
Expected: PASS (all tests from Tasks 1-3).

- [ ] **Step 4: End-to-end live run on the proven series**

Run (from repo root): `python scripts/synopsis_harvester/harvest_synopsis.py --series grand-blue-dreaming`
Expected: prints `synopsis coverage: N/M volumes`, writes `data/manga_enrichment/grand-blue-dreaming.volumes.json`. Open the file and confirm Volume 4's `synopsis` contains "Swimsuits! Ramen!" and "the bane of all morons: an exam."

- [ ] **Step 5: Coverage sweep across the pilot catalog (measure, don't assume)**

Run the harvester for each niche/pilot series and record coverage. These are the series with local catalogs (`data/mangafire_catalog/`); some have non-obvious Wikipedia titles, so pass `--wiki` where the default fails:
```bash
python scripts/synopsis_harvester/harvest_synopsis.py --series grand-blue-dreaming
python scripts/synopsis_harvester/harvest_synopsis.py --series death-note
python scripts/synopsis_harvester/harvest_synopsis.py --series bleach
python scripts/synopsis_harvester/harvest_synopsis.py --series berserk
python scripts/synopsis_harvester/harvest_synopsis.py --series 20th-century-boys --wiki "20th Century Boys"
```
Record each `coverage: N/M` line. This is the niche-coverage measurement the research promised — capture the numbers (and which volumes fall through) rather than assuming the route is universal. Volumes that come back empty are the long-tail gap (out-of-print / no English release / Wikipedia table missing the English ISBN).

- [ ] **Step 6: Commit (code + the generated grand-blue overlay as proof)**

```bash
git add scripts/synopsis_harvester/harvest_synopsis.py scripts/synopsis_harvester/README.md data/manga_enrichment/grand-blue-dreaming.volumes.json
git commit -m "feat(synopsis): harvester CLI + end-to-end Grand Blue overlay (Wikipedia->ISBN->B&N)"
```

- [ ] **Step 7: Visual smoke (eyes-on gate — Hemanth)**

Launch the app (`build_and_run.bat` — no rebuild needed; the overlay is data), open Grand Blue Dreaming's series view, and confirm the per-volume synopses now render under the volume rows (especially Vol 4). This is the acceptance gate: data present on disk proves the harvest; only eyes-on confirms it renders. If a volume's text looks like a generic series blurb rather than the per-volume one, flag it — that means B&N served series-level copy for that ISBN and we note it as a per-volume-fidelity exception.

---

## Self-Review

**Spec coverage:**
- Wikipedia → English ISBN (keying resolver): Task 1. ✓
- English ISBN → synopsis text (B&N primary, Google Books optional): Task 2. ✓
- Write the loader-schema enrichment overlay: Task 3. ✓
- Orchestrate per series + end-to-end on the proven niche series: Task 4 Steps 1,4. ✓
- Niche-coverage *measurement* (the research deliverable): Task 4 Step 5. ✓
- Zero app/C++ changes (overlay already consumed + rendered): confirmed in ground-truth; Task 4 Step 7 smoke. ✓
- English-vs-Japanese ISBN keying rule (the earlier dead-end): Task 1 `_is_japanese` classifier + Task 1 test asserting EN over JP for Vol 4. ✓

**Placeholder scan:** No TBD/TODO; every code step has complete, runnable code; HTML that can't be embedded verbatim is captured as a real fixture in an explicit step with a verifiable oracle phrase. ✓

**Type/name consistency:** `parse_volume_table` returns dicts with keys `volumeNumber/isbnEn/isbnJp/englishTitle/englishReleaseDate`, consumed unchanged by the orchestrator; `build_enrichment(series_id, anilist_id, title, volumes, verified_date)` signature matches its call in `harvest()`; `resolve_synopsis(isbn13, session, delay)` matches its call. Output keys (`volumeNumber/englishTitle/englishReleaseDate/synopsis`) match the loader contract at `LocalMangaCatalogLoader.cpp:214-235`. ✓

**Scope:** Single subsystem (offline synopsis harvester). The comics-catalog arc and any back-cover-image work are explicitly out of scope (parked — see memory). ✓
