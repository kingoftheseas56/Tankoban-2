# COMICS_WESTERN_RICHNESS Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give each Western catalogue series manga-grade series-level richness (synopsis + author + publisher + year + genre), auto-sourced with zero signup, baked at harvest and shown on the series detail page.

**Architecture:** Two halves joined by the catalogue-JSON contract. Half A (Python harvester) parses the RCO `Summary:` block already in the fetched HTML, enriches author/publisher/year/genre from Wikidata, falls back to Wikipedia for a thin synopsis, and bakes everything into the per-series JSON. Half B (C++) extends `WesternCatalogLoader` to read the new fields and adds an "about-block" header to the Western series detail render. Per-edition covers, per-TPB synopsis, live search, and downloads are explicitly out (separate arcs).

**Tech Stack:** Python 3 (stdlib `urllib`, `re`, `json`; pytest), C++/Qt6 (GoogleTest `tankoban_tests`).

**Spec:** `docs/superpowers/specs/2026-06-01-comics-western-richness-design.md`

---

## File Structure

**Half A — Python (`scripts/comics_catalogue/`):**
- Modify `parse_rco.py` — add `parse_series_summary(html)`.
- Create `wikidata_enrich.py` — `pick_comic_entity()`, `extract_fields()` (pure) + network glue `enrich(title)`.
- Create `wikipedia_fallback.py` — `fetch_extract(title)` + `needs_fallback(text)` (pure).
- Modify `harvest.py` — `build_record()` accepts enrichment kwargs; `harvest_series()` orchestrates; bakes new JSON keys.
- Tests: modify `tests/test_parse_rco.py`; create `tests/test_wikidata_enrich.py`, `tests/test_wikipedia_fallback.py`; create fixture `tests/fixtures/wikidata_invincible.json`.

**Half B — C++:**
- Modify `src/core/manga/WesternCatalogLoader.cpp` — read `author`, `publisher`(→`studio`), `genres`, `yearStart`/`yearEnd`, `status`.
- Modify `src/ui/pages/comics/ComicsSeriesView.{h,cpp}` — about-block header on the catalog-populate path.
- Create `tests/core/manga/test_western_catalog_loader.cpp` + fixture `tests/fixtures/western_catalogue/enriched.json`.
- Modify `cmake/TankobanTests.cmake` — register the new test.

**JSON contract (the seam — land Task 4 first):** existing keys `{seriesId, seriesTitle, source, seriesCover, editions[]}` gain `synopsis` (string), `author` (string), `publisher` (string), `genres` (string[]), `yearStart` (int), `yearEnd` (int), `status` (string). `schemaVersion` bumped to 2. Missing keys tolerated (empty → graceful).

---

## Half A — Python harvester enrichment

### Task 1: RCO `Summary:` parser

**Files:**
- Modify: `scripts/comics_catalogue/parse_rco.py`
- Test: `scripts/comics_catalogue/tests/test_parse_rco.py`
- Fixture (exists): `scripts/comics_catalogue/tests/fixtures/rco_series_invincible.html`

- [ ] **Step 1: Write the failing test** (append to `tests/test_parse_rco.py`)

```python
from parse_rco import parse_series_summary

def _fixture(name):
    import pathlib
    return (pathlib.Path(__file__).parent / "fixtures" / name).read_text(
        encoding="utf-8", errors="replace")

def test_parse_series_summary_extracts_rco_block():
    html = _fixture("rco_series_invincible.html")
    summary = parse_series_summary(html)
    assert summary.startswith("Girls, acne, homework, supervillains")
    assert "Mark Grayson" in summary
    assert "<" not in summary  # tags stripped

def test_parse_series_summary_missing_returns_empty():
    assert parse_series_summary("<html><body>no summary here</body></html>") == ""
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd scripts/comics_catalogue && python -m pytest tests/test_parse_rco.py -k summary -v`
Expected: FAIL — `ImportError: cannot import name 'parse_series_summary'`.

- [ ] **Step 3: Implement** (add to `parse_rco.py`)

```python
import html as _html

def parse_series_summary(page_html: str) -> str:
    """Extract the RCO series 'Summary:' prose block. RCO markup:
        <span class="info">Summary:</span> <p>...prose...</p>
    Returns the stripped, unescaped text, or '' if absent."""
    m = re.search(
        r'<span class="info">\s*Summary:\s*</span>\s*<p>(.*?)</p>',
        page_html, re.S | re.I)
    if not m:
        return ""
    text = re.sub(r'<[^>]+>', ' ', m.group(1))
    return re.sub(r'\s+', ' ', _html.unescape(text)).strip()
```

(If `import re` is not already at the top of `parse_rco.py`, add it.)

- [ ] **Step 4: Run test to verify it passes**

Run: `cd scripts/comics_catalogue && python -m pytest tests/test_parse_rco.py -k summary -v`
Expected: PASS (2 passed).

- [ ] **Step 5: Commit**

```bash
git add scripts/comics_catalogue/parse_rco.py scripts/comics_catalogue/tests/test_parse_rco.py
git commit -m "feat(comics): RCO series Summary block parser"
```

---

### Task 2: Wikidata enrichment module

**Files:**
- Create: `scripts/comics_catalogue/wikidata_enrich.py`
- Create: `scripts/comics_catalogue/tests/test_wikidata_enrich.py`
- Create: `scripts/comics_catalogue/tests/fixtures/wikidata_invincible.json`

- [ ] **Step 1: Create the mock entity fixture** `tests/fixtures/wikidata_invincible.json`

```json
{
  "id": "Q1426726",
  "claims": {
    "P31":  [{"mainsnak": {"datavalue": {"value": {"id": "Q14406742"}}}}],
    "P50":  [{"mainsnak": {"datavalue": {"value": {"id": "Q357331"}}}}],
    "P110": [{"mainsnak": {"datavalue": {"value": {"id": "Q5176580"}}}}],
    "P123": [{"mainsnak": {"datavalue": {"value": {"id": "Q1057217"}}}}],
    "P136": [{"mainsnak": {"datavalue": {"value": {"id": "Q1535153"}}}}],
    "P577": [{"mainsnak": {"datavalue": {"value": {"time": "+2003-01-01T00:00:00Z"}}}}]
  }
}
```

- [ ] **Step 2: Write the failing tests** `tests/test_wikidata_enrich.py`

```python
import json, pathlib
from wikidata_enrich import pick_comic_entity, extract_fields

def _fix(name):
    return json.loads((pathlib.Path(__file__).parent / "fixtures" / name)
                      .read_text(encoding="utf-8"))

def test_pick_comic_entity_prefers_comic_instance():
    cands = [
        {"id": "Q999", "instance_of": ["Q35", "Q5"]},        # not a comic
        {"id": "Q1426726", "instance_of": ["Q14406742"]},    # comic book series
    ]
    assert pick_comic_entity(cands) == "Q1426726"

def test_pick_comic_entity_none_when_no_comic():
    cands = [{"id": "Q999", "instance_of": ["Q5"]}]
    assert pick_comic_entity(cands) is None

def test_extract_fields_maps_claims_to_labels():
    entity = _fix("wikidata_invincible.json")
    labels = {"Q357331": "Robert Kirkman", "Q5176580": "Cory Walker",
              "Q1057217": "Image Comics", "Q1535153": "superhero comics"}
    out = extract_fields(entity, labels)
    assert out["author"] == "Robert Kirkman, Cory Walker"   # P50 + P110, joined
    assert out["publisher"] == "Image Comics"               # P123
    assert out["genres"] == ["superhero comics"]            # P136
    assert out["yearStart"] == 2003                         # P577 year
```

- [ ] **Step 3: Run to verify it fails**

Run: `cd scripts/comics_catalogue && python -m pytest tests/test_wikidata_enrich.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'wikidata_enrich'`.

- [ ] **Step 4: Implement** `wikidata_enrich.py`

```python
"""Enrich a Western series with structured facts from Wikidata (free, no
signup). Pure extraction (pick_comic_entity, extract_fields) is unit-tested;
the network glue (enrich) is exercised by the harvester run. No API key."""
import json
import re
import time
import urllib.parse
import urllib.request

_API = "https://www.wikidata.org/w/api.php"
_UA = "TankobanCatalogue/1.0 (comics catalogue enrichment; contact: local)"

# P31 (instance-of) targets that mark a Western comic. Tunable.
COMIC_TYPES = {
    "Q14406742",  # comic book series
    "Q1760610",   # comic book
    "Q725377",    # graphic novel
    "Q333291",    # comic strip
    "Q1004",      # comics
}
# author-ish properties, in display order
_CREATOR_PROPS = ["P50", "P58", "P110"]  # author, screenwriter, illustrator


def _get(params: dict) -> dict:
    qs = urllib.parse.urlencode({**params, "format": "json"})
    req = urllib.request.Request(_API + "?" + qs, headers={"User-Agent": _UA})
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.loads(r.read().decode("utf-8"))


def _claim_ids(entity: dict, prop: str) -> list[str]:
    out = []
    for c in entity.get("claims", {}).get(prop, []):
        try:
            out.append(c["mainsnak"]["datavalue"]["value"]["id"])
        except (KeyError, TypeError):
            pass
    return out


def _claim_year(entity: dict, prop: str) -> int:
    for c in entity.get("claims", {}).get(prop, []):
        try:
            t = c["mainsnak"]["datavalue"]["value"]["time"]  # "+2003-01-01T..."
            m = re.search(r"([+-]?\d{4})", t)
            if m:
                return abs(int(m.group(1)))
        except (KeyError, TypeError):
            pass
    return 0


def pick_comic_entity(candidates: list[dict]) -> str | None:
    """candidates: [{id, instance_of:[Qid,...]}]. Return the first whose
    instance_of intersects COMIC_TYPES, else None."""
    for c in candidates:
        if COMIC_TYPES.intersection(c.get("instance_of", [])):
            return c["id"]
    return None


def extract_fields(entity: dict, labels: dict) -> dict:
    """Pure: map an entity's claims + a {Qid: label} map to richness fields."""
    creators = []
    for prop in _CREATOR_PROPS:
        for qid in _claim_ids(entity, prop):
            name = labels.get(qid)
            if name and name not in creators:
                creators.append(name)
    publisher_ids = _claim_ids(entity, "P123")
    genre_ids = _claim_ids(entity, "P136")
    year = _claim_year(entity, "P577") or _claim_year(entity, "P571")
    return {
        "author": ", ".join(creators),
        "publisher": next((labels[q] for q in publisher_ids if q in labels), ""),
        "genres": [labels[q] for q in genre_ids if q in labels],
        "yearStart": year,
    }


def enrich(title: str) -> dict:
    """Network glue: search Wikidata for `title`, disambiguate to a comic
    entity, fetch its claims, resolve referenced labels, return richness
    fields. Returns {} on any miss (caller treats as 'no enrichment')."""
    try:
        search = _get({"action": "wbsearchentities", "search": title,
                       "language": "en", "type": "item", "limit": 10})
        ids = [h["id"] for h in search.get("search", [])]
        if not ids:
            return {}
        time.sleep(0.5)
        ent = _get({"action": "wbgetentities", "ids": "|".join(ids),
                    "props": "claims"})
        cands = []
        for qid, e in ent.get("entities", {}).items():
            cands.append({"id": qid, "instance_of": _claim_ids(e, "P31")})
        chosen = pick_comic_entity(cands)
        if not chosen:
            return {}
        entity = ent["entities"][chosen]
        # collect referenced Qids needing labels
        refs = set()
        for prop in _CREATOR_PROPS + ["P123", "P136"]:
            refs.update(_claim_ids(entity, prop))
        labels = {}
        if refs:
            time.sleep(0.5)
            lab = _get({"action": "wbgetentities", "ids": "|".join(sorted(refs)),
                        "props": "labels", "languages": "en"})
            for qid, e in lab.get("entities", {}).items():
                labels[qid] = e.get("labels", {}).get("en", {}).get("value", "")
        return extract_fields(entity, labels)
    except Exception:
        return {}
```

- [ ] **Step 5: Run to verify it passes**

Run: `cd scripts/comics_catalogue && python -m pytest tests/test_wikidata_enrich.py -v`
Expected: PASS (3 passed).

- [ ] **Step 6: Commit**

```bash
git add scripts/comics_catalogue/wikidata_enrich.py scripts/comics_catalogue/tests/test_wikidata_enrich.py scripts/comics_catalogue/tests/fixtures/wikidata_invincible.json
git commit -m "feat(comics): Wikidata enrichment (author/publisher/genre/year)"
```

---

### Task 3: Wikipedia synopsis fallback

**Files:**
- Create: `scripts/comics_catalogue/wikipedia_fallback.py`
- Create: `scripts/comics_catalogue/tests/test_wikipedia_fallback.py`

- [ ] **Step 1: Write the failing test** `tests/test_wikipedia_fallback.py`

```python
from wikipedia_fallback import needs_fallback

def test_needs_fallback_true_for_empty_or_thin():
    assert needs_fallback("") is True
    assert needs_fallback("Too short.") is True          # < 120 chars

def test_needs_fallback_false_for_substantial():
    assert needs_fallback("x" * 130) is False
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd scripts/comics_catalogue && python -m pytest tests/test_wikipedia_fallback.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'wikipedia_fallback'`.

- [ ] **Step 3: Implement** `wikipedia_fallback.py`

```python
"""Wikipedia REST summary as a synopsis fallback when RCO's is thin/missing.
needs_fallback() is pure (unit-tested); fetch_extract() is network glue."""
import json
import urllib.parse
import urllib.request

_REST = "https://en.wikipedia.org/api/rest_v1/page/summary/"
_UA = "TankobanCatalogue/1.0 (comics catalogue; contact: local)"
_MIN_CHARS = 120  # below this, treat RCO summary as too thin


def needs_fallback(rco_summary: str) -> bool:
    return len((rco_summary or "").strip()) < _MIN_CHARS


def fetch_extract(title: str) -> str:
    """Best-effort Wikipedia overview for `title`. '' on any miss."""
    try:
        slug = urllib.parse.quote(title.replace(" ", "_"))
        req = urllib.request.Request(_REST + slug, headers={"User-Agent": _UA})
        with urllib.request.urlopen(req, timeout=15) as r:
            return json.loads(r.read().decode("utf-8")).get("extract", "") or ""
    except Exception:
        return ""
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd scripts/comics_catalogue && python -m pytest tests/test_wikipedia_fallback.py -v`
Expected: PASS (2 passed).

- [ ] **Step 5: Commit**

```bash
git add scripts/comics_catalogue/wikipedia_fallback.py scripts/comics_catalogue/tests/test_wikipedia_fallback.py
git commit -m "feat(comics): Wikipedia synopsis fallback helper"
```

---

### Task 4: Wire enrichment into the harvester + bake JSON keys

**Files:**
- Modify: `scripts/comics_catalogue/harvest.py`
- Test: `scripts/comics_catalogue/tests/test_harvest.py`

- [ ] **Step 1: Write the failing test** (append to `tests/test_harvest.py`)

```python
def test_build_record_bakes_enrichment_fields():
    items = [{"label": "TPB 1 Family matters", "href": "/Comic/X/TPB-1"}]
    rec = build_record(
        "x", "X", items,
        series_cover="/Uploads/x.jpg",
        synopsis="A teenage superhero grows up.",
        author="Robert Kirkman",
        publisher="Image Comics",
        genres=["superhero comics"],
        year_start=2003,
    )
    assert rec["synopsis"] == "A teenage superhero grows up."
    assert rec["author"] == "Robert Kirkman"
    assert rec["publisher"] == "Image Comics"
    assert rec["genres"] == ["superhero comics"]
    assert rec["yearStart"] == 2003
    assert rec["schemaVersion"] == 2

def test_build_record_enrichment_defaults_empty():
    rec = build_record("y", "Y", [{"label": "Omnibus 1", "href": "/Comic/Y/O1"}])
    assert rec["synopsis"] == ""
    assert rec["author"] == ""
    assert rec["genres"] == []
    assert rec["yearStart"] == 0
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd scripts/comics_catalogue && python -m pytest tests/test_harvest.py -k enrichment -v`
Expected: FAIL — `TypeError: build_record() got an unexpected keyword argument 'synopsis'` (and missing keys).

- [ ] **Step 3: Implement — extend `build_record`** in `harvest.py`

Replace the `build_record` signature + return dict with:

```python
def build_record(series_id: str, series_title: str, items: list[dict],
                 series_cover: str = "",
                 synopsis: str = "", author: str = "", publisher: str = "",
                 genres: list[str] | None = None,
                 year_start: int = 0, year_end: int = 0,
                 status: str = "") -> dict:
    collected = [it for it in items if is_collected(it["label"])]
    collected.sort(key=lambda it: edition_tier(it["label"]))
    editions = [
        {"label": it["label"], "href": it["href"],
         "formatTier": edition_tier(it["label"])}
        for it in collected
    ]
    return {
        "seriesId": series_id,
        "seriesTitle": series_title,
        "source": "rco",
        "seriesCover": series_cover,
        "schemaVersion": 2,
        "synopsis": synopsis,
        "author": author,
        "publisher": publisher,
        "genres": genres or [],
        "yearStart": year_start,
        "yearEnd": year_end,
        "status": status,
        "editions": editions,
    }
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd scripts/comics_catalogue && python -m pytest tests/test_harvest.py -k enrichment -v`
Expected: PASS (2 passed).

- [ ] **Step 5: Wire enrichment into `harvest_series`** in `harvest.py`

Add imports at top: `from parse_rco import parse_series, parse_series_cover, parse_series_summary` (extend existing import), `import wikidata_enrich`, `import wikipedia_fallback`. Replace `harvest_series` body:

```python
def harvest_series(series_id: str, series_title: str, rco_name: str) -> dict:
    """Fetch one RCO series page, build its record, and enrich it with
    synopsis (RCO Summary -> Wikipedia fallback) + Wikidata structured fields.
    Enrichment failures degrade silently to empty (graceful)."""
    html = _fetch(f"{_BASE}/Comic/{rco_name}")
    items = parse_series(html)
    cover = parse_series_cover(html)

    synopsis = parse_series_summary(html)
    if wikipedia_fallback.needs_fallback(synopsis):
        wiki = wikipedia_fallback.fetch_extract(series_title)
        if len(wiki) > len(synopsis):
            synopsis = wiki

    wd = wikidata_enrich.enrich(series_title)  # {} on miss

    return build_record(
        series_id, series_title, items, series_cover=cover,
        synopsis=synopsis,
        author=wd.get("author", ""),
        publisher=wd.get("publisher", ""),
        genres=wd.get("genres", []),
        year_start=wd.get("yearStart", 0),
    )
```

- [ ] **Step 6: Run the full harvester test suite to confirm nothing regressed**

Run: `cd scripts/comics_catalogue && python -m pytest -v`
Expected: PASS (all tests, including the prior 26 + the new ones).

- [ ] **Step 7: Commit**

```bash
git add scripts/comics_catalogue/harvest.py scripts/comics_catalogue/tests/test_harvest.py
git commit -m "feat(comics): harvester bakes synopsis + Wikidata richness into catalogue JSON"
```

---

### Task 5: Regenerate the 13 catalogue JSONs (data run)

**Files:**
- Modify (regenerate): `data/western_catalogue/*.json`

- [ ] **Step 1: Run the harvester from repo root**

Run: `python scripts/comics_catalogue/harvest.py`
Expected: 13 lines `<series>: N editions -> ...\data\western_catalogue\<slug>.json`, no tracebacks. (Wikidata/Wikipedia calls add ~1-2s/series; total ~1-2 min.)

- [ ] **Step 2: Verify enrichment landed**

Run:
```bash
cd "c:/Users/Suprabha/Desktop/Tankoban 2" && for f in invincible spawn watchmen chew; do python -c "import json;d=json.load(open('data/western_catalogue/$f.json'));print('%-12s syn=%d author=%r pub=%r genres=%d year=%s'%(d['seriesId'],len(d['synopsis']),d['author'][:30],d['publisher'][:20],len(d['genres']),d['yearStart']))"; done
```
Expected: each shows a non-trivial `syn=` length and most show author/publisher/year populated. (Some long-tail series may have empty Wikidata fields — acceptable per graceful-degradation. Log which were thin.)

- [ ] **Step 3: Commit the regenerated catalogue**

```bash
git add data/western_catalogue/
git commit -m "data(comics): regenerate Western catalogue with synopsis + richness"
```

---

## Half B — C++ render wiring

### Task 6: `WesternCatalogLoader` reads the new fields

**Files:**
- Modify: `src/core/manga/WesternCatalogLoader.cpp`
- Create: `tests/core/manga/test_western_catalog_loader.cpp`
- Create: `tests/fixtures/western_catalogue/enriched.json`
- Modify: `cmake/TankobanTests.cmake`

- [ ] **Step 1: Create the test fixture** `tests/fixtures/western_catalogue/enriched.json`

```json
{
  "seriesId": "invincible",
  "seriesTitle": "Invincible",
  "source": "rco",
  "seriesCover": "/Uploads/Etc/x.jpg",
  "schemaVersion": 2,
  "synopsis": "A teenage superhero grows up.",
  "author": "Robert Kirkman, Cory Walker",
  "publisher": "Image Comics",
  "genres": ["superhero comics", "coming-of-age"],
  "yearStart": 2003,
  "yearEnd": 2018,
  "status": "FINISHED",
  "editions": [
    {"label": "TPB 1 Family Matters", "href": "/Comic/Invincible/TPB-1", "formatTier": 2}
  ]
}
```

- [ ] **Step 2: Write the failing test** `tests/core/manga/test_western_catalog_loader.cpp`

```cpp
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QFileInfo>
#include "core/manga/WesternCatalogLoader.h"

using tankoban::manga::WesternCatalogLoader;

static QString fixturePath() {
    // tests run with CWD = build dir; the repo-root fixture is two levels up.
    return QFINDTESTDATA("tests/fixtures/western_catalogue/enriched.json");
}

TEST(WesternCatalogLoaderTest, ReadsEnrichmentFields) {
    const auto cat = WesternCatalogLoader::loadFromFile(fixturePath());
    ASSERT_TRUE(cat.has_value());
    EXPECT_EQ(cat->seriesSynopsis, QStringLiteral("A teenage superhero grows up."));
    EXPECT_EQ(cat->author, QStringLiteral("Robert Kirkman, Cory Walker"));
    EXPECT_EQ(cat->studio, QStringLiteral("Image Comics"));   // 'studio' slot = publisher
    EXPECT_EQ(cat->genres.size(), 2);
    EXPECT_EQ(cat->publishedYearStart, 2003);
    EXPECT_EQ(cat->publishedYearEnd, 2018);
    EXPECT_EQ(cat->volumes.size(), 1);
}
```

(If `QFINDTESTDATA` is not already used in this test target, instead read via an absolute path built from `SOURCE_DIR`; check how `tests/core/book/*` resolve fixtures and mirror that exact mechanism.)

- [ ] **Step 3: Register the test** in `cmake/TankobanTests.cmake`

Add to the `add_executable(tankoban_tests ...)` source list (alongside the other `tests/core/...` entries):

```cmake
        tests/core/manga/test_western_catalog_loader.cpp
```

- [ ] **Step 4: Run to verify it fails**

Run: `cmake --build out --target tankoban_tests && cd out && ctest --output-on-failure -R WesternCatalogLoaderTest`
Expected: FAIL — assertions on `author`/`studio`/`genres`/`publishedYearStart` (loader doesn't read them yet).

- [ ] **Step 5: Implement** — extend the parse block in `WesternCatalogLoader.cpp`

After the existing `cat.status = ...` / synopsis lines (the block that sets `cat.seriesSynopsis`, `cat.status`), add:

```cpp
    cat.author = root.value(QStringLiteral("author")).toString();
    cat.studio = root.value(QStringLiteral("publisher")).toString();  // publisher reuses 'studio'
    cat.publishedYearStart = root.value(QStringLiteral("yearStart")).toInt();
    cat.publishedYearEnd   = root.value(QStringLiteral("yearEnd")).toInt();
    const QJsonArray genresArr = root.value(QStringLiteral("genres")).toArray();
    cat.genres.clear();
    for (const auto& g : genresArr) {
        const QString s = g.toString().trimmed();
        if (!s.isEmpty()) cat.genres.append(s);
    }
```

Also ensure `cat.seriesSynopsis = root.value(QStringLiteral("synopsis")).toString();` (the harvester now writes `synopsis`; confirm the loader reads that key — adjust if it previously read a different key).

- [ ] **Step 6: Run to verify it passes**

Run: `cmake --build out --target tankoban_tests && cd out && ctest --output-on-failure -R WesternCatalogLoaderTest`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/core/manga/WesternCatalogLoader.cpp tests/core/manga/test_western_catalog_loader.cpp tests/fixtures/western_catalogue/enriched.json cmake/TankobanTests.cmake
git commit -m "feat(comics): WesternCatalogLoader reads enrichment fields"
```

---

### Task 7: Western series detail about-block header

**Files:**
- Modify: `src/ui/pages/comics/ComicsSeriesView.h`
- Modify: `src/ui/pages/comics/ComicsSeriesView.cpp`

This is UI render work, verified by `build_check` + a render smoke (no unit test — matches the codebase's UI-layer convention).

- [ ] **Step 1: Add the header widget + builder declaration** in `ComicsSeriesView.h`

In the private section, add:

```cpp
    QWidget*  m_aboutBlock = nullptr;   // Western catalog about-block (synopsis + meta)
    QLabel*   m_aboutSynopsis = nullptr;
    QLabel*   m_aboutMeta = nullptr;    // "Author · Publisher · Year · Genre"
    void buildAboutBlock();             // lazy-construct the about-block widgets
    void updateAboutBlock(const tankoban::manga::MangaCatalog& catalog);
```

- [ ] **Step 2: Implement `buildAboutBlock` + `updateAboutBlock`** in `ComicsSeriesView.cpp`

```cpp
void ComicsSeriesView::buildAboutBlock()
{
    if (m_aboutBlock) return;
    m_aboutBlock = new QWidget(this);
    m_aboutBlock->setObjectName("WesternAboutBlock");
    auto* v = new QVBoxLayout(m_aboutBlock);
    v->setContentsMargins(0, 0, 0, 12);
    v->setSpacing(6);
    m_aboutMeta = new QLabel(m_aboutBlock);
    m_aboutMeta->setObjectName("WesternAboutMeta");
    m_aboutMeta->setWordWrap(true);
    m_aboutSynopsis = new QLabel(m_aboutBlock);
    m_aboutSynopsis->setObjectName("WesternAboutSynopsis");
    m_aboutSynopsis->setWordWrap(true);
    v->addWidget(m_aboutMeta);
    v->addWidget(m_aboutSynopsis);
    // Insert at the top of the volumes column, above the rows.
    if (m_volumesLayout)
        m_volumesLayout->insertWidget(0, m_aboutBlock);
}

void ComicsSeriesView::updateAboutBlock(const tankoban::manga::MangaCatalog& catalog)
{
    buildAboutBlock();
    QStringList meta;
    if (!catalog.author.isEmpty())  meta << catalog.author;
    if (!catalog.studio.isEmpty())  meta << catalog.studio;       // publisher
    if (catalog.publishedYearStart) meta << QString::number(catalog.publishedYearStart);
    if (!catalog.genres.isEmpty())  meta << catalog.genres.join(QStringLiteral(", "));
    m_aboutMeta->setText(meta.join(QStringLiteral("  ·  ")));
    m_aboutMeta->setVisible(!meta.isEmpty());
    m_aboutSynopsis->setText(catalog.seriesSynopsis);
    m_aboutSynopsis->setVisible(!catalog.seriesSynopsis.isEmpty());
    m_aboutBlock->setVisible(!meta.isEmpty() || !catalog.seriesSynopsis.isEmpty());
}
```

(If `m_volumesLayout` is not the correct member name for the volume-rows column, grep `populateVolumeRowsFromCatalog` for the layout it inserts tiles into and use that exact member.)

- [ ] **Step 3: Call `updateAboutBlock` from the catalog-populate path**

In `populateVolumeRowsFromCatalog`, right after `m_currentMangaCatalog = catalog;` (near the top), add:

```cpp
    updateAboutBlock(catalog);
```

- [ ] **Step 4: Add minimal QSS** (gray-on-dark, matches house style — no color, see `feedback_no_color_no_emoji`)

In the view's stylesheet setup (grep for an existing `setStyleSheet` block in `ComicsSeriesView.cpp`), add rules:

```cpp
        "QLabel#WesternAboutMeta { color: rgba(238,238,238,0.62); font-size: 13px; }"
        "QLabel#WesternAboutSynopsis { color: rgba(238,238,238,0.82); font-size: 14px; }"
```

- [ ] **Step 5: Build**

Run: `build_check.bat` (via PowerShell: `& cmd.exe /c "C:\Users\Suprabha\Desktop\Tankoban 2\build_check.bat"`)
Expected: `BUILD OK`, `out/Tankoban.exe` mtime advances.

- [ ] **Step 6: Render smoke**

Kill any running instance, launch `build_and_run.bat`, open Comics → Western → open a series (e.g. Invincible). Eyeball: the about-block shows synopsis + "Robert Kirkman · Image Comics · 2003 · superhero comics" above the editions; an un-enriched series shows editions with no broken/empty header.

- [ ] **Step 7: Commit**

```bash
git add src/ui/pages/comics/ComicsSeriesView.h src/ui/pages/comics/ComicsSeriesView.cpp
git commit -m "feat(comics): Western series detail about-block (synopsis + credits)"
```

---

## Self-Review

**Spec coverage:** D2 synopsis (Task 1 + Task 4 fallback wire) ✓; D3 Wikidata fields (Task 2) ✓; D4 all four fields surfaced (Task 6 loader + Task 7 render) ✓; D5 baked at harvest (Task 4/5) ✓; D6 per-edition covers deferred — not in plan ✓; graceful degradation (Task 2/3 return-empty, Task 7 visibility guards) ✓; testing (pure-logic Python + C++ loader test + render smoke) ✓; scope fence honored (no search/downloads/per-edition tasks) ✓.

**Placeholder scan:** no TBD/TODO; every code step has concrete code; two explicit "verify against the real artifact" notes (QFINDTESTDATA mechanism, `m_volumesLayout` member name) are grounded checks, not placeholders.

**Type consistency:** `parse_series_summary` (Task 1) used in Task 4 ✓; `enrich()`/`needs_fallback()`/`fetch_extract()` signatures match Tasks 2-3 usage in Task 4 ✓; JSON keys (`synopsis/author/publisher/genres/yearStart/yearEnd/status`) consistent across Task 4 (write) ↔ Task 5 (verify) ↔ Task 6 fixture+loader (read) ✓; `studio`=publisher mapping consistent Task 6 ↔ Task 7 ✓.
